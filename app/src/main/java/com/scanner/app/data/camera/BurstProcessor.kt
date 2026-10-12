package com.scanner.app.data.camera

import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Semaphore

/** One decoder/fusion worker for the whole app, independent of screen navigation. */
object BurstProcessor {
    private val slots = Semaphore(3)
    private val queue = Channel<suspend () -> Unit>(Channel.UNLIMITED)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    init {
        scope.launch {
            for (work in queue) {
                try { work() }
                catch (error: Exception) { Log.e("HachiCam-Burst", "Worker failed", error) }
                finally { slots.release() }
            }
        }
    }

    fun reserve(): Boolean = slots.tryAcquire()
    fun cancelReservation() = slots.release()
    fun submit(work: suspend () -> Unit) { check(queue.trySend(work).isSuccess) }
}
