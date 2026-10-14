package com.scanner.app.data.camera

/** User position intent survives metering auto-cancel; geometry/session owns its lifetime. */
class TapFocusTarget<T> {
    data class Selection<T>(val point: T, val x: Float, val y: Float, val ageMs: Long, val session: Long)
    private data class Target<T>(val point: T, val x: Float, val y: Float, val time: Long, val session: Long)
    private var target: Target<T>? = null
    @Synchronized fun remember(point: T, x: Float, y: Float, nowMs: Long, session: Long): Boolean {
        if (!x.isFinite() || !y.isFinite() || x !in 0f..1f || y !in 0f..1f || nowMs < 0) return false
        target = Target(point, x, y, nowMs, session); return true
    }
    @Synchronized fun select(nowMs: Long, session: Long): Selection<T>? {
        val value = target ?: return null
        if (value.session != session || nowMs < value.time) return null
        return Selection(value.point, value.x, value.y, nowMs - value.time, session)
    }
    @Synchronized fun clear() { target = null }
}
