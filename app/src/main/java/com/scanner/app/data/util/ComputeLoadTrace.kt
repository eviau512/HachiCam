package com.scanner.app.data.util

/** Counter semantics are kept independent of Android and of the sampling transport. */
object ComputeLoadMath {
    data class Cpu(val fields: List<Long>)
    data class Device(val observedMs: Double, val readMs: Double, val cpu: Cpu?,
                      val gpuBusy: Long?, val gpuTotal: Long?, val gpuHz: Long?)

    fun cpu(text: String): Cpu? {
        val words = text.trim().split(Regex("\\s+"))
        if (words.size < 9 || words[0] != "cpu") return null
        val fields = words.subList(1, 9).map { it.toLongOrNull()?.takeIf { n -> n >= 0 } ?: return null }
        return try { fields.fold(0L, Math::addExact); Cpu(fields) } catch (_: ArithmeticException) { null }
    }

    fun cpuPercent(before: Cpu?, after: Cpu?): Double? {
        if (before == null || after == null || before.fields.size != 8 || after.fields.size != 8) return null
        val delta = after.fields.zip(before.fields).map { (a, b) -> if (a < b) return null else a - b }
        val total = try { delta.fold(0L, Math::addExact) } catch (_: ArithmeticException) { return null }
        if (total <= 0) return null
        return 100.0 * (total - delta[3] - delta[4]) / total
    }

    fun gpuPercent(busy: Long?, total: Long?): Double? =
        if (busy != null && total != null && total > 0 && busy in 0..total) 100.0 * busy / total else null

    fun device(line: String): Device? {
        if (line.length > 1536) return null
        val words = line.split('|')
        if (words.size != 6 || words[0] != "HCLOAD") return null
        val start = words[1].toDoubleOrNull() ?: return null
        val end = words[2].toDoubleOrNull() ?: return null
        if (!start.isFinite() || !end.isFinite() || start < 0 || end < start || end > 315_576_000 || end - start > 3) return null
        val gpu = words[4].trim().split(Regex("\\s+"))
        val busy = gpu.getOrNull(0)?.toLongOrNull()?.takeIf { it >= 0 }
        val total = gpu.getOrNull(1)?.toLongOrNull()?.takeIf { it >= 0 }
        val pairValid = gpu.size == 2 && busy != null && total != null && busy <= total
        return Device(end * 1000, (end - start) * 1000, cpu(words[3]),
            if (pairValid) busy else null, if (pairValid) total else null,
            words[5].trim().toLongOrNull()?.takeIf { it in 1..10_000_000_000L })
    }
}

class ComputeLoadTrace(val captureId: String, private val originNs: Long,
                       initialCpuMs: Long, initialCores: Int) {
    companion object { const val LIMIT = 600; const val INTERVAL_MS = 500L; const val FRESH_MS = 1500.0 }
    data class Sample(val offsetMs: Double, val intervalMs: Double, val stage: String,
        val appOneCorePct: Double?, val appCapacityPct: Double?, val availableCores: Int?, val deviceCpuPct: Double?,
        val gpuPct: Double?, val gpuHz: Long?, val thermal: Int?, val readerAgeMs: Double?,
        val readMs: Double?, val gpuBusy: Long?, val gpuTotal: Long?, val status: String)
    data class Snapshot(val captureId: String, val durationMs: Double, val samples: List<Sample>,
                        val dropped: Int, val endReason: String)
    private var lastNs = originNs
    private var lastCpuMs = initialCpuMs
    private var lastCores = initialCores
    private var lastDevice: ComputeLoadMath.Device? = null
    private val samples = ArrayDeque<Sample>()
    private var dropped = 0
    private var frozen: Snapshot? = null

    @Synchronized fun record(nowNs: Long, processCpuMs: Long, cores: Int, stage: String,
                             device: ComputeLoadMath.Device?, thermal: Int?, status: String) {
        if (frozen != null || nowNs <= lastNs) return
        val interval = (nowNs - lastNs) / 1e6
        val app = if (interval >= 100 && processCpuMs >= lastCpuMs && lastCpuMs >= 0) (processCpuMs - lastCpuMs) * 100.0 / interval else null
        val capacity = if (cores > 0 && cores == lastCores) app?.div(cores) else null
        val age = device?.let { nowNs / 1e6 - it.observedMs }
        val fresh = device != null && age != null && age in 0.0..FRESH_MS
        val distinct = fresh && (lastDevice == null || device!!.observedMs > lastDevice!!.observedMs)
        val system = if (distinct) ComputeLoadMath.cpuPercent(lastDevice?.cpu, device?.cpu) else null
        if (distinct) lastDevice = device
        if (samples.size == LIMIT) { samples.removeFirst(); dropped++ }
        samples.addLast(Sample((nowNs - originNs).coerceAtLeast(0) / 1e6, interval, stage.take(80), app, capacity, cores.takeIf { it > 0 },
            system, if (fresh) ComputeLoadMath.gpuPercent(device?.gpuBusy, device?.gpuTotal) else null,
            if (fresh) device?.gpuHz else null, thermal?.takeIf { it in 0..6 }, age?.takeIf { it.isFinite() },
            device?.readMs, if (fresh) device?.gpuBusy else null, if (fresh) device?.gpuTotal else null,
            when { !fresh -> if (device == null) status.take(80) else "stale_device_snapshot"
                device?.gpuTotal == 0L -> "gpu_zero_driver_window"
                device?.gpuBusy == null || device.gpuTotal == null -> "gpu_counter_missing_denied_or_invalid"
                else -> "${status.take(40)}:device_driver_window" }))
        lastNs = nowNs; lastCpuMs = processCpuMs; lastCores = cores
    }

    @Synchronized fun finish(nowNs: Long, reason: String): Snapshot {
        frozen?.let { return it }
        return Snapshot(captureId, (nowNs - originNs).coerceAtLeast(0) / 1e6,
            samples.toList(), dropped, reason.take(80)).also { frozen = it }
    }
}
