package com.scanner.app.data.camera

/** Capability/resource decisions kept independent of Android for regression. */
object CapturePolicy {
    const val NORMAL_PIXELS = 16_000_000L
    const val HIGH_PIXELS = 56_000_000L
    const val ENCODED_LIMIT = 96L * 1024 * 1024
    const val HDR_KEY = "multi_frame_enabled"
    const val HIGH_KEY = "native_high_resolution"
    data class Size(val width: Int, val height: Int) {
        val pixels get() = width.toLong() * height
        val valid get() = width > 0 && height > 0
    }
    fun choose(sizes: List<Size>, high: Boolean): Size? {
        val eligible = sizes.filter { it.valid && if (high) it.pixels in (NORMAL_PIXELS+1)..HIGH_PIXELS else it.pixels <= NORMAL_PIXELS }
        val fourThree = eligible.filter { kotlin.math.abs(it.width.toDouble()/it.height - 4.0/3) < .03 }
        return (fourThree.ifEmpty { eligible }).maxByOrNull { it.pixels }
    }
    fun highPeakBytes(pixels: Long): Long = pixels * 20 + 128L * 1024 * 1024
    fun canUseHigh(pixels: Long, availableBytes: Long, lowMemory: Boolean): Boolean =
        pixels in (NORMAL_PIXELS+1)..HIGH_PIXELS && !lowMemory && availableBytes >= highPeakBytes(pixels) * 3 / 2
}

/** A high-resolution job owns admission until save/cleanup, including queueing. */
class BurstAdmission(private val limit: Int = 3) {
    class Ticket internal constructor(internal val id: Long, val exclusive: Boolean)
    private val active = mutableMapOf<Long, Boolean>()
    private var nextId = 0L
    @Synchronized fun reserve(exclusive: Boolean = false): Ticket? {
        if (active.values.any { it } || active.size >= limit || (exclusive && active.isNotEmpty())) return null
        return Ticket(++nextId, exclusive).also { active[it.id] = exclusive }
    }
    @Synchronized fun release(ticket: Ticket): Boolean = active.remove(ticket.id) != null
}
