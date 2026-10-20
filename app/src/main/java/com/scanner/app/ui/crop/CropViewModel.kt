package com.scanner.app.ui.crop

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.PointF
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.scanner.app.R
import com.scanner.app.data.camera.UnitPoint
import com.scanner.app.data.image.PageRenderer
import com.scanner.app.data.image.QuarterTurn
import com.scanner.app.data.image.ImageWork
import com.scanner.app.data.image.ImageMemoryPolicy
import com.scanner.app.data.repository.PageRepository
import com.scanner.app.domain.model.*
import com.scanner.app.engine.NativeEdgeDetector
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import org.opencv.core.Core
import org.opencv.core.Mat
import org.opencv.core.Size
import org.opencv.imgcodecs.Imgcodecs
import org.opencv.imgproc.Imgproc

class CropViewModel : ViewModel() {
    private var currentPageId: String? = null
    private var sourceW = 0
    private var sourceH = 0
    @Volatile private var epoch = 0L
    private var pageObserveJob: Job? = null
    private var edgeJob: Job? = null
    private var lsdJob: Job? = null
    private var grayMat: Mat? = null
    private var edgeMat: Mat? = null
    private val _imagePath = MutableStateFlow<String?>(null)
    val imagePath = _imagePath.asStateFlow()
    private val _rotation = MutableStateFlow(0)
    val rotation = _rotation.asStateFlow()
    private val _selectedRatio = MutableStateFlow(AspectRatioPreset.CUSTOM)
    val selectedRatio = _selectedRatio.asStateFlow()
    private val _customRatioValue = MutableStateFlow<Float?>(null)
    val customRatioValue = _customRatioValue.asStateFlow()
    private val _currentQuad = MutableStateFlow(DocumentQuad(PointF(), PointF(), PointF(), PointF()))
    val currentQuad = _currentQuad.asStateFlow()
    private val _detectedQuad = MutableStateFlow<DocumentQuad?>(null)
    val detectedQuad = _detectedQuad.asStateFlow()
    private val _selectedFilter = MutableStateFlow(ImageFilter.ORIGINAL)
    val selectedFilter = _selectedFilter.asStateFlow()
    private val _imageVersion = MutableStateFlow(0)
    val imageVersion = _imageVersion.asStateFlow()
    private val _isProcessing = MutableStateFlow(false)
    val isProcessing = _isProcessing.asStateFlow()
    private val _grayMatAddr = MutableStateFlow(0L)
    val grayMatAddr = _grayMatAddr.asStateFlow()
    private val _edgeMatAddr = MutableStateFlow(0L)
    val edgeMatAddr = _edgeMatAddr.asStateFlow()
    private val _horizontalLines = MutableStateFlow(FloatArray(0))
    val horizontalLines = _horizontalLines.asStateFlow()
    private val _verticalLines = MutableStateFlow(FloatArray(0))
    val verticalLines = _verticalLines.asStateFlow()
    private val _error = MutableStateFlow<Int?>(null)
    val error = _error.asStateFlow()
    fun clearError() { _error.value = null }
    private fun invalidate() { ++epoch; edgeJob?.cancel(); lsdJob?.cancel() }
    private fun displaySize(): Pair<Int, Int> = if (_rotation.value % 180 == 0) sourceW to sourceH else sourceH to sourceW
    private fun rotateQuad(q: DocumentQuad, w: Int, h: Int, degrees: Int): DocumentQuad {
        val outputW = if (QuarterTurn.normalized(degrees) % 180 == 0) w else h
        val outputH = if (QuarterTurn.normalized(degrees) % 180 == 0) h else w
        val points = QuarterTurn.corners(q.toPointList().map { UnitPoint(it.x / (w - 1), it.y / (h - 1)) }, degrees)
        return DocumentQuad.fromFloatArray(points.flatMap { listOf(it.x * (outputW - 1), it.y * (outputH - 1)) }.toFloatArray())
    }
    private fun fullQuad(w: Int, h: Int) = DocumentQuad(PointF(0f, 0f), PointF(w - 1f, 0f), PointF(w - 1f, h - 1f), PointF(0f, h - 1f))
    private fun selectRatio(value: Float?) {
        _selectedRatio.value = AspectRatioPreset.fromRatio(value)
        _customRatioValue.value = if (_selectedRatio.value == AspectRatioPreset.CUSTOM) value else null
    }
    private fun ratio(): Float? = if (_selectedRatio.value == AspectRatioPreset.CUSTOM) _customRatioValue.value else _selectedRatio.value.ratio

