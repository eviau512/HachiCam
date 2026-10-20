package com.scanner.app.data.export

import android.content.ContentResolver
import android.net.Uri
import android.provider.MediaStore

internal fun rollbackOwnedDestination(resolver: ContentResolver, target: Uri) {
    if (resolver.delete(target, null, null) > 0) return
    val absent = resolver.query(target, arrayOf(MediaStore.MediaColumns._ID), null, null, null)
        ?.use { !it.moveToFirst() } == true
    check(absent) { "Incomplete export destination could not be removed" }
}
