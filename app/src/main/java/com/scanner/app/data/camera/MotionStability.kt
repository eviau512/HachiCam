package com.scanner.app.data.camera

data class PreviewMotion(val distance: Double, val confidence: Double)

/** Time-based hysteresis for image motion; document corners are deliberately absent. */
class MotionStability {
    private var previousTime: Long? = null
    private var lastReliableTime: Long? = null
    private var quietMs = 0L
    private var movingMs = 0L
    private var observations = 0
    var isStable = false
        private set

    fun reset() {
        previousTime = null
        lastReliableTime = null
        quietMs = 0
        movingMs = 0
        observations = 0
        isStable = false
    }

    fun update(timeMs: Long, motion: PreviewMotion?): Boolean {
        val previous = previousTime
        val dt = if (previous == null) 0 else timeMs - previous
        if (previous == null || dt !in 1..250) {
            reset()
            previousTime = timeMs
            return false
        }
        previousTime = timeMs
        if (motion == null || !motion.distance.isFinite() || motion.distance < 0 ||
            !motion.confidence.isFinite() || motion.confidence < .55) {
            // One uncertain frame must not make the pill flash, but stale evidence
            // must never keep reporting steady after tracking has been lost.
            if (lastReliableTime == null || timeMs - lastReliableTime!! > 150) {
                quietMs = 0
                observations = 0
                movingMs = 0
                isStable = false
            }
            return isStable
        }
        lastReliableTime = timeMs
        val speed = motion.distance * 1000.0 / dt
        if (speed >= .25) {
            quietMs = 0
            movingMs = 0
            observations = 0
            isStable = false
            return false
        }
        if (isStable) {
            movingMs = if (speed > .08) movingMs + dt else 0
            if (movingMs >= 100) {
                isStable = false
                quietMs = 0
                observations = 0
                movingMs = 0
            }
        } else {
            if (speed <= .04) {
                quietMs += minOf(dt, 120)
                observations++
            } else {
                quietMs = maxOf(0, quietMs - dt)
                observations = maxOf(0, observations - 1)
            }
            if (quietMs >= 350 && observations >= 4) isStable = true
        }
        return isStable
    }
}
