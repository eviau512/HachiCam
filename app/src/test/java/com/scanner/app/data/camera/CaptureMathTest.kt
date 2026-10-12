package com.scanner.app.data.camera

import org.junit.Assert.*
import org.junit.Test
import kotlin.math.pow

class CaptureMathTest {
    @Test fun bracketsUseRelativeEVAndDeviceLimits() {
        val base = ManualExposure(20_000_000, 800)
        for (stops in listOf(2.5, 6.0)) {
            val bracket = ExposurePlan.bracket(base, stops, 100_000, 100_000_000, 50, 6400)!!
            assertTrue(bracket.iso in 50..6400)
            assertTrue(bracket.timeNs in 100_000L..100_000_000L)
            assertEquals(base.product / 2.0.pow(stops), bracket.product, base.product * .001)
        }
    }
    @Test fun impossibleBracketDoesNotIncreaseBrightSceneExposure() {
        assertNull(ExposurePlan.bracket(ManualExposure(50_000, 50), 6.0, 50_000, 1_000_000, 50, 800))
    }
    @Test fun allDeviceRotationsRoundTrip() {
        for (angle in listOf(0, 90, 180, 270)) {
            val source = UnitPoint(.12f, .73f)
            val p = ImageCoordinates.rotate(ImageCoordinates.rotate(source, angle), -angle)
            assertEquals(source.x, p.x, .00001f)
            assertEquals(source.y, p.y, .00001f)
        }
    }
    @Test fun fitCenterUsesLetterboxAndRejectsBlackBars() {
        assertNull(ImageCoordinates.fitPoint(100f, 50f, 1000f, 2000f, 750f, 1000f))
        val center = ImageCoordinates.fitPoint(500f, 1000f, 1000f, 2000f, 750f, 1000f)!!
        assertEquals(.5f, center.x, .00001f)
        assertEquals(.5f, center.y, .00001f)
    }
}
