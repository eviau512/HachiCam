package com.scanner.app.engine

import org.opencv.core.Mat

class NativeBurstFusion {
    data class Report(val anchors: Int = 1, val highlights: Int = 0, val scale: Int = 1,
        val reference: Int = 0, val left: Int = 0, val top: Int = 0, val cropW: Int = 0,
        val cropH: Int = 0, val sourceW: Int = 0, val sourceH: Int = 0,
        val firstToReference: DoubleArray = doubleArrayOf(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0),
        val geometryKnown: Boolean = true, val multiFrameFraction: Double = 0.0, val meanContributors: Double = 1.0,
        val recoveredFraction: Double = 0.0, val clippedFraction: Double = 0.0)
    var lastReport = Report()
        private set

    init {
        System.loadLibrary("doc_scanner_engine")
    }

    /**
     * CPU JPEG fusion with explicit exposure tiers and conditional 2x phase reconstruction.
     * Tiers: 0 = reference exposure, 1/2 = shorter exposures; products are ISO * ns.
     * [lastReport] describes accepted plates and actual output scale, including degradation.
     */
    fun fuseBurstFrames(
        burstFrames: List<Mat>,
        removeGlare: Boolean = true,
        isScreenMode: Boolean = true,
        superResolution: Boolean = false,
        tiers: IntArray = IntArray(burstFrames.size),
        exposureProducts: DoubleArray = DoubleArray(burstFrames.size)
    ): Mat {
        if (burstFrames.isEmpty()) return Mat()
        if (burstFrames.size == 1 && !superResolution) return burstFrames[0].clone()

        val addrs = burstFrames.map { it.nativeObjAddr }.toLongArray()
        require(tiers.size == burstFrames.size && exposureProducts.size == burstFrames.size)
        val result = nativeFuseBurstFrames(addrs, removeGlare, isScreenMode, superResolution, tiers, exposureProducts)
        return decodeResult(result)
    }

    fun fuseBurstFiles(paths: List<String>, tiers: IntArray, exposureProducts: DoubleArray): Mat {
        require(paths.size in 1..7 && tiers.size == paths.size && exposureProducts.size == paths.size)
        return decodeResult(nativeFuseBurstFiles(paths.toTypedArray(), tiers, exposureProducts))
    }

    private fun decodeResult(result: LongArray): Mat {
        if (result.size < 25 || result[0] == 0L) return Mat()
        lastReport = Report(result[1].toInt(), result[2].toInt(), result[3].toInt(), result[4].toInt(),
            result[5].toInt(), result[6].toInt(), result[7].toInt(), result[8].toInt(), result[9].toInt(), result[10].toInt(),
            DoubleArray(9) { Double.fromBits(result[11 + it]) }, result[20] == 1L,
            Double.fromBits(result[21]), Double.fromBits(result[22]), Double.fromBits(result[23]), Double.fromBits(result[24]))
        return Mat(result[0])
    }

    private external fun nativeFuseBurstFiles(paths: Array<String>, tiers: IntArray, exposureProducts: DoubleArray): LongArray

    private external fun nativeFuseBurstFrames(
        matAddrs: LongArray,
        removeGlare: Boolean,
        isScreenMode: Boolean,
        superResolution: Boolean,
        tiers: IntArray,
        exposureProducts: DoubleArray
    ): LongArray
}
