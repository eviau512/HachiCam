package com.scanner.app.data.camera

object BurstFocusPolicy {
    fun heldDistance(success: Boolean, sample: CaptureEvidence?, ageMs: Long,
                     minimumFocusDistance: Float?, supportsOff: Boolean): Float? {
        if (!success || !supportsOff || ageMs !in 0..500 || sample?.lensState != 0 ||
            sample.afState !in listOf(2, 4)) return null
        val maximum = minimumFocusDistance?.takeIf { it.isFinite() && it > 0 } ?: return null
        return sample.focusDistance?.takeIf { it.isFinite() && it in 0f..maximum }
    }
}
