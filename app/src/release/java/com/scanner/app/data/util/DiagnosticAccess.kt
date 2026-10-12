package com.scanner.app.data.util

/** Release has no root probe or command implementation, including coroutine lambdas. */
object DiagnosticAccess {
    val rootAvailable: Boolean = false
    fun initialize() = Unit
    @Suppress("UNUSED_PARAMETER")
    fun command(args: List<String>, timeoutSeconds: Long = 5): String = "(disabled in release)"
}