    fun loadPage(pageId: String) {
        pageObserveJob?.cancel(); invalidate(); updateMats(null, null)
        currentPageId = pageId; _imagePath.value = null
        val generation = epoch
        val page = PageRepository.getPage(pageId) ?: return
        if (page.status == PageStatus.PROCESSING) {
            _isProcessing.value = true
            pageObserveJob = viewModelScope.launch {
                val ready = PageRepository.pages.first { list -> list.find { it.id == pageId }?.status != PageStatus.PROCESSING }
                    .find { it.id == pageId }
                if (generation != epoch || currentPageId != pageId) return@launch
                _isProcessing.value = false
                if (ready?.status == PageStatus.READY) applyPage(ready) else _error.value = R.string.edit_failed
            }
        } else {
            _isProcessing.value = false
            if (page.status == PageStatus.READY) applyPage(page) else _error.value = R.string.edit_failed
        }
    }
    private fun applyPage(page: ScannedPage) {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(page.originalImagePath, bounds)
        if (bounds.outWidth < 2 || bounds.outHeight < 2) { _error.value = R.string.edit_failed; return }
        sourceW = bounds.outWidth; sourceH = bounds.outHeight
        _rotation.value = QuarterTurn.normalized(page.rotation)
        _imagePath.value = page.originalImagePath
        selectRatio(page.targetAspectRatio); _selectedFilter.value = page.filter
        val sourceQuad = page.quad ?: fullQuad(sourceW, sourceH)
        _currentQuad.value = rotateQuad(sourceQuad, sourceW, sourceH, _rotation.value)
        _detectedQuad.value = _currentQuad.value
        _imageVersion.update { it + 1 }
        computeEdgeMat()
    }
    @Synchronized private fun updateMats(gray: Mat?, edges: Mat?) {
        val oldGray = grayMat; val oldEdges = edgeMat
        val oldBytes = listOfNotNull(oldGray, oldEdges).sumOf { it.total() * it.elemSize() }
        val newBytes = listOfNotNull(gray, edges).sumOf { it.total() * it.elemSize() }
        ImageWork.retained.addAndGet(newBytes - oldBytes)
        grayMat = gray; edgeMat = edges
        _grayMatAddr.value = gray?.nativeObjAddr ?: 0L
        _edgeMatAddr.value = edges?.nativeObjAddr ?: 0L
        oldGray?.release(); oldEdges?.release()
    }
    private fun uprightSource(path: String, degrees: Int): Mat {
        val src = Imgcodecs.imread(path, Imgcodecs.IMREAD_COLOR or Imgcodecs.IMREAD_IGNORE_ORIENTATION)
        if (degrees == 0 || src.empty()) return src
        val dst = Mat()
        try {
            Core.rotate(src, dst, when (degrees) {
                90 -> Core.ROTATE_90_CLOCKWISE
                180 -> Core.ROTATE_180
                else -> Core.ROTATE_90_COUNTERCLOCKWISE
            })
            return dst
        } catch (error: Exception) { dst.release(); throw error }
        finally { src.release() }
    }
    private fun computeEdgeMat() {
        edgeJob?.cancel()
        val path = _imagePath.value ?: return
        val pageId = currentPageId ?: return
        val generation = epoch; val degrees = _rotation.value
        edgeJob = viewModelScope.launch(Dispatchers.Default) {
            val owned = mutableListOf<Mat>()
            fun own(mat: Mat) = mat.also { owned.add(it) }
            try {
                ImageWork.run(ImageMemoryPolicy.analysisBytes(sourceW.toLong() * sourceH)) {
                if (generation != epoch) return@run
                val sourceLease = PageRepository.readMaster(pageId) ?: return@run
                sourceLease.use {
                val src = own(uprightSource(path, degrees)); check(!src.empty())
                val gray = own(Mat()); Imgproc.cvtColor(src, gray, Imgproc.COLOR_BGR2GRAY)
                val enhanced = own(Mat())
                val clahe = Imgproc.createCLAHE(2.0, Size(8.0, 8.0))
                try { clahe.apply(gray, enhanced) } finally { clahe.collectGarbage(); clahe.clear() }
                val blurred = own(Mat()); Imgproc.GaussianBlur(enhanced, blurred, Size(5.0, 5.0), 1.0)
                val edges = own(Mat()); Imgproc.Canny(blurred, edges, 25.0, 70.0)
                val kernel = own(Imgproc.getStructuringElement(Imgproc.MORPH_RECT, Size(3.0, 3.0)))
                Imgproc.morphologyEx(edges, edges, Imgproc.MORPH_CLOSE, kernel)
                withContext(Dispatchers.Main) {
                    if (generation == epoch && isActive && PageRepository.getPage(pageId)?.originalImagePath == path) {
                        updateMats(gray, edges); owned.remove(gray); owned.remove(edges)
                    }
                }
                }
                }
            } catch (cancel: CancellationException) { throw cancel }
            catch (_: Exception) { /* Optional snapping must not invalidate the draft. */ }
            finally { owned.asReversed().forEach(Mat::release) }
        }
    }
    fun onBitmapLoaded(bitmap: Bitmap, origW: Float = 0f, origH: Float = 0f) {
        lsdJob?.cancel(); val generation = epoch
        lsdJob = viewModelScope.launch(Dispatchers.Default) {
            try {
                ImageWork.run(ImageMemoryPolicy.analysisBytes(bitmap.width.toLong() * bitmap.height)) {
                if (generation != epoch) return@run
                val (h, v) = NativeEdgeDetector().detectStructuralLines(bitmap, origW, origH)
                withContext(Dispatchers.Main) { if (generation == epoch && isActive) { _horizontalLines.value = h; _verticalLines.value = v } }
                }
            } catch (cancel: CancellationException) { throw cancel }
            catch (_: Exception) { }
        }
    }
    fun tapToSnap(touchX: Float, touchY: Float) {
        val snapshot = synchronized(this) { grayMat?.let { Mat(it, org.opencv.core.Range.all()) } } ?: return
        val generation = epoch
        val retainedBytes = snapshot.total() * snapshot.elemSize()
        ImageWork.retained.addAndGet(retainedBytes)
        val released = java.util.concurrent.atomic.AtomicBoolean(false)
        fun release() { if (released.compareAndSet(false, true)) { snapshot.release(); ImageWork.retained.addAndGet(-retainedBytes) } }
        val job = viewModelScope.launch(Dispatchers.Default) {
            try {
                ImageWork.run(ImageMemoryPolicy.analysisBytes(snapshot.total())) {
                if (generation != epoch) return@run
                val quad = NativeEdgeDetector().findContourAtPoint(snapshot, touchX, touchY)
                withContext(Dispatchers.Main) { if (generation == epoch && quad != null) updateQuad(quad) }
                }
            } catch (cancel: CancellationException) { throw cancel }
            catch (_: Exception) { }
            finally { release() }
        }
        job.invokeOnCompletion { release() }
    }
    fun setAspectRatio(preset: AspectRatioPreset) { invalidate(); _selectedRatio.value = preset; _customRatioValue.value = null }
    fun setCustomRatio(value: Float) {
        if (!value.isFinite() || value !in .05f..20f) { _error.value = R.string.edit_failed; return }
        invalidate(); selectRatio(value)
    }
    fun setFilter(filter: ImageFilter) { _selectedFilter.value = filter }
    fun updateQuad(quad: DocumentQuad) {
        val (w, h) = displaySize()
        if (w < 2 || h < 2 || quad.toFloatArray().any { !it.isFinite() }) return
        val points = quad.toPointList().map { PointF(it.x.coerceIn(0f, w - 1f), it.y.coerceIn(0f, h - 1f)) }
        invalidate(); _currentQuad.value = DocumentQuad(points[0], points[1], points[2], points[3])
    }
    fun updateCorner(index: Int, p: PointF) {
        val values = _currentQuad.value.toPointList().toMutableList()
        if (index !in values.indices) return
        values[index] = p; updateQuad(DocumentQuad(values[0], values[1], values[2], values[3]))
    }
    fun updateEdge(index: Int, dx: Float, dy: Float) {
        val values = _currentQuad.value.toPointList().map { PointF(it.x, it.y) }.toMutableList()
        if (index !in 0..3) return
        for (i in listOf(index, (index + 1) % 4)) { values[i].x += dx; values[i].y += dy }
        updateQuad(DocumentQuad(values[0], values[1], values[2], values[3]))
    }
    fun resetToFullImage() { val (w, h) = displaySize(); if (w > 1 && h > 1) updateQuad(fullQuad(w, h)) }
    fun rotateImage(onComplete: () -> Unit = {}) {
        val (w, h) = displaySize()
        if (w > 1 && h > 1) {
            invalidate(); _currentQuad.value = rotateQuad(_currentQuad.value, w, h, 90)
            _detectedQuad.value = _detectedQuad.value?.let { rotateQuad(it, w, h, 90) }
            _rotation.value = (_rotation.value + 90) % 360
            selectRatio(ratio()?.let { 1f / it })
            _horizontalLines.value = FloatArray(0); _verticalLines.value = FloatArray(0)
            updateMats(null, null); _imageVersion.update { it + 1 }; computeEdgeMat()
        }
        onComplete()
    }
    fun reDetect() {
        val path = _imagePath.value ?: return
        val pageId = currentPageId ?: return
        invalidate(); val generation = epoch; val degrees = _rotation.value
        viewModelScope.launch(Dispatchers.Default) {
            try {
            ImageWork.run(ImageMemoryPolicy.analysisBytes(sourceW.toLong() * sourceH)) {
            if (generation != epoch) return@run
            val sourceLease = PageRepository.readMaster(pageId) ?: return@run
            sourceLease.use {
            val mat = uprightSource(path, degrees)
            val gray = Mat()
            try {
                if (mat.empty()) return@run
                Imgproc.cvtColor(mat, gray, Imgproc.COLOR_BGR2GRAY)
                val result = NativeEdgeDetector().detectDocument(gray, false)
                withContext(Dispatchers.Main) {
                    if (generation == epoch && PageRepository.getPage(pageId)?.originalImagePath == path && result.found && !result.partial && result.quad != null) {
                        updateQuad(result.quad); _detectedQuad.value = result.quad
                    }
                    computeEdgeMat()
                }
            } catch (cancel: CancellationException) { throw cancel }
            catch (_: Exception) { withContext(Dispatchers.Main) { if (generation == epoch) _error.value = R.string.edit_failed } }
            finally { gray.release(); mat.release() }
            }
            }
            } catch (cancel: CancellationException) { throw cancel }
            catch (_: Exception) { withContext(Dispatchers.Main) { if (generation == epoch) _error.value = R.string.edit_failed } }
        }
    }
    fun confirmCrop(onDone: (Boolean) -> Unit = {}) {
        val id = currentPageId ?: run { onDone(false); return }
        val (w, h) = displaySize()
        if (w < 2 || h < 2) { onDone(false); return }
        val quad = rotateQuad(_currentQuad.value, w, h, -_rotation.value)
        val filter = _selectedFilter.value; val aspect = ratio(); val rotation = _rotation.value
        val request = PageRepository.beginEdit(id) { it.copy(quad = quad, filter = filter, targetAspectRatio = aspect, rotation = rotation) }
            ?: run { onDone(false); return }
        val job = viewModelScope.launch {
            var saved = false
            try { saved = PageRenderer.render(request); if (!saved) _error.value = R.string.edit_failed }
            catch (cancel: CancellationException) { throw cancel }
            catch (error: Exception) { android.util.Log.e("HachiCam-Crop", "Crop failed; previous result retained", error); _error.value = R.string.edit_failed }
            finally { PageRepository.failEdit(request); withContext(NonCancellable) { onDone(saved) } }
        }
        job.invokeOnCompletion { PageRepository.failEdit(request) }
    }
    override fun onCleared() { invalidate(); pageObserveJob?.cancel(); updateMats(null, null); super.onCleared() }
}
