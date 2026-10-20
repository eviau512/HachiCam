package com.scanner.app.data.camera

import com.scanner.app.data.util.ComputeLoadMath
import com.scanner.app.data.util.ComputeLoadTrace
import org.junit.Assert.*
import org.junit.Test

class ComputeLoadTraceTest {
    private fun cpu(user: Long, idle: Long) = ComputeLoadMath.cpu("cpu $user 0 0 $idle 0 0 0 0 999999 888888")!!
    private fun device(time: Double, user: Long = 100, idle: Long = 100) =
        ComputeLoadMath.Device(time, 0.0, cpu(user, idle), 25, 100, 500_000_000)

    @Test fun deviceCpuUsesEightFieldsAndDoesNotCountGuestTwice() {
        assertEquals(40.0, ComputeLoadMath.cpuPercent(cpu(100, 100), cpu(140, 160))!!, 1e-9)
        assertNull(ComputeLoadMath.cpuPercent(cpu(100, 100), cpu(100, 100)))
        assertNull(ComputeLoadMath.cpuPercent(cpu(100, 100), cpu(99, 110)))
        assertNull(ComputeLoadMath.cpuPercent(null, cpu(100, 100)))
        assertNull(ComputeLoadMath.cpu("cpu 1 2 -1 4 5 6 7 8"))
        assertNull(ComputeLoadMath.cpu("cpu 1 2"))
        assertNull(ComputeLoadMath.cpu("cpu 9223372036854775807 1 0 0 0 0 0 0"))
    }

    @Test fun gpuWindowIsARatioRatherThanACumulativeCounterDelta() {
        assertEquals(25.0, ComputeLoadMath.gpuPercent(25, 100)!!, 1e-9)
        assertEquals(25.0, ComputeLoadMath.gpuPercent(25, 100)!!, 1e-9) // Same driver window may repeat.
        assertEquals(0.0, ComputeLoadMath.gpuPercent(0, 100)!!, 0.0)
        for ((busy, total) in listOf(0L to 0L, -1L to 10L, 11L to 10L))
            assertNull(ComputeLoadMath.gpuPercent(busy, total))
        assertNull(ComputeLoadMath.gpuPercent(null, 100))
    }

    @Test fun helperProtocolRejectsInvalidClockSizeAndMalformedCounterValues() {
        val line = "HCLOAD|1.0|1.01|cpu 10 0 0 20 0 0 0 0|25 100|500000000"
        val got = ComputeLoadMath.device(line)!!
        assertEquals(1010.0, got.observedMs, 1e-6)
        assertEquals(10.0, got.readMs, 1e-6)
        assertEquals(25L, got.gpuBusy)
        assertEquals(500_000_000L, got.gpuHz)
        assertNull(ComputeLoadMath.device(line.replace("1.01", "0.9")))
        assertNull(ComputeLoadMath.device(line.replace("1.01", "NaN")))
        assertNull(ComputeLoadMath.device(line.replace("1.01", "5.0")))
        assertNull(ComputeLoadMath.device("x".repeat(1537)))
        val missing = ComputeLoadMath.device("HCLOAD|1|1.01||permission denied|")!!
        assertNull(missing.cpu); assertNull(missing.gpuBusy); assertNull(missing.gpuHz)
        assertNull(ComputeLoadMath.device(line.replace("25 100", "200 100"))!!.gpuBusy)
    }

    @Test fun processCpuCanExceedOneCoreAndCapacityIsExplicit() {
        val trace = ComputeLoadTrace("A", 1_000_000_000, 100, 4)
        trace.record(1_500_000_000, 900, 4, "nativeFusion", null, 2, "unavailable")
        val sample = trace.finish(1_600_000_000, "finished").samples.single()
        assertEquals(160.0, sample.appOneCorePct!!, 1e-9)
        assertEquals(40.0, sample.appCapacityPct!!, 1e-9)
        assertEquals(4, sample.availableCores)
        assertNull(sample.deviceCpuPct); assertNull(sample.gpuPct)
        assertEquals(2, sample.thermal)
    }

