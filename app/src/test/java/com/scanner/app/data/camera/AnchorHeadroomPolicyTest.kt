package com.scanner.app.data.camera

import org.junit.Assert.*
import org.junit.Test
import kotlin.math.pow

class AnchorHeadroomPolicyTest {
    private val base = ManualExposure(58_324_786, 6400, 238)
    private val darkScreen = AnchorHeadroomPolicy.Statistics(10000, 9000, 140, 200, 500)
    private fun plan(exposure: ManualExposure = base, stats: AnchorHeadroomPolicy.Statistics = darkScreen,
                     unity: Boolean = true) = AnchorHeadroomPolicy.plan(exposure, stats, 88_889, 1_000_000_000, 100, 6400, unity)
    private val clean = ManualExposure(33_330_000, 4712, 100)
    private val probe = CaptureEvidence(100, base, 4, 0, 2.8f, 3, true)
    private val replacement = CaptureEvidence(200, clean, 0, 0, 2.8f, 0, false)

    @Test fun nativeHighAndLongAspectInputsUseBoundedDecoderReduction() {
        for ((w, h) in listOf(4096 to 3072, 8192 to 6144, 6144 to 8192, 11000 to 4000, 4000 to 11000)) {
            val sample = AnchorHeadroomPolicy.decodeSample(w, h)!!
            assertEquals(0, sample and (sample - 1))
            assertTrue((maxOf(w, h) + sample - 1) / sample <= 1024)
        }
        assertNull(AnchorHeadroomPolicy.decodeSample(0, 100))
        assertNull(AnchorHeadroomPolicy.decodeSample(Int.MAX_VALUE, Int.MAX_VALUE))
        assertNull(AnchorHeadroomPolicy.decodeSample(10000, 10000))
    }

    @Test fun statisticsAreBoundedAndColoredClippingDoesNotImplyNeutralWhite() {
        val pixels = IntArray(10000) { 0xff101010.toInt() }
        for (i in 0 until 140) pixels[i] = 0xfffafafa.toInt()
        for (i in 140 until 240) pixels[i] = 0xffff0000.toInt()
        val stats = AnchorHeadroomPolicy.statistics(pixels)!!
        assertEquals(140, stats.neutralClipped)
        assertEquals(140, stats.bright)
        assertEquals(9760, stats.dark)
        assertTrue(stats.valid)
        assertNull(AnchorHeadroomPolicy.statistics(IntArray(4095)))
        assertNull(AnchorHeadroomPolicy.statistics(IntArray(AnchorHeadroomPolicy.MAX_PIXELS + 1)))
    }

    @Test fun observedHighGainDarkScreenCanRequestLowerProductWithoutLongerTime() {
        for (boost in listOf(238, 253, 276)) {
            val original = base.copy(postRawBoost = boost)
            val requested = plan(original)!!
            assertEquals(100, requested.postRawBoost)
            assertTrue(requested.timeNs <= 33_333_333L && requested.timeNs <= original.timeNs)
            assertTrue(requested.iso in 100..6400)
            assertEquals(original.product / 2.0.pow(2.5), requested.product, original.product * .001)
            assertTrue(requested.iso.toDouble() < original.iso * boost / 100.0)
        }
    }

    @Test fun normalPaperLowGainShortExposureAndUnknownBoostKeepOriginalAcquisition() {
        assertNull(plan(stats = darkScreen.copy(dark = 1000, bright = 8000, foreground = 9000)))
        assertNull(plan(stats = darkScreen.copy(neutralClipped = 0)))
        assertNull(plan(stats = darkScreen.copy(bright = 2000, foreground = 3000)))
        assertNull(plan(stats = darkScreen.copy(samples = 0)))
        assertNull(plan(base.copy(iso = 200, postRawBoost = 100)))
        assertNull(plan(base.copy(timeNs = 8_000_000)))
        assertNull(plan(base.copy(postRawBoost = null)))
        assertNull(plan(base.copy(postRawBoost = 0)))
        assertNull(plan(unity = false))
    }

    @Test fun impossibleDeviceLimitsCannotSilentlyChangeRequestedStops() {
        assertNull(AnchorHeadroomPolicy.plan(base, darkScreen, 50_000_000, 100_000_000, 100, 6400, true))
        assertNull(AnchorHeadroomPolicy.plan(base, darkScreen, 88_889, 1_000_000_000, 100, 100, true))
        assertNull(AnchorHeadroomPolicy.plan(base, darkScreen, 0, 1_000_000_000, 100, 6400, true))
    }

    @Test fun controlAcknowledgmentCannotReplaceMatchedObservedSensorEvidence() {
        fun accepted(frame: CaptureEvidence? = replacement, original: CaptureEvidence? = probe) =
            AnchorHeadroomPolicy.acceptsEvidence(clean, original, frame, 2.8f, false)
        assertTrue(accepted())
        assertFalse(accepted(null))
        assertFalse(accepted(original = null))
        assertFalse(accepted(replacement.copy(timestamp = 100)))
        assertFalse(accepted(replacement.copy(aeState = 2)))
        assertFalse(accepted(replacement.copy(aeLocked = null)))
        assertFalse(accepted(replacement.copy(aeLocked = true)))
        assertFalse(accepted(replacement.copy(exposure = base)))
        assertFalse(accepted(replacement.copy(exposure = clean.copy(postRawBoost = null))))
        assertFalse(accepted(replacement.copy(exposure = clean.copy(postRawBoost = 200))))
        // Equal product with doubled ISO/halved time is still an ignored requested allocation.
        assertFalse(accepted(replacement.copy(exposure = clean.copy(timeNs = clean.timeNs / 2, iso = clean.iso * 2))))
    }

    @Test fun stationaryHeldFocusIsRequiredUnlessCameraIsFixedFocus() {
        for (bad in listOf(replacement.copy(focusDistance = null), replacement.copy(focusDistance = Float.NaN),
                replacement.copy(focusDistance = 3f), replacement.copy(lensState = 1), replacement.copy(afState = 4)))
            assertFalse(AnchorHeadroomPolicy.acceptsEvidence(clean, probe, bad, 2.8f, false))
        assertFalse(AnchorHeadroomPolicy.acceptsEvidence(clean, probe, replacement, null, false))
        assertTrue(AnchorHeadroomPolicy.acceptsEvidence(clean, probe, replacement.copy(focusDistance = null, lensState = null), null, true))
    }

    @Test fun blackOrStillClippedReplacementCannotBeAdopted() {
        assertTrue(AnchorHeadroomPolicy.improves(darkScreen, darkScreen.copy(neutralClipped = 20)))
        assertFalse(AnchorHeadroomPolicy.improves(darkScreen, darkScreen))
        assertFalse(AnchorHeadroomPolicy.improves(darkScreen, darkScreen.copy(neutralClipped = 0, bright = 0, foreground = 0)))
        assertFalse(AnchorHeadroomPolicy.improves(darkScreen, darkScreen.copy(samples = 0)))
    }

    @Test fun fallbackMayRestoreOriginalBoostButCannotMixDifferentExposureAnchors() {
        assertTrue(AnchorHeadroomPolicy.matches(base, base))
        assertFalse(AnchorHeadroomPolicy.matches(base, clean))
        assertFalse(AnchorHeadroomPolicy.matches(base, base.copy(postRawBoost = null)))
        assertFalse(AnchorHeadroomPolicy.matches(base, base.copy(timeNs = base.timeNs / 2)))
    }
}
