package com.scanner.app.data.camera

import android.util.Log
import com.scanner.app.data.image.ImageWork
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch

/** One camera fusion worker; fatal termination closes admission and drains waiting jobs. */
object BurstProcessor {
    private val admission = BurstAdmission()
    private val guard = Any()
    private var healthy = true
    private data class Work(val ticket: BurstAdmission.Ticket, val failure: (Throwable) -> Unit, val action: suspend () -> Unit)
    private val queue = Channel<Work>(3)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    val isHealthy: Boolean get() = synchronized(guard) { healthy }
    private fun release(ticket: BurstAdmission.Ticket) {
        if (admission.release(ticket)) ImageWork.encodedReserved.addAndGet(-CapturePolicy.ENCODED_LIMIT)
    }
    private fun failed(work: Work, error: Throwable) { runCatching { work.failure(error) }; release(work.ticket) }
    init {
        scope.launch {
            var termination: Throwable? = null
            try {
                for (work in queue) {
                    try { ImageWork.run { work.action() } }
                    catch (error: Exception) { Log.e("HachiCam-Burst", "Worker operation failed", error); runCatching { work.failure(error) } }
                    catch (fatal: Throwable) { runCatching { work.failure(fatal) }; throw fatal }
                    finally { release(work.ticket) }
                }
            } catch (error: Throwable) { termination = error; Log.e("HachiCam-Burst", "Worker stopped; capture admission closed", error) }
            finally {
                synchronized(guard) { healthy = false; queue.close() }
                while (true) {
                    val waiting = queue.tryReceive().getOrNull() ?: break
                    failed(waiting, termination ?: IllegalStateException("Image worker stopped"))
                }
            }
        }
    }
    fun reserve(exclusive: Boolean = false): BurstAdmission.Ticket? = synchronized(guard) {
        if (!healthy) null else admission.reserve(exclusive)?.also { ImageWork.encodedReserved.addAndGet(CapturePolicy.ENCODED_LIMIT) }
    }
    fun cancelReservation(ticket: BurstAdmission.Ticket) = release(ticket)
    fun submit(ticket: BurstAdmission.Ticket, failure: (Throwable) -> Unit = {}, work: suspend () -> Unit) = synchronized(guard) {
        if (!healthy || !queue.trySend(Work(ticket, failure, work)).isSuccess) {
            val error = IllegalStateException("Image worker unavailable")
            runCatching { failure(error) }
            throw error
        }
    }
}