    @Test fun deviceReadFreshnessAndDistinctCpuSnapshotsAreRequired() {
        val trace = ComputeLoadTrace("A", 1_000_000_000, 100, 4)
        trace.record(1_500_000_000, 200, 4, "queue", device(1500.0), null, "normal")
        trace.record(1_550_000_000, 300, 4, "nativeFusion", device(1550.0, 140, 160), null, "normal")
        trace.record(1_600_000_000, 400, 4, "nativeFusion", device(1550.0, 140, 160), null, "normal")
        trace.record(2_000_000_000, 500, 4, "save", device(1550.0), null, "normal")
        val samples = trace.finish(2_000_000_000, "finished").samples
        assertNull(samples[0].deviceCpuPct)
        assertEquals(40.0, samples[1].deviceCpuPct!!, 1e-9)
        assertEquals(50.0, samples[1].deviceCpuIntervalMs!!, 1e-9)
        assertNull(samples[2].deviceCpuPct)
        assertNull(samples[2].deviceCpuIntervalMs)
        assertEquals(25.0, samples[2].gpuPct!!, 1e-9)
        assertNull(samples[3].gpuPct); assertNull(samples[3].gpuHz)
        assertEquals("stale_device_snapshot", samples[3].status)
    }

    @Test fun counterResetAndChangedProcessorCountRemainUnknownForThatInterval() {
        val trace = ComputeLoadTrace("A", 1_000_000_000, 100, 4)
        trace.record(1_500_000_000, 99, 8, "nativeFusion", device(1500.0), null, "normal")
        trace.record(2_000_000_000, 199, 8, "nativeFusion", device(2000.0, 90, 110), null, "normal")
        val samples = trace.finish(2_000_000_000, "finished").samples
        assertNull(samples[0].appOneCorePct); assertNull(samples[0].appCapacityPct)
        assertEquals(8, samples[0].availableCores)
        assertNull(samples[1].deviceCpuPct)
        assertEquals(2.5, samples[1].appCapacityPct!!, 1e-9)
    }

    @Test fun startupSubTickIntervalsDoNotInventProcessCpuSpikes() {
        val trace = ComputeLoadTrace("A", 1_000_000_000, 100, 8)
        trace.record(1_001_000_000, 110, 8, "acquisition", null, null, "starting")
        assertNull(trace.finish(1_002_000_000, "finished").samples.single().appOneCorePct)
    }

    @Test fun fiftyMillisecondCpuSamplesRetainRawCountersAndAreNotDiscarded() {
        val trace = ComputeLoadTrace("A", 1_000_000_000, 100, 4)
        trace.record(1_050_000_000, 110, 4, "nativeFusion", null, null, "missing")
        trace.record(1_100_000_000, 160, 4, "nativeFusion", null, null, "missing")
        trace.record(1_150_000_000, 90, 4, "nativeFusion", null, null, "missing")
        val samples = trace.finish(1_160_000_000, "finished").samples
        assertEquals(50.0, samples[0].intervalMs, 0.0)
        assertEquals(20.0, samples[0].appOneCorePct!!, 1e-9)
        assertEquals(5.0, samples[0].appCapacityPct!!, 1e-9)
        assertEquals(110L, samples[0].processCpuMs)
        assertEquals(10L, samples[0].processCpuDeltaMs)
        assertEquals(100.0, samples[1].appOneCorePct!!, 1e-9)
        assertEquals(50L, samples[1].processCpuDeltaMs)
        assertEquals(90L, samples[2].processCpuMs)
        assertNull(samples[2].processCpuDeltaMs)
        assertNull(samples[2].appOneCorePct)
    }

    @Test fun boundedImmutableSnapshotsDoNotMixCaptureIdentitiesOrAcceptLaterSamples() {
        val a = ComputeLoadTrace("A", 0, 0, 4)
        val b = ComputeLoadTrace("B", 100_000_000, 0, 4)
        for (i in 1..6050) a.record(i * 50_000_000L, i.toLong(), 4, "nativeFusion", null, null, "missing")
        b.record(600_000_000, 10, 4, "queue", null, null, "missing")
        val frozen = a.finish(303_000_000_000, "finished")
        assertEquals(6000, frozen.samples.size); assertEquals(50, frozen.dropped)
        assertEquals(299_950.0, frozen.samples.last().offsetMs - frozen.samples.first().offsetMs, 0.0)
        assertEquals("A", frozen.captureId)
        a.record(327_000_000_000, 999, 4, "late", null, null, "missing")
        assertSame(frozen, a.finish(328_000_000_000, "changed"))
        assertEquals("B", b.finish(700_000_000, "cancelled").captureId)
        assertEquals("queue", b.finish(700_000_000, "changed").samples.single().stage)
    }
}
