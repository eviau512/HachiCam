package com.scanner.app.data.camera

import kotlin.math.pow
import kotlin.math.roundToLong

data class ManualExposure(val timeNs: Long, val iso: Int) {
    val product: Double get() = timeNs.toDouble() * iso
}

object ExposurePlan {
    fun bracket(base: ManualExposure, stops: Double, minTime: Long, maxTime: Long,
                minIso: Int, maxIso: Int): ManualExposure? {
        require(minTime > 0 && maxTime >= minTime && minIso > 0 && maxIso >= minIso)
        if (base.timeNs <= 0 || base.iso <= 0 || stops <= 0) return null
        val target = base.product / 2.0.pow(stops)
        val iso = base.iso.coerceIn(minIso, maxIso)
        val time = (target / iso).roundToLong().coerceIn(minTime, maxTime)
        val adjustedIso = (target / time).roundToLong().coerceIn(minIso.toLong(), maxIso.toLong()).toInt()
        return ManualExposure(time, adjustedIso).takeIf { it.product < base.product * 0.75 }
    }
}
