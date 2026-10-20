package com.scanner.app.ui.review

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.scanner.app.R
import com.scanner.app.data.image.PageRenderer
import com.scanner.app.data.repository.PageRepository
import com.scanner.app.domain.model.AspectRatioPreset
import com.scanner.app.domain.model.ImageFilter
import com.scanner.app.domain.model.ScannedPage
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class ReviewViewModel : ViewModel() {
    val pages = PageRepository.pages
    private val _selectedPageId = MutableStateFlow<String?>(null)
    val selectedPageId = _selectedPageId.asStateFlow()
    private val active = MutableStateFlow(0)
    private val _isProcessing = MutableStateFlow(false)
    val isProcessing = _isProcessing.asStateFlow()
    private val _imageVersion = MutableStateFlow(0)
    val imageVersion = _imageVersion.asStateFlow()
    private val _error = MutableStateFlow<Int?>(null)
    val error = _error.asStateFlow()
    fun clearError() { _error.value = null }
    fun selectPage(pageId: String?) { _selectedPageId.value = pageId }
    fun deletePage(pageId: String) {
        if (_selectedPageId.value == pageId) _selectedPageId.value = null
        PageRepository.deletePage(pageId)
    }
    fun deleteBatchPages(pageIds: Collection<String>) = pageIds.forEach(::deletePage)

    private fun edit(ids: Collection<String>, onComplete: () -> Unit = {}, change: (ScannedPage) -> ScannedPage) {
        // Register intent synchronously so UI event order, not worker scheduling, wins.
        val requests = try { ids.distinct().mapNotNull { PageRepository.beginEdit(it, change) } }
        catch (_: IllegalArgumentException) { _error.value = R.string.edit_failed; onComplete(); return }
        if (requests.isEmpty()) { onComplete(); return }
        active.update { it + 1 }; _isProcessing.value = true
        val finished = java.util.concurrent.atomic.AtomicBoolean(false)
        fun finish() {
            if (finished.compareAndSet(false, true)) {
                requests.forEach(PageRepository::failEdit)
                active.update { it - 1 }; _isProcessing.value = active.value > 0
            }
        }
        val job = viewModelScope.launch {
            try {
                for (request in requests) if (PageRenderer.render(request)) _imageVersion.update { it + 1 }
            } catch (cancel: CancellationException) { throw cancel }
            catch (error: Exception) {
                android.util.Log.e("HachiCam-Review", "Edit failed; previous result retained", error)
                _error.value = R.string.edit_failed
            } finally {
                finish()
                withContext(NonCancellable) { onComplete() }
            }
        }
        job.invokeOnCompletion { finish() }
    }
    fun rotatePage(pageId: String, onComplete: () -> Unit = {}) = edit(listOf(pageId), onComplete) {
        it.copy(rotation = (it.rotation + 90) % 360, targetAspectRatio = it.targetAspectRatio?.let { ratio -> 1f / ratio })
    }
    fun setPageFilter(pageId: String, newFilter: ImageFilter) = edit(listOf(pageId)) { it.copy(filter = newFilter) }
    fun setBatchFilter(pageIds: Collection<String>, newFilter: ImageFilter, onComplete: () -> Unit = {}) =
        edit(pageIds, onComplete) { it.copy(filter = newFilter) }
    fun setPageAspectRatio(pageId: String, preset: AspectRatioPreset) = edit(listOf(pageId)) { it.copy(targetAspectRatio = preset.ratio) }
    fun setPageCustomRatio(pageId: String, customRatio: Float) = edit(listOf(pageId)) {
        require(customRatio.isFinite() && customRatio in .05f..20f); it.copy(targetAspectRatio = customRatio)
    }
    fun setBatchAspectRatio(pageIds: Collection<String>, preset: AspectRatioPreset, onComplete: () -> Unit = {}) =
        edit(pageIds, onComplete) { it.copy(targetAspectRatio = preset.ratio) }
    fun setBatchCustomRatio(pageIds: Collection<String>, customRatio: Float, onComplete: () -> Unit = {}) = edit(pageIds, onComplete) {
        require(customRatio.isFinite() && customRatio in .05f..20f); it.copy(targetAspectRatio = customRatio)
    }
    fun reorderPages(fromIndex: Int, toIndex: Int) = PageRepository.reorderPages(fromIndex, toIndex)
}
