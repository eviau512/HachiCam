package com.scanner.app.data.camera

/** Non-overlapping native wall intervals; includes streaming decode in remaining time. */
data class FusionPerformance(val totalMs:Double,val globalRegistrationMs:Double,
    val localRegistrationMs:Double,val gpuSupportMaps:Int,val gpuFitProposals:Int) {
    val remainingNativeMs:Double get()=(totalMs-globalRegistrationMs-localRegistrationMs).coerceAtLeast(0.0)
    companion object {
        fun decode(packet:LongArray):FusionPerformance? {
            if(packet.size in listOf(42,44))return null
            require(packet.size==50 && packet[44]==1L)
            fun time(index:Int)=Double.fromBits(packet[index]).also { require(it.isFinite() && it in 0.0..86_400_000.0) }
            require(packet[48] in 0..6 && packet[49] in 0..24_576)
            val result=FusionPerformance(time(45),time(46),time(47),packet[48].toInt(),packet[49].toInt())
            require(result.globalRegistrationMs+result.localRegistrationMs<=result.totalMs+.001)
            return result
        }
    }
}
