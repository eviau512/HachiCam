package com.scanner.app.ui.camera

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageDecoder
import android.graphics.ImageFormat
import android.graphics.PointF
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.CaptureResult
import android.hardware.camera2.TotalCaptureResult
import android.net.Uri
import android.util.Log
import androidx.camera.camera2.interop.*
import androidx.camera.core.*
import androidx.core.content.ContextCompat
import androidx.exifinterface.media.ExifInterface
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.scanner.app.R
import com.scanner.app.BuildConfig
import com.scanner.app.data.camera.*
import com.scanner.app.data.image.ImageStorage
import com.scanner.app.data.image.ImageWork
import com.scanner.app.data.image.ImageMemoryPolicy
import com.scanner.app.data.repository.PageRepository
import com.scanner.app.data.util.ExifUtils
import com.scanner.app.data.util.CameraDiagnostics
import com.scanner.app.data.util.BurstArchiveStore
import org.json.JSONArray
import org.json.JSONObject
import com.scanner.app.domain.model.*
import com.scanner.app.engine.NativeBurstFusion
import com.scanner.app.engine.NativeEdgeDetector
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import org.opencv.core.Core
import org.opencv.core.Mat
import org.opencv.core.MatOfByte
import org.opencv.core.MatOfInt
import org.opencv.imgcodecs.Imgcodecs
import java.io.ByteArrayInputStream
import java.io.File
import java.util.UUID
import java.util.Locale
import kotlin.coroutines.resume
import kotlin.math.sqrt

