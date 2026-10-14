package com.scanner.app.data.camera

import kotlin.math.abs
import kotlin.math.ln

data class MeteringObservation(val session: Long, val revision: Long,
    val observedAtMs: Long, val frame: CaptureEvidence)

/** Fresh post-action exposure evidence, separate from AF completion and JPEG matching. */
object BurstMeteringPolicy {
    const val WAIT_MS = 750L
    const val FRESH_MS = 500L
    const val MAXIMUM_STOPS = .15

    fun settled(previous: MeteringObservation?, current: MeteringObservation,
                afterRevision: Long, session: Long, nowMs: Long, needsRegions: Boolean): Boolean {
        if (previous == null || nowMs < 0) return false
        for (sample in listOf(previous, current)) {
            val frame = sample.frame
            if (sample.session != session || sample.revision <= afterRevision || sample.observedAtMs < 0 ||
                nowMs - sample.observedAtMs !in 0..FRESH_MS || frame.timestamp <= 0 ||
                frame.aeState != 2 || frame.aeLocked == true) return false
            if (needsRegions && (frame.aeRegionCount == null || frame.aeRegionCount <= 0 ||
                        frame.aeRegionSignature.isNullOrBlank())) return false
            val exposure = frame.exposure ?: return false
            if (!exposure.product.isFinite() || exposure.product <= 0 ||
                exposure.timeNs <= 0 || exposure.iso <= 0 ||
                exposure.postRawBoost?.let { it <= 0 } == true) return false
        }
        val a = previous.frame
        val b = current.frame
        if (current.revision <= previous.revision || b.timestamp <= a.timestamp ||
            current.observedAtMs < previous.observedAtMs ||
            a.aeRegionCount != b.aeRegionCount || a.aeRegionSignature != b.aeRegionSignature ||
            a.aeCompensation != b.aeCompensation ||
            a.exposure!!.postRawBoost != b.exposure!!.postRawBoost) return false
        return abs(ln(b.exposure.product / a.exposure.product) / ln(2.0)) <= MAXIMUM_STOPS
    }
}
