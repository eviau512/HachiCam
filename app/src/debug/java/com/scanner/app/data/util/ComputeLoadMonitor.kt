package com.scanner.app.data.util

import android.os.SystemClock
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/** One off-thread reader shared by all admitted captures; it only reads fixed counters. */
object ComputeLoadMonitor {
    const val enabled = true
    private val guard = Any()
    private val sessions = linkedSetOf<Session>()
    private val executor = Executors.newSingleThreadScheduledExecutor { runnable ->
        Thread(runnable, "HachiCam-load-sampler").apply { isDaemon = true }
    }
    private var scheduled: ScheduledFuture<*>? = null
    private var helper: Helper? = null // Only the sampler executor owns process lifecycle.
    @Volatile private var power: android.os.PowerManager? = null
    private var thermalObservedNs = Long.MIN_VALUE
    private var thermalCached: Int? = null

    fun start(id: String, timeline: ProcessingTimeline, context: android.content.Context): Session {
        if (power == null) power = context.applicationContext.getSystemService(android.os.PowerManager::class.java)
        val now = SystemClock.elapsedRealtimeNanos()
        val session = Session(ComputeLoadTrace(id, now, android.os.Process.getElapsedCpuTime(),
            Runtime.getRuntime().availableProcessors()), timeline)
        synchronized(guard) {
            check(sessions.size < 3) { "Load capture admission exceeded" }
            sessions.add(session)
            if (scheduled == null) scheduled = executor.scheduleWithFixedDelay({ tick() }, 0,
                ComputeLoadTrace.INTERVAL_MS, TimeUnit.MILLISECONDS)
        }
        return session
    }

    class Session internal constructor(internal val trace: ComputeLoadTrace, internal val timeline: ProcessingTimeline) : AutoCloseable {
        private var closed = false
        private var json: String? = null
        @Synchronized fun finish(): String {
            json?.let { return it }
            val snapshot = trace.finish(SystemClock.elapsedRealtimeNanos(), "finished")
            close()
            return serialize(snapshot).also { json = it }
        }
        @Synchronized override fun close() {
            if (closed) return
            closed = true
            trace.finish(SystemClock.elapsedRealtimeNanos(), "cancelled_or_failed")
            synchronized(guard) {
                sessions.remove(this)
                if (sessions.isEmpty()) { scheduled?.cancel(false); scheduled = null }
            }
            executor.execute { if (synchronized(guard) { sessions.isEmpty() }) { helper?.stop(); helper = null } }
        }
    }

    private fun tick() {
        val active = synchronized(guard) { sessions.toList() }
        if (active.isEmpty()) return
        try {
            if (helper == null || (helper?.root == false && DiagnosticAccess.rootAvailable)) {
                helper?.stop(); helper = Helper(DiagnosticAccess.rootAvailable)
            }
            val reader = helper!!
            val now = SystemClock.elapsedRealtimeNanos()
            val snapshot = reader.latest.get()
            if (now - reader.startedNs > 5_000_000_000L &&
                (snapshot == null || now / 1e6 - snapshot.observedMs > 3000)) reader.stop()
            val cpuMs = android.os.Process.getElapsedCpuTime()
            val cores = Runtime.getRuntime().availableProcessors()
            // Thermal severity is separate from load. Do not issue 20 Binder calls/sec.
            if (thermalObservedNs == Long.MIN_VALUE || now - thermalObservedNs >= ComputeLoadTrace.THERMAL_INTERVAL_MS * 1_000_000) {
                thermalCached = runCatching { power?.currentThermalStatus }.getOrNull()
                thermalObservedNs = now
            }
            active.forEach { session ->
                session.trace.record(now, cpuMs, cores, session.timeline.pending().lastOrNull() ?: "between_stages",
                    snapshot, thermalCached, reader.status)
            }
        } catch (_: Exception) {
            val now = SystemClock.elapsedRealtimeNanos()
            active.forEach { it.trace.record(now, android.os.Process.getElapsedCpuTime(),
                Runtime.getRuntime().availableProcessors(), it.timeline.pending().lastOrNull() ?: "between_stages",
                null, null, "sampler_exception") }
        }
    }

