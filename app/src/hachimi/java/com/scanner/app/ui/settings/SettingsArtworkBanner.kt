package com.scanner.app.ui.settings

import androidx.compose.foundation.Image
import android.graphics.BitmapFactory
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import androidx.compose.ui.unit.dp
import com.scanner.app.R

internal fun LazyListScope.settingsArtworkBanner() {
    item(key = "settings_artwork_banner", contentType = "artwork") {
        BoxWithConstraints(Modifier.fillMaxWidth()) {
            val resources = LocalContext.current.resources
            val width = constraints.maxWidth.coerceIn(1, 4096)
            val bitmap by produceState<ImageBitmap?>(null, resources, width) {
                value = withContext(Dispatchers.IO) {
                    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                    BitmapFactory.decodeResource(resources, R.drawable.settings_character_banner, bounds)
                    var sample = 1
                    while (bounds.outWidth / (sample * 2) >= width) sample *= 2
                    BitmapFactory.decodeResource(resources, R.drawable.settings_character_banner,
                        BitmapFactory.Options().apply { inSampleSize = sample; inScaled = false })?.asImageBitmap()
                }
            }
            val frame = Modifier.fillMaxWidth().aspectRatio(6688f / 3764f).clip(RoundedCornerShape(20.dp))
            bitmap?.let { Image(bitmap = it, contentDescription = null, modifier = frame, contentScale = ContentScale.Fit) }
                ?: Spacer(frame)
        }
    }
}
