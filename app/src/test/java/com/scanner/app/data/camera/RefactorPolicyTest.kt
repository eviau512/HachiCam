package com.scanner.app.data.camera

import com.scanner.app.data.image.ImageMemoryPolicy
import com.scanner.app.data.image.PerspectiveSize
import com.scanner.app.data.util.CaptureJournal
import org.junit.Assert.*
import org.junit.Test

class RefactorPolicyTest {
    @Test fun normalInputReconstructionIncludesTheLargerOutputAllocation() {
        val pixels = 4096L * 3072
        assertTrue(ImageMemoryPolicy.fusionBytes(pixels,7,false,true) > ImageMemoryPolicy.fusionBytes(pixels,7,false,false))
        val complete = ImageMemoryPolicy.fusionBytes(pixels,7,false,true)
        val native = ImageMemoryPolicy.fusionBytes(pixels,7,false,false)
        assertTrue(ImageMemoryPolicy.fusionPlan(pixels,7,false,complete,0)!!.superResolution)
        assertFalse(ImageMemoryPolicy.fusionPlan(pixels,7,false,native,0)!!.superResolution)
        val stream = ImageMemoryPolicy.fusionBytes(pixels,7,true,false)
        assertTrue(ImageMemoryPolicy.fusionPlan(pixels,7,false,stream,0)!!.streaming)
        assertNull(ImageMemoryPolicy.fusionPlan(pixels,7,false,stream,1))
    }
    @Test fun reservationsAndArchitectureCapsAreAppliedBeforeDecoding() {
        val mib=1024L*1024
        assertEquals(768*mib,ImageMemoryPolicy.budget(10_000*mib,false))
        assertEquals(2048*mib,ImageMemoryPolicy.budget(10_000*mib,true))
        assertEquals(200*mib,ImageMemoryPolicy.budget(300*mib,true))
        assertNull(ImageMemoryPolicy.fusionPlan(50_000_000,7,true,256*mib,0))
    }
    @Test fun laterCaptureDoesNotEraseEarlierControlOrCompletionEvidence() {
        val journal=CaptureJournal(3)
        journal.begin("A","camera A");journal.record("A","frame A")
        journal.begin("B","camera B");journal.record("B","frame B")
        journal.record("A","restore A");journal.record("A","completed A")
        assertTrue(journal.snapshot("A").contains("completed A"))
        assertFalse(journal.snapshot("A").contains("frame B"))
        assertEquals("B",journal.latestStarted)
    }
    @Test fun journalRetentionAndLineBudgetsAreBounded() {
        val journal=CaptureJournal(2)
        journal.begin("A","x");journal.begin("B","x");journal.begin("C","x")
        repeat(100){journal.record("C","$it:"+"y".repeat(2000))}
        assertTrue(journal.snapshot("A").contains("unavailable"))
        assertTrue(journal.snapshot("C").length<33000)
        assertFalse(journal.snapshot("C").lines().any { it.startsWith("0:") })
    }
    @Test fun editBudgetIncludesAspectExpandedOutputAndRejectsUnsafeGeometry() {
        val corners=listOf(UnitPoint(0f,0f),UnitPoint(1f,0f),UnitPoint(1f,1f),UnitPoint(0f,1f))
        val plain=PerspectiveSize.plan(2048,2048,corners,null,0)
        val expanded=PerspectiveSize.plan(1024,1024,corners,10f,0)
        assertTrue(expanded.pixels>1024L*1024)
        assertTrue(ImageMemoryPolicy.editBytes(1024L*1024,expanded.pixels)>ImageMemoryPolicy.editBytes(1024L*1024))
        assertEquals(2047,plain.width)
        for (bad in listOf(corners.toMutableList().apply { this[1]=UnitPoint(Float.NaN,0f) },
            listOf(corners[0],corners[2],corners[1],corners[3]))) {
            try { PerspectiveSize.plan(2048,2048,bad,null,0); fail() } catch (_: IllegalArgumentException) { }
        }
        try { PerspectiveSize.plan(2048,2048,corners,20f,0); fail() } catch (_: IllegalArgumentException) { }
    }
}
