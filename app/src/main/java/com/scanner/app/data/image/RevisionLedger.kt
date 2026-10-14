package com.scanner.app.data.image

/** Committed values and composing pending intents; no Android/native dependency. */
class RevisionLedger<T> {
    data class Request<T>(val id: String, val generation: Long, val value: T)
    private data class Entry<T>(var committed: T, var desired: T? = null, var generation: Long = 0)
    private val entries = linkedMapOf<String, Entry<T>>()
    private var nextGeneration = 0L
    @Synchronized fun put(id: String, value: T) { entries[id] = Entry(value, generation = ++nextGeneration) }
    @Synchronized fun get(id: String): T? = entries[id]?.committed
    @Synchronized fun values(): List<T> = entries.values.map { it.committed }
    @Synchronized fun begin(id: String, change: (T) -> T): Request<T>? {
        val entry = entries[id] ?: return null
        val value = change(entry.desired ?: entry.committed)
        entry.desired = value; entry.generation = ++nextGeneration
        return Request(id, entry.generation, value)
    }
    @Synchronized fun current(request: Request<T>) = entries[request.id]?.generation == request.generation
    @Synchronized fun commit(request: Request<T>, value: T, install: () -> Unit): Boolean {
        val entry = entries[request.id] ?: return false
        if (entry.generation != request.generation) return false
        install()
        entry.committed = value; entry.desired = null
        return true
    }
    @Synchronized fun fail(request: Request<T>) {
        entries[request.id]?.takeIf { it.generation == request.generation }?.let {
            it.desired = null; it.generation = ++nextGeneration
        }
    }
    @Synchronized fun remove(id: String): T? = entries.remove(id)?.committed
    @Synchronized fun clear() { entries.clear() }
    @Synchronized fun reorder(from: Int, to: Int) {
        val ids = entries.keys.toMutableList()
        if (from !in ids.indices || to !in ids.indices) return
        ids.add(to, ids.removeAt(from))
        val reordered = ids.associateWith { entries.getValue(it) }
        entries.clear(); entries.putAll(reordered)
    }
}
