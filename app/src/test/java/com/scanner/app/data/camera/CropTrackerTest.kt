package com.scanner.app.data.camera

import org.junit.Assert.*
import org.junit.Test

class CropTrackerTest {
    private fun quad(inset: Float = .1f) = listOf(UnitPoint(inset, inset), UnitPoint(1-inset, inset),
        UnitPoint(1-inset, 1-inset), UnitPoint(inset, 1-inset))
    private fun acquire(t: CropTracker) {
        assertNull(t.update(quad(), 0, "a"))
        assertNull(t.update(quad(), 120, "a"))
        assertNotNull(t.update(quad(), 260, "a"))
    }
    @Test fun isolatedNestedCandidatesDoNotJump() {
        val t = CropTracker(); acquire(t)
        for (i in 1..8) {
            val points = if (i % 2 == 0) quad() else quad(.3f)
            val selected = t.update(points, 260L + 70L * i, "a")!!
            assertEquals(.1f, selected.points[0].x, .0001f)
        }
    }
    @Test fun persistentChangeWaitsAndDoesNotRefreshOldEvidence() {
        val t = CropTracker(); acquire(t)
        assertEquals(260L, t.update(quad(.3f), 320, "a")!!.observedAtMs)
        assertNotNull(t.update(quad(.3f), 450, "a"))
        assertNull(t.update(quad(.3f), 580, "a"))
        assertNull(t.update(quad(.3f), 680, "a"))
        assertEquals(.3f, t.update(quad(.3f), 730, "a")!!.points[0].x, .0001f)
    }
    @Test fun staleInvalidAndChangedGeometryExpire() {
        val t = CropTracker(); acquire(t)
        assertNotNull(t.update(null, 400, "a"))
        assertNull(t.update(null, 511, "a"))
        assertNull(t.update(quad(), 600, "rotated"))
        assertFalse(CropTracker.valid(quad().reversed()))
        assertFalse(CropTracker.valid(listOf(UnitPoint(Float.NaN, 0f)) + quad().drop(1)))
        assertFalse(CropTracker.valid(listOf(quad()[0], quad()[2], quad()[1], quad()[3])))
    }
    @Test fun jitterIsDampedAndSingleFrameNeverAcquires() {
        val t = CropTracker(); acquire(t)
        val jitter = quad().map { UnitPoint(it.x + .01f, it.y) }
        assertEquals(.1022f, t.update(jitter, 300, "a")!!.points[0].x, .00001f)
        val fresh = CropTracker()
        assertNull(fresh.update(quad(), 0, "a"))
        assertNull(fresh.update(null, 500, "a"))
        assertNull(fresh.update(quad(), 600, "a"))
    }
    @Test fun coverageMappingKeepsPixelCentersAcrossAllRotationsAndScales() {
        val source = UnitPoint(300f / 999, 400f / 799)
        val translated = ImageCoordinates.project(source, 1000, 800,
            doubleArrayOf(1.0, 0.0, 13.0, 0.0, 1.0, -7.0, 0.0, 0.0, 1.0))!!
        for (scale in listOf(1, 2)) for (angle in listOf(0, 90, 180, 270)) {
            val p = ImageCoordinates.throughCrop(translated, 1000, 800, 23, 11, 950, 760, scale, angle)
            val raw = ImageCoordinates.rotate(p, -angle)
            val x = raw.x * (950 * scale - 1)
            val y = raw.y * (760 * scale - 1)
            assertEquals((300 + 13 - 23 + .5f) * scale - .5f, x, .001f)
            assertEquals((400 - 7 - 11 + .5f) * scale - .5f, y, .001f)
        }
        assertNull(ImageCoordinates.project(source, 1000, 800, DoubleArray(9)))
    }
    @Test fun partialSuggestionIsBoundedAndCannotBecomeAnAutomaticCrop() {
        val p = listOf(UnitPoint(.2f,.2f), UnitPoint(1.2f,.18f), UnitPoint(.85f,.85f), UnitPoint(.05f,.8f))
        assertFalse(CropTracker.valid(p))
        assertTrue(CropTracker.valid(p, partial = true))
        assertFalse(CropTracker.valid(p.map { UnitPoint(it.x + .2f, it.y) }, partial = true))
        val t = CropTracker()
        assertNull(t.update(p, 0, "a", partial = true))
        assertNull(t.update(p, 120, "a", partial = true))
        val selected = t.update(p, 260, "a", partial = true)!!
        assertTrue(selected.partial)
        assertFalse(CropTracker.cropEligible(true, selected.partial, selected.observedAtMs, 300))
        assertTrue(t.update(null, 400, "a")!!.partial)
        assertNull(t.update(null, 511, "a"))
    }
    @Test fun incompleteEvidenceRevokesPreviouslyCompleteCropImmediately() {
        val t = CropTracker(); acquire(t)
        assertTrue(CropTracker.cropEligible(true, false, 260, 300))
        assertNull(t.update(quad(), 300, "a", partial = true))
        assertNull(t.update(null, 330, "a"))
        assertFalse(CropTracker.cropEligible(true, true, 260, 300))
        assertFalse(CropTracker.cropEligible(true, false, 260, 511))
        assertFalse(CropTracker.cropEligible(true, false, 260, 259))
        assertFalse(CropTracker.cropEligible(false, false, 260, 300))
    }
    @Test fun completenessUpgradeNeedsFreshConsistentAcquisition() {
        val t = CropTracker()
        t.update(quad(), 0, "a", true); t.update(quad(), 120, "a", true)
        assertTrue(t.update(quad(), 260, "a", true)!!.partial)
        assertTrue(t.update(quad(), 300, "a", false)!!.partial)
        assertTrue(t.update(quad(), 420, "a", false)!!.partial)
        assertNull(t.update(quad(), 580, "a", false))
        assertFalse(t.update(quad(), 710, "a", false)!!.partial)
    }
    @Test fun cameraOrZoomResetCannotReusePreviousAcquisition() {
        val t = CropTracker(); acquire(t)
        t.reset()
        assertNull(t.update(null, 280, "a"))
        assertNull(t.update(quad(), 300, "a"))
        assertNull(t.update(quad(), 420, "a"))
        assertNotNull(t.update(quad(), 560, "a"))
    }
}
