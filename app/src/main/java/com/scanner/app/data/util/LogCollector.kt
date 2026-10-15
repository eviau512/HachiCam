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
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import com.scanner.app.data.camera.BurstProcessor
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object LogCollector {
    suspend fun collectAndShareLogs(context: Context, stage: (String) -> Unit = {}) = withContext(Dispatchers.IO) {
        val timing=ProcessingTimeline { android.os.SystemClock.elapsedRealtimeNanos() }
        try {
            if(BuildConfig.SENSITIVE_DIAGNOSTICS) {
                withContext(Dispatchers.Main) { stage("waiting") }
                timing.start("workerAndSnapshotWait")
                val idle=withTimeoutOrNull(300_000) { BurstProcessor.pendingJobs.first { it==0 } }
                timing.end("workerAndSnapshotWait", if(idle==null) "timeout" else "completed")
                if(idle==null) {
                    withContext(Dispatchers.Main) { Toast.makeText(context,R.string.diagnostics_wait_timeout,Toast.LENGTH_LONG).show() }
                    return@withContext
                }
            }
            withContext(Dispatchers.Main) { stage("preparing") }
            val directory = File(context.cacheDir, "logs").apply { mkdirs() }
            directory.listFiles { f -> f.isFile && f.name.startsWith("hachicam_log_") }
                ?.sortedByDescending { it.lastModified() }?.drop(4)?.forEach { it.delete() }
            val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
            val exportId=java.util.UUID.randomUUID().toString()
            val exportName="${timestamp}_${exportId.take(8)}"
            val latestRequested=CameraDiagnostics.latestCaptureId()
            val store=if(BuildConfig.SENSITIVE_DIAGNOSTICS) BurstArchiveStore(File(context.cacheDir,"debug_burst"),true) else null
            val latestRetained=store?.latestCaptureId()
            val pageCheckpoint=if(BuildConfig.SENSITIVE_DIAGNOSTICS)
                timing.measureSuspend("pageCheckpointWait") { com.scanner.app.data.repository.PageRepository.awaitCheckpoint() } else null
            val file = File(directory, "hachicam_log_$exportName.txt")
            val logs=if(BuildConfig.SENSITIVE_DIAGNOSTICS) coroutineScope {
                val app=async { timing.measure("appLogcat") { DiagnosticAccess.command(listOf("logcat", "-d", "-v", "time", "--pid=${android.os.Process.myPid()}", "-t", "3000")) } }
                val system=if(DiagnosticAccess.rootAvailable) async {
                    timing.measure("rootLogcat") { DiagnosticAccess.command(listOf("su", "-c", "logcat -d -v time -t 2000")) }
                } else null
                app.await() to system?.await()
            } else "" to null
            val text = buildString {
                appendLine("HachiCam Diagnostic Log")
                appendLine("ExportId: $exportId")
                appendLine("Version: ${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE})")
                appendLine("Build: ${BuildConfig.BUILD_TYPE} ${BuildConfig.GIT_HASH} ${BuildConfig.BUILD_TIME}")
                if(pageCheckpoint!=null)appendLine("Latest page checkpoint committed before export: $pageCheckpoint")
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
                    appendLine("Latest requested capture: $latestRequested; latest retained final capture: $latestRetained")
                    if(latestRequested.isNotBlank() && latestRetained!=latestRequested)
                        appendLine("Latest attempt has no retained complete final diagnostic; any included images belong to earlier completed captures.")
                    appendLine("Use export-timing.json for the latest capture ID and ordered groups; each group's matching evidence is in captures/<captureId>/manifest.json and diagnostics.txt.")
                    appendLine("--- App process log ---")
                    appendLine(logs.first)
                    if (logs.second!=null) {
                        appendLine("--- System camera log (root, test only) ---")
                        appendLine(logs.second)
                    }
                } else {
                    appendLine("Release privacy: device identifiers, root access and logcat collection disabled.")
                }
                appendLine("--- Export preparation timing (not photo processing) ---")
                timing.snapshot().forEach { appendLine("${it.name}: durationMs=${it.durationMs} outcome=${it.outcome}") }
            }
            file.writeText(text, Charsets.UTF_8)
            val bundle = if (BuildConfig.SENSITIVE_DIAGNOSTICS) runCatching {
                timing.measure("completeZipExport") { store?.export(file, File(directory, "hachicam_capture_$exportName.zip"), includeHistory=true) }
            }.getOrNull() else null
            directory.listFiles { f -> f.isFile && f.name.startsWith("hachicam_capture_") && f.extension == "zip" }
                ?.sortedByDescending { it.lastModified() }?.drop(2)?.forEach { it.delete() }
            val shared = bundle ?: file
            withContext(Dispatchers.Main) {
                if(BuildConfig.SENSITIVE_DIAGNOSTICS && (bundle==null || latestRetained!=latestRequested))
                    Toast.makeText(context,R.string.diagnostics_latest_missing,Toast.LENGTH_LONG).show()
                val uri = FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", shared)
                val intent = Intent(Intent.ACTION_SEND).apply {
                    type = if (bundle != null) "application/zip" else "text/plain"
                    putExtra(Intent.EXTRA_STREAM, uri)
                    putExtra(Intent.EXTRA_SUBJECT, "HachiCam Diagnostics - ${shared.name}")
                    putExtra(Intent.EXTRA_TEXT, "HachiCam ${BuildConfig.VERSION_NAME} (${BuildConfig.GIT_HASH}); export preparation ${"%.1f".format(Locale.US,timing.elapsedMs()/1000)}s")
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }
                context.startActivity(Intent.createChooser(intent, context.getString(R.string.share_logs_title))
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            }
        } catch (error: CancellationException) { throw error }
        catch (error: Exception) {
            withContext(Dispatchers.Main) { Toast.makeText(context, "Export log failed", Toast.LENGTH_SHORT).show() }
        }
    }
}
