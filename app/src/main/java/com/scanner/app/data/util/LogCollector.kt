package com.scanner.app.data.util

import android.content.Context
import android.content.Intent
import android.os.Build
import android.widget.Toast
import androidx.core.content.FileProvider
import com.scanner.app.BuildConfig
import com.scanner.app.R
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object LogCollector {
    suspend fun collectAndShareLogs(context: Context) = withContext(Dispatchers.IO) {
        try {
            val directory = File(context.cacheDir, "logs").apply { mkdirs() }
            directory.listFiles { f -> f.isFile && f.name.startsWith("hachicam_log_") }
                ?.sortedByDescending { it.lastModified() }?.drop(4)?.forEach { it.delete() }
            val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
            val file = File(directory, "hachicam_log_$timestamp.txt")
            val text = buildString {
                appendLine("HachiCam Diagnostic Log")
                appendLine("Version: ${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE})")
                appendLine("Build: ${BuildConfig.BUILD_TYPE} ${BuildConfig.GIT_HASH} ${BuildConfig.BUILD_TIME}")
                val runtime = Runtime.getRuntime()
                appendLine("Heap: used=${runtime.totalMemory() - runtime.freeMemory()} max=${runtime.maxMemory()}")
                if (BuildConfig.SENSITIVE_DIAGNOSTICS) {
                    appendLine("--- Test-only device diagnostics ---")
                    appendLine("Manufacturer: ${Build.MANUFACTURER}")
                    appendLine("Model: ${Build.MODEL}")
                    appendLine("Device: ${Build.DEVICE}; Board: ${Build.BOARD}; Hardware: ${Build.HARDWARE}")
                    appendLine("Fingerprint: ${Build.FINGERPRINT}")
                    appendLine("Android: ${Build.VERSION.RELEASE} API ${Build.VERSION.SDK_INT}")
                    appendLine("ABIs: ${Build.SUPPORTED_ABIS.joinToString()}")
                    appendLine("Root available: ${DiagnosticAccess.rootAvailable}")
                    appendLine("--- Live context; may describe a newer capture than the retained images ---")
                    appendLine(CameraDiagnostics.snapshot())
                    appendLine("For the exported image set, use capture/manifest.json and capture/diagnostics.txt as its matching evidence.")
                    appendLine("--- App process log ---")
                    appendLine(DiagnosticAccess.command(listOf("logcat", "-d", "-v", "time", "--pid=${android.os.Process.myPid()}", "-t", "3000")))
                    if (DiagnosticAccess.rootAvailable) {
                        appendLine("--- System camera log (root, test only) ---")
                        appendLine(DiagnosticAccess.command(listOf("su", "-c", "logcat -d -v time -t 2000")))
                    }
                } else {
                    appendLine("Release privacy: device identifiers, root access and logcat collection disabled.")
                }
            }
            file.writeText(text, Charsets.UTF_8)
            val bundle = if (BuildConfig.SENSITIVE_DIAGNOSTICS) runCatching {
                BurstArchiveStore(File(context.cacheDir, "debug_burst"), true)
                    .export(file, File(directory, "hachicam_capture_$timestamp.zip"))
            }.getOrNull() else null
            directory.listFiles { f -> f.isFile && f.name.startsWith("hachicam_capture_") && f.extension == "zip" }
                ?.sortedByDescending { it.lastModified() }?.drop(2)?.forEach { it.delete() }
            val shared = bundle ?: file
            withContext(Dispatchers.Main) {
                val uri = FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", shared)
                val intent = Intent(Intent.ACTION_SEND).apply {
                    type = if (bundle != null) "application/zip" else "text/plain"
                    putExtra(Intent.EXTRA_STREAM, uri)
                    putExtra(Intent.EXTRA_SUBJECT, "HachiCam Diagnostics - ${shared.name}")
                    putExtra(Intent.EXTRA_TEXT, "HachiCam ${BuildConfig.VERSION_NAME} (${BuildConfig.GIT_HASH})")
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }
                context.startActivity(Intent.createChooser(intent, context.getString(R.string.share_logs_title))
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            }
        } catch (error: Exception) {
            withContext(Dispatchers.Main) { Toast.makeText(context, "Export log failed", Toast.LENGTH_SHORT).show() }
        }
    }
}
