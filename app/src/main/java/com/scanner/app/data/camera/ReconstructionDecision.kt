package com.scanner.app.data.camera

data class ReconstructionDecision(
    val requested: Boolean = false, val denied: Int = 32,
    val tiles: Int = 0, val qualifiedTiles: Int = 0,
    val meanSupport: Double = 0.0, val centrePhases: Boolean = false,
    val regionFraction: Double = 0.0, val reconstructedFraction: Double = 0.0,
    val tileSize: Int = 64, val minimumArea: Double = .02, val minimumTiles: Int = 4,
    val minimumSupport: Double = .70, val phaseDeterminant: Double = .04,
    val fittedTiles: Int = 0, val initialFitError: Double = 0.0, val finalFitError: Double = 0.0
) {
    fun denialReasons(): List<String> = listOf(1 to "not_requested_by_engine", 2 to "insufficient_anchors",
        4 to "insufficient_qualified_tiles", 8 to "insufficient_regional_area",
        16 to "source_above_16mp", 32 to "native_streaming_profile",
        64 to "no_verified_reconstruction_pixels", 128 to "output_above_56mp").filter { denied and it.first != 0 }.map { it.second }

    companion object {
        fun decode(packet: LongArray): ReconstructionDecision {
            require(packet.size in listOf(42,44,50) && packet[25] == 1L) { "Unsupported fusion packet schema" }
            fun fraction(index: Int): Double = Double.fromBits(packet[index]).also { require(it.isFinite() && it in 0.0..1.0) }
            fun count(index: Int): Int = packet[index].also { require(it in 0..Int.MAX_VALUE.toLong()) }.toInt()
            fun error(index: Int): Double = Double.fromBits(packet[index]).also { require(it.isFinite() && it >= 0) }
            if(packet.size>=44) { require(packet[42]==1L);fraction(43) }
            FusionPerformance.decode(packet)
            require(packet[26] in 0..1 && packet[31] in 0..1 && packet[27] in 0..255)
            val result = ReconstructionDecision(packet[26] == 1L, count(27), count(28), count(29),
                fraction(30), packet[31] == 1L, fraction(32), fraction(33), count(34), fraction(35), count(36), fraction(37), fraction(38), count(39), error(40), error(41))
            require(result.qualifiedTiles <= result.tiles && result.tileSize > 0 && result.minimumTiles > 0)
            require(result.fittedTiles <= result.qualifiedTiles && result.finalFitError <= result.initialFitError)
            require(result.reconstructedFraction <= result.regionFraction + 1e-12)
            require(result.fittedTiles != 0 || result.initialFitError == 0.0 && result.finalFitError == 0.0)
            require(packet[3] in 1..2 && (packet[3] != 2L || result.requested && result.denied == 0))
            require(packet[3] != 2L || result.fittedTiles > 0 && result.reconstructedFraction > 0)
            require(packet[3] != 2L || result.finalFitError <= result.initialFitError * .95 + 1e-12)
            require(packet[3] != 1L || result.reconstructedFraction == 0.0)
            return result
        }
    }
}
