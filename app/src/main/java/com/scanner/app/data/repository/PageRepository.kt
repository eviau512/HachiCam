package com.scanner.app.data.repository

import com.scanner.app.data.image.AtomicImageCommit
import com.scanner.app.data.image.RevisionLedger
import com.scanner.app.data.image.PageIndexStore
import com.scanner.app.domain.model.PageStatus
import com.scanner.app.domain.model.DocumentQuad
import com.scanner.app.domain.model.ScannedPage
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.Closeable
import java.io.File
import android.graphics.PointF
import com.scanner.app.domain.model.CurvedBoundary
import com.scanner.app.domain.model.ImageFilter
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.update

object PageRepository {
    private val guard = Any()
    private val ledger = RevisionLedger<ScannedPage>()
    private val _pages = MutableStateFlow<List<ScannedPage>>(emptyList())
    val pages = _pages.asStateFlow()
    private val readers = mutableMapOf<String, Int>()
    private val retired = mutableSetOf<String>()
    private var index:PageIndexStore?=null
    private var scanDirectory:File?=null
    private var persistenceFailure:(()->Unit)?=null
    private val durablePins=java.util.ArrayDeque<ReadLease>()
    private data class Snapshot(val records:List<PageIndexStore.Record>,val pin:ReadLease,val sequence:Long)
    private data class Checkpoint(val completed:Long=0,val committed:Long=0)
    private var checkpointRequested=0L
    private val checkpoint=MutableStateFlow(Checkpoint())
    private val persistenceQueue=Channel<Snapshot>(Channel.CONFLATED,onUndeliveredElement={it.pin.close()})
    private val persistenceScope=CoroutineScope(SupervisorJob()+Dispatchers.IO)
    init {
        persistenceScope.launch {
            for(snapshot in persistenceQueue) {
                var installed=false
                try {
                    checkNotNull(index).save(snapshot.records)
                    synchronized(guard) {
                        durablePins.addLast(snapshot.pin)
                        while(durablePins.size>2)durablePins.removeFirst().close()
                    }
                    installed=true
                } catch(_:Exception) { persistenceFailure?.invoke() }
                finally {
                    if(!installed)snapshot.pin.close()
                    checkpoint.update { Checkpoint(maxOf(it.completed,snapshot.sequence),
                        if(installed)maxOf(it.committed,snapshot.sequence) else it.committed) }
                }
            }
        }
    }
    /** Called once before UI admission; validation reads JPEG bounds, never full pixels. */
    fun initialize(metadata:File,scans:File,validImage:(File)->Boolean,onFailure:()->Unit)=synchronized(guard) {
        check(index==null && scanDirectory==null)
        scanDirectory=scans.canonicalFile;persistenceFailure=onFailure
        val store=PageIndexStore(metadata)
        try {
            val records=store.load()
            records.forEach { r ->
                fun path(name:String?)=name?.let { File(scans,it).canonicalFile.also { f -> require(f.parentFile==scans.canonicalFile) }.absolutePath }
                fun points(values:List<Float>)=values.chunked(2).map { PointF(it[0],it[1]) }
                val source=path(r.source)!!;val processed=path(r.processed);val thumb=path(r.thumbnail)
                val sourceValid=validImage(File(source));val derivativeValid=processed!=null && validImage(File(processed))
                val fallback=processed!=null && !derivativeValid
                val quad=r.quad.takeIf { it.size==8 }?.let { DocumentQuad.fromFloatArray(it.toFloatArray()) }
                val curved=r.curves.takeIf { it.size==4 }?.map(::points)?.let { CurvedBoundary(it[0],it[1],it[2],it[3]) }
                ledger.put(r.id,ScannedPage(r.id,source,processed.takeIf { derivativeValid },thumb?.takeIf { validImage(File(it)) },
                    quad,curved,if(fallback)ImageFilter.ORIGINAL else ImageFilter.valueOf(r.filter),
                    if(fallback)0 else r.rotation,if(fallback)null else r.aspect,r.createdAt,
                    if(sourceValid)PageStatus.READY else PageStatus.FAILED))
            }
            index=store
            // Pin original checkpoint records too: a recovered/dropped derivative
            // can still be referenced by the previous metadata checkpoint.
            val paths=records.flatMap { listOfNotNull(it.source,it.processed,it.thumbnail) }
                .map { File(scans,it).canonicalPath }.distinct()
            durablePins.addLast(pinPaths(paths))
            publish() // Persist corrected interrupted states in the background.
        } catch(_:Exception) { onFailure();_pages.value=ledger.values() }
    }
    private fun record(page:ScannedPage):PageIndexStore.Record {
        fun name(path:String?)=path?.let { File(it).canonicalFile.also { f -> require(f.parentFile==scanDirectory) }.name }
        fun points(p:List<PointF>)=p.flatMap { listOf(it.x,it.y) }
        val c=page.curvedBoundary
        return PageIndexStore.Record(page.id,name(page.originalImagePath)!!,name(page.processedImagePath),name(page.thumbnailPath),
            page.filter.name,page.rotation,page.targetAspectRatio,page.createdAt,page.status.name,
            page.quad?.toFloatArray()?.toList().orEmpty(),c?.let { listOf(points(it.topCurve),points(it.bottomCurve),points(it.leftCurve),points(it.rightCurve)) }.orEmpty())
    }
    private fun pinPaths(paths:List<String>):ReadLease {
        paths.forEach { readers[it]=(readers[it]?:0)+1 }
        return ReadLease(emptyList(),paths)
    }
    private fun publish() {
        val values=ledger.values();_pages.value=values
        if(index!=null) {
            val sequence=++checkpointRequested
            try {
                val snapshot=Snapshot(values.map(::record),pinPaths(values.flatMap(::files).distinct()),sequence)
                if(!persistenceQueue.trySend(snapshot).isSuccess)snapshot.pin.close()
            } catch(_:Exception) {
                checkpoint.update { it.copy(completed=maxOf(it.completed,sequence)) }
                persistenceFailure?.invoke()
            }
        }
    }
    suspend fun awaitCheckpoint(timeoutMillis:Long=5000):Boolean {
        val target=synchronized(guard) { if(index==null)return false;checkpointRequested }
        val reached=withTimeoutOrNull(timeoutMillis) { checkpoint.first { it.completed>=target } }
        return reached!=null && reached.committed>=target
    }
    private fun stillOwned(path: String) = ledger.values().any { path in files(it) }
    private fun retire(path: String?) {
        if (path == null) return
        if (stillOwned(path)) return
        if ((readers[path] ?: 0) > 0) retired.add(path) else File(path).delete()
    }
    private fun files(page: ScannedPage) = listOfNotNull(page.originalImagePath, page.processedImagePath, page.thumbnailPath)

    fun addPage(page: ScannedPage) = synchronized(guard) {
        require(ledger.get(page.id)!=null || ledger.values().size<PageIndexStore.PAGE_LIMIT) { "Page limit" }
        ledger.put(page.id, page); publish()
    }
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
