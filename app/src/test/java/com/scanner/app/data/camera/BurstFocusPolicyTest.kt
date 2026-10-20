package com.scanner.app.data.camera

import org.junit.Assert.*
import org.junit.Test
import kotlin.math.pow

class BurstFocusPolicyTest {
    private fun sample(af: Int? = 4, lens: Int? = 0, distance: Float? = .96f) = CaptureEvidence(1, null, af, lens, distance)
    @Test fun stationaryConfirmedFocusMayBeHeld() {
        assertEquals(.96f, BurstFocusPolicy.heldDistance(true, sample(), 100, 8f, true)!!, 0f)
    }
    @Test fun scanningMovingStaleAndFailedFocusCannotBeHeld() {
        assertNull(BurstFocusPolicy.heldDistance(true, sample(af = 1), 0, 8f, true))
        assertNull(BurstFocusPolicy.heldDistance(true, sample(lens = 1), 0, 8f, true))
        assertNull(BurstFocusPolicy.heldDistance(true, sample(), 501, 8f, true))
        assertNull(BurstFocusPolicy.heldDistance(false, sample(), 0, 8f, true))
    }
    @Test fun unknownInvalidAndFixedFocusStayUnknown() {
        for (distance in listOf<Float?>(null, Float.NaN, Float.POSITIVE_INFINITY, -1f, 9f))
            assertNull(BurstFocusPolicy.heldDistance(true, sample(distance = distance), 0, 8f, true))
        assertNull(BurstFocusPolicy.heldDistance(true, sample(), 0, 0f, true))
        assertNull(BurstFocusPolicy.heldDistance(true, sample(lens = null), 0, 8f, true))
        assertNull(BurstFocusPolicy.heldDistance(true, sample(), 0, 8f, false))
    }
    @Test fun deepestBracketReducesGainAndPreservesFullTargetProduct() {
        val base = ManualExposure(58_324_786, 6400, 228)
        val short = ExposurePlan.cleanBracket(base, 6.0, 88_889, 1_000_000_000, 100, 6400, true)!!
        assertTrue(short.iso < base.iso / 4); assertEquals(100, short.postRawBoost)
        assertTrue(short.timeNs <= 16_666_667)
        assertEquals(base.product / 2.0.pow(6), short.product, base.product * .0001)
    }
    @Test fun unsupportedOrUnknownBoostIsNotInvented() {
        assertEquals(228, ExposurePlan.cleanBracket(ManualExposure(58_324_786,6400,228),6.0,88_889,1_000_000_000,100,6400,false)!!.postRawBoost)
        assertNull(ExposurePlan.cleanBracket(ManualExposure(58_324_786,6400),6.0,88_889,1_000_000_000,100,6400,true)!!.postRawBoost)
        assertNull(ExposurePlan.cleanBracket(ManualExposure(20,100,0),6.0,1,1000,100,6400,true))
    }
}
