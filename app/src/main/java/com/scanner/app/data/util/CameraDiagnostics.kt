package com.scanner.app.data.util

import com.scanner.app.BuildConfig

/** Test-only bounded metadata survives logcat rollover; never retains image bytes. */
object CameraDiagnostics {
    private var capabilities = ""
    private var negotiated = ""
    private var activeBurst = ""
    private val frames = ArrayDeque<String>()
    @Synchronized fun capabilities(lines: List<String>) {
        if (BuildConfig.SENSITIVE_DIAGNOSTICS) { capabilities = lines.joinToString("\n").take(32768); negotiated = "" }
    }
    @Synchronized fun negotiated(text: String) { if (BuildConfig.SENSITIVE_DIAGNOSTICS) negotiated = text.take(1024) }
    @Synchronized fun beginBurst(id: String) {
        if (BuildConfig.SENSITIVE_DIAGNOSTICS) { activeBurst = id; frames.clear() }
    }
    @Synchronized fun frame(id: String, text: String) {
        if (!BuildConfig.SENSITIVE_DIAGNOSTICS || id != activeBurst) return
        frames.addLast(text.take(4096)); while (frames.size > 16) frames.removeFirst()
    }
    @Synchronized fun snapshot(): String = if (!BuildConfig.SENSITIVE_DIAGNOSTICS) "" else
        "$capabilities\nNegotiated: $negotiated\nLatest burst=$activeBurst\n${frames.joinToString("\n")}"
}
