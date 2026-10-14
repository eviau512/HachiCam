package com.scanner.app.data.util

import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/** One completed test capture; replacement/export share a lock across callers. */
class BurstArchiveStore(private val root: File, private val enabled: Boolean) {
    companion object {
        private val guard = Any()
        const val LIMIT = 192L * 1024 * 1024
    }
    private fun removeChild(file: File) {
        require(file.canonicalFile.parentFile == root.canonicalFile)
        if (file.exists()) check(file.deleteRecursively())
    }
    private fun recoverPrevious() {
        val current = File(root, "current"); val previous = File(root, "previous")
        if (!current.exists() && previous.isDirectory) check(previous.renameTo(current))
    }
    fun replace(inputs: List<Pair<String, ByteArray>>, output: File, manifest: String): Boolean = synchronized(guard) {
        if (!enabled) return@synchronized false
        require(inputs.isNotEmpty() && inputs.size <= 7)
        require(inputs.map { it.first }.distinct().size == inputs.size &&
            inputs.all { it.first.matches(Regex("[0-9]{2}-tier[0-2]\\.jpg")) })
        val metadata = manifest.toByteArray(Charsets.UTF_8)
        require(metadata.size <= 128 * 1024 && output.isFile)
        val inputBytes = inputs.sumOf { it.second.size.toLong() }
        val total = inputBytes + output.length() + metadata.size
        val parent = root.parentFile ?: return@synchronized false
        if (inputBytes > 96L * 1024 * 1024 || total > LIMIT ||
            parent.usableSpace < total * 3 + 64L * 1024 * 1024) return@synchronized false
        check(root.mkdirs() || root.isDirectory)
        val pending = File(root, "pending")
        val current = File(root, "current")
        val previous = File(root, "previous")
        recoverPrevious()
        removeChild(pending); removeChild(previous)
        try {
            check(pending.mkdir())
            inputs.forEach { (name, bytes) -> File(pending, name).writeBytes(bytes) }
            File(pending, "manifest.json").writeBytes(metadata)
            output.copyTo(File(pending, "fused.jpg"))
            if (current.exists()) check(current.renameTo(previous))
            if (!pending.renameTo(current)) {
                if (previous.exists()) check(previous.renameTo(current))
                error("Diagnostic snapshot commit failed")
            }
            removeChild(previous)
            true
        } finally { removeChild(pending) }
    }
    fun export(log: File, output: File): File? = synchronized(guard) {
        if (!enabled) return@synchronized null
        recoverPrevious()
        val current = File(root, "current")
        if (!File(current, "manifest.json").isFile) return@synchronized null
        val files = current.listFiles()?.filter { it.isFile }?.sortedBy { it.name } ?: return@synchronized null
        val total = files.sumOf { it.length() } + log.length()
        val destination = output.parentFile ?: return@synchronized null
        if (total > LIMIT + 16L * 1024 * 1024 || destination.usableSpace < total + 32L * 1024 * 1024)
            return@synchronized null
        val temporary = File(destination, output.name + ".tmp")
        try {
            ZipOutputStream(temporary.outputStream().buffered()).use { zip ->
                zip.setLevel(0) // JPEG is already compressed; ZIP still supplies entry CRCs.
                (listOf(log to log.name) + files.map { it to "capture/${it.name}" }).forEach { (file, name) ->
                    zip.putNextEntry(ZipEntry(name)); file.inputStream().use { it.copyTo(zip) }; zip.closeEntry()
                }
            }
            check(temporary.renameTo(output))
            output
        } finally { temporary.delete() }
    }
}
