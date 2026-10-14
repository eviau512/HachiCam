package com.scanner.app.data.camera

import kotlin.math.abs
import kotlin.math.ln
import kotlin.math.roundToInt

/** Metadata-derived views, never an inventory of physical lenses. */
object CameraChoicePolicy {
    data class Endpoint(val id: String, val intrinsicRatio: Float, val compatible: Boolean = true,
        val nativeHighPixels: Long = 0)
    fun captureVariant(endpoints: List<Endpoint>, view: Endpoint, highRequested: Boolean): Endpoint {
        if (!highRequested) return view
        return endpoints.filter { it.compatible && it.intrinsicRatio.isFinite() && it.intrinsicRatio > 0 &&
            abs(ln(it.intrinsicRatio / view.intrinsicRatio)) <= ln(1.12f) &&
            it.nativeHighPixels > CapturePolicy.NORMAL_PIXELS && it.nativeHighPixels <= CapturePolicy.HIGH_PIXELS }
            .sortedWith(compareByDescending<Endpoint> { it.nativeHighPixels }.thenBy { if (it.id == view.id) 0 else 1 }.thenBy { it.id })
            .firstOrNull() ?: view
    }
    fun views(endpoints: List<Endpoint>, defaultId: String): List<Endpoint> {
        val primary = endpoints.firstOrNull { it.id == defaultId } ?: return emptyList()
        val selected = mutableListOf(primary.copy(intrinsicRatio = 1f))
        endpoints.filter { it.id != defaultId && it.compatible && it.intrinsicRatio.isFinite() && it.intrinsicRatio > 0 }
            .sortedWith(compareBy<Endpoint> { it.intrinsicRatio }.thenBy { it.id })
            .forEach { candidate ->
                if (selected.none { abs(ln(candidate.intrinsicRatio / it.intrinsicRatio)) <= ln(1.12f) })
                    selected.add(candidate)
            }
        return selected
    }
    fun exposureIndices(step: Float, minimum: Int, maximum: Int): List<Int> {
        if (!step.isFinite() || step <= 0 || minimum > maximum) return emptyList()
        return listOf(-2f, -1f, 0f, 1f).map { (it / step).roundToInt().coerceIn(minimum, maximum) }.distinct()
    }
}
