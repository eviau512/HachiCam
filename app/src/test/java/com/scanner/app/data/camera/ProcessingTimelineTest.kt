package com.scanner.app.data.camera

import com.scanner.app.data.util.ProcessingTimeline
import org.junit.Assert.*
import org.junit.Test

class ProcessingTimelineTest {
    @Test fun overlappingCaptureStagesDoNotChangeEarlierSnapshotOrAnotherJob() {
        var now = 0L
        val first = ProcessingTimeline { now }
        first.start("acquisition"); now = 2_000_000
        first.start("frame"); now = 5_000_000
        first.end("frame"); val frozen = first.snapshot()
        val second = ProcessingTimeline { now }
        second.start("queue"); now = 7_000_000
        first.end("acquisition"); second.end("queue", "failed")
        assertEquals(3.0, frozen.single().durationMs, 0.0)
        assertEquals(1, frozen.size)
        assertEquals(7.0, first.snapshot().last().durationMs, 0.0)
        assertEquals("failed", second.snapshot().single().outcome)
        assertEquals(2.0, second.snapshot().single().durationMs, 0.0)
        assertEquals(emptyList<String>(), first.pending())
    }
    @Test fun duplicateStartAndUnknownEndCannotFabricateDuration() {
        val timing = ProcessingTimeline { 0 }
        timing.start("fusion")
        try { timing.start("fusion"); fail("duplicate stage") } catch (_: IllegalArgumentException) { }
        try { timing.end("other"); fail("unknown stage") } catch (_: IllegalStateException) { }
        assertTrue(timing.snapshot().isEmpty())
        timing.end("fusion", "fallback")
        assertEquals("fallback", timing.snapshot().single().outcome)
    }
}
