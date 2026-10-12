package com.scanner.app.data.repository

import com.scanner.app.domain.model.ScannedPage
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import java.util.Collections

object PageRepository {
    private val _pages = MutableStateFlow<List<ScannedPage>>(emptyList())
    val pages: StateFlow<List<ScannedPage>> = _pages.asStateFlow()

    fun addPage(page: ScannedPage) {
        _pages.update { it + page }
    }

    fun getPage(id: String): ScannedPage? {
        return _pages.value.find { it.id == id }
    }

    fun updatePage(page: ScannedPage) {
        _pages.update { pages -> pages.map { if (it.id == page.id) page else it } }
    }

    fun updatePage(id: String, transform: (ScannedPage) -> ScannedPage) {
        _pages.update { pages -> pages.map { if (it.id == id) transform(it) else it } }
    }

    fun deletePage(id: String) {
        _pages.update { pages -> pages.filter { it.id != id } }
    }

    fun reorderPages(fromIndex: Int, toIndex: Int) {
        _pages.update { pages ->
            val list = pages.toMutableList()
            if (fromIndex in list.indices && toIndex in list.indices) {
                val page = list.removeAt(fromIndex)
                list.add(toIndex, page)
            }
            list
        }
    }

    fun clear() {
        _pages.value = emptyList()
    }
}
