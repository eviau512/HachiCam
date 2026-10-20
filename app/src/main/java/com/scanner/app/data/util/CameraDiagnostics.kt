package com.scanner.app.data.util

import com.scanner.app.BuildConfig

/** Test-only bounded metadata survives logcat rollover; never retains image bytes. */
object CameraDiagnostics {
    private var capabilities = ""
    private var negotiated = ""
    private val journal = CaptureJournal()
    @Synchronized fun capabilities(lines: List<String>) {
        if (BuildConfig.SENSITIVE_DIAGNOSTICS) { capabilities = lines.joinToString("\n").take(32768); negotiated = "" }
    }
    @Synchronized fun negotiated(text: String) { if (BuildConfig.SENSITIVE_DIAGNOSTICS) negotiated = text.take(1024) }
    @Synchronized fun beginBurst(id: String) {
        if (BuildConfig.SENSITIVE_DIAGNOSTICS) journal.begin(id, "$capabilities\nNegotiated: $negotiated")
    }
    @Synchronized fun frame(id: String, text: String) {
        if (BuildConfig.SENSITIVE_DIAGNOSTICS) journal.record(id, text)
    }
    @Synchronized fun snapshot(): String = if (!BuildConfig.SENSITIVE_DIAGNOSTICS) "" else
        "Live context at export, latest STARTED capture:\n${journal.snapshot(journal.latestStarted)}"
    @Synchronized fun snapshot(id: String): String = if (BuildConfig.SENSITIVE_DIAGNOSTICS) journal.snapshot(id) else ""
    @Synchronized fun latestCaptureId(): String = if (BuildConfig.SENSITIVE_DIAGNOSTICS) journal.latestStarted else ""
    fun memory(id:String,stage:String) {
        if(!BuildConfig.SENSITIVE_DIAGNOSTICS)return
        val proc=runCatching {
            java.io.File("/proc/self/status").bufferedReader().use { reader ->
                reader.lineSequence().take(100).filter { it.startsWith("VmRSS:") || it.startsWith("VmHWM:") }.joinToString("; ")
            }
        }.getOrDefault("unknown")
        val runtime=Runtime.getRuntime()
        frame(id,"memory stage=$stage javaUsedBytes=${runtime.totalMemory()-runtime.freeMemory()} nativeAllocatedBytes=${android.os.Debug.getNativeHeapAllocatedSize()} proc=$proc; VmHWM is process-lifetime, not capture peak")
    }
}
