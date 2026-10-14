package com.scanner.app.data.camera
import org.junit.Assert.*
import org.junit.Test
class FusionPerformanceTest {
    private fun packet()=LongArray(50).apply { this[44]=1;this[45]=30.0.toBits();this[46]=12.0.toBits();this[47]=10.0.toBits();this[48]=3;this[49]=100 }
    @Test fun perResultTimingAndCountsHaveExplicitRemainingScopeAndLegacyUnknown() {
        val result=FusionPerformance.decode(packet())!!
        assertEquals(8.0,result.remainingNativeMs,0.0);assertEquals(3,result.gpuSupportMaps);assertEquals(100,result.gpuFitProposals)
        assertNull(FusionPerformance.decode(LongArray(42)));assertNull(FusionPerformance.decode(LongArray(44)))
    }
    @Test fun invalidOrOverlappingTelemetryFailsBeforeNativeOwnershipIsPublished() {
        val invalid=listOf(packet().apply { this[44]=2 },packet().apply { this[45]=Double.NaN.toBits() },
            packet().apply { this[46]=40.0.toBits() },packet().apply { this[48]=7 },packet().apply { this[49]=-1 })
        invalid.forEach { try { FusionPerformance.decode(it);fail("Invalid timing accepted") } catch(_:IllegalArgumentException){} }
    }
}
