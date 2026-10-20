package com.scanner.app.data.camera

import com.scanner.app.data.repository.PageRepository
import com.scanner.app.domain.model.ScannedPage
import com.scanner.app.data.image.PageIndexStore
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.nio.file.Files

class PageCheckpointLeaseTest {
    @Test fun derivativeRetirementWaitsForBothDurableCheckpointReplacements()= runBlocking {
        val root=Files.createTempDirectory("page-checkpoint-leases").toFile()
        try {
            val scans=File(root,"scans").apply { mkdir() };val metadata=File(root,"metadata")
            var warnings=0
            PageRepository.initialize(metadata,scans,{ it.isFile && it.length()>0 },{warnings++})
            assertTrue(PageRepository.awaitCheckpoint())
            val source=File(scans,"A.jpg").apply { writeText("master A") }
            PageRepository.addPage(ScannedPage(id="A",originalImagePath=source.absolutePath))
            assertTrue(PageRepository.awaitCheckpoint())
            fun edit(n:Int):File {
                val request=PageRepository.beginEdit("A"){it}!!
                val temporary=File(scans,"edit-$n.tmp").apply { writeText("derivative $n") }
                val result=File(scans,"edit-$n.jpg")
                assertTrue(PageRepository.commitEdit(request,temporary,result));return result
            }
            val first=edit(1);assertTrue(PageRepository.awaitCheckpoint())
            val second=edit(2);assertTrue(PageRepository.awaitCheckpoint());assertTrue(first.exists())
            val third=edit(3);assertTrue(PageRepository.awaitCheckpoint());assertFalse(first.exists());assertTrue(second.exists())
            PageRepository.deletePage("A");assertTrue(PageRepository.awaitCheckpoint())
            assertFalse(second.exists());assertTrue(third.exists());assertTrue(source.exists())
            val other=File(scans,"B.jpg").apply { writeText("master B") }
            PageRepository.addPage(ScannedPage(id="B",originalImagePath=other.absolutePath))
            assertTrue(PageRepository.awaitCheckpoint());assertFalse(third.exists());assertFalse(source.exists())
            assertEquals(listOf("B"),PageIndexStore(metadata).load().map { it.id });assertEquals(0,warnings)
            PageRepository.clear();assertTrue(PageRepository.awaitCheckpoint());assertTrue(other.exists())
        } finally { root.deleteRecursively() }
    }
}
