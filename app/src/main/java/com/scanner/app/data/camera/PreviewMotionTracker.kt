package com.scanner.app.data.camera

import android.util.Log
import com.scanner.app.BuildConfig
import org.opencv.calib3d.Calib3d
import org.opencv.core.*
import org.opencv.imgproc.Imgproc
import org.opencv.video.Video
import kotlin.math.hypot

/** Sparse LK tracks actual scene texture, independent of contour selection. */
class PreviewMotionTracker {
    private val previous = Mat()
    private val stability = MotionStability()
    private var rotation: Int? = null
    private var previousTime: Long? = null
    private var loggedAt: Long? = null

    fun reset() {
        previous.release()
        rotation = null
        previousTime = null
        stability.reset()
    }

    fun update(gray: Mat, timeMs: Long, rotationDegrees: Int): Boolean {
        val current = Mat()
        try {
            val scale = minOf(1.0, 480.0 / maxOf(gray.cols(), gray.rows()))
            Imgproc.resize(gray, current, Size(), scale, scale, Imgproc.INTER_AREA)
            if (rotation != rotationDegrees || previous.size() != current.size()) reset()
            rotation = rotationDegrees
            val motion = if (previous.empty()) null else measure(previous, current)
            val stable = stability.update(timeMs, motion)
            if (BuildConfig.DEBUG && (loggedAt == null || timeMs - loggedAt!! >= 1000)) {
                val dt = previousTime?.let { timeMs - it } ?: 0
                val speed = if (dt > 0) motion?.distance?.times(1000.0 / dt) else null
                Log.d("HachiCam-Motion", "steady=$stable speed=$speed confidence=${motion?.confidence}")
                loggedAt = timeMs
            }
            previousTime = timeMs
            current.copyTo(previous)
            return stable
        } finally { current.release() }
    }

    private fun measure(before: Mat, after: Mat): PreviewMotion? {
        val corners = MatOfPoint()
        val source = MatOfPoint2f()
        val target = MatOfPoint2f()
        val back = MatOfPoint2f()
        val status = MatOfByte()
        val backStatus = MatOfByte()
        val error = MatOfFloat()
        val backError = MatOfFloat()
        val from = MatOfPoint2f()
        val to = MatOfPoint2f()
        val inliers = Mat()
        var affine: Mat? = null
        try {
            Imgproc.goodFeaturesToTrack(before, corners, 100, .01, 8.0)
            val points = corners.toArray()
            if (points.size < 12) return null
            source.fromArray(*points)
            Video.calcOpticalFlowPyrLK(before, after, source, target, status, error, Size(21.0, 21.0), 3)
            Video.calcOpticalFlowPyrLK(after, before, target, back, backStatus, backError, Size(21.0, 21.0), 3)
            val next = target.toArray()
            val returned = back.toArray()
            val ok = status.toArray()
            val backOk = backStatus.toArray()
            val errors = error.toArray()
            val accepted = points.indices.filter { i ->
                ok[i].toInt() != 0 && backOk[i].toInt() != 0 && errors[i] < 30 &&
                    hypot(points[i].x - returned[i].x, points[i].y - returned[i].y) < 1.0 &&
                    next[i].x in 0.0..(after.cols() - 1.0) && next[i].y in 0.0..(after.rows() - 1.0)
            }
            if (accepted.size < 12) return null
            from.fromList(accepted.map { points[it] })
            to.fromList(accepted.map { next[it] })
            affine = Calib3d.estimateAffinePartial2D(from, to, inliers, Calib3d.RANSAC, 1.0)
            val transform = affine
            if (transform.empty() || !Core.checkRange(transform)) return null
            val mask = ByteArray(inliers.total().toInt())
            inliers.get(0, 0, mask)
            val reliable = accepted.indices.filter { mask[it].toInt() != 0 }
            if (reliable.size < 12) return null
            val confidence = reliable.size.toDouble() / points.size
            if (confidence < .55) return null
            val features = reliable.map { points[accepted[it]] }
            if (features.maxOf { it.x } - features.minOf { it.x } < before.cols() * .15 ||
                features.maxOf { it.y } - features.minOf { it.y } < before.rows() * .15) return null
            val values = DoubleArray(6)
            transform.get(0, 0, values)
            var displacement = 0.0
            for (x in listOf(.1, .5, .9)) for (y in listOf(.1, .5, .9)) {
                val px = x * before.cols(); val py = y * before.rows()
                val dx = values[0] * px + values[1] * py + values[2] - px
                val dy = values[3] * px + values[4] * py + values[5] - py
                displacement = maxOf(displacement, hypot(dx, dy))
            }
            return PreviewMotion(displacement / hypot(before.cols().toDouble(), before.rows().toDouble()), confidence)
        } finally {
            listOf(corners, source, target, back, status, backStatus, error, backError, from, to, inliers).forEach(Mat::release)
            affine?.release()
        }
    }
}