@androidx.annotation.OptIn(ExperimentalCamera2Interop::class)
class CameraViewModel : ViewModel() {
    private val _detectedQuad = MutableStateFlow<DetectionResult?>(null)
    val detectedQuad = _detectedQuad.asStateFlow()
    private val _curvedModeEnabled = MutableStateFlow(false)
    val curvedModeEnabled = _curvedModeEnabled.asStateFlow()
    private val _fullHdrEnabled = MutableStateFlow(true)
    val fullHdrEnabled = _fullHdrEnabled.asStateFlow()
    private val _highResolutionRequested = MutableStateFlow(false)
    val highResolutionRequested = _highResolutionRequested.asStateFlow()
    private val _highResolutionActive = MutableStateFlow(false)
    val highResolutionActive = _highResolutionActive.asStateFlow()
    private val _captureSize = MutableStateFlow<CapturePolicy.Size?>(null)
    val captureSize = _captureSize.asStateFlow()
    private val _lastOutputSize = MutableStateFlow<CapturePolicy.Size?>(null)
    val lastOutputSize = _lastOutputSize.asStateFlow()
    private val captureEvidence = CaptureEvidenceStore()
    private var latestMetering: MeteringObservation? = null
    private var meteringRevision = 0L
    private val focusTarget = TapFocusTarget<MeteringPoint>()
    private var previewWidth = 0
    private var previewHeight = 0
    private var capturePixels = 0L
    private val evidenceLock = Any()
    @Volatile private var cameraGeneration = 0L
    @Volatile private var evidenceAfterMs = 0L
    private var zoomPending = false
    private var controlsReady = false
    private val _rebindRequest = MutableStateFlow(0L)
    val rebindRequest = _rebindRequest.asStateFlow()
    fun controlsBound(generation: Long) { if (isCameraSession(generation)) controlsReady = true }
    private var zoomGeneration = 0L
    private val _isStable = MutableStateFlow(false)
    val isStable = _isStable.asStateFlow()
    private val _isCapturing = MutableStateFlow(false)
    val isCapturing = _isCapturing.asStateFlow()
    private val _isImporting = MutableStateFlow(false)
    val isImporting = _isImporting.asStateFlow()
    private val _lastThumbnailBitmap = MutableStateFlow<Bitmap?>(null)
    val lastThumbnailBitmap = _lastThumbnailBitmap.asStateFlow()
    private val _errorMessage = MutableStateFlow<String?>(null)
    val errorMessage = _errorMessage.asStateFlow()
    fun clearError() { _errorMessage.value = null }
    val capturedPages = PageRepository.pages
    val pageCount = capturedPages.map { it.size }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), 0)

    var imageCapture: ImageCapture? = null
    var imageAnalysis: ImageAnalysis? = null
    var cameraControl: CameraControl? = null
    var cameraInfo: CameraInfo? = null
    var frameAnalyzer: FrameAnalyzer? = null
    private data class Frame(val bytes: ByteArray, val rotation: Int, val tier: Int, val exposure: ManualExposure?,
        val exifExposure: ManualExposure?, val evidence: CaptureEvidence?)

    fun updateSettings(context: Context) {
        val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
        _highResolutionRequested.value = prefs.getBoolean(CapturePolicy.HIGH_KEY, false)
        _fullHdrEnabled.value = _highResolutionRequested.value || prefs.getBoolean(CapturePolicy.HDR_KEY, true)
    }
    fun beginCameraSession(): Long {
        val oldAnalysis = imageAnalysis; val oldAnalyzer = frameAnalyzer
        val generation = synchronized(evidenceLock) {
            ++cameraGeneration
            imageCapture = null; imageAnalysis = null; frameAnalyzer = null; cameraControl = null; cameraInfo = null
            cropTracker.reset(); _detectedQuad.value = null; _isStable.value = false
            _highResolutionActive.value = false; capturePixels = 0; zoomPending = false; evidenceAfterMs = 0
            _captureSize.value = null; captureEvidence.clear(); latestMetering = null; meteringRevision = 0
            controlsReady = false
            focusTarget.clear(); previewWidth = 0; previewHeight = 0
            cameraGeneration
        }
        // Never wait for the analyzer while holding the result-publication lock.
        oldAnalysis?.clearAnalyzer(); oldAnalyzer?.close()
        return generation
    }
    fun isCameraSession(generation: Long) = generation == cameraGeneration
    fun endCameraSession(generation: Long) { if (isCameraSession(generation)) beginCameraSession() }
    fun cameraNotice(context: Context, message: Int) { _errorMessage.value = context.getString(message) }
    fun negotiatedCapture(width: Int, height: Int) {
        capturePixels = width.toLong() * height
        _captureSize.value = CapturePolicy.Size(width, height)
        _highResolutionActive.value = _highResolutionRequested.value && capturePixels > CapturePolicy.NORMAL_PIXELS
        CameraDiagnostics.negotiated("${width}x$height highRequested=${_highResolutionRequested.value} highActive=${_highResolutionActive.value}")
        if (BuildConfig.SENSITIVE_DIAGNOSTICS)
            Log.d("HachiCam-Camera", "negotiated=${width}x$height highRequested=${_highResolutionRequested.value} highActive=${_highResolutionActive.value}")
    }
    fun onCameraCaptureResult(result: TotalCaptureResult, generation: Long) {
        val timestamp = result.get(CaptureResult.SENSOR_TIMESTAMP) ?: return
        val time = result.get(CaptureResult.SENSOR_EXPOSURE_TIME)
        val iso = result.get(CaptureResult.SENSOR_SENSITIVITY)
        val boost = result.get(CaptureResult.CONTROL_POST_RAW_SENSITIVITY_BOOST)?.takeIf { it > 0 }
        val observed = if (time != null && time > 0 && iso != null && iso > 0) ManualExposure(time, iso, boost) else null
        val regions = result.request.get(CaptureRequest.CONTROL_AE_REGIONS)
        val sample = CaptureEvidence(timestamp, observed, result.get(CaptureResult.CONTROL_AF_STATE),
            result.get(CaptureResult.LENS_STATE), result.get(CaptureResult.LENS_FOCUS_DISTANCE),
            result.get(CaptureResult.CONTROL_AE_STATE), result.get(CaptureResult.CONTROL_AE_LOCK),
            result.get(CaptureResult.CONTROL_AE_EXPOSURE_COMPENSATION), regions?.size,
            regions?.joinToString(";") { "${it.rect.left},${it.rect.top},${it.rect.right},${it.rect.bottom},${it.meteringWeight}" },
            result.get(CaptureResult.CONTROL_AE_REGIONS)?.size)
        synchronized(evidenceLock) { if (isCameraSession(generation)) {
            captureEvidence.record(sample)
            latestMetering = MeteringObservation(generation, ++meteringRevision,
                android.os.SystemClock.elapsedRealtime(), sample)
        } }
    }
    fun rememberFocusPoint(point: MeteringPoint, generation: Long): Boolean {
        if (!isCameraSession(generation)) return false
        return focusTarget.remember(point, point.x, point.y, android.os.SystemClock.elapsedRealtime(), generation)
    }
    fun invalidateFocusTarget(generation: Long) { if (isCameraSession(generation)) focusTarget.clear() }
    fun invalidateFocusTargetCurrentSession() { focusTarget.clear() }
    fun previewGeometryChanged(generation: Long, width: Int, height: Int) {
        if (!isCameraSession(generation) || width <= 0 || height <= 0) return
        if (width != previewWidth || height != previewHeight) focusTarget.clear()
        previewWidth = width; previewHeight = height
    }
    private data class MeteringActionResult(val focused: Boolean = false, val acknowledged: Boolean = false)
    private fun meteringSnapshot(generation: Long): MeteringObservation? = synchronized(evidenceLock) {
        latestMetering?.takeIf { isCameraSession(generation) && it.session == generation }
    }
    private fun meteringLog(id: String, stage: String, sample: MeteringObservation?) {
        val frame = sample?.frame
        CameraDiagnostics.frame(id, "$stage revision=${sample?.revision} ageMs=${sample?.let { android.os.SystemClock.elapsedRealtime() - it.observedAtMs }} timestamp=${frame?.timestamp} exposure=${frame?.exposure} ae=${frame?.aeState} lock=${frame?.aeLocked} compensation=${frame?.aeCompensation} requestRegions=${frame?.aeRegionCount} resultRegions=${frame?.aeResultRegionCount} regionSignature=${frame?.aeRegionSignature}")
    }
    private suspend fun settledMetering(generation: Long, needsRegions: Boolean): MeteringObservation? {
        val boundary = meteringSnapshot(generation)?.revision ?: 0
        return withTimeoutOrNull(BurstMeteringPolicy.WAIT_MS) {
            var previous: MeteringObservation? = null
            var examined = boundary
            while (true) {
                check(isCameraSession(generation)) { "Camera session changed during metering" }
                val sample = meteringSnapshot(generation)
                if (sample != null && sample.revision > examined) {
                    if (BurstMeteringPolicy.settled(previous, sample, boundary, generation,
                            android.os.SystemClock.elapsedRealtime(), needsRegions)) return@withTimeoutOrNull sample
                    previous = sample; examined = sample.revision
                }
                delay(15)
            }
            @Suppress("UNREACHABLE_CODE") null
        }
    }
    private suspend fun focusForBurst(control: CameraControl?, info: Camera2CameraInfo?, context: Context, id: String, generation: Long): MeteringActionResult {
        if (!isCameraSession(generation)) { CameraDiagnostics.frame(id, "shutterTargetRejected=old_session"); return MeteringActionResult() }
        val selected = focusTarget.select(android.os.SystemClock.elapsedRealtime(), generation)
        CameraDiagnostics.frame(id, "shutterTarget=${if (selected != null) "tap" else "centre"} x=${selected?.x ?: .5f} y=${selected?.y ?: .5f} tapAgeMs=${selected?.ageMs ?: -1} session=$generation")
        meteringLog(id, "beforeShutterMetering", meteringSnapshot(generation))
        val supportsAf = (info?.getCameraCharacteristic(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE) ?: 0f) > 0
        val supportsAe = (info?.getCameraCharacteristic(CameraCharacteristics.CONTROL_MAX_REGIONS_AE) ?: 0) > 0
        if (control == null || !supportsAf && !supportsAe) {
            CameraDiagnostics.frame(id, "shutterMeteringSupported=false"); return MeteringActionResult()
        }
        val point = selected?.point
            ?: SurfaceOrientedMeteringPointFactory(1f, 1f).createPoint(.5f, .5f)
        val flags = FocusMeteringAction.FLAG_AE or if (supportsAf) FocusMeteringAction.FLAG_AF else 0
        val action = FocusMeteringAction.Builder(point, flags).disableAutoCancel().build()
        CameraDiagnostics.frame(id, "shutterMeteringFlags=$flags afSupported=$supportsAf aeRegionsSupported=$supportsAe")
        return withTimeoutOrNull(1500) {
            suspendCancellableCoroutine<MeteringActionResult> { continuation ->
                try {
                    val future = control.startFocusAndMetering(action)
                    continuation.invokeOnCancellation { future.cancel(false) }
                    future.addListener({
                        val result = runCatching {
                            val outcome = future.get()
                            MeteringActionResult(supportsAf && outcome.isFocusSuccessful, true)
                        }
                            .getOrDefault(MeteringActionResult())
                        if (continuation.isActive) continuation.resume(result)
                    }, ContextCompat.getMainExecutor(context))
                } catch (_: Exception) { if (continuation.isActive) continuation.resume(MeteringActionResult()) }
            }
        } ?: MeteringActionResult()
    }
    fun setZoom(context: Context, ratio: Float, onChanged: () -> Unit) {
        changeCameraControl(context, onChanged, geometryChange = true) { it.setZoomRatio(ratio) }
    }
    fun resetExposure(context: Context) {
        changeCameraControl(context, {}) { it.setExposureCompensationIndex(0) }
    }
    private fun changeCameraControl(context: Context, onChanged: () -> Unit, geometryChange: Boolean = false,
        apply: (CameraControl) -> com.google.common.util.concurrent.ListenableFuture<*>) {
        if (_isCapturing.value || zoomPending) return
        val control = cameraControl ?: return
        val generation = cameraGeneration
        if (geometryChange) focusTarget.clear()
        val request = ++zoomGeneration
        synchronized(evidenceLock) {
            zoomPending = true; evidenceAfterMs = Long.MAX_VALUE
            cropTracker.reset(); _detectedQuad.value = null; _isStable.value = false
        }
        frameAnalyzer?.resetStability()
        val future = try { apply(control) } catch (_: Exception) {
            zoomPending = false; evidenceAfterMs = android.os.SystemClock.elapsedRealtime() + 300
            cameraNotice(context, R.string.camera_change_failed)
            return
        }
        future.addListener({
            if (isCameraSession(generation) && zoomGeneration == request) {
                zoomPending = false; evidenceAfterMs = android.os.SystemClock.elapsedRealtime() + 300
                runCatching { future.get() }.onSuccess { onChanged() }
                    .onFailure { cameraNotice(context, R.string.camera_change_failed) }
            }
        }, ContextCompat.getMainExecutor(context))
        viewModelScope.launch {
            delay(3000)
            if (isCameraSession(generation) && zoomGeneration == request && zoomPending) {
                ++zoomGeneration; zoomPending = false
                evidenceAfterMs = android.os.SystemClock.elapsedRealtime() + 300
                future.cancel(false)
                cameraNotice(context, R.string.camera_change_failed)
            }
        }
    }
    private fun highMemoryAvailable(context: Context): Boolean {
        val info = android.app.ActivityManager.MemoryInfo()
        (context.getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager).getMemoryInfo(info)
        return CapturePolicy.canUseHigh(capturePixels, info.availMem, info.lowMemory)
    }
    fun toggleCurvedMode() {
        _curvedModeEnabled.value = !_curvedModeEnabled.value
        frameAnalyzer?.curvedMode = _curvedModeEnabled.value
    }
    fun setTouchPoint(normX: Float, normY: Float) {
        val analyzer = frameAnalyzer ?: return
        val point = PointF(normX, normY)
        analyzer.touchPoint = point
        viewModelScope.launch {
            delay(3000)
            if (analyzer.touchPoint === point) analyzer.touchPoint = null
        }
    }
    private val cropTracker = CropTracker()
    private var lastDetectionLogMs = 0L
    fun onFrameAnalyzed(result: DetectionResult, generation: Long = cameraGeneration) {
        synchronized(evidenceLock) {
        if (!isCameraSession(generation) || result.observedAtMs < evidenceAfterMs) return
        _isStable.value = result.isStable
        val w = (result.frameWidth - 1).coerceAtLeast(1).toFloat()
        val h = (result.frameHeight - 1).coerceAtLeast(1).toFloat()
        val points = if (result.found) result.quad?.toPointList()?.map { UnitPoint(it.x / w, it.y / h) } else null
        val selected = cropTracker.update(points, result.observedAtMs.takeIf { it > 0 } ?: android.os.SystemClock.elapsedRealtime(),
            "${result.frameWidth}:${result.frameHeight}:${result.rotationDegrees}", result.partial)
        _detectedQuad.value = if (selected == null) result.copy(found = false, quad = null)
        else result.copy(found = true, partial = selected.partial, observedAtMs = selected.observedAtMs, quad = selected.points.let {
            DocumentQuad(PointF(it[0].x * w, it[0].y * h), PointF(it[1].x * w, it[1].y * h),
                PointF(it[2].x * w, it[2].y * h), PointF(it[3].x * w, it[3].y * h))
        })
        val now = android.os.SystemClock.elapsedRealtime()
        if (BuildConfig.SENSITIVE_DIAGNOSTICS && now - lastDetectionLogMs >= 1000) {
            lastDetectionLogMs = now
            Log.d("HachiCam-Detection", "raw=${result.found} partial=${result.partial} method=${result.method} " +
                "candidates=${result.candidates} minSide=${result.sideSupport} tracker=${cropTracker.status} " +
                "selected=${selected != null} selectedPartial=${selected?.partial} ageMs=${selected?.let { now - it.observedAtMs }} " +
                "analysisMs=${result.analysisDurationMs} geometry=${result.frameWidth}x${result.frameHeight}/${result.rotationDegrees}")
        }
        }
    }

    fun capturePhoto(context: Context, onPageSaved: (String) -> Unit = {}) {
        if (_isCapturing.value) return
        if (!controlsReady || !BurstProcessor.isHealthy) { cameraNotice(context, R.string.camera_change_failed); return }
        val capture = imageCapture ?: return
        if (zoomPending) return
        val highResolution = _highResolutionActive.value
        if (highResolution && !highMemoryAvailable(context)) {
            _errorMessage.value = context.getString(R.string.high_pixel_memory)
            return
        }
        val ticket = BurstProcessor.reserve(highResolution)
        if (ticket == null) {
            _errorMessage.value = context.getString(R.string.capture_queue_full)
            return
        }
        _isCapturing.value = true
        val captureId = UUID.randomUUID().toString()
        val timing = com.scanner.app.data.util.ProcessingTimeline { android.os.SystemClock.elapsedRealtimeNanos() }
        timing.start("acquisitionAndControls")
        CameraDiagnostics.beginBurst(captureId)
        CameraDiagnostics.memory(captureId,"shutterAdmission")
        val app = context.applicationContext
        val hdr = _fullHdrEnabled.value
        val captureGeneration = cameraGeneration
        val capturedControl = cameraControl
        val capturedInfo = cameraInfo
        viewModelScope.launch {
            var submitted = false
            var restoreAttempted = false
            val control = capturedControl
            suspend fun restoreCaptureControls() {
                if (restoreAttempted) return
                restoreAttempted = true
                val restored = restoreOptions(control, app)
                CameraDiagnostics.frame(captureId, "controlsRestoreAcknowledged=$restored session=$captureGeneration")
                if (isCameraSession(captureGeneration)) {
                    controlsReady = restored
                    if (!restored) { _rebindRequest.update { it + 1 }; cameraNotice(app, R.string.camera_change_failed) }
                }
            }
            try {
                if (hdr && !_isStable.value) withTimeoutOrNull(400) { _isStable.first { it } }
                val frames = mutableListOf<Frame>()
                var encodedBytes = 0L
                fun retain(frame: Frame?) {
                    if (frame == null) return
                    if (encodedBytes + frame.bytes.size > CapturePolicy.ENCODED_LIMIT) {
                        Log.w("HachiCam-Burst", "Compressed burst budget reached; frame dropped")
                        return
                    }
                    encodedBytes += frame.bytes.size; frames.add(frame)
                }
                val info = capturedInfo?.let { Camera2CameraInfo.from(it) }
                val meteringAction = focusForBurst(control, info, app, captureId, captureGeneration)
                val focusConfirmed = meteringAction.focused
                check(isCameraSession(captureGeneration)) { "Camera session changed during focus" }
                val hasAeRegions = (info?.getCameraCharacteristic(CameraCharacteristics.CONTROL_MAX_REGIONS_AE) ?: 0) > 0
                val settled = if (meteringAction.acknowledged && hasAeRegions) settledMetering(captureGeneration, true) else null
                val lockTargetAe = hdr && settled != null && info?.getCameraCharacteristic(CameraCharacteristics.CONTROL_AE_LOCK_AVAILABLE) == true
                CameraDiagnostics.frame(captureId, "shutterMeteringAcknowledged=${meteringAction.acknowledged} targetExposureSettled=${settled != null} aeLockRequested=$lockTargetAe waitBoundMs=${BurstMeteringPolicy.WAIT_MS}")
                meteringLog(captureId, if (settled != null) "settledTargetExposure" else "targetExposureUnknown", settled ?: meteringSnapshot(captureGeneration))
                val minimumFocus = info?.getCameraCharacteristic(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE)
                val supportsFocusOff = info?.getCameraCharacteristic(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES)
                    ?.contains(CaptureRequest.CONTROL_AF_MODE_OFF) == true
                // Only the first newly captured frame can establish a held distance.
                // A recent preview result may still precede the shutter AF operation.
                var heldFocus: Float? = null
                fun focusOptions(builder: CaptureRequestOptions.Builder): CaptureRequestOptions.Builder {
                    heldFocus?.let {
                        builder.setCaptureRequestOption(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_OFF)
                        builder.setCaptureRequestOption(CaptureRequest.LENS_FOCUS_DISTANCE, it)
                    }
                    return builder
                }
                fun baseOptions(): CaptureRequestOptions {
                    val builder = focusOptions(CaptureRequestOptions.Builder())
                    if (lockTargetAe)
                        builder.setCaptureRequestOption(CaptureRequest.CONTROL_AE_LOCK, true)
                    if (info?.getCameraCharacteristic(CameraCharacteristics.CONTROL_AWB_LOCK_AVAILABLE) == true)
                        builder.setCaptureRequestOption(CaptureRequest.CONTROL_AWB_LOCK, true)
                    return builder.build()
                }
                if (hdr && control != null) {
                    val applied = applyOptions(control, app, baseOptions())
                    CameraDiagnostics.frame(captureId, "baseOptionsAcknowledged=$applied")
                    check(applied) { "Base camera options were not acknowledged" }
                }
                val detection = _detectedQuad.value?.takeIf {
                    CropTracker.cropEligible(it.found, it.partial, it.observedAtMs, android.os.SystemClock.elapsedRealtime())
                }
                if (BuildConfig.SENSITIVE_DIAGNOSTICS)
                    Log.d("HachiCam-Detection", "shutter=${if (detection == null) "full-image" else "complete-crop"} latestPartial=${_detectedQuad.value?.partial}")
                repeat(if (hdr) 4 else 1) { index ->
                    check(isCameraSession(captureGeneration)) { "Camera session changed during burst" }
                    val frame = takeFrame(capture, app, 0, captureId)
                    retain(frame)
                    if (index == 0 && hdr && control != null && heldFocus == null) {
                        heldFocus = BurstFocusPolicy.heldDistance(focusConfirmed, frame?.evidence, 0, minimumFocus, supportsFocusOff)
                        if (heldFocus != null) {
                            val applied = applyOptions(control, app, baseOptions())
                            CameraDiagnostics.frame(captureId, "heldFocusOptionsAcknowledged=$applied")
                            check(applied) { "Focus hold options were not acknowledged" }
                        }
                    }
                }
                if (BuildConfig.SENSITIVE_DIAGNOSTICS) CameraDiagnostics.frame(captureId, "focusConfirmed=$focusConfirmed heldDistance=$heldFocus")
                check(frames.isNotEmpty()) { "No camera frame received" }
                val base = frames.first().exposure
                val caps = info?.getCameraCharacteristic(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES)
                val times = info?.getCameraCharacteristic(CameraCharacteristics.SENSOR_INFO_EXPOSURE_TIME_RANGE)
                val isoRange = info?.getCameraCharacteristic(CameraCharacteristics.SENSOR_INFO_SENSITIVITY_RANGE)
                val manual = caps?.contains(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR) == true
                if (hdr && control != null && manual && base != null && times != null && isoRange != null) {
                    for ((tier, count, stops) in listOf(Triple(1, 2, 2.5), Triple(2, 1, 6.0))) {
                        val boostRange = info?.getCameraCharacteristic(CameraCharacteristics.CONTROL_POST_RAW_SENSITIVITY_BOOST_RANGE)
                        val exposure = (if (tier == 2) ExposurePlan.cleanBracket(base, stops, times.lower, times.upper,
                            isoRange.lower, isoRange.upper, boostRange?.contains(100) == true)
                        else ExposurePlan.bracket(base, stops, times.lower, times.upper, isoRange.lower, isoRange.upper)) ?: continue
                        val options = focusOptions(CaptureRequestOptions.Builder())
                            .setCaptureRequestOption(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_OFF)
                            .setCaptureRequestOption(CaptureRequest.SENSOR_EXPOSURE_TIME, exposure.timeNs)
                            .setCaptureRequestOption(CaptureRequest.SENSOR_SENSITIVITY, exposure.iso)
                        if (info?.getCameraCharacteristic(CameraCharacteristics.CONTROL_AWB_LOCK_AVAILABLE) == true)
                            options.setCaptureRequestOption(CaptureRequest.CONTROL_AWB_LOCK, true)
                        exposure.postRawBoost?.takeIf { boostRange?.contains(it) == true }?.let {
                            options.setCaptureRequestOption(CaptureRequest.CONTROL_POST_RAW_SENSITIVITY_BOOST, it)
                        }
                        val applied = applyOptions(control, app, options.build())
                        CameraDiagnostics.frame(captureId, "tier=$tier optionsAcknowledged=$applied")
                        if (!applied) continue
                        repeat(count) {
                            check(isCameraSession(captureGeneration)) { "Camera session changed during brackets" }
                            val frame = takeFrame(capture, app, tier, captureId)
                            val sensorBase = frames.first().evidence?.exposure
                            val sensorShort = frame?.evidence?.exposure
                            val evidence = ExposurePlan.evidence(listOf(sensorBase,sensorShort),listOf(frames.first().exifExposure,frame?.exifExposure))
                            val baseProduct = evidence.products[0]; val shortProduct = evidence.products[1]
                            if (frame != null && baseProduct > 0 && shortProduct > 0 && shortProduct < baseProduct * .75)
                                retain(frame)
                            else Log.w("HachiCam-Burst", "Rejected bracket: requested=$exposure actual=${frame?.exposure}")
                        }
                    }
                }
                val id = captureId
                // Complete and record hardware cleanup before the worker snapshots this capture.
                restoreCaptureControls()
                timing.end("acquisitionAndControls")
                timing.start("queue")
                val output = File(ImageStorage(app).getStorageDir(), "$id.jpg")
                PageRepository.addPage(ScannedPage(id = id, originalImagePath = output.absolutePath,
                    filter = ImageFilter.ORIGINAL, status = PageStatus.PROCESSING))
                BurstProcessor.submit(ticket, failure = { error ->
                    timing.finishPending("workerFailed")
                    PageRepository.updatePage(id) { if (it.status == PageStatus.PROCESSING) it.copy(status = PageStatus.FAILED) else it }
                    Log.e("HachiCam-Burst", "Capture job unavailable", error)
                }, dequeued = { timing.end("queue"); timing.start("heavyPermitWait") },
                    admitted = { timing.end("heavyPermitWait") }) {
                    processFrames(app, frames, detection, id, output, hdr, highResolution, timing)
                }
                submitted = true
                onPageSaved(id)
            } catch (error: CancellationException) { throw error }
            catch (error: Exception) {
                Log.e("HachiCam-Burst", "Capture failed", error)
                timing.finishPending("captureFailed")
                _errorMessage.value = app.getString(R.string.capture_failed)
            } finally {
                restoreCaptureControls()
                _isCapturing.value = false
                frameAnalyzer?.resetStability()
                if (!submitted) BurstProcessor.cancelReservation(ticket)
            }
        }
    }

    private suspend fun restoreOptions(control: CameraControl?, context: Context): Boolean {
        if (control == null) return true
        return withContext(NonCancellable + Dispatchers.Main) {
            val clear = withTimeoutOrNull(3000) { awaitControl(context) { Camera2CameraControl.from(control).clearCaptureRequestOptions() } } == true
            val cancel = withTimeoutOrNull(1500) { awaitControl(context) { control.cancelFocusAndMetering() } } == true
            clear && cancel
        }
    }
    private suspend fun applyOptions(control: CameraControl, context: Context, options: CaptureRequestOptions): Boolean =
        withTimeoutOrNull(3000) { awaitControl(context) { Camera2CameraControl.from(control).setCaptureRequestOptions(options) } } == true
    private suspend fun awaitControl(context: Context, operation: () -> com.google.common.util.concurrent.ListenableFuture<Void>): Boolean =
        suspendCancellableCoroutine { continuation ->
            try {
                val future = operation()
                continuation.invokeOnCancellation { future.cancel(false) }
                future.addListener({
                    val success = try { future.get(); true } catch (_: Exception) { false }
                    if (continuation.isActive) continuation.resume(success)
                }, ContextCompat.getMainExecutor(context))
            } catch (_: Exception) { if (continuation.isActive) continuation.resume(false) }
        }

    private suspend fun takeFrame(capture: ImageCapture, context: Context, tier: Int, captureId: String): Frame? {
        val start = android.os.SystemClock.elapsedRealtime()
        val data = withTimeoutOrNull(15000) {
            suspendCancellableCoroutine<Triple<ByteArray, Int, Long>?> { continuation ->
                capture.takePicture(ContextCompat.getMainExecutor(context), object : ImageCapture.OnImageCapturedCallback() {
                    override fun onCaptureSuccess(image: ImageProxy) {
                        try {
                            check(image.format == ImageFormat.JPEG || image.format == ImageFormat.JPEG_R)
                            val buffer = image.planes[0].buffer
                            val bytes = ByteArray(buffer.remaining()).also { buffer.get(it) }
                            if (continuation.isActive) continuation.resume(Triple(bytes, image.imageInfo.rotationDegrees, image.imageInfo.timestamp))
                        } catch (error: Exception) {
                            Log.e("HachiCam-Burst", "JPEG capture failed", error)
                            if (continuation.isActive) continuation.resume(null)
                        } finally { image.close() }
                    }
                    override fun onError(exception: ImageCaptureException) {
                        Log.e("HachiCam-Burst", "Camera capture failed", exception)
                        if (continuation.isActive) continuation.resume(null)
                    }
                })
            }
        } ?: return null
        val exifExposure = withContext(Dispatchers.Default) {
            runCatching {
                val exif = ByteArrayInputStream(data.first).use { ExifInterface(it) }
                val seconds = exif.getAttributeDouble(ExifInterface.TAG_EXPOSURE_TIME, 0.0)
                val iso = exif.getAttributeInt(ExifInterface.TAG_PHOTOGRAPHIC_SENSITIVITY, 0)
                if (seconds > 0 && iso > 0) ManualExposure((seconds * 1e9).toLong(), iso) else null
            }.getOrNull()
        }
        val evidence = captureEvidence.take(data.third) ?: withTimeoutOrNull(100) {
            var observed: CaptureEvidence? = null
            while (observed == null) { delay(5); observed = captureEvidence.take(data.third) }
            observed
        }
        val exposure = evidence?.exposure ?: exifExposure
        Log.i("HachiCam-Burst", "tier=$tier elapsed=${android.os.SystemClock.elapsedRealtime() - start}ms rotation=${data.second} exposure=$exposure")
        if (BuildConfig.SENSITIVE_DIAGNOSTICS) {
            val line = "tier=$tier timestamp=${data.third} sensor=${evidence?.exposure} exif=$exifExposure af=${evidence?.afState} lens=${evidence?.lensState} focusDistance=${evidence?.focusDistance} ae=${evidence?.aeState} aeLock=${evidence?.aeLocked} compensation=${evidence?.aeCompensation} requestAeRegions=${evidence?.aeRegionCount} resultAeRegions=${evidence?.aeResultRegionCount} aeRegionSignature=${evidence?.aeRegionSignature}"
            Log.d("HachiCam-Burst", line); CameraDiagnostics.frame(captureId,line)
        }
        return Frame(data.first, data.second, tier, exposure, exifExposure, evidence)
    }

    private fun decode(frame: Frame): Mat {
        val buffer = Mat(frame.bytes.size,1,org.opencv.core.CvType.CV_8UC1)
        buffer.put(0,0,frame.bytes)
        return try { Imgcodecs.imdecode(buffer, Imgcodecs.IMREAD_COLOR or Imgcodecs.IMREAD_IGNORE_ORIENTATION) }
        finally { buffer.release() }
    }
    private fun saveUpright(mat: Mat, frame: Frame, output: File, mode: String,
                            timing: com.scanner.app.data.util.ProcessingTimeline,
                            targetQuad: (Float, Float) -> DocumentQuad): Pair<Float, Float>? {
        val upright = if (frame.rotation == 0) mat else Mat()
        val params = MatOfInt(Imgcodecs.IMWRITE_JPEG_QUALITY, 95)
        val temp = File(output.parentFile, "${output.nameWithoutExtension}.tmp.jpg")
        try {
            timing.measure("rotation") { when (frame.rotation) {
                90 -> Core.rotate(mat, upright, Core.ROTATE_90_CLOCKWISE)
                180 -> Core.rotate(mat, upright, Core.ROTATE_180)
                270 -> Core.rotate(mat, upright, Core.ROTATE_90_COUNTERCLOCKWISE)
                else -> Unit
            } }
            timing.measure("jpegEncoding") { check(Imgcodecs.imwrite(temp.absolutePath, upright, params)) { "JPEG write failed" } }
            timing.measure("exifStamping") { ExifUtils.copyAndStampExif(frame.bytes, temp, mode) }
            val w = upright.cols().toFloat(); val h = upright.rows().toFloat()
            val committed = timing.measure("fileCommitAndReady") { PageRepository.commitCapture(output.nameWithoutExtension, temp, output, targetQuad(w, h)) }
            if (!committed) return null
            return w to h
        } finally { if (upright !== mat) upright.release(); params.release(); temp.delete() }
    }

    private fun retainBurstDiagnostics(context: Context, frames: List<Frame>, id: String, output: File,
                                       w: Float, h: Float, mode: String, reference: Int, domain: String,
                                       timing: com.scanner.app.data.util.ProcessingTimeline, reconstruction: JSONObject? = null) {
        if (!BuildConfig.SENSITIVE_DIAGNOSTICS) return
        runCatching {
            val sourceLease=PageRepository.readMaster(id) ?: return@runCatching
            sourceLease.use {
            check(sourceLease.pages.single().originalImagePath==output.absolutePath)
            val metadata = JSONObject().put("captureId", id).put("version", BuildConfig.VERSION_NAME)
                .put("build", BuildConfig.BUILD_TIME).put("source", BuildConfig.GIT_HASH)
                .put("outputWidth", w.toInt()).put("outputHeight", h.toInt())
                .put("exposureDomain", domain).put("result", mode).put("referenceIndex", reference)
                .put("reconstruction", reconstruction ?: JSONObject.NULL)
                .put("timing", JSONObject().put("schema", 1).put("clock", "elapsedRealtimeNanos")
                    .put("elapsedToSnapshotMs", timing.elapsedMs()).put("stages", JSONArray().apply {
                        timing.snapshot().forEach { stage -> put(JSONObject().put("name", stage.name)
                            .put("offsetMs", stage.offsetMs).put("durationMs", stage.durationMs).put("outcome", stage.outcome)) }
                    }).put("unfinishedStages", JSONArray(timing.pending())))
                .put("frames", JSONArray().apply { frames.forEachIndexed { index, frame ->
                    put(JSONObject().put("file", String.format(Locale.US, "%02d-tier%d.jpg", index, frame.tier))
                        .put("tier", frame.tier).put("rotation", frame.rotation)
                        .put("sensorTimestamp", frame.evidence?.timestamp ?: JSONObject.NULL)
                        .put("sensorExposure", frame.evidence?.exposure?.toString() ?: JSONObject.NULL)
                        .put("aeState", frame.evidence?.aeState ?: JSONObject.NULL)
                        .put("aeLocked", frame.evidence?.aeLocked ?: JSONObject.NULL)
                        .put("aeCompensation", frame.evidence?.aeCompensation ?: JSONObject.NULL)
                        .put("requestAeRegionCount", frame.evidence?.aeRegionCount ?: JSONObject.NULL)
                        .put("requestAeRegionSignature", frame.evidence?.aeRegionSignature ?: JSONObject.NULL)
                        .put("resultAeRegionCount", frame.evidence?.aeResultRegionCount ?: JSONObject.NULL)
                        .put("exifExposure", frame.exifExposure?.toString() ?: JSONObject.NULL)
                        .put("afState", frame.evidence?.afState ?: JSONObject.NULL)
                        .put("lensState", frame.evidence?.lensState ?: JSONObject.NULL)
                        .put("focusDistance", frame.evidence?.focusDistance?.takeIf { it.isFinite() } ?: JSONObject.NULL))
                } })
            val stored = BurstArchiveStore(File(context.cacheDir, "debug_burst"), true).replace(
                frames.mapIndexed { index, frame -> String.format(Locale.US, "%02d-tier%d.jpg", index, frame.tier) to frame.bytes },
                output, metadata.toString(2), CameraDiagnostics.snapshot(id))
            CameraDiagnostics.frame(id, "diagnosticArchiveStored=$stored")
            CameraDiagnostics.memory(id,"finalDiagnosticSnapshot")
            }
        }.onFailure { Log.w("HachiCam-Burst", "Diagnostic archive skipped; photo remains valid", it) }
    }

    private fun processFrames(context: Context, frames: List<Frame>, detection: DetectionResult?, id: String, output: File, hdr: Boolean, highResolution: Boolean,
                              timing: com.scanner.app.data.util.ProcessingTimeline) {
        val mats = mutableListOf<Mat>()
        val valid = mutableListOf<Frame>()
        var fused: Mat? = null
        val temporaryInputs = mutableListOf<File>()
        try {
            if (PageRepository.getPage(id)?.status != PageStatus.PROCESSING) return
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeByteArray(frames.first().bytes, 0, frames.first().bytes.size, bounds)
            val pixels = bounds.outWidth.toLong() * bounds.outHeight
            check(bounds.outWidth > 1 && bounds.outHeight > 1 && pixels <= CapturePolicy.HIGH_PIXELS) { "Invalid capture dimensions" }
            val memoryPlan = checkNotNull(ImageMemoryPolicy.fusionPlan(pixels, frames.size, highResolution,
                ImageWork.budget(), ImageWork.reserved())) { "Fusion memory budget unavailable" }
            ImageWork.check(memoryPlan.bytes)
            CameraDiagnostics.frame(id, "memoryPlan=${memoryPlan.reason} bytes=${memoryPlan.bytes} reserved=${ImageWork.reserved()}")
            val useStreaming = memoryPlan.streaming
            timing.measure("jpegDecodeOrSpill") { frames.forEachIndexed { index, frame ->
                if (frame.rotation != frames[0].rotation) {
                    Log.w("HachiCam-Burst", "Dropped frame $index: burst orientation changed")
                    return@forEachIndexed
                }
                val frameBounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                BitmapFactory.decodeByteArray(frame.bytes, 0, frame.bytes.size, frameBounds)
                if (frameBounds.outWidth != bounds.outWidth || frameBounds.outHeight != bounds.outHeight) {
                    check(index != 0) { "Reference dimensions are invalid" }
                    Log.w("HachiCam-Burst", "Dropped frame $index before decode: incompatible dimensions")
                    return@forEachIndexed
                }
                if (useStreaming) {
                    val temp = File.createTempFile("burst_${id}_", ".jpg", context.cacheDir)
                    temporaryInputs.add(temp); temp.writeBytes(frame.bytes); valid.add(frame)
                } else {
                    val mat = decode(frame)
                    if (mat.empty()) { mat.release(); check(index != 0) { "Reference decode failed" } }
                    else { mats.add(mat); valid.add(frame) }
                }
            }
            } // timed decode/spill
            CameraDiagnostics.memory(id,"decodedOrSpilled")
            val fuser = NativeBurstFusion()
            val evidence = ExposurePlan.evidence(valid.map { it.evidence?.exposure },valid.map { it.exifExposure })
            val products = evidence.products
            if (BuildConfig.SENSITIVE_DIAGNOSTICS) Log.d("HachiCam-Burst", "fusionExposureDomain=${evidence.description}")
            fused = timing.measure("nativeFusion") { if (useStreaming) {
                fuser.fuseBurstFiles(temporaryInputs.map { it.absolutePath }, valid.map { it.tier }.toIntArray(),
                    products)
            } else if (hdr) fuser.fuseBurstFrames(mats, removeGlare = false, isScreenMode = true,
                superResolution = memoryPlan.superResolution, tiers = valid.map { it.tier }.toIntArray(),
                exposureProducts = products) else mats[0].clone() }
            check(!fused.empty()) { "Fusion returned no image" }
            CameraDiagnostics.memory(id,"nativeFusionComplete")
            val report = fuser.lastReport
            val decision = if (hdr || useStreaming) report.reconstruction else ReconstructionDecision(denied = 1)
            val highlightOutcome=com.scanner.app.data.camera.HighlightOutcome.classify(report.cropW.toLong()*report.cropH,report.clippedFraction,report.recoveredFraction)
            val srRecord = JSONObject().put("schema", 1).put("requestedRegular2x", hdr && !highResolution)
                .put("engineRequested", decision.requested).put("memoryPlan", memoryPlan.reason)
                .put("memoryAdmitted2x", hdr && memoryPlan.superResolution).put("deniedFlags", decision.denied)
                .put("denialReasons", JSONArray(decision.denialReasons())).put("sourceWidth", bounds.outWidth)
                .put("sourceHeight", bounds.outHeight).put("anchorCount", report.anchors).put("selectedScale", report.scale)
                .put("regionalEvaluationPerformed", decision.requested && report.anchors >= 3 && bounds.outWidth.toLong() * bounds.outHeight <= 16_000_000 && report.cropW.toLong() * report.cropH * 4 <= 56_000_000)
                .put("meanWholeSupportDiagnostic", decision.meanSupport).put("centrePhasesDiagnostic", decision.centrePhases)
                .put("qualifiedTiles", decision.qualifiedTiles).put("totalTiles", decision.tiles)
                .put("qualifiedAreaFraction", decision.regionFraction).put("reconstructedPixelFraction", decision.reconstructedFraction)
                .put("tileSize", decision.tileSize).put("minimumArea", decision.minimumArea).put("minimumTiles", decision.minimumTiles)
                .put("minimumTileSupport", decision.minimumSupport).put("phaseDeterminant", decision.phaseDeterminant)
                .put("fittedTiles", decision.fittedTiles).put("initialFitError", decision.initialFitError).put("finalFitError", decision.finalFitError)
                .put("contributionDomain", "nativeFusionGrid")
                .put("highlightMetricDomain", "nativeFusionGrid")
                .put("maximumSourcePixels", 16_000_000).put("maximumOutputPixels", 56_000_000).put("minimumAnchors", 3).put("probeGrid", 3)
                .put("refinementParameterVersion", "jpeg-footprint-v1")
                .put("maximumIterations", 8).put("residualLimit", 14).put("correctionLimit", 24)
                .put("requiredFitErrorRatio", .95).put("minimumObservedPlates", 3).put("minimumPlateObservations", 256)
                .put("contrastRangeFloor", 12).put("noiseSigmaFactor", 6).put("patchHalo", 8)
                .put("maximumPatchObservations", 50_000).put("maximumSourceSearch", 128)
            srRecord.put("registrationParameterVersion","masked-ecc-edges-v1").put("optimizerMargin",10)
            srRecord.put("gpuFitParameterVersion","ecc-return-parity-v2-tiled-resident")
                .put("highlightScheduling",if(hdr && !useStreaming) "native-demand-v1" else "not-used")
            srRecord.put("nativePerformance",report.performance?.let { perf ->
                JSONObject().put("schema",1).put("totalMs",perf.totalMs)
                    .put("globalRegistrationMs",perf.globalRegistrationMs).put("localRegistrationMs",perf.localRegistrationMs)
                    .put("remainingNativeMs",perf.remainingNativeMs).put("gpuSupportMaps",perf.gpuSupportMaps)
                    .put("gpuFitProposals",perf.gpuFitProposals).put("remainingScope","reference/decode/prepare/fusion/HDR/reconstruction; not pure SR")
            } ?: JSONObject.NULL)
            srRecord.put("highlightMetrics",JSONObject().put("schema",1).put("domain","nativeFusionGrid")
                .put("outcome",highlightOutcome).put("preClippedFraction",report.clippedFraction)
                .put("changedFraction",report.recoveredFraction).put("postClippedFraction",report.postClippedFraction ?: JSONObject.NULL)
                .put("minimumChangedPixels",128).put("minimumClippedShare",.01).put("targetRoiVerified",false))
            srRecord.toString().chunked(440).forEachIndexed { index, chunk -> CameraDiagnostics.frame(id, "srDecisionPart$index=$chunk") }
            val mode = if (!hdr) "Normal" else
                "${if (highResolution) "Native-high-resolution " else ""}Burst; highlightOutcome=$highlightOutcome; anchors=${report.anchors}; highlights=${report.highlights}; scale=${report.scale}; reference=${report.reference}; multiFrameFraction=${report.multiFrameFraction}; meanContributors=${report.meanContributors}; contributionDomain=nativeFusionGrid; recoveredFraction=${report.recoveredFraction}; clippedFraction=${report.clippedFraction}; postClippedFraction=${report.postClippedFraction}; sourceCrop=${report.left},${report.top},${report.cropW},${report.cropH}; JPEG fusion"
            val (w, h) = saveUpright(fused, valid[if (hdr) report.reference else 0], output, mode, timing) { w, h ->
                computeTargetQuad(detection, w, h, frames[0].rotation, if (hdr) report else null)
            } ?: return
            CameraDiagnostics.frame(id,"saved=${w.toInt()}x${h.toInt()} exposureDomain=${evidence.description} $mode")
            retainBurstDiagnostics(context, valid, id, output, w, h, mode, if (hdr) report.reference else 0, evidence.description, timing, srRecord)
            _lastOutputSize.value = CapturePolicy.Size(w.toInt(), h.toInt())
            if (hdr && (report.anchors < 2 || report.multiFrameFraction <= 0.0))
                _errorMessage.value = context.getString(R.string.fusion_single_frame)
            else if (hdr && report.clippedFraction > .005 && report.recoveredFraction <= 0)
                _errorMessage.value = context.getString(R.string.hdr_no_recovery)
        } catch (error: Exception) {
            Log.e("HachiCam-Burst", "Fusion failed; retaining reference", error)
            try {
                if (PageRepository.getPage(id)?.status != PageStatus.PROCESSING) return
                // Free failed fusion buffers before attempting the smaller fallback.
                fused?.release(); fused = null; mats.forEach(Mat::release); mats.clear()
                val fallbackBounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                BitmapFactory.decodeByteArray(frames[0].bytes, 0, frames[0].bytes.size, fallbackBounds)
                check(fallbackBounds.outWidth > 1 && fallbackBounds.outHeight > 1) { "Invalid fallback dimensions" }
                ImageWork.check(ImageMemoryPolicy.analysisBytes(fallbackBounds.outWidth.toLong() * fallbackBounds.outHeight))
                val fallback = decode(frames[0])
                try {
                    check(!fallback.empty())
                    val (w, h) = saveUpright(fallback, frames[0], output, "Fallback reference (fusion failed)", timing) { w, h ->
                        computeTargetQuad(detection, w, h, frames[0].rotation)
                    } ?: return
                    retainBurstDiagnostics(context, frames, id, output, w, h, "Fallback reference (fusion failed)", 0, "see per-frame metadata", timing)
                    _lastOutputSize.value = CapturePolicy.Size(w.toInt(), h.toInt())
                    CameraDiagnostics.frame(id,"savedFallback=${w.toInt()}x${h.toInt()} first-base reference")
                    _errorMessage.value = context.getString(R.string.fusion_fallback)
                } finally { fallback.release() }
            } catch (saveError: Exception) {
                Log.e("HachiCam-Burst", "Reference save failed", saveError)
                PageRepository.updatePage(id) { if (it.status == PageStatus.PROCESSING) it.copy(status = PageStatus.FAILED) else it }
                _errorMessage.value = context.getString(R.string.capture_failed)
            }
        } finally {
            timing.finishPending("interrupted")
            fused?.release(); mats.forEach(Mat::release)
            temporaryInputs.forEach { if (it.exists() && !it.delete()) Log.w("HachiCam-Burst", "Temporary burst cleanup failed") }
        }
        // Published files retire through the repository, including active export leases.
        if (PageRepository.getPage(id) == null) return
        if (output.exists()) _lastThumbnailBitmap.value = BitmapFactory.decodeFile(output.absolutePath,
            BitmapFactory.Options().apply { inSampleSize = 16 })
    }

    private fun computeTargetQuad(result: DetectionResult?, width: Float, height: Float, rotation: Int, coverage: NativeBurstFusion.Report? = null): DocumentQuad {
        fun fullFrame() = DocumentQuad(PointF(0f, 0f), PointF(width - 1, 0f),
            PointF(width - 1, height - 1), PointF(0f, height - 1))
        val quad = result?.quad
        if (result?.found != true || result.partial || quad == null || result.frameWidth <= 1 || result.frameHeight <= 1 ||
            coverage?.geometryKnown == false) return fullFrame()
        val points = quad.toPointList().map {
            val raw = ImageCoordinates.rotate(UnitPoint(it.x / (result.frameWidth - 1), it.y / (result.frameHeight - 1)), -result.rotationDegrees)
            val p = if (coverage != null && coverage.cropW > 1 && coverage.cropH > 1) {
                val reference = ImageCoordinates.project(raw, coverage.sourceW, coverage.sourceH, coverage.firstToReference)
                    ?: return fullFrame()
                ImageCoordinates.throughCrop(reference, coverage.sourceW, coverage.sourceH, coverage.left, coverage.top,
                    coverage.cropW, coverage.cropH, coverage.scale, rotation)
            } else ImageCoordinates.rotate(raw, rotation)
            // Out-of-crop corners imply truncated document evidence: retain the
            // whole available image for manual correction instead of stretching it.
            if (p.x !in 0f..1f || p.y !in 0f..1f) return fullFrame()
            p
        }
        if (!CropTracker.valid(points)) return fullFrame()
        val start = points.indices.minBy { points[it].x + points[it].y }
        fun point(i: Int) = points[(start + i) % 4].let { PointF(it.x * (width - 1), it.y * (height - 1)) }
        return DocumentQuad(point(0), point(1), point(2), point(3))
    }

    fun importFromUris(context: Context, uris: List<Uri>, onSaved: (List<String>) -> Unit) {
        if (_isImporting.value) return
        _isImporting.value = true
        viewModelScope.launch(Dispatchers.IO) {
            val ids = mutableListOf<String>()
            try {
                for (uri in uris) {
                    ensureActive()
                    val id = UUID.randomUUID().toString()
                    val file = File(ImageStorage(context).getStorageDir(), "$id.jpg")
                    var published = false
                    try {
                        ImageWork.run {
                        val bitmap = ImageDecoder.decodeBitmap(ImageDecoder.createSource(context.contentResolver, uri)) { decoder, info, _ ->
                            decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
                            val pixels = info.size.width.toDouble() * info.size.height
                            val memory = (context.getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager).memoryClass
                            val budget = minOf(16000000.0, memory * 1024.0 * 1024.0 / 20.0)
                            var targetWidth = info.size.width; var targetHeight = info.size.height
                            if (pixels > budget) {
                                val scale = sqrt(budget / pixels)
                                targetWidth = maxOf(1, (info.size.width * scale).toInt())
                                targetHeight = maxOf(1, (info.size.height * scale).toInt())
                                decoder.setTargetSize(targetWidth, targetHeight)
                            }
                            ImageWork.check(ImageMemoryPolicy.analysisBytes(targetWidth.toLong() * targetHeight))
                        }
                        val w = bitmap.width.toFloat(); val h = bitmap.height.toFloat()
                        try { file.outputStream().use { check(bitmap.compress(Bitmap.CompressFormat.JPEG, 95, it)) } }
                        finally { bitmap.recycle() }
                        ExifUtils.stampSignature(file, "Imported")
                        val mat = Imgcodecs.imread(file.absolutePath)
                        val detected = try { NativeEdgeDetector().detectDocument(mat, false) } finally { mat.release() }
                        PageRepository.addPage(ScannedPage(id = id, originalImagePath = file.absolutePath,
                            quad = computeTargetQuad(detected, w, h, 0), filter = ImageFilter.ORIGINAL))
                        published = true
                        ids.add(id)
                        }
                    } catch (error: CancellationException) { if (!published) file.delete(); throw error }
                    catch (error: Exception) {
                        if (!published) file.delete()
                        Log.e("HachiCam-Import", "Cannot import selected image", error)
                        _errorMessage.value = context.getString(R.string.import_failed)
                    }
                }
                withContext(Dispatchers.Main) { if (ids.isNotEmpty()) onSaved(ids) }
            } finally { _isImporting.value = false }
        }
    }
}
