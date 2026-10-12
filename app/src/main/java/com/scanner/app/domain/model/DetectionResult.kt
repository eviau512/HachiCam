package com.scanner.app.domain.model

import android.graphics.PointF

data class DetectionResult(
    val found: Boolean,
    val quad: DocumentQuad? = null,
    val boundaryPoints: List<PointF>? = null,
    val isCurved: Boolean = false,
    val frameWidth: Int = 0,
    val frameHeight: Int = 0,
    val isStable: Boolean = false,
    val rotationDegrees: Int = 0,
    val observedAtMs: Long = 0,
    val partial: Boolean = false,
    val method: Int = 0,
    val candidates: Int = 0,
    val sideSupport: Float = 0f,
    val analysisDurationMs: Long = 0
) {
    val points: List<PointF>
        get() = quad?.toPointList() ?: boundaryPoints ?: emptyList()
}
