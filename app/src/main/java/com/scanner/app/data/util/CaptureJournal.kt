package com.scanner.app.data.util

/** Bounded immutable-at-export capture context survives later capture starts. */
class CaptureJournal(private val capacity: Int = 6) {
    private data class Group(val context: String, val lines: ArrayDeque<String> = ArrayDeque())
    private val groups = linkedMapOf<String, Group>()
    var latestStarted: String = ""
        private set
    init { require(capacity > 0) }
    @Synchronized fun begin(id: String, context: String) {
        groups[id] = Group(context.take(16384)); latestStarted = id
        while (groups.size > capacity) groups.remove(groups.keys.first())
    }
    @Synchronized fun record(id: String, line: String) {
        val group = groups[id] ?: return
        group.lines.addLast(line.take(512))
        while (group.lines.size > 64) group.lines.removeFirst()
    }
    @Synchronized fun snapshot(id: String): String = groups[id]?.let {
        "CaptureId=$id\n${it.context}\n${it.lines.joinToString("\n")}"
    } ?: "CaptureId=$id\nPer-capture context unavailable (expired or unknown)"
}
