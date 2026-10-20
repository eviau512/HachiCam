package com.scanner.app.data.export

import android.content.ContentValues
import android.content.Context
import android.graphics.BitmapFactory
import android.graphics.pdf.PdfDocument
import android.net.Uri
import android.os.Environment
import android.provider.MediaStore
import com.scanner.app.domain.model.ExportConfig
import com.scanner.app.domain.model.ScannedPage
import java.io.File

class PdfExporter(private val context: Context) {
    fun export(pages: List<ScannedPage>, config: ExportConfig, checkpoint: () -> Unit = {}): ExportReceipt {
        require(pages.isNotEmpty())
        require(config.name.none { it == '/' || it == '\\' || it.isISOControl() }) { "Invalid export name" }
        pages.forEach { check(File(it.imagePath).isFile && File(it.imagePath).length() > 0) { "Source image is missing" } }
        val document = PdfDocument()
        val resolver = context.contentResolver
        val folder = "${Environment.DIRECTORY_DOCUMENTS}/HachiCam"
        val filename = "${config.name.ifBlank { "HachiCam" }}.pdf"
        try {
            pages.forEachIndexed { index, page ->
                checkpoint()
                val bitmap = checkNotNull(BitmapFactory.decodeFile(page.imagePath)) { "PDF page decode failed" }
                try {
                    val info = PdfDocument.PageInfo.Builder(bitmap.width, bitmap.height, index + 1).create()
                    val output = document.startPage(info)
                    try { output.canvas.drawBitmap(bitmap, 0f, 0f, null) }
                    finally { document.finishPage(output) }
                } finally { bitmap.recycle() }
            }
            val targets = ExportTransaction.execute(1,
                create = {
                    val values = ContentValues().apply {
                        put(MediaStore.MediaColumns.DISPLAY_NAME, filename)
                        put(MediaStore.MediaColumns.MIME_TYPE, "application/pdf")
                        put(MediaStore.MediaColumns.RELATIVE_PATH, folder)
                        put(MediaStore.MediaColumns.IS_PENDING, 1)
                    }
                    checkNotNull(resolver.insert(MediaStore.Files.getContentUri("external"), values)) { "Cannot create PDF destination" }
                },
                write = { target: Uri, _ ->
                    checkNotNull(resolver.openOutputStream(target, "w")) { "Cannot open PDF destination" }.use { document.writeTo(it); it.flush() }
                },
                publish = { target ->
                    check(resolver.update(target, ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }, null, null) == 1) { "Cannot publish PDF" }
                }, rollback = { target -> rollbackOwnedDestination(resolver, target) }, checkpoint = checkpoint)
            return ExportReceipt("$folder/$filename", targets.map(Uri::toString), pages.size)
        } finally { document.close() }
    }
}
