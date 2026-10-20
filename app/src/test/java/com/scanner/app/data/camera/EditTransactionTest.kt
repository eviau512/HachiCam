package com.scanner.app.data.camera

import com.scanner.app.data.image.AtomicImageCommit
import com.scanner.app.data.image.QuarterTurn
import com.scanner.app.data.image.RevisionLedger
import com.scanner.app.data.repository.PageRepository
import com.scanner.app.domain.model.ImageFilter
import com.scanner.app.domain.model.PageStatus
import com.scanner.app.domain.model.ScannedPage
import org.junit.Assert.*
import org.junit.Test
import java.nio.file.Files

class EditTransactionTest {
    data class Recipe(val filter: Int = 0, val aspect: Float? = null, val rotation: Int = 0)
    @Test fun pendingChangesComposeAndOldCompletionCannotInstall() {
        val ledger = RevisionLedger<Recipe>(); ledger.put("page", Recipe())
        val a = ledger.begin("page") { it.copy(filter = 2) }!!
        val b = ledger.begin("page") { it.copy(aspect = .7f) }!!
        assertEquals(2, b.value.filter)
        assertFalse(ledger.commit(a, a.value) { fail("obsolete file installed") })
        var installs = 0
        assertTrue(ledger.commit(b, b.value) { installs++ })
        assertEquals(1, installs); assertEquals(b.value, ledger.get("page"))
    }
    @Test fun failedCommitRetainsRecipeAndDoesNotReviveOlderIntent() {
        val ledger = RevisionLedger<Recipe>(); ledger.put("page", Recipe())
        val old = ledger.begin("page") { it.copy(filter = 1) }!!
        val next = ledger.begin("page") { it.copy(rotation = 90) }!!
        try { ledger.commit(next, next.value) { error("disk failure") }; fail() } catch (_: IllegalStateException) { }
        ledger.fail(next)
        assertEquals(Recipe(), ledger.get("page"))
        assertFalse(ledger.current(old)); assertFalse(ledger.current(next))
        assertEquals(Recipe(aspect = 1f), ledger.begin("page") { it.copy(aspect = 1f) }!!.value)
    }
    @Test fun deleteAndReaddCannotReuseOldGeneration() {
        val ledger = RevisionLedger<Recipe>(); ledger.put("page", Recipe())
        val old = ledger.begin("page") { it.copy(filter = 2) }!!
        ledger.remove("page"); ledger.put("page", Recipe())
        assertFalse(ledger.commit(old, old.value) { fail() })
    }
    @Test fun fourTurnsAndInverseRetainPixelCentreCoordinatesAndCornerOrder() {
        val original = listOf(UnitPoint(.1f,.2f),UnitPoint(.9f,.15f),UnitPoint(.85f,.8f),UnitPoint(.2f,.9f))
        var current = original
        repeat(4) { current = QuarterTurn.corners(current, 90) }
        fun equal(a: List<UnitPoint>, b: List<UnitPoint>) = a.zip(b).forEach { (x,y) ->
            assertEquals(x.x,y.x,1e-6f); assertEquals(x.y,y.y,1e-6f)
        }
        equal(original,current)
        for (angle in listOf(0,90,180,270)) equal(original,QuarterTurn.corners(QuarterTurn.corners(original,angle),-angle))
        assertEquals(2f,QuarterTurn.preRotationAspect(.5f,90),0f)
        assertEquals(.5f,QuarterTurn.preRotationAspect(.5f,180),0f)
        assertEquals(0f,QuarterTurn.preRotationAspect(null,270),0f)
    }
    @Test fun atomicCommitNeverOverwritesExistingGoodResult() {
        val dir = Files.createTempDirectory("image-commit").toFile()
        try {
            val temp = dir.resolve("temp.jpg").apply { writeText("new image") }
            val result = dir.resolve("result.jpg").apply { writeText("previous image") }
            try { AtomicImageCommit.install(temp,result); fail() } catch (_: IllegalStateException) { }
            assertEquals("previous image",result.readText()); assertTrue(temp.exists())
            result.delete(); AtomicImageCommit.install(temp,result)
            assertEquals("new image",result.readText()); assertFalse(temp.exists())
        } finally { dir.deleteRecursively() }
    }
    @Test fun deletionAndRevisionRetirementKeepLeasedExportFilesAlive() {
        val dir = Files.createTempDirectory("page-lease").toFile()
        PageRepository.clear()
        try {
            val master = dir.resolve("source.jpg").apply { writeText("immutable source") }
            val old = dir.resolve("old.jpg").apply { writeText("old committed") }
            val page = ScannedPage(id="page",originalImagePath=master.path,processedImagePath=old.path,filter=ImageFilter.ORIGINAL)
            PageRepository.addPage(page)
            val lease = PageRepository.readPages(listOf("page"))
            val request = PageRepository.beginEdit("page") { it.copy(filter=ImageFilter.PAPER) }!!
            val temp = dir.resolve("next.tmp.jpg").apply { writeText("new committed") }
            val result = dir.resolve("next.jpg")
            assertTrue(PageRepository.commitEdit(request,temp,result))
            assertTrue(old.exists()); assertEquals("old committed",lease.pages.single().imagePath.let { java.io.File(it).readText() })
            PageRepository.deletePage("page")
            assertTrue(old.exists()); assertFalse(result.exists())
            lease.close(); assertFalse(old.exists()); assertFalse(master.exists())
        } finally { PageRepository.clear(); dir.deleteRecursively() }
    }
    @Test fun captureInstallationAndReadyStateShareDeletionOwnership() {
        val dir=Files.createTempDirectory("capture-commit").toFile(); PageRepository.clear()
        try {
            val output=dir.resolve("capture.jpg")
            val temp=dir.resolve("capture.tmp.jpg").apply { writeText("valid capture") }
            val page=ScannedPage(id="capture",originalImagePath=output.absolutePath,status=PageStatus.PROCESSING)
            PageRepository.addPage(page); PageRepository.deletePage(page.id)
            assertFalse(PageRepository.commitCapture(page.id,temp,output,null)); assertFalse(output.exists())
            PageRepository.addPage(page)
            assertTrue(PageRepository.commitCapture(page.id,temp,output,null))
            assertEquals(PageStatus.READY,PageRepository.getPage(page.id)!!.status)
            temp.writeText("late fallback")
            assertFalse(PageRepository.commitCapture(page.id,temp,output,null))
            assertEquals("valid capture",output.readText())
            val edit = PageRepository.beginEdit(page.id) { it.copy(filter = ImageFilter.PAPER) }!!
            PageRepository.updatePage(page.id) { if (it.status == PageStatus.PROCESSING) it.copy(status = PageStatus.FAILED) else it }
            assertTrue("Late no-op failure must preserve a pending edit", PageRepository.isCurrent(edit))
            PageRepository.failEdit(edit)
            val lease = PageRepository.readPages(listOf(page.id))
            PageRepository.deletePage(page.id)
            assertTrue("Capture source remains available to an export reader",output.exists())
            lease.close(); assertFalse(output.exists())
        } finally { PageRepository.clear(); dir.deleteRecursively() }
    }
}
