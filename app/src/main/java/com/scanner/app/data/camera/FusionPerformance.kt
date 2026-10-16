package com.scanner.app.data.camera

/** Non-overlapping native wall intervals; includes streaming decode in remaining time. */
data class FusionPerformance(val totalMs:Double,val globalRegistrationMs:Double,
    val localRegistrationMs:Double,val gpuSupportMaps:Int,val gpuFitProposals:Int,
    val gpuNoiseGrids:Int=0,val gpuMergeTiles:Int=0,val cpuMergePixels:Long=0,val gpuMergeMs:Double=0.0,
    val gpuSelectorPatches:Int=0,val cpuSelectorPatches:Int=0,val selectorMs:Double=0.0) {
    val remainingNativeMs:Double get()=(totalMs-globalRegistrationMs-localRegistrationMs).coerceAtLeast(0.0)
    companion object {
        fun decode(packet:LongArray):FusionPerformance? {
            if(packet.size in listOf(42,44))return null
            require((packet.size==50 && packet[44]==1L)||(packet.size==54 && packet[44]==2L)||(packet.size==57 && packet[44]==3L))
            fun time(index:Int)=Double.fromBits(packet[index]).also { require(it.isFinite() && it in 0.0..86_400_000.0) }
            require(packet[48] in 0..6 && packet[49] in 0..24_576)
            if(packet.size>=54) {
                require(packet[50] in 0..7 && packet[51] in 0..4096 && packet[52] in 0..16_000_000)
                require((packet[51]>0)||(packet[52]==0L && time(53)==0.0))
            }
            if(packet.size==57)require(packet[54] in 0..24_576 && packet[55] in 0..packet[54])
            val result=FusionPerformance(time(45),time(46),time(47),packet[48].toInt(),packet[49].toInt(),
                if(packet.size>=54)packet[50].toInt() else 0,if(packet.size>=54)packet[51].toInt() else 0,
                if(packet.size>=54)packet[52] else 0,if(packet.size>=54)time(53) else 0.0,
                if(packet.size==57)packet[54].toInt() else 0,if(packet.size==57)packet[55].toInt() else 0,
                if(packet.size==57)time(56) else 0.0)
            require(result.globalRegistrationMs+result.localRegistrationMs<=result.totalMs+.001)
            require(result.gpuMergeMs<=result.remainingNativeMs+.001)
            require(result.selectorMs<=result.localRegistrationMs+.001)
            return result
        }
    }
}
