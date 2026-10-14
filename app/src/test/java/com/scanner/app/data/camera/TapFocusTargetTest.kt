package com.scanner.app.data.camera

import org.junit.Assert.*
import org.junit.Test

class TapFocusTargetTest {
    @Test fun selectedTargetSurvivesMeteringTimeoutUntilGeometryInvalidation() {
        val target=TapFocusTarget<String>()
        assertTrue(target.remember("screen",.4f,.7f,1000,4))
        assertEquals("screen",target.select(100_000,4)?.point)
        assertEquals(99_000L,target.select(100_000,4)?.ageMs)
        target.clear();assertNull(target.select(100_001,4))
    }
    @Test fun invalidTapsDoNotReplaceUserIntent() {
        val target=TapFocusTarget<String>();target.remember("screen",.4f,.7f,1000,4)
        for(x in listOf(Float.NaN,Float.POSITIVE_INFINITY,-.1f,1.1f))
            assertFalse(target.remember("invalid",x,.5f,2000,4))
        assertEquals("screen",target.select(3000,4)?.point)
        assertTrue(target.remember("new",.8f,.3f,3000,4));assertEquals("new",target.select(3001,4)?.point)
    }
    @Test fun oldSessionAndClockRollbackCannotSelectStalePoint() {
        val target=TapFocusTarget<String>();target.remember("screen",.4f,.7f,1000,4)
        assertNull(target.select(2000,5));assertNull(target.select(999,4))
        assertEquals("screen",target.select(2000,4)?.point)
    }
}
