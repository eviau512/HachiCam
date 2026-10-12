package com.scanner.app.data.util

import com.scanner.app.BuildConfig
import kotlinx.coroutines.*
import java.util.concurrent.TimeUnit

object DiagnosticAccess {
    @Volatile var rootAvailable: Boolean = false
        private set

    fun initialize() {
        if (!BuildConfig.SENSITIVE_DIAGNOSTICS) return
        CoroutineScope(SupervisorJob() + Dispatchers.IO).launch {
            rootAvailable = command(listOf("su", "-c", "id"), 15).contains("uid=0")
        }
    }

    /** Bound both runtime and output; a denied or missing su must never hang UI. */
    fun command(args: List<String>, timeoutSeconds: Long = 5): String {
        if (!BuildConfig.SENSITIVE_DIAGNOSTICS) return "(disabled in release)"
        val process = try { ProcessBuilder(args).redirectErrorStream(true).start() }
        catch (_: Exception) { return "(unavailable)" }
        val output = StringBuilder()
        val reader = Thread {
            runCatching {
                process.inputStream.bufferedReader().useLines { lines ->
                    lines.forEach { line -> synchronized(output) {
                        if (output.length < 1024 * 1024) output.appendLine(line.take(8192))
                    } }
                }
            }
        }.apply { isDaemon = true; start() }
        return try {
            if (!process.waitFor(timeoutSeconds, TimeUnit.SECONDS)) process.destroyForcibly()
            reader.join(1000)
            synchronized(output) { output.toString() }
        } finally { process.destroy(); process.inputStream.close() }
    }
}
