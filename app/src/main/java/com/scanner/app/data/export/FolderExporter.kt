package com.scanner.app.data.export

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Environment
import android.provider.MediaStore
import com.scanner.app.domain.model.ExportConfig
import com.scanner.app.domain.model.ScannedPage
import java.io.File
import java.util.Locale

class FolderExporter(private val context: Context) {
    fun export(pages: List<ScannedPage>, config: ExportConfig, checkpoint: () -> Unit = {}): ExportReceipt {
        require(pages.isNotEmpty())
        val sources = pages.map { File(it.imagePath).also { f -> check(f.isFile && f.length() > 0) { "Source image is missing" } } }
        val resolver = context.contentResolver
        val baseName = config.name.ifBlank { java.text.SimpleDateFormat("yyyyMMdd-HHmm", Locale.US).format(java.util.Date()) }
        require(baseName.none { it == '/' || it == '\\' || it.isISOControl() }) { "Invalid export name" }
        val folder = "${Environment.DIRECTORY_DCIM}/HachiCam"
        val targets = ExportTransaction.execute(pages.size,
            create = { index ->
                val values = ContentValues().apply {
                    put(MediaStore.MediaColumns.DISPLAY_NAME, String.format(Locale.US, "%s-P%03d.jpg", baseName, index + 1))
                    put(MediaStore.MediaColumns.MIME_TYPE, "image/jpeg")
                    put(MediaStore.MediaColumns.RELATIVE_PATH, folder)
                    put(MediaStore.MediaColumns.IS_PENDING, 1)
                }
                checkNotNull(resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values)) { "Cannot create export destination" }
            },
            write = { target: Uri, index ->
                checkNotNull(resolver.openOutputStream(target, "w")) { "Cannot open export destination" }.use { output ->
                    sources[index].inputStream().use { input -> check(input.copyTo(output) == sources[index].length()) { "Incomplete image export" } }
                    output.flush()
                }
            },
            publish = { target ->
                check(resolver.update(target, ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }, null, null) == 1) { "Cannot publish export" }
            },
            rollback = { target -> rollbackOwnedDestination(resolver, target) }, checkpoint = checkpoint)
        return ExportReceipt(folder, targets.map(Uri::toString), pages.size)
    }
}
