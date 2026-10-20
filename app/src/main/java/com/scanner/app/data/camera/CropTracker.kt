package com.scanner.app.data.camera

import kotlin.math.abs
import kotlin.math.hypot

/** Temporal target continuity, independent of camera motion and Android graphics. */
class CropTracker {
    data class Selection(val points: List<UnitPoint>, val observedAtMs: Long, val partial: Boolean = false)
    var status: String = "missing"
        private set
    private var selected: Selection? = null
    private var pending: List<UnitPoint>? = null
    private var pendingSince = 0L
    private var pendingCount = 0
    private var pendingDwell = 250
    private var pendingPartial = false
    private var geometry: String? = null

    @Synchronized fun reset() {
        selected = null; pending = null; pendingCount = 0; geometry = null; status = "reset"
    }

    @Synchronized
    fun update(points: List<UnitPoint>?, nowMs: Long, geometryKey: String, partial: Boolean = false): Selection? {
        if (geometry != geometryKey) {
            selected = null
            pending = null
            pendingCount = 0
            geometry = geometryKey
        }
        if (selected?.let { nowMs - it.observedAtMs > 250 || nowMs < it.observedAtMs } == true)
            selected = null
        if (points == null || !valid(points, partial)) {
            if (partial && selected?.partial == false) selected = null
            pending = null
            pendingCount = 0
            status = if (selected != null) "retained" else if (points == null) "missing-or-expired" else "invalid"
            return selected
        }
        val old = selected
        if (old != null && old.partial == partial && sameTarget(old.points, points)) {
            val smoothed = old.points.zip(points).map { (a, b) ->
                if (hypot(a.x - b.x, a.y - b.y) < .0015f * 1.414214f) a
                else UnitPoint(a.x * .78f + b.x * .22f, a.y * .78f + b.y * .22f)
            }
            selected = Selection(smoothed, nowMs, partial)
            status = "tracked"
            pending = null
            pendingCount = 0
        } else {
            if (pendingPartial != partial || pending?.let { sameTarget(it, points) } != true) {
                pending = points
                pendingPartial = partial
                pendingSince = nowMs
                pendingCount = 1
                pendingDwell = if (old == null) 250 else 400
            } else pendingCount++
            status = if (old == null) "acquiring" else "switching"
            // Incomplete new evidence immediately revokes automatic-crop permission.
            if (partial && old?.partial == false) selected = null
            // A rejected candidate must not refresh the selected target's freshness.
            if (pendingCount >= 3 && nowMs - pendingSince >= pendingDwell) {
                selected = Selection(points, nowMs, partial)
                status = "acquired"
                pending = null
                pendingCount = 0
            }
        }
        return selected
    }

    companion object {
        fun area(p: List<UnitPoint>): Float = abs(p.indices.sumOf { i ->
            val a = p[i]; val b = p[(i + 1) % p.size]
            (a.x * b.y - b.x * a.y).toDouble()
        }.toFloat()) * .5f

        fun valid(p: List<UnitPoint>, partial: Boolean = false): Boolean {
            val bounds = if (partial) -.25f..1.25f else 0f..1f
            if (p.size != 4 || p.any { !it.x.isFinite() || !it.y.isFinite() || it.x !in bounds || it.y !in bounds }) return false
            if (partial && p.count { it.x !in 0f..1f || it.y !in 0f..1f } > 1) return false
            if (area(p) < .005f) return false
            val crosses = p.indices.map { i ->
                val a = p[i]; val b = p[(i + 1) % 4]; val c = p[(i + 2) % 4]
                (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x)
            }
            return crosses.all { it > .00001f }
        }
        fun cropEligible(found: Boolean, partial: Boolean, observedAtMs: Long, nowMs: Long): Boolean =
            found && !partial && nowMs - observedAtMs in 0..250
        private fun sameTarget(a: List<UnitPoint>, b: List<UnitPoint>): Boolean {
            val ratio = area(b) / area(a).coerceAtLeast(.00001f)
            return ratio in .75f..1.33f && a.zip(b).all { (x, y) -> hypot(x.x - y.x, x.y - y.y) <= .03f * 1.414214f }
        }
    }
}
