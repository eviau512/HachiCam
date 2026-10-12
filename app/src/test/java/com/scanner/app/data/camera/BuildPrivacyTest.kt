package com.scanner.app.data.camera

import com.scanner.app.BuildConfig
import org.junit.Assert.*
import org.junit.Test

class BuildPrivacyTest {
    @Test fun sensitiveDiagnosticsAreOnlyEnabledInTestBuilds() {
        assertEquals(BuildConfig.DEBUG, BuildConfig.SENSITIVE_DIAGNOSTICS)
        assertEquals(BuildConfig.DEBUG, BuildConfig.APPLICATION_ID.endsWith(".test"))
    }
}
