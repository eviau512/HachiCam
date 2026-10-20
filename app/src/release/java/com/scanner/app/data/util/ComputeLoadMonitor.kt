package com.scanner.app.data.util

/** Release contains no sampling, root helper, device-node reads or retained traces. */
object ComputeLoadMonitor {
    const val enabled = false
    @Suppress("UNUSED_PARAMETER")
    fun start(id: String, timeline: ProcessingTimeline, context: android.content.Context): Session = Session()
    class Session : AutoCloseable {
        fun finish(): String = ""
        override fun close() = Unit
    }
}
