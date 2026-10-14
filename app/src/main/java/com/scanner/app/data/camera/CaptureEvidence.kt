package com.scanner.app.data.camera

data class CaptureEvidence(val timestamp: Long, val exposure: ManualExposure?,
    val afState: Int?, val lensState: Int?, val focusDistance: Float?)

/** Exact timestamp matching; no nearest-frame substitution or unbounded history. */
class CaptureEvidenceStore(private val capacity: Int = 96) {
    init { require(capacity > 0) }
    private val frames = LinkedHashMap<Long, CaptureEvidence>()
    @Synchronized fun record(frame: CaptureEvidence) {
        if (frame.timestamp <= 0) return
        frames[frame.timestamp] = frame
        while (frames.size > capacity) frames.remove(frames.keys.first())
    }
    @Synchronized fun take(timestamp: Long): CaptureEvidence? = frames.remove(timestamp)
    @Synchronized fun clear() = frames.clear()
}
