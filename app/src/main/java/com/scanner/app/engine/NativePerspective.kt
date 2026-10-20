package com.scanner.app.engine

import com.scanner.app.domain.model.DocumentQuad
import com.scanner.app.domain.model.ImageFilter
import org.opencv.core.Mat

class NativePerspective {
    
    init {
        System.loadLibrary("doc_scanner_engine")
    }

    fun correctPerspective(srcMat: Mat, corners: DocumentQuad, targetAspectRatio: Float = 0f): Mat {
        val resultMatAddr = nativeCorrectPerspective(srcMat.nativeObjAddr, corners.toFloatArray(), targetAspectRatio)
        check(resultMatAddr != 0L) { "Perspective returned no image" }
        return Mat(resultMatAddr)
    }

    fun processDocument(srcMat: Mat, corners: DocumentQuad, filter: ImageFilter, targetAspectRatio: Float = 0f): Mat {
        val filterId = when (filter) {
            ImageFilter.ORIGINAL -> 0
            ImageFilter.MAGIC_COLOR -> 1
            ImageFilter.PAPER -> 2
        }
        val resultMatAddr = nativeProcessDocument(srcMat.nativeObjAddr, corners.toFloatArray(), filterId, targetAspectRatio)
        check(resultMatAddr != 0L) { "Document rendering returned no image" }
        return Mat(resultMatAddr)
    }

    fun applyFilter(srcMat: Mat, filter: ImageFilter): Mat {
        val filterId = when (filter) {
            ImageFilter.ORIGINAL -> 0
            ImageFilter.MAGIC_COLOR -> 1
            ImageFilter.PAPER -> 2
        }
        val resultMatAddr = nativeApplyFilter(srcMat.nativeObjAddr, filterId)
        check(resultMatAddr != 0L) { "Filter returned no image" }
        return Mat(resultMatAddr)
    }

    private external fun nativeCorrectPerspective(srcMatAddr: Long, corners: FloatArray, targetAspectRatio: Float): Long
    private external fun nativeProcessDocument(srcMatAddr: Long, corners: FloatArray, filterType: Int, targetAspectRatio: Float): Long
    private external fun nativeApplyFilter(srcMatAddr: Long, filterType: Int): Long
}
