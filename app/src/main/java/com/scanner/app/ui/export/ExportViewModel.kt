package com.scanner.app.ui.export

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.scanner.app.domain.model.ExportConfig
import com.scanner.app.domain.model.ExportMode
import com.scanner.app.domain.model.ScannedPage
import com.scanner.app.data.export.FolderExporter
import com.scanner.app.data.export.PdfExporter
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
    private val _exportState = MutableStateFlow<ExportState>(ExportState.Idle)
    val exportState: StateFlow<ExportState> = _exportState.asStateFlow()

    fun export(context: Context, pages: List<ScannedPage>, config: ExportConfig) {
        viewModelScope.launch(Dispatchers.IO) {
            _exportState.value = ExportState.Exporting
            try {
                check(pages.isNotEmpty() && pages.all { it.status == com.scanner.app.domain.model.PageStatus.READY }) {
                    context.getString(com.scanner.app.R.string.export_not_ready)
                }
                val exportedFile = when (config.exportMode) {
                    ExportMode.FOLDER -> {
                        val exporter = FolderExporter(context)
                        exporter.export(pages, config)
                    }
                    ExportMode.PDF -> {
                        val exporter = PdfExporter(context)
                        exporter.export(pages, config)
                    }
                }
                _exportState.value = ExportState.Success(exportedFile.absolutePath)
            } catch (e: Exception) {
                _exportState.value = ExportState.Error(e.message ?: "Unknown error occurred")
            }
        }
    }

    fun resetState() {
        _exportState.value = ExportState.Idle
    }
}
