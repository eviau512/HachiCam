package com.scanner.app.ui.export

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.scanner.app.domain.model.ExportConfig
import com.scanner.app.domain.model.ExportMode
import com.scanner.app.domain.model.ScannedPage
import com.scanner.app.data.export.FolderExporter
import com.scanner.app.data.export.PdfExporter
import com.scanner.app.data.repository.PageRepository
import com.scanner.app.data.image.ImageWork
import com.scanner.app.data.image.ImageMemoryPolicy
import com.scanner.app.data.image.ImageMemoryUnavailableException
import com.scanner.app.data.export.ExportCleanupException
import android.graphics.BitmapFactory
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

sealed class ExportState {
    object Idle : ExportState()
    object Exporting : ExportState()
    data class Success(val path: String) : ExportState()
    data class Error(val message: String) : ExportState()
}

class ExportViewModel : ViewModel() {
    private var exportJob: Job? = null
    @Volatile private var generation = 0L
    private val _exportState = MutableStateFlow<ExportState>(ExportState.Idle)
    val exportState: StateFlow<ExportState> = _exportState.asStateFlow()

    fun export(context: Context, pages: List<ScannedPage>, config: ExportConfig) {
        if (_exportState.value is ExportState.Exporting) return
        _exportState.value = ExportState.Exporting
        val request = ++generation
        exportJob = viewModelScope.launch(Dispatchers.IO) {
            try {
                val operation = currentCoroutineContext()
                check(pages.isNotEmpty() && pages.all { it.status == com.scanner.app.domain.model.PageStatus.READY }) {
                    context.getString(com.scanner.app.R.string.export_not_ready)
                }
                val receipt = PageRepository.readPages(pages.map { it.id }).use { lease ->
                    val pixels = lease.pages.map { page ->
                        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                        BitmapFactory.decodeFile(page.imagePath, bounds)
                        check(bounds.outWidth > 0 && bounds.outHeight > 0) { "Export source is invalid" }
                        bounds.outWidth.toLong() * bounds.outHeight
                    }
                    ImageWork.run(ImageMemoryPolicy.exportBytes(pixels, config.exportMode == ExportMode.PDF)) { when (config.exportMode) {
                    ExportMode.FOLDER -> {
                        val exporter = FolderExporter(context)
                        exporter.export(lease.pages, config) { operation.ensureActive() }
                    }
                    ExportMode.PDF -> {
                        val exporter = PdfExporter(context)
                        exporter.export(lease.pages, config) { operation.ensureActive() }
                    }
                } } }
                if (request == generation) _exportState.value = ExportState.Success(receipt.displayLocation)
            } catch (cancel: CancellationException) { throw cancel
            } catch (e: Exception) {
                android.util.Log.e("HachiCam-Export", "Export failed", e)
                val message = when (e) {
                    is ExportCleanupException -> com.scanner.app.R.string.export_cleanup_failed
                    is ImageMemoryUnavailableException -> com.scanner.app.R.string.export_memory_shortage
                    else -> com.scanner.app.R.string.export_failed_preserved
                }
                if (request == generation) _exportState.value = ExportState.Error(context.getString(message))
            }
        }
    }

    fun resetState() {
        ++generation; exportJob?.cancel()
        _exportState.value = ExportState.Idle
    }
}
