package com.scanner.app.data.camera

import org.junit.Assert.*
import org.junit.Test
import kotlin.math.pow

class BurstMeteringPolicyTest {
    private fun observation(sequence: Long = 12, timestamp: Long = 1200, now: Long = 110,
                            frame: CaptureEvidence = CaptureEvidence(timestamp,
                                ManualExposure(8_000_000, 200, 100), 4, 0, 1f,
                                2, false, 0, 1, "120,100,240,200,1000", 1),
                            session: Long = 5) = MeteringObservation(session, sequence, now, frame)
    private val first = observation(11, 1100, 100)
    private fun settle(second: MeteringObservation, prior: MeteringObservation? = first,
                       now: Long = 120, boundary: Long = 10) =
        BurstMeteringPolicy.settled(prior, second, boundary, 5, now, true)

    @Test fun newConvergedTargetPairCanSettleWithoutOpticalAfSuccess() {
        assertTrue(settle(observation()))
        // Fixed focus / unsuccessful AF does not remove usable AE evidence.
        assertTrue(settle(observation(frame = observation().frame.copy(afState = null, focusDistance = null))))
        assertFalse(settle(observation(), prior = null))
    }
    @Test fun preActionOldSessionDuplicateAndStaleResultsCannotCertifyTargetExposure() {
        assertFalse(settle(observation(), boundary = 11))
        assertFalse(settle(observation(session = 4)))
        assertFalse(settle(observation(timestamp = 1100)))
        assertFalse(settle(observation(sequence = 11)))
        assertFalse(settle(observation(), now = 650))
        assertFalse(settle(observation(), now = 99))
    }
    @Test fun afSuccessCannotSubstituteForConvergenceOrPreservedAeRegion() {
        for (state in listOf(null, 0, 1, 3, 4, 5))
            assertFalse(settle(observation(frame = observation().frame.copy(aeState = state))))
        assertFalse(settle(observation(frame = observation().frame.copy(aeLocked = true))))
        assertFalse(settle(observation(frame = observation().frame.copy(aeRegionCount = 0, aeRegionSignature = ""))))
        assertFalse(settle(observation(frame = observation().frame.copy(aeRegionSignature = "400,300,520,400,1000"))))
    }
    @Test fun increasingExposureChangingCompensationAndInconsistentBoostCannotSettle() {
        val frame = observation().frame
        assertFalse(settle(observation(frame = frame.copy(exposure = ManualExposure(16_000_000, 200, 100)))))
        assertFalse(settle(observation(frame = frame.copy(aeCompensation = 1))))
        assertFalse(settle(observation(frame = frame.copy(exposure = ManualExposure(8_000_000, 200, null)))))
        assertFalse(settle(observation(frame = frame.copy(exposure = ManualExposure(8_000_000, 200, 200)))))
        val inside = (8_000_000 * 2.0.pow(.14)).toLong()
        val outside = (8_000_000 * 2.0.pow(.16)).toLong()
        assertTrue(settle(observation(frame = frame.copy(exposure = ManualExposure(inside, 200, 100)))))
        assertFalse(settle(observation(frame = frame.copy(exposure = ManualExposure(outside, 200, 100)))))
    }
    @Test fun missingAndInvalidExposureRemainUnknown() {
        val frame = observation().frame
        for (exposure in listOf(null, ManualExposure(0, 200, 100), ManualExposure(8_000_000, -1, 100),
                ManualExposure(8_000_000, 200, 0)))
            assertFalse(settle(observation(frame = frame.copy(exposure = exposure))))
        assertFalse(settle(observation(now = -1)))
    }
}
