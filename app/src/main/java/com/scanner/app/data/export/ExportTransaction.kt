package com.scanner.app.data.export

/** Owns destinations until every write and publication succeeds. */
object ExportTransaction {
    fun <T> execute(count: Int, create: (Int) -> T, write: (T, Int) -> Unit,
                    publish: (T) -> Unit, rollback: (T) -> Unit, checkpoint: () -> Unit = {}): List<T> {
        require(count > 0)
        val owned = mutableListOf<T>()
        try {
            repeat(count) { index ->
                checkpoint()
                val target = create(index)
                owned.add(target)
                write(target, index)
                checkpoint()
            }
            owned.forEach { checkpoint(); publish(it) }
            return owned.toList()
        } catch (error: Exception) {
            var cleanupFailed = false
            owned.asReversed().forEach { target ->
                try { rollback(target) } catch (cleanup: Exception) { cleanupFailed = true; error.addSuppressed(cleanup) }
            }
            if (cleanupFailed) throw ExportCleanupException(error)
            throw error
        }
    }
}

data class ExportReceipt(val displayLocation: String, val items: List<String>, val pageCount: Int)
class ExportCleanupException(cause: Exception) : IllegalStateException("Export failed; some incomplete items could not be removed", cause)
