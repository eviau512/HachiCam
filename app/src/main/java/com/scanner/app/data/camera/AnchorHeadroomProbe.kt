package com.scanner.app.data.camera

import android.graphics.Bitmap
import android.graphics.BitmapFactory

/** JPEG decoder reduction keeps the optional acquisition check below 8 MiB of pixels. */
object AnchorHeadroomProbe {
    data class Result(val sourceWidth: Int, val sourceHeight: Int,
                      val statistics: AnchorHeadroomPolicy.Statistics)

    fun read(jpeg: ByteArray): Result? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(jpeg, 0, jpeg.size, bounds)
        val sample = AnchorHeadroomPolicy.decodeSample(bounds.outWidth, bounds.outHeight) ?: return null
        val options = BitmapFactory.Options().apply {
            inSampleSize = sample; inPreferredConfig = Bitmap.Config.ARGB_8888; inScaled = false
        }
        val bitmap = BitmapFactory.decodeByteArray(jpeg, 0, jpeg.size, options) ?: return null
        try {
            if (maxOf(bitmap.width, bitmap.height) > AnchorHeadroomPolicy.MAX_SIDE ||
                bitmap.width.toLong() * bitmap.height > AnchorHeadroomPolicy.MAX_PIXELS) return null
            val pixels = IntArray(bitmap.width * bitmap.height)
            bitmap.getPixels(pixels, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
            return AnchorHeadroomPolicy.statistics(pixels)?.let { Result(bounds.outWidth, bounds.outHeight, it) }
        } finally { bitmap.recycle() }
    }
}
