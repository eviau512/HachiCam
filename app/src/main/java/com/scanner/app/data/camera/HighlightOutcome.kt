package com.scanner.app.data.camera

/** Diagnostic labels; never a reason to remove accepted image evidence. */
object HighlightOutcome {
    const val MINIMUM_CHANGED_PIXELS=128
    const val MINIMUM_CLIPPED_SHARE=.01
    fun classify(nativePixels: Long, clipped: Double, recovered: Double): String {
        if(nativePixels<=0 || !clipped.isFinite() || !recovered.isFinite() || clipped !in 0.0..1.0 || recovered !in 0.0..1.0)
            return "unknown"
        if(clipped==0.0)return if(recovered>0) "near_highlight_adjustment" else "no_clipped_baseline"
        if(recovered==0.0)return "no_reliable_recovery"
        return if(recovered*nativePixels>=MINIMUM_CHANGED_PIXELS && recovered>=clipped*MINIMUM_CLIPPED_SHARE)
            "partial_highlight_recovery" else "limited_pixel_changes"
    }
}
