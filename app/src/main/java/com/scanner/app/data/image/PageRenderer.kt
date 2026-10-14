package com.scanner.app.data.image

import android.graphics.BitmapFactory
import android.graphics.PointF
import com.scanner.app.data.camera.UnitPoint
import com.scanner.app.data.repository.PageRepository
import com.scanner.app.data.util.ExifUtils
import com.scanner.app.domain.model.DocumentQuad
import com.scanner.app.domain.model.ScannedPage
import com.scanner.app.engine.NativePerspective
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import org.opencv.core.Core
import org.opencv.core.Mat
import org.opencv.core.MatOfInt
import org.opencv.imgcodecs.Imgcodecs
import java.io.File
import java.util.UUID

object PageRenderer {
    suspend fun render(request: RevisionLedger.Request<ScannedPage>): Boolean = withContext(Dispatchers.Default) {
        val sourceLease = PageRepository.readSource(request) ?: return@withContext false
        sourceLease.use {
            val sourceBounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(request.value.originalImagePath, sourceBounds)
            val sourceW=sourceBounds.outWidth; val sourceH=sourceBounds.outHeight
            val plannedCorners=request.value.quad?.toPointList()?.map { UnitPoint(it.x/(sourceW-1),it.y/(sourceH-1)) }
                ?: listOf(UnitPoint(0f,0f),UnitPoint(1f,0f),UnitPoint(1f,1f),UnitPoint(0f,1f))
            val outputSize=PerspectiveSize.plan(sourceW,sourceH,plannedCorners,request.value.targetAspectRatio,request.value.rotation)
            ImageWork.run(ImageMemoryPolicy.editBytes(sourceW.toLong() * sourceH,outputSize.pixels)) {
                if (!PageRepository.isCurrent(request)) return@run false
                val page = request.value
                val sourceFile = File(page.originalImagePath)
                val suffix = "${page.id}_${request.generation}_${UUID.randomUUID()}"
                val temporary = File(sourceFile.parentFile, "render_${suffix}.tmp.jpg")
                val result = File(sourceFile.parentFile, "render_${suffix}.jpg")
                val mats = mutableListOf<Mat>()
                fun <T : Mat> own(mat: T): T = mat.also { mats.add(it) }
                try {
                    currentCoroutineContext().ensureActive()
                    val source = own(Imgcodecs.imread(sourceFile.absolutePath, Imgcodecs.IMREAD_COLOR or Imgcodecs.IMREAD_IGNORE_ORIENTATION))
                    check(!source.empty()) { "Source decode failed" }
                    val w = source.cols(); val h = source.rows()
                    check(w == sourceW && h == sourceH && w > 1 && h > 1 && w.toLong() * h <= 56_000_000)
                    val quad = page.quad ?: DocumentQuad(PointF(0f, 0f), PointF(w - 1f, 0f),
                        PointF(w - 1f, h - 1f), PointF(0f, h - 1f))
                    val rendered = own(NativePerspective().processDocument(source, quad,
                        page.filter, QuarterTurn.preRotationAspect(page.targetAspectRatio, page.rotation)))
                    check(!rendered.empty()) { "Render failed" }
                    val upright = own(Mat())
                    when (QuarterTurn.normalized(page.rotation)) {
                        90 -> Core.rotate(rendered, upright, Core.ROTATE_90_CLOCKWISE)
                        180 -> Core.rotate(rendered, upright, Core.ROTATE_180)
                        270 -> Core.rotate(rendered, upright, Core.ROTATE_90_COUNTERCLOCKWISE)
                        else -> rendered.copyTo(upright)
                    }
                    currentCoroutineContext().ensureActive()
                    val params = own(MatOfInt(Imgcodecs.IMWRITE_JPEG_QUALITY, 100, Imgcodecs.IMWRITE_JPEG_OPTIMIZE, 1))
                    check(Imgcodecs.imwrite(temporary.absolutePath, upright, params)) { "JPEG encode failed" }
                    ExifUtils.copyAndStampExif(sourceFile, temporary)
                    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                    BitmapFactory.decodeFile(temporary.absolutePath, bounds)
                    check(bounds.outWidth == upright.cols() && bounds.outHeight == upright.rows()) { "Encoded image is invalid" }
                    currentCoroutineContext().ensureActive()
                    PageRepository.commitEdit(request, temporary, result)
                } catch (error: Exception) {
                    PageRepository.failEdit(request)
                    throw error
                } finally { temporary.delete(); mats.asReversed().forEach(Mat::release) }
            }
        }
    }
}
