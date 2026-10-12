package com.scanner.app.data.camera

data class UnitPoint(val x: Float, val y: Float)

object ImageCoordinates {
    fun project(p: UnitPoint, width: Int, height: Int, h: DoubleArray): UnitPoint? {
        if (h.size != 9 || h.any { !it.isFinite() } || width < 2 || height < 2) return null
        val x = p.x * (width - 1); val y = p.y * (height - 1)
        val z = h[6] * x + h[7] * y + h[8]
        if (!z.isFinite() || kotlin.math.abs(z) < 1e-9) return null
        return UnitPoint(((h[0] * x + h[1] * y + h[2]) / z / (width - 1)).toFloat(),
            ((h[3] * x + h[4] * y + h[5]) / z / (height - 1)).toFloat()).takeIf { it.x.isFinite() && it.y.isFinite() }
    }
    fun throughCrop(raw: UnitPoint, sourceW: Int, sourceH: Int, left: Int, top: Int,
                    cropW: Int, cropH: Int, scale: Int, clockwise: Int): UnitPoint {
        val x = (((raw.x * (sourceW - 1) - left) + .5f) * scale - .5f) / (cropW * scale - 1).coerceAtLeast(1)
        val y = (((raw.y * (sourceH - 1) - top) + .5f) * scale - .5f) / (cropH * scale - 1).coerceAtLeast(1)
        return rotate(UnitPoint(x, y), clockwise)
    }
    fun rotate(p: UnitPoint, clockwise: Int): UnitPoint = when ((clockwise % 360 + 360) % 360) {
        90 -> UnitPoint(1f - p.y, p.x)
        180 -> UnitPoint(1f - p.x, 1f - p.y)
        270 -> UnitPoint(p.y, 1f - p.x)
        else -> p
    }
    fun fitPoint(x: Float, y: Float, viewW: Float, viewH: Float, imageW: Float, imageH: Float): UnitPoint? {
        if (minOf(viewW, viewH, imageW, imageH) <= 0) return null
        val scale = minOf(viewW / imageW, viewH / imageH)
        val nx = (x - (viewW - imageW * scale) / 2f) / (imageW * scale)
        val ny = (y - (viewH - imageH * scale) / 2f) / (imageH * scale)
        return if (nx in 0f..1f && ny in 0f..1f) UnitPoint(nx, ny) else null
    }
}
