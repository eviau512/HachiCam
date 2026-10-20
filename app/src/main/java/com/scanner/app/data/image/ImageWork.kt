package com.scanner.app.data.image

import android.app.ActivityManager
import android.content.Context
import android.os.Process
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.util.concurrent.atomic.AtomicLong

class ImageMemoryUnavailableException : IllegalStateException("Image memory budget unavailable")

/** One owner of large decode/native/save allocations; queued JPEGs reserve separately. */
object ImageWork {
    private val mutex = Mutex()
    private lateinit var app: Context
    val encodedReserved = AtomicLong(0)
    val retained = AtomicLong(0)
    fun initialize(context: Context) { app = context.applicationContext }
    fun budget(): Long {
        val info = ActivityManager.MemoryInfo()
        (app.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager).getMemoryInfo(info)
        return if (info.lowMemory) 0 else ImageMemoryPolicy.budget(info.availMem, Process.is64Bit())
    }
    fun reserved(): Long = encodedReserved.get() + retained.get()
    fun check(bytes: Long) {
        if (bytes <= 0 || bytes > (budget() - reserved()).coerceAtLeast(0)) throw ImageMemoryUnavailableException()
    }
    suspend fun <T> run(bytes: Long? = null, block: suspend () -> T): T = mutex.withLock {
        currentCoroutineContext().ensureActive()
        bytes?.let(::check)
        block()
    }
}
