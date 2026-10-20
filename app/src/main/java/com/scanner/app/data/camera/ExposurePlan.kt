package com.scanner.app.data.camera

import kotlin.math.pow
import kotlin.math.roundToLong

data class ManualExposure(val timeNs: Long, val iso: Int, val postRawBoost: Int? = null) {
    val product: Double get() = timeNs.toDouble() * iso * (postRawBoost ?: 100) / 100.0
}

object ExposurePlan {
    fun cleanBracket(base: ManualExposure, stops: Double, minTime: Long, maxTime: Long,
                     minIso: Int, maxIso: Int, unityBoostSupported: Boolean): ManualExposure? {
        require(minTime > 0 && maxTime >= minTime && minIso > 0 && maxIso >= minIso)
        if (base.timeNs <= 0 || base.iso <= 0 || !stops.isFinite() || stops <= 0 ||
            (base.postRawBoost != null && base.postRawBoost <= 0)) return null
        val boost = if (base.postRawBoost != null && unityBoostSupported) 100 else base.postRawBoost
        val factor = (boost ?: 100) / 100.0
        val target = base.product / 2.0.pow(stops)
        val cap = minOf(maxTime, 16_666_667L.coerceAtLeast(minTime))
        val shutter = base.timeNs.coerceIn(minTime, cap)
        val iso = (target / (shutter * factor)).roundToLong().coerceIn(minIso.toLong(), maxIso.toLong()).toInt()
        val time = (target / (iso * factor)).roundToLong().coerceIn(minTime, cap)
        return ManualExposure(time, iso, boost).takeIf { it.product < base.product * .75 }
    }
    data class EvidenceSeries(val products: DoubleArray, val description: String)
    fun evidence(sensor: List<ManualExposure?>, exif: List<ManualExposure?>): EvidenceSeries {
        require(sensor.size == exif.size)
        val sensorComplete = sensor.isNotEmpty() && sensor.all { it != null } &&
            sensor.map { it?.postRawBoost != null }.distinct().size == 1
        val chosen = if (sensorComplete) sensor else exif
        val description = if (!sensorComplete) "EXIF" else if (sensor.first()?.postRawBoost != null)
            "sensor+postRawBoost" else "sensor (boost unknown)"
        return EvidenceSeries(chosen.map { it?.product ?: 0.0 }.toDoubleArray(), description)
    }
    fun bracket(base: ManualExposure, stops: Double, minTime: Long, maxTime: Long,
                minIso: Int, maxIso: Int): ManualExposure? {
        require(minTime > 0 && maxTime >= minTime && minIso > 0 && maxIso >= minIso)
        if (base.timeNs <= 0 || base.iso <= 0 || stops <= 0 || (base.postRawBoost != null && base.postRawBoost <= 0)) return null
        val target = base.timeNs.toDouble() * base.iso / 2.0.pow(stops)
        val iso = base.iso.coerceIn(minIso, maxIso)
        val time = (target / iso).roundToLong().coerceIn(minTime, maxTime)
        val adjustedIso = (target / time).roundToLong().coerceIn(minIso.toLong(), maxIso.toLong()).toInt()
        return ManualExposure(time, adjustedIso, base.postRawBoost).takeIf { it.product < base.product * 0.75 }
    }
}
