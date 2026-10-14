package com.scanner.app.data.image

import java.io.File

object AtomicImageCommit {
    fun install(temporary: File, result: File) {
        require(temporary.canonicalFile.parentFile == result.canonicalFile.parentFile)
        check(temporary.isFile && temporary.length() > 0) { "Encoded image is missing" }
        check(!result.exists()) { "Result revision already exists" }
        check(temporary.renameTo(result)) { "Image commit failed" }
    }
}
