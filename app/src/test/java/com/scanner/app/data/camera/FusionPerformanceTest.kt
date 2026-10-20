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
    @Test fun gpuExpansionCountersAndNestedTimingDecodeWithBounds() {
        val expanded=packet().copyOf(54).apply {this[44]=2;this[50]=6;this[51]=192;this[52]=1234;this[53]=2.0.toBits()}
        val result=FusionPerformance.decode(expanded)!!
        assertEquals(6,result.gpuNoiseGrids);assertEquals(192,result.gpuMergeTiles)
        assertEquals(1234L,result.cpuMergePixels);assertEquals(2.0,result.gpuMergeMs,0.0)
        val invalid=listOf(expanded.copyOf().apply {this[50]=8},expanded.copyOf().apply {this[51]=0},
            expanded.copyOf().apply {this[52]=16_000_001},expanded.copyOf().apply {this[53]=9.0.toBits()})
        invalid.forEach {try {FusionPerformance.decode(it);fail("Invalid GPU counters accepted")}catch(_:IllegalArgumentException){}}
    }
    @Test fun selectorCountersAndCallTimeRemainInsideLocalRegistration() {
        val value=packet().copyOf(57).apply {this[44]=3;this[54]=221;this[55]=7;this[56]=1.5.toBits()}
        val result=FusionPerformance.decode(value)!!
        assertEquals(221,result.gpuSelectorPatches);assertEquals(7,result.cpuSelectorPatches);assertEquals(1.5,result.selectorMs,0.0)
        for(invalid in listOf(value.copyOf().apply {this[55]=222},value.copyOf().apply {this[56]=11.0.toBits()},value.copyOf().apply {this[54]=24_577}))
            try {FusionPerformance.decode(invalid);fail("Invalid selector telemetry accepted")}catch(_:IllegalArgumentException){}
    }
    @Test fun globalIterationsAndCompleteCallTimeAreBoundedInsideGlobalInterval() {
        val value=packet().copyOf(60).apply {this[44]=4;this[57]=3;this[58]=53;this[59]=5.0.toBits()}
        val result=FusionPerformance.decode(value)!!
        assertEquals(3,result.gpuGlobalCalls);assertEquals(53,result.gpuGlobalIterations);assertEquals(5.0,result.gpuGlobalMs,0.0)
        for(invalid in listOf(value.copyOf().apply {this[57]=0},value.copyOf().apply {this[58]=121},value.copyOf().apply {this[59]=13.0.toBits()},value.copyOf().apply {this[58]=2}))
            try {FusionPerformance.decode(invalid);fail("Invalid global telemetry accepted")}catch(_:IllegalArgumentException){}
    }
}
