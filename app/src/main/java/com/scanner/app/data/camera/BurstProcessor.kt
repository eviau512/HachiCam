package com.scanner.app.data.camera

import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch

/** One decoder/fusion worker for the whole app, independent of screen navigation. */
object BurstProcessor {
    private val admission = BurstAdmission()
    private data class Work(val ticket: BurstAdmission.Ticket, val action: suspend () -> Unit)
    private val queue = Channel<Work>(Channel.UNLIMITED)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    init {
        scope.launch {
            for (work in queue) {
                try { work.action() }
                catch (error: Exception) { Log.e("HachiCam-Burst", "Worker failed", error) }
                finally { admission.release(work.ticket) }
            }
        }
    }

    fun reserve(exclusive: Boolean = false) = admission.reserve(exclusive)
    fun cancelReservation(ticket: BurstAdmission.Ticket) = admission.release(ticket)
    fun submit(ticket: BurstAdmission.Ticket, work: suspend () -> Unit) { check(queue.trySend(Work(ticket, work)).isSuccess) }
}
