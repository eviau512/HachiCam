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
                assertEquals(5,zip.size())
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
}