    private class Helper(val root: Boolean) {
        val startedNs = SystemClock.elapsedRealtimeNanos()
        val latest = AtomicReference<ComputeLoadMath.Device?>()
        @Volatile var status = if (root) "root_helper_starting" else "unprivileged_helper_starting"
        private var process: Process? = null
        init {
            // gpubusy is a driver-window pair; never subtract successive values.
            val script = """
                i=0
                gpu=/sys/class/kgsl/kgsl-3d0/gpubusy
                freq=/sys/class/kgsl/kgsl-3d0/gpuclk
                while [ "${'$'}i" -lt ${ComputeLoadTrace.LIMIT} ]; do
                  before=; after=; cpu=; busy=; clock=
                  read -r before rest < /proc/uptime 2>/dev/null
                  IFS= read -r cpu < /proc/stat 2>/dev/null
                  IFS= read -r busy < "${'$'}gpu" 2>/dev/null
                  IFS= read -r clock < "${'$'}freq" 2>/dev/null
                  read -r after rest < /proc/uptime 2>/dev/null
                  printf 'HCLOAD|%s|%s|%s|%s|%s\n' "${'$'}before" "${'$'}after" "${'$'}cpu" "${'$'}busy" "${'$'}clock" || exit 0
                  i=${'$'}((i+1))
                  stop=
                  read -r -t ${ComputeLoadTrace.INTERVAL_MS / 1000.0} stop
                  [ "${'$'}stop" = stop ] && exit 0
                done
            """.trimIndent()
            try {
                process = ProcessBuilder(if (root) listOf("su", "-c", script) else listOf("sh", "-c", script))
                    .redirectErrorStream(true).start()
                val owned = process!!
                Thread({
                    try {
                        owned.inputStream.bufferedReader().use { reader ->
                            val line = StringBuilder(); var total = 0
                            while (total++ < 4 * 1024 * 1024) {
                                val next = reader.read(); if (next < 0) break
                                if (next == 10) {
                                    ComputeLoadMath.device(line.toString())?.let {
                                        latest.set(it); status = if (root) "root_helper" else "unprivileged_helper"
                                    }
                                    line.setLength(0)
                                } else if (line.length < 1537) line.append(next.toChar())
                            }
                        }
                    } catch (_: Exception) { status = "helper_read_failed" }
                    finally { status = "helper_ended"; owned.destroy() }
                }, "HachiCam-load-reader").apply { isDaemon = true; start() }
            } catch (_: Exception) { status = "helper_unavailable" }
        }
        fun stop() {
            status = "helper_stopped"
            process?.let { owned ->
                // The stdin command reaches the root child too; do not rely on
                // terminating only the su parent. Cleanup never blocks export.
                Thread({
                    runCatching { owned.outputStream.write("stop\n".toByteArray()); owned.outputStream.flush(); owned.outputStream.close() }
                    if (!runCatching { owned.waitFor(1500, TimeUnit.MILLISECONDS) }.getOrDefault(false)) owned.destroyForcibly()
                    runCatching { owned.inputStream.close() }
                }, "HachiCam-load-stop").apply { isDaemon = true; start() }
            }
            process = null
        }
    }

    private fun serialize(snapshot: ComputeLoadTrace.Snapshot): String {
        fun value(v: Any?): Any = when (v) {
            null -> JSONObject.NULL
            is Double -> if (v.isFinite()) kotlin.math.round(v * 1000) / 1000 else JSONObject.NULL
            else -> v
        }
        val result = JSONObject().put("schema", 2).put("captureId", snapshot.captureId)
            .put("clock", "elapsedRealtime/procUptime").put("targetIntervalMs", ComputeLoadTrace.INTERVAL_MS)
            .put("durationMs", snapshot.durationMs).put("droppedSamples", snapshot.dropped).put("endReason", snapshot.endReason)
            .put("scope", "app CPU is whole process; device CPU/GPU include other jobs/apps/display/camera; not per-capture attribution")
            .put("appCpuProvider", "Process.getElapsedCpuTime; one-core equivalent may exceed 100; capacity uses stable availableProcessors")
            .put("appCpuMinimumIntervalMs", ComputeLoadTrace.CPU_MIN_INTERVAL_MS)
            .put("appCpuResolution", "millisecond counters; short-window ratios may be quantized; retain raw deltas for weighted aggregation")
            .put("deviceFreshnessMs", ComputeLoadTrace.FRESH_MS)
            .put("thermalTargetIntervalMs", ComputeLoadTrace.THERMAL_INTERVAL_MS)
            .put("deviceCpuProvider", "/proc/stat aggregate first eight fields; idle+iowait are non-busy")
            .put("gpuProvider", "Qualcomm KGSL gpubusy; busy/total recent driver window, not cumulative or sampler interval")
            .put("gpuCadence", "50 ms read target; driver windows may repeat or update more slowly")
            .put("rootAvailableAtFinish", DiagnosticAccess.rootAvailable)
            .put("sampleCount", snapshot.samples.size)
            .put("validAppCpuSamples", snapshot.samples.count { it.appOneCorePct != null })
            .put("validDeviceCpuSamples", snapshot.samples.count { it.deviceCpuPct != null })
            .put("validGpuSamples", snapshot.samples.count { it.gpuPct != null })
            .put("columns", JSONArray(listOf("offsetMs", "intervalMs", "stage", "appCpuOneCorePct", "appCpuCapacityPct", "availableLogicalProcessors",
                "deviceCpuPct", "deviceGpuBusyPct", "gpuClockHz", "thermalSeverity", "readerAgeMs", "counterReadMs",
                "gpuBusyRaw", "gpuTotalRaw", "status", "processCpuMs", "processCpuDeltaMs", "deviceCpuIntervalMs")))
            .put("samples", JSONArray().apply { snapshot.samples.forEach { s ->
                put(JSONArray(listOf(value(s.offsetMs), value(s.intervalMs), s.stage, value(s.appOneCorePct), value(s.appCapacityPct), value(s.availableCores),
                    value(s.deviceCpuPct), value(s.gpuPct), value(s.gpuHz), value(s.thermal), value(s.readerAgeMs),
                    value(s.readMs), value(s.gpuBusy), value(s.gpuTotal), s.status,
                    value(s.processCpuMs), value(s.processCpuDeltaMs), value(s.deviceCpuIntervalMs))))
            } })
        return result.toString().takeIf { it.toByteArray(Charsets.UTF_8).size <= ComputeLoadTrace.MAX_BYTES }
            ?: JSONObject().put("schema", 2).put("captureId", snapshot.captureId).put("error", "trace_size_limit").toString()
    }
}
