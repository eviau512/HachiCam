package com.scanner.app.data.util

import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/** Bounded completed test captures; replacement/export share a lock across callers. */
class BurstArchiveStore(private val root: File, private val enabled: Boolean) {
    companion object {
        private val guard = Any()
        const val LIMIT = 192L * 1024 * 1024
        const val GROUP_LIMIT = 4
    }
    private fun removeChild(file: File) {
        require(file.canonicalFile.parentFile == root.canonicalFile)
        if (file.exists()) check(file.deleteRecursively())
    }
    private fun recoverPrevious() {
        val current = File(root, "current"); val previous = File(root, "previous")
        if (!current.exists() && previous.isDirectory) check(previous.renameTo(current))
    }
    private fun files(folder: File): List<File> = folder.listFiles()?.filter { it.isFile }?.sortedBy { it.name }.orEmpty().also { entries ->
        require(entries.all { it.canonicalFile.parentFile == folder.canonicalFile })
    }
    private fun history(): List<File> {
        val directory = File(root, "history")
        require(directory.canonicalFile.parentFile == root.canonicalFile)
        return directory.listFiles()?.filter { it.isDirectory && File(it,"manifest.json").isFile }
            ?.sortedByDescending { it.name.substringBefore('-').toLongOrNull() ?: it.lastModified() }.orEmpty().also { groups ->
                require(groups.all { it.canonicalFile.parentFile == directory.canonicalFile })
            }
    }
    private fun pruneHistory() {
        var bytes=files(File(root,"current")).sumOf { it.length() }
        val seen=mutableSetOf(groupId(File(root,"current")))
        var kept=0
        history().forEach { folder ->
            val size=files(folder).sumOf { it.length() }
            if(!seen.add(groupId(folder)) || kept>=GROUP_LIMIT-1 || bytes+size>LIMIT) check(folder.deleteRecursively())
            else { bytes+=size; kept++ }
        }
    }
    private fun groupId(folder: File): String {
        val manifest=File(folder,"manifest.json")
        require(manifest.length() in 1..128L*1024)
        val text=manifest.readText()
        return Regex("\"captureId\"\\s*:\\s*\"([a-fA-F0-9-]{36})\"").find(text)?.groupValues?.get(1)
            ?: folder.name
    }
    fun latestCaptureId(): String? = synchronized(guard) {
        if(!enabled) return@synchronized null
        recoverPrevious()
        File(root,"current").takeIf { File(it,"manifest.json").isFile }?.let(::groupId)
    }
    fun replace(inputs: List<Pair<String, ByteArray>>, output: File, manifest: String, diagnostics: String = "",
        requestNs: Long = System.nanoTime()): Boolean = synchronized(guard) {
        val acquiredNs=System.nanoTime()
        if (!enabled) return@synchronized false
        require(inputs.isNotEmpty() && inputs.size <= 7)
        require(inputs.map { it.first }.distinct().size == inputs.size &&
            inputs.all { it.first.matches(Regex("[0-9]{2}-tier[0-2]\\.jpg")) })
        val metadata = manifest.toByteArray(Charsets.UTF_8)
        val context = diagnostics.toByteArray(Charsets.UTF_8)
        require(context.size <= 128 * 1024)
        require(metadata.isNotEmpty() && metadata.size <= 128 * 1024 && output.isFile && output.length()>0 && inputs.all { it.second.isNotEmpty() })
        val inputBytes = inputs.sumOf { it.second.size.toLong() }
        val total = inputBytes + output.length() + metadata.size + context.size + 1024 // timing footer allowance
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
            val copiedNs=System.nanoTime()
            val measured = diagnostics + "\nSnapshot timing: clock=System.nanoTime lockWaitMs=${(acquiredNs-requestNs).coerceAtLeast(0)/1e6} copyMs=${(copiedNs-acquiredNs).coerceAtLeast(0)/1e6}; excludes final metadata/rename\n"
            require(measured.toByteArray(Charsets.UTF_8).size<=128*1024)
            File(pending,"diagnostics.txt").writeText(measured)
            if (current.exists()) check(current.renameTo(previous))
            if (!pending.renameTo(current)) {
                if (previous.exists()) check(previous.renameTo(current))
                error("Diagnostic snapshot commit failed")
            }
            if(previous.isDirectory) {
                val older=File(root,"history").apply { check(mkdirs() || isDirectory) }
                val last=history().mapNotNull { it.name.substringBefore('-').toLongOrNull() }.maxOrNull() ?: 0L
                val order=maxOf(System.currentTimeMillis(),last+1)
                check(previous.renameTo(File(older,"${order.toString().padStart(20,'0')}-${java.util.UUID.randomUUID()}")))
            }
            pruneHistory()
            true
        } finally { removeChild(pending) }
    }
    fun export(log: File, output: File, includeHistory: Boolean = false,
        requestNs: Long = System.nanoTime()): File? = synchronized(guard) {
        val acquiredNs=System.nanoTime()
        if (!enabled) return@synchronized null
        recoverPrevious()
        val current = File(root, "current")
        if (!File(current, "manifest.json").isFile || !File(current,"fused.jpg").isFile) return@synchronized null
        val groups=(listOf(current) + if(includeHistory)history() else emptyList()).distinctBy(::groupId).take(GROUP_LIMIT)
        val selected=groups.flatMap { folder -> files(folder).map { file ->
            file to "captures/${groupId(folder)}/${file.name}"
        } }
        val total = selected.sumOf { it.first.length() } + log.length()
        val destination = output.parentFile ?: return@synchronized null
        if (total > LIMIT + 16L * 1024 * 1024 || destination.usableSpace < total + 32L * 1024 * 1024)
            return@synchronized null
        val temporary = File(destination, output.name + ".tmp")
        try {
            ZipOutputStream(temporary.outputStream().buffered()).use { zip ->
                zip.setLevel(0) // JPEG is already compressed; ZIP still supplies entry CRCs.
                (listOf(log to log.name) + selected).forEach { (file, name) ->
                    zip.putNextEntry(ZipEntry(name))
                    val copied=file.inputStream().use { it.copyTo(zip) }
                    check(copied==file.length()) { "Incomplete diagnostic copy" }
                    zip.closeEntry()
                }
                val payloadEndNs=System.nanoTime()
                val ids=groups.joinToString(",") { "\"${groupId(it)}\"" }
                val footer="{\"schema\":2,\"layout\":\"captures-by-id-v2\",\"latestCaptureId\":\"${groupId(current)}\",\"clock\":\"System.nanoTime\",\"groups\":[$ids],\"groupCount\":${groups.size},\"payloadBytes\":$total,\"snapshotLockWaitMs\":${(acquiredNs-requestNs).coerceAtLeast(0)/1e6},\"zipPayloadMs\":${(payloadEndNs-acquiredNs).coerceAtLeast(0)/1e6},\"scope\":\"before footer and ZIP close\"}"
                zip.putNextEntry(ZipEntry("export-timing.json"));zip.write(footer.toByteArray(Charsets.UTF_8));zip.closeEntry()
            }
            check(temporary.renameTo(output))
            output
        } finally { temporary.delete() }
    }
}
