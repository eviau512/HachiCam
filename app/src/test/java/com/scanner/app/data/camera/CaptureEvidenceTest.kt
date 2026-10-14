package com.scanner.app.data.camera

import org.junit.Assert.*
import org.junit.Test
import kotlin.math.pow

class CaptureEvidenceTest {
    @Test fun sensorAndExifProductsNeverMixWhenOneResultIsMissing() {
        val selected = ExposurePlan.evidence(listOf(ManualExposure(20,100,400),null),
            listOf(ManualExposure(20,500),ManualExposure(5,500)))
        assertEquals("EXIF",selected.description)
        assertArrayEquals(doubleArrayOf(10000.0,2500.0),selected.products,0.0)
    }
    @Test fun partialBoostEvidenceFallsBackAndUnknownIsExplicit() {
        val exif = listOf(ManualExposure(20,500),ManualExposure(5,500))
        assertEquals("EXIF",ExposurePlan.evidence(listOf(ManualExposure(20,100,400),ManualExposure(5,100)),exif).description)
        assertEquals("sensor (boost unknown)",ExposurePlan.evidence(listOf(ManualExposure(20,100),ManualExposure(5,100)),exif).description)
    }
    @Test fun completeBoostEvidenceUsesSensorProducts() {
        val selected = ExposurePlan.evidence(listOf(ManualExposure(20,100,400),ManualExposure(5,100,400)),
            listOf(ManualExposure(20,500),ManualExposure(5,500)))
        assertEquals("sensor+postRawBoost",selected.description)
        assertArrayEquals(doubleArrayOf(8000.0,2000.0),selected.products,0.0)
    }
    @Test fun matchesOnlyExactTimestampAndConsumesOnce() {
        val store = CaptureEvidenceStore(2)
        val frame = CaptureEvidence(100, ManualExposure(20,100,400),4,0,.8f)
        store.record(frame)
        assertNull(store.take(101)); assertEquals(frame,store.take(100)); assertNull(store.take(100))
    }
    @Test fun boundedHistoryAndSessionResetDoNotLeakOldEvidence() {
        val store = CaptureEvidenceStore(2)
        for (time in 1L..3L) store.record(CaptureEvidence(time,null,null,null,null))
        assertNull(store.take(1)); assertNotNull(store.take(3)); store.clear(); assertNull(store.take(2))
    }
    @Test fun bracketsPreserveActualBoostAndRelativeStops() {
        val base = ManualExposure(20_000_000,100,400)
        val bracket = ExposurePlan.bracket(base,2.5,100_000,100_000_000,50,6400)!!
        assertEquals(400,bracket.postRawBoost)
        assertEquals(base.product / 2.0.pow(2.5),bracket.product,base.product*.001)
        assertTrue(bracket.iso <= base.iso)
    }
}
