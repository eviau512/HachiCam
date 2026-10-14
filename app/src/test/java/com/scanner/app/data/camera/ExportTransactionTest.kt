package com.scanner.app.data.camera

import com.scanner.app.data.export.ExportTransaction
import org.junit.Assert.*
import org.junit.Test

class ExportTransactionTest {
    @Test fun publishStartsOnlyAfterAllWrites() {
        val events = mutableListOf<String>()
        val result = ExportTransaction.execute(3, { it }, { _, i -> events.add("write$i") },
            { events.add("publish$it") }, { fail("rollback on success") })
        assertEquals(listOf(0,1,2),result)
        assertEquals(listOf("write0","write1","write2","publish0","publish1","publish2"),events)
    }
    @Test fun writeFailureRollsBackEveryOwnedDestinationWithoutPublishing() {
        val removed = mutableListOf<Int>()
        try {
            ExportTransaction.execute(3, { it }, { _, i -> if(i==1) error("write failed") },
                { fail("published partial export") }, { removed.add(it) }); fail()
        } catch (_: IllegalStateException) { }
        assertEquals(listOf(1,0),removed)
    }
    @Test fun failedPublicationRollsBackPublishedAndPendingItems() {
        val removed = mutableListOf<Int>()
        try {
            ExportTransaction.execute(3, { it }, { _, _ -> }, { if(it==1) error("publish failed") }, { removed.add(it) }); fail()
        } catch (_: IllegalStateException) { }
        assertEquals(listOf(2,1,0),removed)
    }
    @Test fun rollbackFailureIsReportedAsPossiblePartialExport() {
        try {
            ExportTransaction.execute(2, { it }, { _, _ -> error("write failed") }, {}, { error("delete failed") }); fail()
        } catch (error: IllegalStateException) { assertTrue(error.message!!.contains("could not be removed")) }
    }
    @Test fun cancellationBeforePublicationRollsBackOwnedOutput() {
        val removed=mutableListOf<Int>(); var checkpoints=0
        try {
            ExportTransaction.execute(2,{it},{_,_->},{fail("Canceled export published")},{removed.add(it)},
                { if(++checkpoints==3) throw java.util.concurrent.CancellationException() }); fail()
        } catch (_: java.util.concurrent.CancellationException) { }
        assertEquals(listOf(0),removed)
    }
}
