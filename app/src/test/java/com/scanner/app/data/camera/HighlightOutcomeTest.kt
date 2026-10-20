package com.scanner.app.data.camera
import org.junit.Assert.*
import org.junit.Test

class HighlightOutcomeTest {
    @Test fun onePixelAndNoClippedBaselineCannotImplySuccessfulHdr() {
        assertEquals("near_highlight_adjustment",HighlightOutcome.classify(12_000_000,0.0,1.0/12_000_000))
        assertEquals("limited_pixel_changes",HighlightOutcome.classify(12_000_000,.01,1.0/12_000_000))
        assertEquals("no_reliable_recovery",HighlightOutcome.classify(12_000_000,.01,0.0))
    }
    @Test fun partialChangesAndUnknownEvidenceRemainDistinct() {
        assertEquals("partial_highlight_recovery",HighlightOutcome.classify(12_000_000,.008,.00058))
        assertEquals("limited_pixel_changes",HighlightOutcome.classify(12_000_000,.05,.0001))
        assertEquals("unknown",HighlightOutcome.classify(0,.01,.01))
        assertEquals("unknown",HighlightOutcome.classify(12_000_000,Double.NaN,.01))
    }
}
