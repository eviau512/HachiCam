package com.scanner.app.data.image

object ImageMemoryPolicy {
    private const val MIB = 1024L * 1024
    const val OVERHEAD = 128 * MIB
    data class Plan(val streaming: Boolean, val superResolution: Boolean, val bytes: Long, val reason: String)
    fun budget(available: Long, is64Bit: Boolean): Long = minOf(available.coerceAtLeast(0) * 2 / 3, (if (is64Bit) 2048 else 768) * MIB)
    fun fusionBytes(pixels: Long, frames: Int, streaming: Boolean, superResolution: Boolean): Long {
        require(pixels in 1..56_000_000 && frames in 1..7)
        return if (streaming) pixels * 20 + OVERHEAD
        else pixels * (frames * 3L + 20L + if (superResolution) 24L else 6L) + OVERHEAD
    }
    fun fusionPlan(pixels: Long, frames: Int, nativeHigh: Boolean, budget: Long, reserved: Long): Plan? {
        val variants = if (nativeHigh) listOf(true to false) else listOf(false to true, false to false, true to false)
        for ((streaming, sr) in variants) {
            if (sr && pixels > 16_000_000) continue
            val bytes = fusionBytes(pixels, frames, streaming, sr)
            if (bytes <= (budget - reserved).coerceAtLeast(0)) return Plan(streaming, sr, bytes,
                if (nativeHigh) "native-high streaming" else if (sr) "normal reconstruction budget" else if (streaming) "memory: streamed native-size fusion" else "memory: native-size fusion")
        }
        return null
    }
    fun editBytes(pixels: Long, outputPixels: Long = pixels): Long {
        require(pixels in 1..56_000_000 && outputPixels in 1..56_000_000)
        return pixels * 8 + outputPixels * 12 + OVERHEAD
    }
    fun analysisBytes(pixels: Long): Long { require(pixels in 1..56_000_000); return pixels * 12 + OVERHEAD }
    fun exportBytes(pixels: List<Long>, pdf: Boolean): Long {
        require(pixels.isNotEmpty() && pixels.all { it in 1..56_000_000 })
        return (if (pdf) pixels.sum() * 8 + pixels.max() * 4 else 4 * MIB) + OVERHEAD
    }
}
