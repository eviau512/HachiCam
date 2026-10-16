package com.scanner.app.data.camera

import org.junit.Assert.*
import org.junit.Test

class ReconstructionDecisionTest {
    private fun packet()=LongArray(42).apply {
        this[3]=2;this[25]=1;this[26]=1;this[28]=100;this[29]=12
        this[30]=.4873.toBits();this[31]=1;this[32]=.12.toBits();this[33]=.085.toBits()
        this[34]=64;this[35]=.02.toBits();this[36]=4;this[37]=.70.toBits();this[38]=.04.toBits()
        this[39]=10;this[40]=3.0.toBits();this[41]=2.0.toBits()
    }
    @Test fun versionedPacketSeparatesWholeSupportFromActualRegionalReconstruction() {
        val value=ReconstructionDecision.decode(packet())
        assertEquals(.4873,value.meanSupport,0.0);assertTrue(value.centrePhases)
        assertEquals(.12,value.regionFraction,0.0);assertEquals(.085,value.reconstructedFraction,0.0)
        assertTrue(value.denialReasons().isEmpty());assertEquals(12,value.qualifiedTiles)
    }
    @Test fun denialFlagsExplainNativeOutputWithoutOverloadingPhases() {
        val input=packet().apply { this[3]=1;this[27]=12;this[29]=0;this[32]=0.0.toBits();this[33]=0.0.toBits();this[39]=0;this[40]=0;this[41]=0 }
        val value=ReconstructionDecision.decode(input)
        assertTrue(value.centrePhases)
        assertEquals(listOf("insufficient_qualified_tiles","insufficient_regional_area"),value.denialReasons())
    }
    @Test fun invalidSchemasFractionsAndInconsistentScaleFailExplicitly() {
        val invalid=listOf(packet().copyOf(25),packet().apply { this[25]=2 },
            packet().apply { this[32]=Double.NaN.toBits() },packet().apply { this[29]=101 },
            packet().apply { this[27]=8 },packet().apply { this[3]=1 })
        invalid.forEach { try { ReconstructionDecision.decode(it);fail("Invalid packet accepted") } catch(_:IllegalArgumentException) {} }
    }
    @Test fun appendedHighlightMetricsAreVersionedAndLegacyDecisionIsUnchanged() {
        val legacy=packet();val extended=legacy.copyOf(44).apply { this[42]=1;this[43]=.03.toBits() }
        assertEquals(ReconstructionDecision.decode(legacy),ReconstructionDecision.decode(extended))
        val timed=extended.copyOf(50).apply { this[44]=1;this[45]=30.0.toBits();this[46]=12.0.toBits();this[47]=10.0.toBits();this[48]=2;this[49]=12 }
        assertEquals(ReconstructionDecision.decode(legacy),ReconstructionDecision.decode(timed))
        val gpu=timed.copyOf(54).apply {this[44]=2;this[50]=4;this[51]=192;this[52]=12;this[53]=1.0.toBits()}
        assertEquals(ReconstructionDecision.decode(legacy),ReconstructionDecision.decode(gpu))
        for(invalid in listOf(extended.copyOf().apply { this[42]=2 },extended.copyOf().apply { this[43]=Double.NaN.toBits() }))
            try { ReconstructionDecision.decode(invalid);fail("Invalid highlight extension") } catch(_:IllegalArgumentException) {}
    }
}
