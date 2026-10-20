package com.scanner.app.data.image

import com.scanner.app.data.camera.UnitPoint
import kotlin.math.*

/** Predicts the native rectifier dimensions before decoding/allocation. */
object PerspectiveSize {
    data class Size(val width: Int, val height: Int) { val pixels get() = width.toLong() * height }
    fun plan(width: Int, height: Int, corners: List<UnitPoint>, aspect: Float?, rotation: Int): Size {
        QuarterTurn.normalized(rotation)
        require(width > 1 && height > 1 && width.toLong() * height <= 56_000_000)
        require(corners.size == 4 && corners.all { it.x.isFinite() && it.y.isFinite() && it.x in 0f..1f && it.y in 0f..1f })
        val p = corners.map { it.x.toDouble() * (width - 1) to it.y.toDouble() * (height - 1) }
        var area = 0.0
        for (i in 0..3) {
            val (ax,ay)=p[i]; val (bx,by)=p[(i+1)%4]; val (cx,cy)=p[(i+2)%4]
            require((bx-ax)*(cy-by)-(by-ay)*(cx-bx) > .001) { "Crossed or degenerate crop" }
            area += ax*by-ay*bx
        }
        require(area >= 8) { "Crop is too small" }
        fun edge(a: Int,b: Int) = hypot(p[a].first-p[b].first,p[a].second-p[b].second)
        val wa=edge(2,3); val wb=edge(1,0); val ha=edge(1,2); val hb=edge(0,3)
        val maxW=max(wa,wb); val maxH=max(ha,hb)
        var w=max(10,maxW.roundToInt()); var h=max(10,maxH.roundToInt())
        val ratio=QuarterTurn.preRotationAspect(aspect,rotation).toDouble()
        if (ratio>0) {
            if(maxW/ratio>=maxH) h=max(10,(w/ratio).roundToInt()) else w=max(10,(h*ratio).roundToInt())
        } else {
            val rw=if(maxW>1) min(wa,wb)/maxW else 1.0
            val rh=if(maxH>1) min(ha,hb)/maxH else 1.0
            if(rw<rh && rw>.1) h=max(10,(maxH*sqrt(1/rw).coerceIn(1.0,1.6)).roundToInt())
            else if(rh<rw && rh>.1) w=max(10,(maxW*sqrt(1/rh).coerceIn(1.0,1.6)).roundToInt())
        }
        return Size(w,h).also { require(it.pixels<=56_000_000) { "Crop output exceeds pixel budget" } }
    }
}
