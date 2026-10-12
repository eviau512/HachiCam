package com.scanner.app.ui.camera

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageDecoder
import android.graphics.ImageFormat
import android.graphics.PointF
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CaptureRequest
import android.net.Uri
import android.util.Log
import androidx.camera.camera2.interop.*
import androidx.camera.core.*
import androidx.core.content.ContextCompat
import androidx.exifinterface.media.ExifInterface
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.scanner.app.R
import com.scanner.app.BuildConfig
import com.scanner.app.data.camera.*
import com.scanner.app.data.image.ImageStorage
import com.scanner.app.data.repository.PageRepository
import com.scanner.app.data.util.ExifUtils
import com.scanner.app.domain.model.*
import com.scanner.app.engine.NativeBurstFusion
import com.scanner.app.engine.NativeEdgeDetector
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import org.opencv.core.Core
import org.opencv.core.Mat
import org.opencv.core.MatOfByte
import org.opencv.core.MatOfInt
import org.opencv.imgcodecs.Imgcodecs
import java.io.ByteArrayInputStream
import java.io.File
import java.util.UUID
import kotlin.coroutines.resume
import kotlin.math.sqrt

@androidx.annotation.OptIn(ExperimentalCamera2Interop::class)
class CameraViewModel : ViewModel() {
    private val _detectedQuad = MutableStateFlow<DetectionResult?>(null)
    val detectedQuad = _detectedQuad.asStateFlow()
    private val _curvedModeEnabled = MutableStateFlow(false)
    val curvedModeEnabled = _curvedModeEnabled.asStateFlow()
    private val _fullHdrEnabled = MutableStateFlow(false)
    val fullHdrEnabled = _fullHdrEnabled.asStateFlow()
    private val _isStable = MutableStateFlow(false)
    val isStable = _isStable.asStateFlow()
    private val _isCapturing = MutableStateFlow(false)
    val isCapturing = _isCapturing.asStateFlow()
    private val _isImporting = MutableStateFlow(false)
    val isImporting = _isImporting.asStateFlow()
    private val _lastThumbnailBitmap = MutableStateFlow<Bitmap?>(null)
    val lastThumbnailBitmap = _lastThumbnailBitmap.asStateFlow()
    private val _errorMessage = MutableStateFlow<String?>(null)
    val errorMessage = _errorMessage.asStateFlow()
    fun clearError() { _errorMessage.value = null }
    val capturedPages = PageRepository.pages
    val pageCount = capturedPages.map { it.size }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), 0)

    var imageCapture: ImageCapture? = null
    var imageAnalysis: ImageAnalysis? = null
    var cameraControl: CameraControl? = null
    var cameraInfo: CameraInfo? = null
    var frameAnalyzer: FrameAnalyzer? = null
    private data class Frame(val bytes: ByteArray, val rotation: Int, val tier: Int, val exposure: ManualExposure?)

    fun updateSettings(context: Context) {
        _fullHdrEnabled.value = context.getSharedPreferences("settings", Context.MODE_PRIVATE).getBoolean("full_hdr_enabled", false)
    }
    fun toggleCurvedMode() {
        _curvedModeEnabled.value = !_curvedModeEnabled.value
        frameAnalyzer?.curvedMode = _curvedModeEnabled.value
    }
    fun setTouchPoint(normX: Float, normY: Float) {
        val analyzer = frameAnalyzer ?: return
        val point = PointF(normX, normY)
        analyzer.touchPoint = point
        viewModelScope.launch {
            delay(3000)
            if (analyzer.touchPoint === point) analyzer.touchPoint = null
        }
    }
    private val cropTracker = CropTracker()
    private var lastDetectionLogMs = 0L
    fun onFrameAnalyzed(result: DetectionResult) {
        _isStable.value = result.isStable
        val w = (result.frameWidth - 1).coerceAtLeast(1).toFloat()
        val h = (result.frameHeight - 1).coerceAtLeast(1).toFloat()
        val points = if (result.found) result.quad?.toPointList()?.map { UnitPoint(it.x / w, it.y / h) } else null
        val selected = cropTracker.update(points, result.observedAtMs.takeIf { it > 0 } ?: android.os.SystemClock.elapsedRealtime(),
            "${result.frameWidth}:${result.frameHeight}:${result.rotationDegrees}", result.partial)
        _detectedQuad.value = if (selected == null) result.copy(found = false, quad = null)
        else result.copy(found = true, partial = selected.partial, observedAtMs = selected.observedAtMs, quad = selected.points.let {
            DocumentQuad(PointF(it[0].x * w, it[0].y * h), PointF(it[1].x * w, it[1].y * h),
                PointF(it[2].x * w, it[2].y * h), PointF(it[3].x * w, it[3].y * h))
        })
        val now = android.os.SystemClock.elapsedRealtime()
        if (BuildConfig.SENSITIVE_DIAGNOSTICS && now - lastDetectionLogMs >= 1000) {
            lastDetectionLogMs = now
            Log.d("HachiCam-Detection", "raw=${result.found} partial=${result.partial} method=${result.method} " +
                "candidates=${result.candidates} minSide=${result.sideSupport} tracker=${cropTracker.status} " +
                "selected=${selected != null} selectedPartial=${selected?.partial} ageMs=${selected?.let { now - it.observedAtMs }} " +
                "analysisMs=${result.analysisDurationMs} geometry=${result.frameWidth}x${result.frameHeight}/${result.rotationDegrees}")
        }
    }

    fun capturePhoto(context: Context, onPageSaved: (String) -> Unit = {}) {
        if (_isCapturing.value) return
        val capture = imageCapture ?: return
        if (!BurstProcessor.reserve()) {
            _errorMessage.value = context.getString(R.string.capture_queue_full)
            return
        }
        _isCapturing.value = true
        val app = context.applicationContext
        val hdr = _fullHdrEnabled.value
        viewModelScope.launch {
            var submitted = false
            val control = cameraControl
            try {
                if (hdr && !_isStable.value) withTimeoutOrNull(400) { _isStable.first { it } }
                val frames = mutableListOf<Frame>()
                val info = cameraInfo?.let { Camera2CameraInfo.from(it) }
                if (hdr && control != null) {
                    val options = CaptureRequestOptions.Builder()
                    if (info?.getCameraCharacteristic(CameraCharacteristics.CONTROL_AE_LOCK_AVAILABLE) == true)
                        options.setCaptureRequestOption(CaptureRequest.CONTROL_AE_LOCK, true)
                    if (info?.getCameraCharacteristic(CameraCharacteristics.CONTROL_AWB_LOCK_AVAILABLE) == true)
                        options.setCaptureRequestOption(CaptureRequest.CONTROL_AWB_LOCK, true)
                    applyOptions(control, app, options.build())
                }
                val detection = _detectedQuad.value?.takeIf {
                    CropTracker.cropEligible(it.found, it.partial, it.observedAtMs, android.os.SystemClock.elapsedRealtime())
                }
                if (BuildConfig.SENSITIVE_DIAGNOSTICS)
                    Log.d("HachiCam-Detection", "shutter=${if (detection == null) "full-image" else "complete-crop"} latestPartial=${_detectedQuad.value?.partial}")
                repeat(if (hdr) 4 else 1) { takeFrame(capture, app, 0)?.let(frames::add) }
                check(frames.isNotEmpty()) { "No camera frame received" }
                val base = frames.first().exposure
                val caps = info?.getCameraCharacteristic(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES)
                val times = info?.getCameraCharacteristic(CameraCharacteristics.SENSOR_INFO_EXPOSURE_TIME_RANGE)
                val isoRange = info?.getCameraCharacteristic(CameraCharacteristics.SENSOR_INFO_SENSITIVITY_RANGE)
                val manual = caps?.contains(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR) == true
                if (hdr && control != null && manual && base != null && times != null && isoRange != null) {
                    for ((tier, count, stops) in listOf(Triple(1, 2, 2.5), Triple(2, 1, 6.0))) {
                        val exposure = ExposurePlan.bracket(base, stops, times.lower, times.upper, isoRange.lower, isoRange.upper) ?: continue
                        val options = CaptureRequestOptions.Builder()
                            .setCaptureRequestOption(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_OFF)
                            .setCaptureRequestOption(CaptureRequest.SENSOR_EXPOSURE_TIME, exposure.timeNs)
                            .setCaptureRequestOption(CaptureRequest.SENSOR_SENSITIVITY, exposure.iso).build()
                        if (!applyOptions(control, app, options)) continue
                        repeat(count) {
                            val frame = takeFrame(capture, app, tier)
                            if (frame?.exposure != null && frame.exposure.product < base.product * .75)
                                frames.add(frame)
                            else Log.w("HachiCam-Burst", "Rejected bracket: requested=$exposure actual=${frame?.exposure}")
                        }
                    }
                }
                val id = UUID.randomUUID().toString()
                val output = File(ImageStorage(app).getStorageDir(), "$id.jpg")
                PageRepository.addPage(ScannedPage(id = id, originalImagePath = output.absolutePath,
                    filter = ImageFilter.ORIGINAL, status = PageStatus.PROCESSING))
                BurstProcessor.submit { processFrames(app, frames, detection, id, output, hdr) }
                submitted = true
                onPageSaved(id)
            } catch (error: CancellationException) { throw error }
            catch (error: Exception) {
                Log.e("HachiCam-Burst", "Capture failed", error)
                _errorMessage.value = app.getString(R.string.capture_failed)
            } finally {
                restoreOptions(control, app)
                _isCapturing.value = false
                frameAnalyzer?.resetStability()
                if (!submitted) BurstProcessor.cancelReservation()
            }
        }
    }

    private suspend fun restoreOptions(control: CameraControl?, context: Context) {
        if (control == null) return
        withContext(NonCancellable + Dispatchers.Main) {
            withTimeoutOrNull(3000) { awaitControl(context) { Camera2CameraControl.from(control).clearCaptureRequestOptions() } }
        }
    }
    private suspend fun applyOptions(control: CameraControl, context: Context, options: CaptureRequestOptions): Boolean =
        withTimeoutOrNull(3000) { awaitControl(context) { Camera2CameraControl.from(control).setCaptureRequestOptions(options) } } == true
    private suspend fun awaitControl(context: Context, operation: () -> com.google.common.util.concurrent.ListenableFuture<Void>): Boolean =
        suspendCancellableCoroutine { continuation ->
            try {
                val future = operation()
                future.addListener({
                    val success = try { future.get(); true } catch (_: Exception) { false }
                    if (continuation.isActive) continuation.resume(success)
                }, ContextCompat.getMainExecutor(context))
            } catch (_: Exception) { if (continuation.isActive) continuation.resume(false) }
        }

    private suspend fun takeFrame(capture: ImageCapture, context: Context, tier: Int): Frame? {
        val start = android.os.SystemClock.elapsedRealtime()
        val data = withTimeoutOrNull(15000) {
            suspendCancellableCoroutine<Pair<ByteArray, Int>?> { continuation ->
                capture.takePicture(ContextCompat.getMainExecutor(context), object : ImageCapture.OnImageCapturedCallback() {
                    override fun onCaptureSuccess(image: ImageProxy) {
                        try {
                            check(image.format == ImageFormat.JPEG || image.format == ImageFormat.JPEG_R)
                            val buffer = image.planes[0].buffer
                            val bytes = ByteArray(buffer.remaining()).also { buffer.get(it) }
                            if (continuation.isActive) continuation.resume(bytes to image.imageInfo.rotationDegrees)
                        } catch (error: Exception) {
                            Log.e("HachiCam-Burst", "JPEG capture failed", error)
                            if (continuation.isActive) continuation.resume(null)
                        } finally { image.close() }
                    }
                    override fun onError(exception: ImageCaptureException) {
                        Log.e("HachiCam-Burst", "Camera capture failed", exception)
                        if (continuation.isActive) continuation.resume(null)
                    }
                })
            }
        } ?: return null
        val exposure = withContext(Dispatchers.Default) {
            runCatching {
                val exif = ByteArrayInputStream(data.first).use { ExifInterface(it) }
                val seconds = exif.getAttributeDouble(ExifInterface.TAG_EXPOSURE_TIME, 0.0)
                val iso = exif.getAttributeInt(ExifInterface.TAG_PHOTOGRAPHIC_SENSITIVITY, 0)
                if (seconds > 0 && iso > 0) ManualExposure((seconds * 1e9).toLong(), iso) else null
            }.getOrNull()
        }
        Log.i("HachiCam-Burst", "tier=$tier elapsed=${android.os.SystemClock.elapsedRealtime() - start}ms rotation=${data.second} exposure=$exposure")
        return Frame(data.first, data.second, tier, exposure)
    }

    private fun decode(frame: Frame): Mat {
        val buffer = MatOfByte(*frame.bytes)
        return try { Imgcodecs.imdecode(buffer, Imgcodecs.IMREAD_COLOR or Imgcodecs.IMREAD_IGNORE_ORIENTATION) }
        finally { buffer.release() }
    }
    private fun saveUpright(mat: Mat, frame: Frame, output: File, mode: String): Pair<Float, Float> {
        val upright = Mat()
        val params = MatOfInt(Imgcodecs.IMWRITE_JPEG_QUALITY, 95)
        val temp = File(output.parentFile, "${output.nameWithoutExtension}.tmp.jpg")
        try {
            when (frame.rotation) {
                90 -> Core.rotate(mat, upright, Core.ROTATE_90_CLOCKWISE)
                180 -> Core.rotate(mat, upright, Core.ROTATE_180)
                270 -> Core.rotate(mat, upright, Core.ROTATE_90_COUNTERCLOCKWISE)
                else -> mat.copyTo(upright)
            }
            check(Imgcodecs.imwrite(temp.absolutePath, upright, params)) { "JPEG write failed" }
            ExifUtils.copyAndStampExif(frame.bytes, temp, mode)
            check(temp.renameTo(output)) { "JPEG commit failed" }
            return upright.cols().toFloat() to upright.rows().toFloat()
        } finally { upright.release(); params.release(); temp.delete() }
    }

    private fun processFrames(context: Context, frames: List<Frame>, detection: DetectionResult?, id: String, output: File, hdr: Boolean) {
        val mats = mutableListOf<Mat>()
        val valid = mutableListOf<Frame>()
        var fused: Mat? = null
        try {
            frames.forEachIndexed { index, frame ->
                if (frame.rotation != frames[0].rotation) {
                    Log.w("HachiCam-Burst", "Dropped frame $index: burst orientation changed")
                    return@forEachIndexed
                }
                val mat = decode(frame)
                if (mat.empty()) { mat.release(); check(index != 0) { "Reference decode failed" } }
                else { mats.add(mat); valid.add(frame) }
            }
            val fuser = NativeBurstFusion()
            fused = if (hdr) fuser.fuseBurstFrames(mats, removeGlare = false, isScreenMode = true,
                superResolution = true, tiers = valid.map { it.tier }.toIntArray(),
                exposureProducts = valid.map { it.exposure?.product ?: 0.0 }.toDoubleArray()) else mats[0].clone()
            check(!fused.empty()) { "Fusion returned no image" }
            val report = fuser.lastReport
            val mode = if (!hdr) "Normal" else
                "${if (report.highlights > 0) "HDR" else "Burst"}; anchors=${report.anchors}; highlights=${report.highlights}; scale=${report.scale}; reference=${report.reference}; sourceCrop=${report.left},${report.top},${report.cropW},${report.cropH}; experimental JPEG fusion"
            val (w, h) = saveUpright(fused, valid[if (hdr) report.reference else 0], output, mode)
            PageRepository.updatePage(id) { it.copy(quad = computeTargetQuad(detection, w, h, frames[0].rotation, if (hdr) report else null), status = PageStatus.READY) }
        } catch (error: Exception) {
            Log.e("HachiCam-Burst", "Fusion failed; retaining reference", error)
            try {
                val fallback = decode(frames[0])
                try {
                    check(!fallback.empty())
                    val (w, h) = saveUpright(fallback, frames[0], output, "Fallback reference (fusion failed)")
                    PageRepository.updatePage(id) { it.copy(quad = computeTargetQuad(detection, w, h, frames[0].rotation), status = PageStatus.READY) }
                    _errorMessage.value = context.getString(R.string.fusion_fallback)
                } finally { fallback.release() }
            } catch (saveError: Exception) {
                Log.e("HachiCam-Burst", "Reference save failed", saveError)
                PageRepository.updatePage(id) { it.copy(status = PageStatus.FAILED) }
                _errorMessage.value = context.getString(R.string.capture_failed)
            }
        } finally { fused?.release(); mats.forEach(Mat::release) }
        if (PageRepository.getPage(id) == null) { output.delete(); return }
        if (output.exists()) _lastThumbnailBitmap.value = BitmapFactory.decodeFile(output.absolutePath,
            BitmapFactory.Options().apply { inSampleSize = 16 })
    }

    private fun computeTargetQuad(result: DetectionResult?, width: Float, height: Float, rotation: Int, coverage: NativeBurstFusion.Report? = null): DocumentQuad {
        fun fullFrame() = DocumentQuad(PointF(0f, 0f), PointF(width - 1, 0f),
            PointF(width - 1, height - 1), PointF(0f, height - 1))
        val quad = result?.quad
        if (result?.found != true || result.partial || quad == null || result.frameWidth <= 1 || result.frameHeight <= 1 ||
            coverage?.geometryKnown == false) return fullFrame()
        val points = quad.toPointList().map {
            val raw = ImageCoordinates.rotate(UnitPoint(it.x / (result.frameWidth - 1), it.y / (result.frameHeight - 1)), -result.rotationDegrees)
            val p = if (coverage != null && coverage.cropW > 1 && coverage.cropH > 1) {
                val reference = ImageCoordinates.project(raw, coverage.sourceW, coverage.sourceH, coverage.firstToReference)
                    ?: return fullFrame()
                ImageCoordinates.throughCrop(reference, coverage.sourceW, coverage.sourceH, coverage.left, coverage.top,
                    coverage.cropW, coverage.cropH, coverage.scale, rotation)
            } else ImageCoordinates.rotate(raw, rotation)
            // Out-of-crop corners imply truncated document evidence: retain the
            // whole available image for manual correction instead of stretching it.
            if (p.x !in 0f..1f || p.y !in 0f..1f) return fullFrame()
            p
        }
        if (!CropTracker.valid(points)) return fullFrame()
        val start = points.indices.minBy { points[it].x + points[it].y }
        fun point(i: Int) = points[(start + i) % 4].let { PointF(it.x * (width - 1), it.y * (height - 1)) }
        return DocumentQuad(point(0), point(1), point(2), point(3))
    }

    fun importFromUris(context: Context, uris: List<Uri>, onSaved: (List<String>) -> Unit) {
        if (_isImporting.value) return
        _isImporting.value = true
        viewModelScope.launch(Dispatchers.IO) {
            val ids = mutableListOf<String>()
            try {
                for (uri in uris) {
                    ensureActive()
                    val id = UUID.randomUUID().toString()
                    val file = File(ImageStorage(context).getStorageDir(), "$id.jpg")
                    try {
                        val bitmap = ImageDecoder.decodeBitmap(ImageDecoder.createSource(context.contentResolver, uri)) { decoder, info, _ ->
                            decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
                            val pixels = info.size.width.toDouble() * info.size.height
                            val memory = (context.getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager).memoryClass
                            val budget = minOf(16000000.0, memory * 1024.0 * 1024.0 / 20.0)
                            if (pixels > budget) {
                                val scale = sqrt(budget / pixels)
                                decoder.setTargetSize(maxOf(1, (info.size.width * scale).toInt()), maxOf(1, (info.size.height * scale).toInt()))
                            }
                        }
                        val w = bitmap.width.toFloat(); val h = bitmap.height.toFloat()
                        try { file.outputStream().use { check(bitmap.compress(Bitmap.CompressFormat.JPEG, 95, it)) } }
                        finally { bitmap.recycle() }
                        ExifUtils.stampSignature(file, "Imported")
                        val mat = Imgcodecs.imread(file.absolutePath)
                        val detected = try { NativeEdgeDetector().detectDocument(mat, false) } finally { mat.release() }
                        PageRepository.addPage(ScannedPage(id = id, originalImagePath = file.absolutePath,
                            quad = computeTargetQuad(detected, w, h, 0), filter = ImageFilter.ORIGINAL))
                        ids.add(id)
                    } catch (error: CancellationException) { file.delete(); throw error }
                    catch (error: Exception) {
                        file.delete()
                        Log.e("HachiCam-Import", "Cannot import selected image", error)
                        _errorMessage.value = context.getString(R.string.import_failed)
                    }
                }
                withContext(Dispatchers.Main) { if (ids.isNotEmpty()) onSaved(ids) }
            } finally { _isImporting.value = false }
        }
    }
}
