package com.scanner.app.ui.camera

import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.CameraCharacteristics
import androidx.camera.camera2.interop.Camera2CameraInfo
import androidx.camera.camera2.interop.Camera2Interop
import androidx.camera.camera2.interop.ExperimentalCamera2Interop
import androidx.camera.core.AspectRatio
import androidx.camera.core.CameraSelector
import androidx.camera.core.FocusMeteringAction
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageCapture
import androidx.camera.core.Preview
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.*
import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import java.io.File
import java.util.concurrent.TimeUnit
import kotlin.math.max
import kotlin.math.min
import com.scanner.app.domain.model.PageStatus
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Collections
import androidx.compose.material.icons.filled.CropFree
import androidx.compose.material.icons.filled.FlashOff
import androidx.compose.material.icons.filled.FlashOn
import androidx.compose.material.icons.filled.HdrOn
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import android.net.Uri
import android.view.ViewGroup
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.result.PickVisualMediaRequest
import com.scanner.app.data.camera.ImageCoordinates
import com.scanner.app.data.camera.CapturePolicy
import com.scanner.app.data.camera.CameraChoicePolicy
import com.scanner.app.BuildConfig
import com.scanner.app.data.util.CameraDiagnostics
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import android.view.OrientationEventListener
import android.view.Surface
import androidx.lifecycle.viewmodel.compose.viewModel
import com.scanner.app.R
import com.scanner.app.domain.model.DetectionResult
import com.scanner.app.engine.NativeEdgeDetector
import com.scanner.app.ui.theme.PrismCyan
import com.scanner.app.ui.theme.SteadyEmerald
import com.scanner.app.ui.theme.SteadyAmber
import java.util.concurrent.Executors
import kotlinx.coroutines.delay

