package com.scanner.app.data.camera

import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import com.scanner.app.domain.model.DetectionResult
import com.scanner.app.engine.NativeEdgeDetector
import org.opencv.core.CvType
import org.opencv.core.Mat
import org.opencv.core.Core

class FrameAnalyzer(
    private val detector: NativeEdgeDetector,
    var curvedMode: Boolean = false,
    private val onResult: (DetectionResult) -> Unit
) : ImageAnalysis.Analyzer {

    private val motionTracker = PreviewMotionTracker()
    private var closed = false

    @Volatile var touchPoint: android.graphics.PointF? = null

    @Synchronized fun resetStability() {
        motionTracker.reset()
    }

    @Synchronized fun close() {
        closed = true
        motionTracker.reset()
        onResult(DetectionResult(found = false))
    }

    @Synchronized override fun analyze(imageProxy: ImageProxy) {
        if (closed) { imageProxy.close(); return }
        val observedAtMs = android.os.SystemClock.elapsedRealtime()
        val grayMat = Mat()
        val rotatedMat = Mat()
        try {
            val yPlane = imageProxy.planes[0]
            val yBuffer = yPlane.buffer.duplicate()
            val bufferStart = yBuffer.position()
            val rowStride = yPlane.rowStride
            val pixelStride = yPlane.pixelStride
            val width = imageProxy.width
            val height = imageProxy.height

            grayMat.create(height, width, CvType.CV_8UC1)
            if (rowStride == width && pixelStride == 1) {
                val yBytes = ByteArray(width * height)
                yBuffer.get(yBytes)
                grayMat.put(0, 0, yBytes)
            } else {
                val rowData = ByteArray(width)
                for (row in 0 until height) {
                    if (pixelStride == 1) {
                        yBuffer.position(bufferStart + row * rowStride)
                        yBuffer.get(rowData, 0, width)
                    } else {
                        for (col in 0 until width)
                            rowData[col] = yBuffer.get(bufferStart + row * rowStride + col * pixelStride)
                    }
                    grayMat.put(row, 0, rowData)
                }
            }
            
            val rotationDegrees = imageProxy.imageInfo.rotationDegrees
            
            if (rotationDegrees == 90 || rotationDegrees == 270) {
                Core.transpose(grayMat, rotatedMat)
                Core.flip(rotatedMat, rotatedMat, if (rotationDegrees == 90) 1 else 0)
            } else if (rotationDegrees == 180) {
                Core.flip(grayMat, rotatedMat, -1)
            } else {
                grayMat.copyTo(rotatedMat)
            }

            val isStable = try {
                motionTracker.update(rotatedMat, imageProxy.imageInfo.timestamp / 1_000_000, rotationDegrees)
            } catch (error: Exception) {
                android.util.Log.w("HachiCam-Motion", "Motion tracking failed", error)
                motionTracker.reset()
                false
            }
            val tp = touchPoint
            val touchX = tp?.x ?: -1.0f
            val touchY = tp?.y ?: -1.0f
            val result = try { detector.detectDocument(rotatedMat, curvedMode, touchX, touchY) }
            catch (error: Exception) {
                android.util.Log.w("HachiCam-Analysis", "Document detection failed", error)
                DetectionResult(found = false, frameWidth = rotatedMat.cols(), frameHeight = rotatedMat.rows())
            }
            val finalResult = result.copy(isStable = isStable, rotationDegrees = rotationDegrees,
                observedAtMs = observedAtMs,
                analysisDurationMs = android.os.SystemClock.elapsedRealtime() - observedAtMs)
            onResult(finalResult)
        } catch (error: Exception) {
            android.util.Log.e("HachiCam-Analysis", "Preview analysis failed", error)
            resetStability()
            onResult(DetectionResult(found = false))
        } finally {
            grayMat.release()
            rotatedMat.release()
            imageProxy.close()
        }
    }
}
