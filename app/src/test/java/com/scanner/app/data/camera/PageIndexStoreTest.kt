package com.scanner.app.data.camera

import com.scanner.app.data.image.PageIndexStore
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.nio.file.Files

class PageIndexStoreTest {
    private fun record(id:String)=PageIndexStore.Record(id,"$id.jpg","$id-edit.jpg",null,"PAPER",90,1.4f,123,"READY",
        listOf(10f,20f,500f,20f,500f,800f,10f,800f),emptyList())
    @Test fun recipeOrderAndPriorCheckpointSurviveTruncatedLatest() {
        val root=Files.createTempDirectory("page-index").toFile()
        try {
            val store=PageIndexStore(root)
            val first=listOf(record("B"),record("A"))
            store.save(first);assertEquals(first,store.load())
            store.save(listOf(record("C")));assertEquals(listOf(record("C")),store.load())
            File(root,"pages.index").writeBytes(byteArrayOf(1,2,3))
            assertEquals(first,store.load())
            store.save(listOf(record("D")));assertEquals(listOf(record("D")),store.load())
        } finally { root.deleteRecursively() }
    }
    @Test fun checksumTraversalNonfiniteAndOversizeCannotReplaceCheckpoint() {
        val root=Files.createTempDirectory("page-index-invalid").toFile()
        try {
            val store=PageIndexStore(root);val kept=listOf(record("A"));store.save(kept)
            for(bad in listOf(record("B").copy(source="../outside.jpg"),record("B").copy(quad=List(8){Float.NaN}),
                record("B").copy(rotation=45),record("B").copy(aspect=Float.POSITIVE_INFINITY))) {
                try { store.save(listOf(bad));fail("Invalid metadata accepted") } catch(_:IllegalArgumentException){}
                assertEquals(kept,store.load())
            }
            try { store.save(List(513){record("$it")});fail("Page bound exceeded") } catch(_:IllegalArgumentException){}
            assertEquals(kept,store.load())
            val bytes=File(root,"pages.index").readBytes();bytes[bytes.lastIndex]=(bytes.last().toInt() xor 1).toByte()
            File(root,"pages.index").writeBytes(bytes)
            try { store.load();fail("Unvalidated index accepted") } catch(_:Exception){}
        } finally { root.deleteRecursively() }
    }
}
