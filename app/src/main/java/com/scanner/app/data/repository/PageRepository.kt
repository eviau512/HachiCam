package com.scanner.app.data.repository

import com.scanner.app.data.image.AtomicImageCommit
import com.scanner.app.data.image.RevisionLedger
import com.scanner.app.domain.model.PageStatus
import com.scanner.app.domain.model.DocumentQuad
import com.scanner.app.domain.model.ScannedPage
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.Closeable
import java.io.File

object PageRepository {
    private val guard = Any()
    private val ledger = RevisionLedger<ScannedPage>()
    private val _pages = MutableStateFlow<List<ScannedPage>>(emptyList())
    val pages = _pages.asStateFlow()
    private val readers = mutableMapOf<String, Int>()
    private val retired = mutableSetOf<String>()
    private fun publish() { _pages.value = ledger.values() }
    private fun stillOwned(path: String) = ledger.values().any { path in files(it) }
    private fun retire(path: String?) {
        if (path == null) return
        if (stillOwned(path)) return
        if ((readers[path] ?: 0) > 0) retired.add(path) else File(path).delete()
    }
    private fun files(page: ScannedPage) = listOfNotNull(page.originalImagePath, page.processedImagePath, page.thumbnailPath)

    fun addPage(page: ScannedPage) = synchronized(guard) { ledger.put(page.id, page); publish() }
    fun getPage(id: String): ScannedPage? = synchronized(guard) { ledger.get(id) }
    fun updatePage(id: String, change: (ScannedPage) -> ScannedPage) = synchronized(guard) {
        ledger.get(id)?.let { page ->
            val updated = change(page)
            require(updated.id == page.id && updated.originalImagePath == page.originalImagePath) { "Source identity is immutable" }
            if (updated != page) { ledger.put(id, updated); publish() }
        }
    }
    fun beginEdit(id: String, change: (ScannedPage) -> ScannedPage): RevisionLedger.Request<ScannedPage>? = synchronized(guard) {
        if (ledger.get(id)?.status != PageStatus.READY) null else ledger.begin(id) { page ->
            change(page).also { require(it.id == page.id && it.originalImagePath == page.originalImagePath) { "Source identity is immutable" } }
        }
    }
    fun isCurrent(request: RevisionLedger.Request<ScannedPage>) = synchronized(guard) { ledger.current(request) }
    fun failEdit(request: RevisionLedger.Request<ScannedPage>) = synchronized(guard) { ledger.fail(request) }
    fun commitEdit(request: RevisionLedger.Request<ScannedPage>, temporary: File, result: File): Boolean = synchronized(guard) {
        val old = ledger.get(request.id) ?: return false
        if (old.originalImagePath != request.value.originalImagePath) return false
        val committed = ledger.commit(request, request.value.copy(processedImagePath = result.absolutePath)) {
            AtomicImageCommit.install(temporary, result)
        }
        if (committed) { publish(); if (old.processedImagePath != result.absolutePath) retire(old.processedImagePath) }
        committed
    }
    fun commitCapture(id: String, temporary: File, result: File, quad: DocumentQuad?): Boolean = synchronized(guard) {
        val page = ledger.get(id) ?: return false
        if (page.status != PageStatus.PROCESSING || page.originalImagePath != result.absolutePath) return false
        AtomicImageCommit.install(temporary, result)
        ledger.put(id, page.copy(quad = quad, status = PageStatus.READY))
        publish(); true
    }
    class ReadLease internal constructor(val pages: List<ScannedPage>, private val paths: List<String>) : Closeable {
        private var closed = false
        override fun close() = synchronized(guard) {
            if (closed) return
            closed = true
            paths.forEach { path ->
                val count = (readers[path] ?: 1) - 1
                if (count <= 0) {
                    readers.remove(path)
                    if (retired.remove(path) && !stillOwned(path)) File(path).delete()
                } else readers[path] = count
            }
        }
    }
    private fun lease(selected: List<ScannedPage>, paths: List<String>): ReadLease {
        paths.forEach { path -> check(File(path).isFile && File(path).length() > 0) { "Source image is missing" } }
        paths.forEach { readers[it] = (readers[it] ?: 0) + 1 }
        return ReadLease(selected, paths)
    }
    fun readPages(ids: List<String>): ReadLease = synchronized(guard) {
        require(ids.isNotEmpty() && ids.distinct().size == ids.size)
        val selected = ids.map { id -> checkNotNull(ledger.get(id)) { "Page was deleted" } }
        check(selected.all { it.status == PageStatus.READY }) { "Page is not ready" }
        lease(selected, selected.map { it.imagePath }.distinct())
    }
    fun readSource(request: RevisionLedger.Request<ScannedPage>): ReadLease? = synchronized(guard) {
        if (!ledger.current(request)) null else lease(listOf(request.value), listOf(request.value.originalImagePath))
    }
    fun readMaster(id: String): ReadLease? = synchronized(guard) {
        ledger.get(id)?.takeIf { it.status == PageStatus.READY }?.let { lease(listOf(it), listOf(it.originalImagePath)) }
    }
    fun deletePage(id: String) = synchronized(guard) {
        ledger.remove(id)?.let { page -> publish(); files(page).distinct().forEach(::retire) }
    }
    fun reorderPages(fromIndex: Int, toIndex: Int) = synchronized(guard) { ledger.reorder(fromIndex, toIndex); publish() }
    fun clear() = synchronized(guard) {
        // Ending a session does not authorize deletion of kept source photos.
        ledger.clear(); publish()
    }
}
