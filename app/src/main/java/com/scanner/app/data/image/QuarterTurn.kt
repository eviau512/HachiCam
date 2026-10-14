package com.scanner.app.data.image

import com.scanner.app.data.camera.ImageCoordinates
import com.scanner.app.data.camera.UnitPoint

object QuarterTurn {
    fun normalized(degrees: Int): Int {
        require(degrees % 90 == 0)
        return (degrees % 360 + 360) % 360
    }
    fun corners(points: List<UnitPoint>, clockwise: Int): List<UnitPoint> {
        require(points.size == 4)
        val turns = normalized(clockwise) / 90
        val mapped = points.map { ImageCoordinates.rotate(it, clockwise) }
        return List(4) { mapped[(it - turns + 4) % 4] }
    }
    fun preRotationAspect(displayAspect: Float?, clockwise: Int): Float =
        displayAspect?.let {
            require(it.isFinite() && it in .05f..20f) { "Unsupported aspect ratio" }
            if (normalized(clockwise) % 180 == 0) it else 1f / it
        } ?: 0f
}
