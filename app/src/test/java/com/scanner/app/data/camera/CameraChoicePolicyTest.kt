package com.scanner.app.data.camera

import org.junit.Assert.*
import org.junit.Test

class CameraChoicePolicyTest {
    @Test fun oneViewCanSelectHigherResolutionCaptureVariant() {
        val main = CameraChoicePolicy.Endpoint("0",1f,true,0)
        val high = CameraChoicePolicy.Endpoint("2",1f,true,50_000_000)
        val wide = CameraChoicePolicy.Endpoint("3",.67f,true,48_000_000)
        assertEquals(listOf("0","3"), CameraChoicePolicy.views(listOf(main,high,wide),"0").map { it.id })
        assertEquals("0",CameraChoicePolicy.captureVariant(listOf(main,high,wide),main,false).id)
        assertEquals("2",CameraChoicePolicy.captureVariant(listOf(main,high,wide),main,true).id)
    }
    @Test fun highVariantRespectsCompatibilityBudgetAndDefaultTie() {
        val main = CameraChoicePolicy.Endpoint("0",1f,true,50_000_000)
        val invalid = CameraChoicePolicy.Endpoint("2",1f,false,55_000_000)
        val tooLarge = CameraChoicePolicy.Endpoint("4",1f,true,100_000_000)
        val alias = CameraChoicePolicy.Endpoint("5",1f,true,50_000_000)
        assertEquals("0",CameraChoicePolicy.captureVariant(listOf(main,invalid,tooLarge,alias),main,true).id)
    }
    private fun e(id: String, ratio: Float, compatible: Boolean = true) = CameraChoicePolicy.Endpoint(id, ratio, compatible)
    @Test fun aliasesDoNotBecomeExtraLenses() {
        val views = CameraChoicePolicy.views(listOf(e("0",1f),e("2",.6f),e("3",1.03f),e("4",.61f)),"0")
        assertEquals(listOf("0","2"),views.map { it.id })
    }
    @Test fun distinctViewsSurviveAndUnknownAuxiliariesDisappear() {
        val views = CameraChoicePolicy.views(listOf(e("main",1f),e("wide",.6f),e("tele",2f),e("depth",.3f,false),e("unknown",Float.NaN)),"main")
        assertEquals(listOf("main","wide","tele"),views.map { it.id })
    }
    @Test fun defaultWinsEvenIfItIsNotFirst() {
        assertEquals(listOf("default"),CameraChoicePolicy.views(listOf(e("alias",1f),e("default",1f)),"default").map { it.id })
        assertTrue(CameraChoicePolicy.views(listOf(e("0",1f)),"missing").isEmpty())
    }
    @Test fun exposurePresetsRespectDriverLimitsAndNonThirdSteps() {
        assertEquals(listOf(-3,0,3),CameraChoicePolicy.exposureIndices(1f/3,-3,3))
        assertEquals(listOf(-4,-2,0,2),CameraChoicePolicy.exposureIndices(.5f,-6,6))
        assertTrue(CameraChoicePolicy.exposureIndices(0f,-3,3).isEmpty())
    }
}
