package com.scanner.app.data.camera

import org.junit.Assert.*
import org.junit.Test

class MotionStabilityTest {
    private fun settled(): MotionStability = MotionStability().also { tracker ->
        tracker.update(0, null)
        for (time in 50L..400L step 50) tracker.update(time, PreviewMotion(.0008, .9))
        assertTrue(tracker.isStable)
    }

    @Test fun steadyTextureSettlesWithoutAnyDocumentCorners() {
        val tracker = MotionStability()
        assertFalse(tracker.update(0, null))
        for (time in 50L..300L step 50) assertFalse(tracker.update(time, PreviewMotion(.0008, .9)))
        assertTrue(tracker.update(350, PreviewMotion(.0008, .9)))
    }

    @Test fun oneUncertainFrameDoesNotFlashButTrackingLossExpires() {
        val tracker = settled()
        assertTrue(tracker.update(450, null))
        assertTrue(tracker.update(500, PreviewMotion(.0008, .9)))
        assertTrue(tracker.update(550, PreviewMotion(.0008, .1)))
        assertFalse(tracker.update(700, null))
    }

    @Test fun modestJitterDoesNotRepeatedlyResetSteady() {
        val tracker = settled()
        for (time in 450L..1000L step 50)
            assertTrue(tracker.update(time, PreviewMotion(if (time % 100 == 0L) .003 else .0008, .9)))
    }

    @Test fun sustainedPanLeavesSteadyAndLargeMovementLeavesImmediately() {
        val tracker = settled()
        assertTrue(tracker.update(450, PreviewMotion(.006, .9)))
        assertFalse(tracker.update(500, PreviewMotion(.006, .9)))
        val another = settled()
        assertFalse(another.update(450, PreviewMotion(.02, .9)))
    }

    @Test fun continuousSlowPanCannotAcquireSteady() {
        val tracker = MotionStability()
        tracker.update(0, null)
        for (time in 50L..2000L step 50) assertFalse(tracker.update(time, PreviewMotion(.003, .9)))
    }

    @Test fun timingGapAndInvalidSamplesCannotKeepStaleSteady() {
        assertFalse(settled().update(701, PreviewMotion(0.0, .9)))
        val tracker = settled()
        tracker.update(450, PreviewMotion(Double.NaN, .9))
        tracker.update(500, PreviewMotion(.0008, Double.NaN))
        assertFalse(tracker.update(600, PreviewMotion(Double.POSITIVE_INFINITY, .9)))
    }

    @Test fun timeAndNormalizedSpeedWorkAtDifferentFrameRates() {
        for (step in listOf(25L, 100L)) {
            val tracker = MotionStability()
            tracker.update(0, null)
            for (time in step..500L step step) tracker.update(time, PreviewMotion(.02 * step / 1000, .9))
            assertTrue(tracker.isStable)
        }
    }
}
