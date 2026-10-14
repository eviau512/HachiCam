package com.scanner.app

import android.app.Application
import android.content.Context
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.LocaleListCompat
import org.opencv.android.OpenCVLoader
import java.util.Locale

class ScannerApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        // Initialize OpenCV
        OpenCVLoader.initLocal()
        com.scanner.app.data.image.ImageWork.initialize(this)
        com.scanner.app.data.util.DiagnosticAccess.initialize()
        val warning=java.util.concurrent.atomic.AtomicBoolean(false)
        com.scanner.app.data.repository.PageRepository.initialize(
            java.io.File(filesDir,"page_state"),java.io.File(filesDir,"scans"),{ file ->
                runCatching {
                    if(!file.isFile || file.length()<4)return@runCatching false
                    java.io.RandomAccessFile(file,"r").use { f ->
                        if(f.readUnsignedShort()!=0xffd8)return@runCatching false
                        f.seek(f.length()-2);if(f.readUnsignedShort()!=0xffd9)return@runCatching false
                    }
                    val bounds=android.graphics.BitmapFactory.Options().apply { inJustDecodeBounds=true }
                    android.graphics.BitmapFactory.decodeFile(file.absolutePath,bounds)
                    bounds.outWidth>0 && bounds.outHeight>0 && bounds.outWidth.toLong()*bounds.outHeight<=56_000_000
                }.getOrDefault(false)
            },{
                if(warning.compareAndSet(false,true))android.os.Handler(android.os.Looper.getMainLooper()).post {
                    android.widget.Toast.makeText(this,com.scanner.app.R.string.page_recovery_warning,android.widget.Toast.LENGTH_LONG).show()
                }
            })

        // Initialize saved language if configured
        val prefs = getSharedPreferences("settings", Context.MODE_PRIVATE)
        val langCode = prefs.getString("language_code", null)
        if (!langCode.isNullOrEmpty()) {
            val tag = if (langCode == "zh") "zh-CN" else langCode
            val locale = if (langCode == "zh") Locale.SIMPLIFIED_CHINESE else Locale(langCode)
            Locale.setDefault(locale)
            if (AppCompatDelegate.getApplicationLocales().isEmpty) {
                AppCompatDelegate.setApplicationLocales(LocaleListCompat.forLanguageTags(tag))
            }
        }

        // Clean legacy orphaned temp files in cacheDir on launch
        try {
            cacheDir.listFiles()?.forEach { file ->
                if (file.isFile && (file.name.startsWith("hdr_") || file.name.endsWith(".jpg"))) {
                    file.delete()
                }
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }
}
