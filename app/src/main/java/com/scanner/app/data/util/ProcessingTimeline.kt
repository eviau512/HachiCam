package com.scanner.app.data.util

/** One operation's monotonic clock; immutable snapshots never mix later captures. */
class ProcessingTimeline(private val clockNs: () -> Long = System::nanoTime) {
    data class Stage(val name: String, val offsetMs: Double, val durationMs: Double, val outcome: String)
    private val origin = clockNs()
    private val active = linkedMapOf<String, Long>()
    private val finished = mutableListOf<Stage>()

    @Synchronized fun start(name: String) {
        require(name.isNotBlank() && name.length <= 80 && name !in active)
        check(finished.size + active.size < 64) { "Timing stage limit" }
        active[name] = clockNs()
    }
    @Synchronized fun end(name: String, outcome: String = "completed") {
        val started = active.remove(name) ?: error("Unknown timing stage: $name")
        val ended = clockNs()
        finished += Stage(name, ((started-origin).coerceAtLeast(0))/1e6,
            (ended-started).coerceAtLeast(0)/1e6, outcome.take(80))
    }
    @Synchronized fun snapshot(): List<Stage> = finished.toList()
    @Synchronized fun pending(): List<String> = active.keys.toList()
    @Synchronized fun elapsedMs(): Double = (clockNs()-origin).coerceAtLeast(0)/1e6
    fun <T> measure(name: String, block: () -> T): T {
        start(name)
        return try { block().also { end(name) } }
        catch (error: Exception) { end(name, "failed"); throw error }
    }
    @Synchronized fun finishPending(outcome: String) {
        active.keys.toList().forEach { end(it, outcome) }
    }
    suspend fun <T> measureSuspend(name:String,block:suspend ()->T):T {
        start(name)
        return try { block().also { end(name) } }
        catch(error:Exception){end(name,"failed");throw error}
    }
}
