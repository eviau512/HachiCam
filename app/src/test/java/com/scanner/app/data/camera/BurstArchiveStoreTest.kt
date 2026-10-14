package com.scanner.app.data.camera

import com.scanner.app.data.util.BurstArchiveStore
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.nio.file.Files
import java.util.zip.ZipFile

class BurstArchiveStoreTest {
    @Test fun disabledStoreNeverCreatesOrExportsPhotoFiles() {
        val base = Files.createTempDirectory("burst-disabled").toFile()
        try {
            val root = File(base,"archive")
            val store = BurstArchiveStore(root,false)
            assertFalse(store.replace(emptyList(),File(base,"missing"),"{}"))
            assertNull(store.export(File(base,"missing"),File(base,"export.zip")))
            assertFalse(root.exists())
        } finally { base.deleteRecursively() }
    }
    @Test fun exportedEntriesBelongToOneLatestCompletedSnapshot() {
        val base = Files.createTempDirectory("burst-complete").toFile()
        try {
            val store = BurstArchiveStore(File(base,"archive"),true)
            val fused = File(base,"fused.jpg").apply { writeBytes(byteArrayOf(9,8)) }
            assertTrue(store.replace(listOf("00-tier0.jpg" to byteArrayOf(1,2)),fused,"old"))
            assertTrue(store.replace(listOf("00-tier0.jpg" to byteArrayOf(3,4),"01-tier2.jpg" to byteArrayOf(5,6)),fused,"latest"))
            val log = File(base,"log.txt").apply { writeText("logs") }
            val exported = store.export(log,File(base,"capture.zip"))!!
            ZipFile(exported).use { zip ->
                assertEquals(7,zip.size())
                assertEquals("latest",zip.getInputStream(zip.getEntry("capture/manifest.json")).reader().readText())
                assertArrayEquals(byteArrayOf(3,4),zip.getInputStream(zip.getEntry("capture/00-tier0.jpg")).readBytes())
                assertArrayEquals(byteArrayOf(5,6),zip.getInputStream(zip.getEntry("capture/01-tier2.jpg")).readBytes())
            }
        } finally { base.deleteRecursively() }
    }
    @Test fun invalidOrOversizedMetadataCannotReplacePreviousSnapshot() {
        val base = Files.createTempDirectory("burst-atomic").toFile()
        try {
            val root = File(base,"archive"); val store = BurstArchiveStore(root,true)
            val fused = File(base,"fused.jpg").apply { writeBytes(byteArrayOf(9)) }
            assertTrue(store.replace(listOf("00-tier0.jpg" to byteArrayOf(1)),fused,"kept"))
            for ((name, metadata) in listOf("../outside.jpg" to "bad", "00-tier0.jpg" to "x".repeat(128*1024+1))) {
                try { store.replace(listOf(name to byteArrayOf(2)),fused,metadata); fail("Invalid snapshot accepted") }
                catch (_: IllegalArgumentException) { }
            }
            assertEquals("kept",File(root,"current/manifest.json").readText())
            assertFalse(File(base,"outside.jpg").exists())
        } finally { base.deleteRecursively() }
    }
    @Test fun completedCaptureKeepsItsOwnJournalWhenLiveCaptureHasChanged() {
        val base=Files.createTempDirectory("capture-journal").toFile()
        try {
            val store=BurstArchiveStore(File(base,"archive"),true)
            val fused=File(base,"fused.jpg").apply { writeText("A result") }
            assertTrue(store.replace(listOf("00-tier0.jpg" to byteArrayOf(1)),fused,"A manifest","CaptureId=A\nA cleanup and result"))
            val live=File(base,"live.txt").apply { writeText("Latest started=B") }
            ZipFile(store.export(live,File(base,"export.zip"))!!).use { zip ->
                assertTrue(zip.getInputStream(zip.getEntry("capture/diagnostics.txt")).reader().readText().startsWith("CaptureId=A\nA cleanup and result"))
                assertEquals("Latest started=B",zip.getInputStream(zip.getEntry("live.txt")).reader().readText())
            }
        } finally { base.deleteRecursively() }
    }
    @Test fun fourCompletedGroupsExportOnceAndEvictOldestWithoutMixingInputs() {
        val base=Files.createTempDirectory("capture-history").toFile()
        try {
            val root=File(base,"archive");val store=BurstArchiveStore(root,true)
            val fused=File(base,"fused.jpg")
            for(index in 1..5) {
                fused.writeText("result $index")
                val id="00000000-0000-0000-0000-${index.toString().padStart(12,'0')}"
                assertTrue(store.replace(listOf("00-tier0.jpg" to byteArrayOf(index.toByte())),fused,"{\"captureId\":\"$id\"}","CaptureId=$id"))
            }
            val log=File(base,"live.txt").apply { writeText("context") }
            ZipFile(store.export(log,File(base,"four.zip"),includeHistory=true)!!).use { zip ->
                val inputs=zip.entries().asSequence().filter { it.name.endsWith("00-tier0.jpg") }.toList()
                assertEquals(4,inputs.size)
                assertEquals(setOf(2,3,4,5),inputs.map { zip.getInputStream(it).readBytes().single().toInt() }.toSet())
                assertEquals("result 5",zip.getInputStream(zip.getEntry("capture/fused.jpg")).reader().readText())
                val footer=zip.getInputStream(zip.getEntry("export-timing.json")).reader().readText()
                assertTrue(footer.contains("\"groupCount\":4"))
                assertTrue(footer.contains("\"snapshotLockWaitMs\""))
            }
            assertEquals(3,File(root,"history").listFiles()!!.size)
        } finally { base.deleteRecursively() }
    }
    @Test fun byteEvictionAndFailedExportKeepLatestCompletedSnapshot() {
        val base=Files.createTempDirectory("capture-byte-history").toFile()
        try {
            val root=File(base,"archive");val store=BurstArchiveStore(root,true)
            val fused=File(base,"fused.jpg").apply { writeText("first") }
            assertTrue(store.replace(listOf("00-tier0.jpg" to byteArrayOf(1)),fused,"first"))
            fused.writeText("latest")
            assertTrue(store.replace(listOf("00-tier0.jpg" to byteArrayOf(2)),fused,"latest"))
            // Sparse old payload triggers the production aggregate byte bound
            // without allocating hundreds of MiB in the JVM test process.
            val history=File(root,"history").listFiles()!!.single()
            java.io.RandomAccessFile(File(history,"00-tier0.jpg"),"rw").use { it.setLength(BurstArchiveStore.LIMIT) }
            fused.writeText("newest")
            assertTrue(store.replace(listOf("00-tier0.jpg" to byteArrayOf(3)),fused,"newest"))
            assertFalse(history.exists())
            val log=File(base,"log.txt").apply { writeText("log") }
            val blocked=File(base,"blocked").apply { mkdir() }
            try { store.export(log,blocked);fail("Directory exported over") } catch(_:Exception){}
            assertEquals("newest",File(root,"current/manifest.json").readText())
            ZipFile(store.export(log,File(base,"valid.zip"),includeHistory=true)!!).use { zip ->
                assertEquals("newest",zip.getInputStream(zip.getEntry("capture/manifest.json")).reader().readText())
            }
        } finally { base.deleteRecursively() }
    }
    @Test fun simultaneousSnapshotAndExportNeverMixGroupPayloads() {
        val base=Files.createTempDirectory("capture-concurrent").toFile()
        try {
            val store=BurstArchiveStore(File(base,"archive"),true)
            fun replace(n:Int) {
                val fused=File(base,"fused-$n.jpg").apply { writeText("$n") }
                assertTrue(store.replace(listOf("00-tier0.jpg" to "$n".toByteArray()),fused,"$n"))
            }
            replace(0);val log=File(base,"live.txt").apply { writeText("live") }
            val error=java.util.concurrent.atomic.AtomicReference<Throwable?>()
            val writer=Thread { try { for(n in 1..8)replace(n) } catch(t:Throwable){error.set(t)} }.apply { start() }
            for(n in 1..8)ZipFile(store.export(log,File(base,"export-$n.zip"),includeHistory=true)!!).use { zip ->
                val manifest=zip.getInputStream(zip.getEntry("capture/manifest.json")).reader().readText()
                assertEquals(manifest,zip.getInputStream(zip.getEntry("capture/00-tier0.jpg")).reader().readText())
                assertEquals(manifest,zip.getInputStream(zip.getEntry("capture/fused.jpg")).reader().readText())
            }
            writer.join();error.get()?.let { throw it }
        } finally { base.deleteRecursively() }
    }
}