@androidx.annotation.OptIn(ExperimentalCamera2Interop::class)
@Composable
fun CameraScreen(
    onNavigateToReview: () -> Unit,
    onNavigateToCrop: (String) -> Unit,
    onNavigateToSettings: () -> Unit,
    viewModel: CameraViewModel = viewModel()
) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current

    val detectedQuad by viewModel.detectedQuad.collectAsState()
    val curvedModeEnabled by viewModel.curvedModeEnabled.collectAsState()
    val fullHdrEnabled by viewModel.fullHdrEnabled.collectAsState()
    val highRequested by viewModel.highResolutionRequested.collectAsState()
    val rebindRequest by viewModel.rebindRequest.collectAsState()
    var selectedBackId by rememberSaveable { mutableStateOf<String?>(null) }
    var backChoices by remember { mutableStateOf<List<Pair<String, String>>>(emptyList()) }
    var boundBackId by remember { mutableStateOf<String?>(null) }
    var highSuppressed by remember(highRequested, selectedBackId) { mutableStateOf(false) }
    val isStable by viewModel.isStable.collectAsState()
    val isCapturing by viewModel.isCapturing.collectAsState()
    val isImporting by viewModel.isImporting.collectAsState()
    val errorMessage by viewModel.errorMessage.collectAsState()
    val snackbar = remember { SnackbarHostState() }
    LaunchedEffect(errorMessage) {
        errorMessage?.let { snackbar.showSnackbar(it); viewModel.clearError() }
    }
    val analyzerExecutor = remember { Executors.newSingleThreadExecutor() }
    DisposableEffect(Unit) {
        onDispose {
            viewModel.imageAnalysis?.clearAnalyzer()
            analyzerExecutor.shutdown()
            viewModel.frameAnalyzer?.close()
        }
    }
    val pageCount by viewModel.pageCount.collectAsState()
    val capturedPages by viewModel.capturedPages.collectAsState()

    LaunchedEffect(Unit) {
        viewModel.updateSettings(context)
    }

    DisposableEffect(lifecycleOwner) {
        val observer = androidx.lifecycle.LifecycleEventObserver { _, event ->
            if (event == androidx.lifecycle.Lifecycle.Event.ON_RESUME) {
                viewModel.updateSettings(context)
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    var deviceRotationDegrees by remember { mutableFloatStateOf(0f) }
    val windowView = androidx.compose.ui.platform.LocalView.current
    DisposableEffect(context) {
        var orientationActive = true
        val orientationListener = object : OrientationEventListener(context) {
            private var appliedRotation: Int? = null
            override fun onOrientationChanged(orientation: Int) {
                if (!orientationActive || orientation == ORIENTATION_UNKNOWN) return
                val rotation = when (orientation) {
                    in 45 until 135 -> Surface.ROTATION_270
                    in 135 until 225 -> Surface.ROTATION_180
                    in 225 until 315 -> Surface.ROTATION_90
                    else -> Surface.ROTATION_0
                }
                if (!viewModel.isCapturing.value) {
                    if (appliedRotation != null && appliedRotation != rotation) viewModel.invalidateFocusTargetCurrentSession()
                    viewModel.imageCapture?.targetRotation = rotation
                    appliedRotation = rotation
                }

                val relativeRotation = (rotation - (windowView.display?.rotation ?: Surface.ROTATION_0) + 4) % 4
                val rawTarget = when (relativeRotation) {
                    Surface.ROTATION_0 -> 0f
                    Surface.ROTATION_90 -> 90f
                    Surface.ROTATION_180 -> 180f
                    Surface.ROTATION_270 -> 270f
                    else -> 0f
                }
                var diff = (rawTarget - deviceRotationDegrees) % 360f
                if (diff > 180f) diff -= 360f
                if (diff < -180f) diff += 360f
                deviceRotationDegrees += diff
            }
        }
        if (orientationListener.canDetectOrientation()) orientationListener.enable()
        onDispose { orientationActive = false; orientationListener.disable() }
    }

    val uiRotation by animateFloatAsState(
        targetValue = deviceRotationDegrees,
        animationSpec = tween(durationMillis = 300, easing = FastOutSlowInEasing),
        label = "uiRotation"
    )

    val lastPage = capturedPages.lastOrNull()
    val vmThumbnailBitmap by viewModel.lastThumbnailBitmap.collectAsState()
    val diskThumbnailBitmap = remember(lastPage?.id, lastPage?.thumbnailPath, lastPage?.imagePath, lastPage?.status) {
        val path = lastPage?.thumbnailPath ?: lastPage?.imagePath
        if (path != null) {
            val file = File(path)
            if (file.exists()) {
                try {
                    val boundsOpts = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                    BitmapFactory.decodeFile(path, boundsOpts)
                    val sampleSize = max(1, min(boundsOpts.outWidth / 120, boundsOpts.outHeight / 120))
                    val opts = BitmapFactory.Options().apply { inSampleSize = sampleSize }
                    BitmapFactory.decodeFile(path, opts)
                } catch (e: Exception) { null }
            } else null
        } else null
    }
    val effectiveThumbnail = diskThumbnailBitmap ?: vmThumbnailBitmap
    val hasProcessingPages = capturedPages.any { it.status == PageStatus.PROCESSING }

    val galleryLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.PickMultipleVisualMedia(20)
    ) { uris: List<Uri> ->
        if (uris.isNotEmpty()) {
            viewModel.importFromUris(context, uris) { ids ->
                if (ids.size == 1) onNavigateToCrop(ids.first()) else onNavigateToReview()
            }
        }
    }

    // ─── Tap-to-focus state (SPEC_16 §4) ───
    // tapFocusOffset: screen pixel coords of last tap; null = no active focus ring
    var tapFocusOffset by remember { mutableStateOf<Offset?>(null) }
    // We need a reference to the PreviewView to create a MeteringPointFactory
    val previewViewRef = remember { mutableStateOf<PreviewView?>(null) }

    LaunchedEffect(tapFocusOffset) {
        if (tapFocusOffset != null) {
            // Focus ring shows for 1.5s then auto-clears (camera also auto-cancels after 3s)
            delay(1500)
            tapFocusOffset = null
        }
    }

    // ─── Ring & pulse animations ───
    val infiniteTransition = rememberInfiniteTransition(label = "pulseTransition")
    val pulseAlpha by infiniteTransition.animateFloat(
        initialValue = 0.5f,
        targetValue = 1.0f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 750, easing = LinearEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "pulseAlpha"
    )

    val targetRingColor = when {
        fullHdrEnabled -> Color(0xFFFFB74D)
        isStable -> SteadyEmerald
        else -> Color.White.copy(alpha = 0.45f)
    }
    val ringColor by animateColorAsState(
        targetValue = targetRingColor,
        animationSpec = tween(durationMillis = 250),
        label = "stabilityRingColor"
    )
    val ringWidth by animateDpAsState(
        targetValue = if (isStable || fullHdrEnabled) 4.dp else 2.5.dp,
        animationSpec = tween(durationMillis = 250),
        label = "ringWidth"
    )

    val shutterInteractionSource = remember { MutableInteractionSource() }
    val isShutterPressed by shutterInteractionSource.collectIsPressedAsState()
    val shutterScale by animateFloatAsState(
        targetValue = if (isShutterPressed) 0.92f else 1.0f,
        animationSpec = spring(dampingRatio = 0.6f, stiffness = Spring.StiffnessMedium),
        label = "shutterScale"
    )

    var isTorchOn by remember { mutableStateOf(false) }
    var cameraControl by remember { mutableStateOf<androidx.camera.core.CameraControl?>(null) }

    DisposableEffect(Unit) {
        onDispose { cameraControl?.enableTorch(false) }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black)
    ) {
        // ─── 1. Fullscreen Viewfinder ───
        key(selectedBackId, highRequested, highSuppressed, rebindRequest) {
        val generation = remember { viewModel.beginCameraSession() }
        DisposableEffect(generation) {
            onDispose { viewModel.endCameraSession(generation) }
        }
        AndroidView(
            factory = { ctx ->
                val previewView = PreviewView(ctx).apply {
                    layoutParams = ViewGroup.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT
                    )
                    scaleType = PreviewView.ScaleType.FIT_CENTER
                }
                previewViewRef.value = previewView
                previewView.addOnLayoutChangeListener { _, left, top, right, bottom, _, _, _, _ ->
                    viewModel.previewGeometryChanged(generation, right - left, bottom - top)
                }

                val cameraProviderFuture = ProcessCameraProvider.getInstance(ctx)
                val executor = ContextCompat.getMainExecutor(ctx)

                cameraProviderFuture.addListener({
                    if (!viewModel.isCameraSession(generation)) return@addListener
                    try {
                    val cameraProvider = cameraProviderFuture.get()
                    val back = CameraSelector.DEFAULT_BACK_CAMERA.filter(cameraProvider.availableCameraInfos)
                    if (back.isEmpty()) {
                        viewModel.cameraNotice(ctx, R.string.camera_change_failed)
                        return@addListener
                    }
                    val defaultInfo = back.first()
                    val defaultId = Camera2CameraInfo.from(defaultInfo).cameraId
                    val capabilityLines = mutableListOf<String>()
                    val endpoints = back.map { info ->
                        val c2 = Camera2CameraInfo.from(info)
                        val id = c2.cameraId
                        val configuration = c2.getCameraCharacteristic(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
                        val compatible = !configuration?.getOutputSizes(android.graphics.ImageFormat.JPEG).isNullOrEmpty() &&
                            !configuration?.getOutputSizes(android.graphics.ImageFormat.YUV_420_888).isNullOrEmpty()
                        val ratio = runCatching { info.intrinsicZoomRatio }.getOrDefault(1f)
                        val ordinary = configuration?.getOutputSizes(android.graphics.ImageFormat.JPEG)?.toList().orEmpty()
                        val slower = configuration?.getHighResolutionOutputSizes(android.graphics.ImageFormat.JPEG)?.toList().orEmpty()
                        val nativeHigh = CapturePolicy.choose((ordinary + slower).map { CapturePolicy.Size(it.width, it.height) }, true)?.pixels ?: 0L
                        if (BuildConfig.SENSITIVE_DIAGNOSTICS)
                            android.util.Log.d("HachiCam-Camera", "endpoint=$id intrinsicRatio=$ratio compatible=$compatible focal=${c2.getCameraCharacteristic(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS)?.contentToString()} sensor=${c2.getCameraCharacteristic(CameraCharacteristics.SENSOR_INFO_PHYSICAL_SIZE)}")
                        if (BuildConfig.SENSITIVE_DIAGNOSTICS) {
                            val maximum = if (android.os.Build.VERSION.SDK_INT >= 31)
                                c2.getCameraCharacteristic(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP_MAXIMUM_RESOLUTION)
                                    ?.getOutputSizes(android.graphics.ImageFormat.JPEG)?.toList().orEmpty() else emptyList()
                            val summary = "endpoint=$id intrinsicRatio=$ratio compatible=$compatible ordinaryJPEG=$ordinary highJPEG=$slower maximumModeJPEG=$maximum nativeHighPixels=$nativeHigh caps=${c2.getCameraCharacteristic(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES)?.contentToString()} pixelArray=${c2.getCameraCharacteristic(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE)} boostRange=${c2.getCameraCharacteristic(CameraCharacteristics.CONTROL_POST_RAW_SENSITIVITY_BOOST_RANGE)} afModes=${c2.getCameraCharacteristic(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES)?.contentToString()}"
                            android.util.Log.d("HachiCam-Camera", summary); capabilityLines.add(summary)
                        }
                        CameraChoicePolicy.Endpoint(id, ratio, compatible, nativeHigh)
                    }
                    val views = CameraChoicePolicy.views(endpoints, defaultId)
                    backChoices = views.map { endpoint -> endpoint.id to
                        if (endpoint.id == defaultId) "1x"
                        else String.format(java.util.Locale.US, "%.1fx", endpoint.intrinsicRatio)
                    }
                    if (BuildConfig.SENSITIVE_DIAGNOSTICS)
                        android.util.Log.d("HachiCam-Camera", "groupedViews=${views.joinToString { it.id }} default=$defaultId")
                    val selectedView = views.firstOrNull { it.id == selectedBackId } ?: views.first()
                    val variant = CameraChoicePolicy.captureVariant(endpoints, selectedView, highRequested && !highSuppressed)
                    val selectedInfo = back.firstOrNull { Camera2CameraInfo.from(it).cameraId == variant.id } ?: defaultInfo
                    val camera2Info = Camera2CameraInfo.from(selectedInfo)
                    val currentId = camera2Info.cameraId
                    if (BuildConfig.SENSITIVE_DIAGNOSTICS)
                        android.util.Log.d("HachiCam-Camera", "selectedView=${selectedView.id} captureEndpoint=$currentId requestedHigh=$highRequested")
                    capabilityLines.add("groupedViews=${views.joinToString { it.id }} selectedView=${selectedView.id} captureEndpoint=$currentId requestedHigh=$highRequested")
                    CameraDiagnostics.capabilities(capabilityLines)
                    val map = camera2Info.getCameraCharacteristic(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
                    val normalSizes = map?.getOutputSizes(android.graphics.ImageFormat.JPEG)?.toList().orEmpty()
                    val highSizes = if (highRequested && !highSuppressed)
                        map?.getHighResolutionOutputSizes(android.graphics.ImageFormat.JPEG)?.toList().orEmpty() else emptyList()
                    val offered = (normalSizes + highSizes).distinct()
                    val highSize = if (highRequested && !highSuppressed)
                        CapturePolicy.choose(offered.map { CapturePolicy.Size(it.width, it.height) }, true) else null
                    val target = highSize ?: CapturePolicy.choose(normalSizes.map { CapturePolicy.Size(it.width, it.height) }, false)
                        ?: CapturePolicy.Size(4096, 3072)
                    val usingHigh = highSize != null
                    if (highRequested && !highSuppressed && !usingHigh)
                        viewModel.cameraNotice(ctx, R.string.high_pixel_unavailable)
                    val sensorResolutionSelector = ResolutionSelector.Builder()
                        .setAllowedResolutionMode(if (usingHigh) ResolutionSelector.PREFER_HIGHER_RESOLUTION_OVER_CAPTURE_RATE
                            else ResolutionSelector.PREFER_CAPTURE_RATE_OVER_HIGHER_RESOLUTION)
                        .setResolutionStrategy(ResolutionStrategy(android.util.Size(target.width, target.height), ResolutionStrategy.FALLBACK_RULE_CLOSEST_LOWER))
                        .setAspectRatioStrategy(AspectRatioStrategy(AspectRatio.RATIO_4_3, AspectRatioStrategy.FALLBACK_RULE_AUTO))
                        .setResolutionFilter { sizes, _ ->
                            sizes.filter { size ->
                                val pixels = size.width.toLong() * size.height
                                pixels <= (if (usingHigh) CapturePolicy.HIGH_PIXELS else CapturePolicy.NORMAL_PIXELS) &&
                                    (!usingHigh || pixels > CapturePolicy.NORMAL_PIXELS)
                            }
                        }.build()

                    val preview = Preview.Builder()
                        .setTargetAspectRatio(AspectRatio.RATIO_4_3)
                        .build().also {
                            it.setSurfaceProvider(previewView.surfaceProvider)
                        }

                    // Low latency capture mode for rapid multi-frame burst (SPEC_16 §2)
                    val imageCaptureBuilder = ImageCapture.Builder()
                        .setCaptureMode(ImageCapture.CAPTURE_MODE_MINIMIZE_LATENCY)
                        .setJpegQuality(95)
                        .setResolutionSelector(sensorResolutionSelector)

                    val camera2Extender = Camera2Interop.Extender(imageCaptureBuilder)
                    camera2Extender.setSessionCaptureCallback(object : android.hardware.camera2.CameraCaptureSession.CaptureCallback() {
                        override fun onCaptureCompleted(session: android.hardware.camera2.CameraCaptureSession,
                            request: CaptureRequest, result: android.hardware.camera2.TotalCaptureResult) {
                            viewModel.onCameraCaptureResult(result, generation)
                        }
                    })
                    fun supports(key: CameraCharacteristics.Key<IntArray>, mode: Int) =
                        camera2Info.getCameraCharacteristic(key)?.contains(mode) == true
                    // Keep camera-default sharpening/tone. Strong ISP denoising can erase
                    // small strokes before the JPEG burst reaches our reconstruction.
                    if (supports(CameraCharacteristics.NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES, CaptureRequest.NOISE_REDUCTION_MODE_FAST))
                        camera2Extender.setCaptureRequestOption(CaptureRequest.NOISE_REDUCTION_MODE, CaptureRequest.NOISE_REDUCTION_MODE_FAST)
                    if (supports(CameraCharacteristics.LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION, CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE_ON))
                        camera2Extender.setCaptureRequestOption(CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE, CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE_ON)
                    camera2Extender.setCaptureRequestOption(CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE, CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE_OFF)

                    val imageCapture = imageCaptureBuilder.build()
                    viewModel.imageCapture = imageCapture

                    val edgeDetector = NativeEdgeDetector()
                    val frameAnalyzer = com.scanner.app.data.camera.FrameAnalyzer(
                        detector = edgeDetector,
                        curvedMode = curvedModeEnabled
                    ) { result -> viewModel.onFrameAnalyzed(result, generation) }
                    viewModel.frameAnalyzer = frameAnalyzer

                    val imageAnalysis = ImageAnalysis.Builder()
                        .setResolutionSelector(
                            ResolutionSelector.Builder()
                                .setResolutionStrategy(
                                    ResolutionStrategy(
                                        android.util.Size(1280, 960),
                                        ResolutionStrategy.FALLBACK_RULE_CLOSEST_HIGHER
                                    )
                                )
                                .setAspectRatioStrategy(
                                    AspectRatioStrategy(AspectRatio.RATIO_4_3, AspectRatioStrategy.FALLBACK_RULE_AUTO)
                                )
                                .build()
                        )
                        .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                        .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_YUV_420_888)
                        .build()
                        .also {
                            it.setAnalyzer(analyzerExecutor, frameAnalyzer)
                        }
                    viewModel.imageAnalysis = imageAnalysis

                    val cameraSelector = CameraSelector.Builder().requireLensFacing(CameraSelector.LENS_FACING_BACK)
                        .addCameraFilter { infos -> infos.filter { Camera2CameraInfo.from(it).cameraId == currentId } }.build()

                    try {
                        cameraProvider.unbindAll()
                        val camera = cameraProvider.bindToLifecycle(
                            lifecycleOwner, cameraSelector, preview, imageCapture, imageAnalysis
                        )
                        cameraControl = camera.cameraControl
                        viewModel.cameraControl = camera.cameraControl
                        viewModel.cameraInfo = camera.cameraInfo
                        viewModel.controlsBound(generation)
                        boundBackId = selectedView.id
                        val exposure = camera.cameraInfo.exposureState
                        if (exposure.isExposureCompensationSupported && exposure.exposureCompensationIndex != 0)
                            viewModel.resetExposure(ctx)
                        imageCapture.resolutionInfo?.resolution?.let { viewModel.negotiatedCapture(it.width, it.height) }
                        camera.cameraControl.enableTorch(isTorchOn)
                    } catch (e: Exception) {
                        viewModel.imageCapture = null
                        viewModel.imageAnalysis?.clearAnalyzer()
                        viewModel.frameAnalyzer?.close()
                        android.util.Log.e("HachiCam-Camera", "Camera binding failed", e)
                        if (usingHigh) {
                            highSuppressed = true
                            viewModel.cameraNotice(ctx, R.string.high_pixel_unavailable)
                        } else if (selectedBackId != null) {
                            selectedBackId = null
                            viewModel.cameraNotice(ctx, R.string.camera_change_failed)
                        } else viewModel.cameraNotice(ctx, R.string.camera_change_failed)
                    }
                    } catch (error: Exception) {
                        viewModel.imageCapture = null
                        android.util.Log.e("HachiCam-Camera", "Camera setup failed", error)
                        viewModel.cameraNotice(ctx, R.string.camera_change_failed)
                    }
                }, executor)

                previewView
            },
            modifier = Modifier
                .fillMaxSize()
                .pointerInput(Unit) {
                    // Tap-to-focus: AF + AE at tap location (SPEC_16 §4)
                    detectTapGestures { offset ->
                        if (viewModel.isCapturing.value) return@detectTapGestures
                        val detection = viewModel.detectedQuad.value
                        val normalized = ImageCoordinates.fitPoint(offset.x, offset.y,
                            size.width.toFloat(), size.height.toFloat(),
                            detection?.frameWidth?.toFloat() ?: 0f, detection?.frameHeight?.toFloat() ?: 0f)
                        if ((detection?.frameWidth ?: 0) > 0 && normalized == null) return@detectTapGestures
                        normalized?.let { viewModel.setTouchPoint(it.x, it.y) }
                        tapFocusOffset = offset
                        val pv = previewViewRef.value ?: return@detectTapGestures
                        val ctrl = cameraControl ?: return@detectTapGestures
                        val factory = pv.meteringPointFactory
                        val point = factory.createPoint(offset.x, offset.y)
                        if (!viewModel.rememberFocusPoint(point, generation)) return@detectTapGestures
                        val action = FocusMeteringAction.Builder(point, FocusMeteringAction.FLAG_AF or FocusMeteringAction.FLAG_AE)
                            .setAutoCancelDuration(3, TimeUnit.SECONDS)
                            .build()
                        ctrl.startFocusAndMetering(action)
                    }
                }
        )

        } // camera session key

        // ─── 2. Real-time Document Edge Overlay ───
        detectedQuad?.let { quad ->
            EdgeOverlay(
                detectionResult = quad,
                isCurvedMode = curvedModeEnabled,
                modifier = Modifier.fillMaxSize()
            )
        }

        // ─── 3. Tap-to-Focus Ring (SPEC_16 §4.3) ───
        tapFocusOffset?.let { offset ->
            val density = androidx.compose.ui.platform.LocalDensity.current
            val focusRingSize = 72.dp
            val offsetXDp = with(density) { offset.x.toDp() }
            val offsetYDp = with(density) { offset.y.toDp() }
            Box(
                modifier = Modifier
                    .offset(
                        x = offsetXDp - focusRingSize / 2,
                        y = offsetYDp - focusRingSize / 2
                    )
                    .size(focusRingSize)
                    .border(1.5.dp, Color.White.copy(alpha = 0.9f), RoundedCornerShape(4.dp))
            )
        }

        // ─── 4. Top Status Bar ───
        Row(
            modifier = Modifier
                .align(Alignment.TopCenter)
                .fillMaxWidth()
                .statusBarsPadding()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Torch toggle
            val torchBg = if (isTorchOn) Color(0x66FBBF24) else Color(0x990A0F1D)
            val torchBorder = if (isTorchOn) Color(0xFFFBBF24) else Color(0x2AFFFFFF)
            val torchTint = if (isTorchOn) Color(0xFFFBBF24) else Color.White.copy(alpha = 0.85f)

            IconButton(
                onClick = {
                    val nextState = !isTorchOn
                    isTorchOn = nextState
                    cameraControl?.enableTorch(nextState)
                },
                modifier = Modifier
                    .size(42.dp)
                    .graphicsLayer { rotationZ = uiRotation }
                    .clip(CircleShape)
                    .background(torchBg)
                    .border(1.2.dp, torchBorder, CircleShape)
            ) {
                Icon(
                    imageVector = if (isTorchOn) Icons.Default.FlashOn else Icons.Default.FlashOff,
                    contentDescription = "Torch",
                    tint = torchTint,
                    modifier = Modifier.size(20.dp)
                )
            }

            // Stability pill
            Box(
                modifier = Modifier
                    .clip(RoundedCornerShape(24.dp))
                    .background(Color(0x990A0F1D))
                    .border(1.dp, Color(0x2AFFFFFF), RoundedCornerShape(24.dp))
                    .padding(horizontal = 14.dp, vertical = 7.dp)
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.Center
                ) {
                    val dotColor = if (isStable) SteadyEmerald else SteadyAmber
                    val effectiveAlpha = if (isStable) pulseAlpha else 1.0f
                    Box(
                        modifier = Modifier
                            .size(9.dp)
                            .background(dotColor.copy(alpha = effectiveAlpha), CircleShape)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = if (isStable) stringResource(R.string.steady) else stringResource(R.string.hold_steady),
                        color = if (isStable) SteadyEmerald else Color.White,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.SemiBold,
                        letterSpacing = 0.3.sp
                    )
                }
            }

            // Settings button
            IconButton(
                onClick = onNavigateToSettings,
                modifier = Modifier
                    .size(42.dp)
                    .graphicsLayer { rotationZ = uiRotation }
                    .clip(CircleShape)
                    .background(Color(0x990A0F1D))
                    .border(1.dp, Color(0x2AFFFFFF), CircleShape)
            ) {
                Icon(
                    imageVector = Icons.Default.Settings,
                    contentDescription = stringResource(R.string.settings),
                    tint = Color.White,
                    modifier = Modifier.size(20.dp)
                )
            }
        }

        // ─── 5. Center Capture Processing Modal ───
        if (isCapturing) {
            Box(
                modifier = Modifier
                    .align(Alignment.Center)
                    .clip(RoundedCornerShape(20.dp))
                    .background(Color(0xE60F172A))
                    .border(
                        1.dp,
                        if (fullHdrEnabled) Color(0xFFFFB74D).copy(alpha = 0.5f) else Color(0x33FFFFFF),
                        RoundedCornerShape(20.dp)
                    )
                    .padding(horizontal = 32.dp, vertical = 24.dp)
            ) {
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center
                ) {
                    CircularProgressIndicator(
                        color = if (fullHdrEnabled) Color(0xFFFFB74D) else SteadyEmerald,
                        strokeWidth = 3.5.dp,
                        modifier = Modifier.size(44.dp)
                    )
                    Spacer(modifier = Modifier.height(16.dp))
                    Text(
                        text = if (fullHdrEnabled)
                            stringResource(R.string.fusing_full_hdr)
                        else
                            stringResource(R.string.processing),
                        color = Color.White,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Medium,
                        letterSpacing = 0.2.sp
                    )
                }
            }
        }

        // ─── 6. Bottom Controls ───
        Column(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .fillMaxWidth()
                .navigationBarsPadding()
                .padding(bottom = 20.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            if (backChoices.isNotEmpty()) {
                Row(Modifier.horizontalScroll(rememberScrollState()).padding(horizontal = 16.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                    backChoices.forEach { (id, label) ->
                        val chosen = boundBackId == id
                        Surface(onClick = { if (!viewModel.isCapturing.value) selectedBackId = id },
                            enabled = !isCapturing, shape = CircleShape,
                            color = if (chosen) SteadyAmber else Color(0xCC11151D),
                            modifier = Modifier.size(48.dp).semantics { selected = chosen }) {
                            Box(contentAlignment = Alignment.Center) {
                                Text(label, color = if (chosen) Color.Black else Color.White,
                                    fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
                            }
                        }
                    }
                }
            }

            // Boundary status is independent of scene steadiness.
            Row(
                modifier = Modifier.padding(horizontal = 24.dp, vertical = 10.dp),
                horizontalArrangement = Arrangement.Center,
                verticalAlignment = Alignment.CenterVertically
            ) {
                val boundaryFound = detectedQuad?.found == true
                val boundaryPartial = boundaryFound && detectedQuad?.partial == true
                val boundaryColor = if (boundaryPartial) SteadyAmber else if (boundaryFound) SteadyEmerald else Color(0xFFE2E8F0)

                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(24.dp))
                        .background(Color(0xAA0A0F1D))
                        .border(1.2.dp, boundaryColor.copy(alpha = .4f), RoundedCornerShape(24.dp))
                        .padding(horizontal = 14.dp, vertical = 8.dp)
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Default.CropFree,
                            contentDescription = null,
                            modifier = Modifier.size(15.dp),
                            tint = boundaryColor
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = stringResource(if (boundaryPartial) R.string.boundary_partial
                                else if (boundaryFound) R.string.boundary_found else R.string.boundary_missing),
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Medium,
                            color = boundaryColor
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Shutter row
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 32.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Left: Gallery import
                Box(
                    modifier = Modifier.size(56.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Box(
                        modifier = Modifier
                            .size(52.dp)
                            .graphicsLayer { rotationZ = uiRotation }
                            .clip(RoundedCornerShape(16.dp))
                            .background(Color(0x990A0F1D))
                            .border(1.5.dp, Color(0x44FFFFFF), RoundedCornerShape(16.dp))
                            .clickable(enabled = !isImporting && !isCapturing) {
                                galleryLauncher.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly))
                            },
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = Icons.Default.Collections,
                            contentDescription = stringResource(R.string.filter_original),
                            tint = Color.White,
                            modifier = Modifier.size(24.dp)
                        )
                    }
                }

                // Center: Shutter
                Box(
                    modifier = Modifier
                        .size(86.dp)
                        .border(width = ringWidth, color = ringColor, shape = CircleShape)
                        .padding(6.dp)
                        .scale(shutterScale),
                    contentAlignment = Alignment.Center
                ) {
                    if (isCapturing) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(54.dp),
                            color = if (fullHdrEnabled) Color(0xFFFFB74D) else SteadyEmerald,
                            strokeWidth = 3.5.dp
                        )
                    } else {
                        val innerColor = if (fullHdrEnabled) Color(0xFFFFB74D) else Color.White
                        Box(
                            modifier = Modifier
                                .fillMaxSize()
                                .clip(CircleShape)
                                .background(innerColor)
                                .clickable(
                                    interactionSource = shutterInteractionSource,
                                    indication = null
                                ) {
                                    viewModel.capturePhoto(context)
                                },
                            contentAlignment = Alignment.Center
                        ) {
                            if (fullHdrEnabled) {
                                Icon(
                                    imageVector = Icons.Default.HdrOn,
                                    contentDescription = stringResource(R.string.capture),
                                    tint = Color(0xFF0F172A),
                                    modifier = Modifier
                                        .size(24.dp)
                                        .graphicsLayer { rotationZ = uiRotation }
                                )
                            }
                        }
                    }
                }

                // Right: Document gallery with count badge
                Box(
                    modifier = Modifier.size(56.dp),
                    contentAlignment = Alignment.Center
                ) {
                    if (pageCount > 0) {
                        Box(
                            modifier = Modifier
                                .size(52.dp)
                                .graphicsLayer { rotationZ = uiRotation }
                                .clip(RoundedCornerShape(16.dp))
                                .background(Color(0x990A0F1D))
                                .border(1.5.dp, Color(0x44FFFFFF), RoundedCornerShape(16.dp))
                                .clickable { onNavigateToReview() },
                            contentAlignment = Alignment.Center
                        ) {
                            if (effectiveThumbnail != null) {
                                Image(
                                    bitmap = effectiveThumbnail.asImageBitmap(),
                                    contentDescription = stringResource(R.string.review),
                                    contentScale = ContentScale.Crop,
                                    modifier = Modifier
                                        .fillMaxSize()
                                        .clip(RoundedCornerShape(16.dp))
                                )
                            } else {
                                Icon(
                                    imageVector = Icons.Default.Collections,
                                    contentDescription = stringResource(R.string.review),
                                    tint = Color.White,
                                    modifier = Modifier.size(24.dp)
                                )
                            }

                            if (hasProcessingPages) {
                                CircularProgressIndicator(
                                    modifier = Modifier.size(24.dp),
                                    color = Color(0xFFFFB74D),
                                    strokeWidth = 2.dp
                                )
                            }

                            Box(
                                modifier = Modifier
                                    .align(Alignment.TopEnd)
                                    .offset(x = 4.dp, y = (-4).dp)
                                    .clip(CircleShape)
                                    .background(SteadyEmerald)
                                    .padding(horizontal = 5.dp, vertical = 2.dp)
                            ) {
                                Text(
                                    text = "$pageCount",
                                    color = Color(0xFF0F172A),
                                    fontSize = 10.sp,
                                    fontWeight = FontWeight.ExtraBold
                                )
                            }
                        }
                    }
                }
            }
        }
        SnackbarHost(hostState = snackbar, modifier = Modifier.align(Alignment.BottomCenter).navigationBarsPadding())
    }
}
