package com.scanner.app.data.camera

import org.junit.Assert.*
import org.junit.Test

class CapturePolicyTest {
    @Test fun sizesRespectNativeProfileAndPixelBudget() {
        val sizes = listOf(CapturePolicy.Size(8192,6144), CapturePolicy.Size(4096,3072),
            CapturePolicy.Size(4608,3456), CapturePolicy.Size(12000,9000), CapturePolicy.Size(0,100))
        assertEquals(CapturePolicy.Size(4608,3456), CapturePolicy.choose(sizes, false))
        assertEquals(CapturePolicy.Size(8192,6144), CapturePolicy.choose(sizes, true))
        assertNull(CapturePolicy.choose(listOf(CapturePolicy.Size(4096,3072)), true))
        assertNull(CapturePolicy.choose(emptyList(), false))
    }
    @Test fun highResolutionRequiresAvailabilityMarginAndNoLowMemoryFlag() {
        val pixels = 50_331_648L
        val peak = CapturePolicy.highPeakBytes(pixels)
        assertFalse(CapturePolicy.canUseHigh(pixels, peak, false))
        assertTrue(CapturePolicy.canUseHigh(pixels, peak * 2, false))
        assertFalse(CapturePolicy.canUseHigh(pixels, peak * 2, true))
        assertFalse(CapturePolicy.canUseHigh(60_000_000, Long.MAX_VALUE, false))
        assertFalse(CapturePolicy.canUseHigh(-1, Long.MAX_VALUE, false))
    }
    @Test fun exclusiveJobBlocksCaptureQueueAndCancellationReleasesIt() {
        val a = BurstAdmission()
        val high = a.reserve(true)!!
        assertNull(a.reserve()); assertNull(a.reserve(true))
        a.release(high)
        val normal = a.reserve()!!
        assertNull(a.reserve(true))
        a.release(normal)
        assertNotNull(a.reserve(true))
    }
    @Test fun normalCapacityAndIdempotentReleasePreserveAdmission() {
        val a = BurstAdmission()
        val tickets = List(3) { a.reserve()!! }
        assertNull(a.reserve())
        a.release(tickets[0]); a.release(tickets[0])
        assertNotNull(a.reserve()); assertNull(a.reserve())
    }
}
