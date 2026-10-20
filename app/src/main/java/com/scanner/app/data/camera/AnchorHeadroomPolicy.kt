package com.scanner.app.data.camera

import kotlin.math.abs
import kotlin.math.ln
import kotlin.math.pow
import kotlin.math.roundToLong

/** A bounded acquisition experiment, independent of Android and of SR eligibility. */
object AnchorHeadroomPolicy {
    const val VERSION = "dark-clipped-foreground-v1"
    const val MAX_SIDE = 1024
    const val MAX_PIXELS = MAX_SIDE * MAX_SIDE
    const val STOPS = 2.5

    fun decodeSample(width: Int, height: Int): Int? {
        if (width <= 1 || height <= 1 || width.toLong() * height > CapturePolicy.HIGH_PIXELS) return null
        var sample = 1
        while ((maxOf(width, height).toLong() + sample - 1) / sample > MAX_SIDE) sample *= 2
        return sample
    }

    data class Statistics(val samples: Int, val dark: Int, val neutralClipped: Int,
                          val bright: Int, val foreground: Int) {
        val valid get() = samples in 4096..MAX_PIXELS &&
            listOf(dark, neutralClipped, bright, foreground).all { it in 0..samples } &&
            neutralClipped <= bright && bright <= foreground
        val darkFraction get() = dark.toDouble() / samples.coerceAtLeast(1)
        val clippedFraction get() = neutralClipped.toDouble() / samples.coerceAtLeast(1)
        val brightFraction get() = bright.toDouble() / samples.coerceAtLeast(1)
        val foregroundFraction get() = foreground.toDouble() / samples.coerceAtLeast(1)
    }

    fun statistics(argb: IntArray): Statistics? {
        if (argb.size !in 4096..MAX_PIXELS) return null
        var dark = 0; var clipped = 0; var bright = 0; var foreground = 0
        for (p in argb) {
            val r = (p ushr 16) and 255; val g = (p ushr 8) and 255; val b = p and 255
            val luma = (299 * r + 587 * g + 114 * b + 500) / 1000
            if (luma < 48) dark++
            if (minOf(r, g, b) >= 240) clipped++
            if (luma >= 210) bright++
            if (luma >= 80) foreground++
        }
        return Statistics(argb.size, dark, clipped, bright, foreground)
    }

    fun requiresProbe(base: ManualExposure): Boolean = base.timeNs >= 20_000_000 && base.iso > 0 &&
        (base.postRawBoost ?: 0) > 0 && base.iso.toDouble() * (base.postRawBoost ?: 0) / 100 >= 3200

    fun eligible(base: ManualExposure, statistics: Statistics): Boolean =
        statistics.valid && requiresProbe(base) &&
            statistics.darkFraction >= .60 && statistics.clippedFraction >= .003 &&
            statistics.brightFraction in .006.. .15

    fun plan(base: ManualExposure, statistics: Statistics, minTime: Long, maxTime: Long,
             minIso: Int, maxIso: Int, unityBoostSupported: Boolean): ManualExposure? {
        if (!unityBoostSupported || !eligible(base, statistics) || minTime <= 0 || maxTime < minTime ||
            minIso <= 0 || maxIso < minIso) return null
        val cap = minOf(base.timeNs, maxTime, 33_333_333L)
        if (cap < minTime) return null
        val target = base.product / 2.0.pow(STOPS)
        val iso = (target / cap).roundToLong().coerceIn(minIso.toLong(), maxIso.toLong()).toInt()
        val time = (target / iso).roundToLong().coerceIn(minTime, cap)
        return ManualExposure(time, iso, 100).takeIf { stopDifference(it.product, target) <= .15 }
    }

    fun matches(requested: ManualExposure, observed: ManualExposure?): Boolean =
        observed != null && observed.timeNs > 0 && observed.iso > 0 && observed.postRawBoost == requested.postRawBoost &&
            requested.timeNs > 0 && requested.iso > 0 && (requested.postRawBoost ?: 0) > 0 &&
            abs(observed.timeNs.toDouble() / requested.timeNs - 1) <= .05 &&
            abs(observed.iso.toDouble() / requested.iso - 1) <= .05 &&
            stopDifference(observed.product, requested.product) <= .25

    fun improves(before: Statistics, after: Statistics): Boolean = before.valid && after.valid &&
        after.clippedFraction <= before.clippedFraction * .75 &&
        after.foregroundFraction >= before.brightFraction * .5

    fun acceptsEvidence(requested: ManualExposure, probe: CaptureEvidence?, replacement: CaptureEvidence?,
                        heldFocus: Float?, fixedFocus: Boolean): Boolean {
        if (requested.postRawBoost != 100 || probe == null || replacement == null || probe.timestamp <= 0 ||
            replacement.timestamp <= probe.timestamp || replacement.aeState != 0 || replacement.aeLocked != false ||
            !matches(requested, replacement.exposure)) return false
        if (fixedFocus) return true
        val distance = replacement.focusDistance ?: return false
        return heldFocus != null && heldFocus.isFinite() && distance.isFinite() &&
            replacement.lensState == 0 && replacement.afState == 0 && abs(distance - heldFocus) <= .02f
    }

    private fun stopDifference(a: Double, b: Double): Double =
        if (a > 0 && b > 0 && a.isFinite() && b.isFinite()) abs(ln(a / b) / ln(2.0)) else Double.POSITIVE_INFINITY
}
