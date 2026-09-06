package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.*
import kotlin.test.*

class V1NameTransactionTest {
    private fun label() = assertNotNull(V1SetupLabel.create("Trail-7K3P9Q"))
    private fun token(byte: Byte = 5) = assertNotNull(V1SetupSessionToken.create(ByteArray(16) { byte }))
    private fun name(text: String = "Camp") = assertNotNull(V1DeviceName.create(text))
    private fun wire(value: CompanionNamePayload) = assertNotNull(CompanionNamePayloadCodec.encode(value))
    private inner class Harness(first: UInt = 1u) {
        var authority = V1NameAuthority(V1NamePhase.READY, V1NameContext(1,2,3,4,5,6,7u,label(),token()))
        val context get() = assertNotNull(authority.context)
        val transaction = V1NameTransaction(V1NameAuthoritySource { authority }, first)
        fun read(revision: ULong = 0u, text: String = "") {
            val request = assertNotNull(transaction.beginRead())
            assertEquals(V1NameResultCode.SNAPSHOT, transaction.receive(context, request.exchangeId,
                wire(CompanionNamePayload(CompanionNameKind.SNAPSHOT,revision,text))).code)
        }
        fun write(text: String = "Camp", revision: ULong = 0u): V1NameRequest {
            read(revision, if (revision == 0uL) "" else text)
            return assertNotNull(transaction.beginWrite(name(text),revision))
        }
        fun applied(request: V1NameRequest): V1NameResult = transaction.receive(context,request.exchangeId,
            wire(CompanionNamePayload(CompanionNameKind.APPLIED, request.payload.revision+1u,request.payload.name)))
        fun progress(): V1SetupProgress = assertNotNull(V1SetupProgress.empty().matchDevice(label(),token())
            .authorize(V1AuthorizationReceipt(label(),token())))
    }

    @Test fun initialReadIsRequiredAndDraftCannotSupplyAuthority() {
        val h=Harness()
        val restored=assertNotNull(V1SetupDraftCodec.decode(V1SetupDraftCodec.encode(assertNotNull(V1SetupDraft.create("Camp")))))
        assertNull(h.transaction.beginWrite(assertNotNull(restored.deviceName),0u))
        assertNull(h.progress().deviceName)
        h.read()
        assertNotNull(h.transaction.beginWrite(name(),0u))
        assertNull(h.transaction.beginRead())
        assertNull(h.transaction.beginWrite(name(),0u))
    }

    @Test fun everyContextFieldAndExchangeMustMatchBeforeReceipt() {
        for (field in 0..8) {
            val h=Harness(); val request=h.write(); val c=h.context
            val wrong=when(field) {
                0->c.copy(device=c.device+1); 1->c.copy(runtime=c.runtime+1)
                2->c.copy(owner=c.owner+1); 3->c.copy(ownerGeneration=c.ownerGeneration+1)
                4->c.copy(transportGeneration=c.transportGeneration+1); 5->c.copy(controller=c.controller+1)
                6->c.copy(sessionNonce=c.sessionNonce+1u)
                7->c.copy(setupLabel=assertNotNull(V1SetupLabel.create("Trail-8M4R2T")))
                else->c.copy(sessionToken=token(6))
            }
            val result=h.transaction.receive(wrong,request.exchangeId,wire(CompanionNamePayload(CompanionNameKind.APPLIED,1u,"Camp")))
            assertNull(result.receipt)
            assertEquals(V1NameResultCode.APPLIED,h.applied(request).code)
        }
        val h=Harness(); val request=h.write()
        assertNull(h.transaction.receive(h.context,request.exchangeId+1u,
            wire(CompanionNamePayload(CompanionNameKind.APPLIED,1u,"Camp"))).receipt)
        assertNotNull(h.applied(request).receipt)
    }

    @Test fun exactNameAndCommittedRevisionAreRequired() {
        for (payload in listOf(CompanionNamePayload(CompanionNameKind.APPLIED,2u,"Camp"),
            CompanionNamePayload(CompanionNameKind.APPLIED,1u,"Other"),
            CompanionNamePayload(CompanionNameKind.SNAPSHOT,1u,"Camp"))) {
            val h=Harness();val request=h.write()
            assertNull(h.transaction.receive(h.context,request.exchangeId,wire(payload)).receipt)
        }
        val h=Harness();h.read(4u,"Camp")
        assertNull(h.transaction.beginWrite(name(),3u))
        assertNotNull(h.transaction.beginWrite(name(),4u))
    }

    @Test fun receiptIsConsumedOnceEvenAgainstOldImmutableSetupSnapshot() {
        val h=Harness();val request=h.write();val receipt=assertNotNull(h.applied(request).receipt)
        val old=h.progress()
        assertNotNull(old.nameDevice(name(),receipt))
        assertNull(old.nameDevice(name(),receipt))
        assertNull(h.progress().nameDevice(name(),receipt))
        assertNull(h.applied(request).receipt)
    }

    @Test fun newRequestInvalidatesUnconsumedReceiptAndOldSameSessionSuccess() {
        val h=Harness();val oldRequest=h.write();val oldReceipt=assertNotNull(h.applied(oldRequest).receipt)
        val next=assertNotNull(h.transaction.beginRead())
        assertNull(h.progress().nameDevice(name(),oldReceipt))
        assertNull(h.applied(oldRequest).receipt)
        assertEquals(V1NameResultCode.SNAPSHOT,h.transaction.receive(h.context,next.exchangeId,
            wire(CompanionNamePayload(CompanionNameKind.SNAPSHOT,1u,"Camp"))).code)
        val newWrite=assertNotNull(h.transaction.beginWrite(name(),1u))
        assertNull(h.applied(oldRequest).receipt)
        assertNotNull(h.applied(newWrite).receipt)
    }

    @Test fun lostAndUncertainWritesRequireFreshReadNotBlindRetry() {
        for (uncertain in listOf(false,true)) {
            val h=Harness();val request=h.write()
            if(uncertain) assertEquals(V1NameResultCode.UNCERTAIN,h.transaction.receive(h.context,request.exchangeId,
                wire(CompanionNamePayload(CompanionNameKind.UNCERTAIN,reason=CompanionNameReason.STORAGE_FAILURE))).code)
            else h.transaction.lostResult(h.context,request.exchangeId)
            assertTrue(h.transaction.reconciliationRequired)
            assertNull(h.transaction.beginWrite(name(),0u))
            assertNull(h.applied(request).receipt)
            h.read(1u,"Camp")
            assertFalse(h.transaction.reconciliationRequired)
            assertNotNull(h.transaction.beginWrite(name(),1u))
        }
    }

    @Test fun snapshotConfirmsCurrentStateWithoutAttributingHistoricalWrite() {
        val h=Harness();val request=h.write();h.transaction.lostResult(h.context,request.exchangeId)
        val read=assertNotNull(h.transaction.beginRead())
        val result=h.transaction.receive(h.context,read.exchangeId,wire(CompanionNamePayload(CompanionNameKind.SNAPSHOT,1u,"Camp")))
        assertNotNull(result.snapshot);assertNull(result.receipt)
        assertNull(h.applied(request).receipt)
        assertNull(h.progress().deviceName)
    }

    @Test fun lifecycleLossInvalidatesReceiptsAndStaleCallbacksCannotTouchReplacement() {
        for(event in V1NameLifecycle.entries) {
            val h=Harness();val request=h.write();val receipt=assertNotNull(h.applied(request).receipt);val old=h.context
            assertTrue(h.transaction.lifecycle(old,event))
            assertNull(h.progress().nameDevice(name(),receipt))
            assertNull(h.transaction.beginRead())
            h.authority=V1NameAuthority(V1NamePhase.READY,old.copy(transportGeneration=old.transportGeneration+1))
            if(event!=V1NameLifecycle.DISCONNECTED) assertNull(h.transaction.beginRead())
            h.authority=V1NameAuthority(V1NamePhase.READY,old.copy(ownerGeneration=old.ownerGeneration+1,transportGeneration=old.transportGeneration+1))
            val fresh=assertNotNull(h.transaction.beginRead())
            assertFalse(h.transaction.lifecycle(old,event))
            assertEquals(V1NameResultCode.SNAPSHOT,h.transaction.receive(h.context,fresh.exchangeId,
                wire(CompanionNamePayload(CompanionNameKind.SNAPSHOT))).code)
        }
    }

    @Test fun passivePartialReadyLossCannotResurrectOldSession() {
        for(phase in listOf(V1NamePhase.CONNECTED,V1NamePhase.DISCONNECTED,V1NamePhase.UNAVAILABLE)) {
            val h=Harness();val request=h.write();val old=h.context
            h.authority=V1NameAuthority(phase,old.copy(transportGeneration=0))
            h.transaction.observe()
            h.authority=V1NameAuthority(V1NamePhase.READY,old)
            assertNull(h.applied(request).receipt);assertNull(h.transaction.beginRead())
            h.authority=V1NameAuthority(V1NamePhase.READY,old.copy(transportGeneration=old.transportGeneration+1))
            assertNotNull(h.transaction.beginRead())
        }
    }

    @Test fun unsignedRevisionAndExchangeExhaustionNeverWrap() {
        val h=Harness();h.read(ULong.MAX_VALUE,"Camp")
        assertNull(h.transaction.beginWrite(name(),ULong.MAX_VALUE))
        val last=Harness(UInt.MAX_VALUE);val request=assertNotNull(last.transaction.beginRead())
        assertEquals(UInt.MAX_VALUE,request.exchangeId)
        last.transaction.receive(last.context,request.exchangeId,wire(CompanionNamePayload(CompanionNameKind.SNAPSHOT)))
        assertNull(last.transaction.beginRead())
        assertNull(Harness(0u).transaction.beginRead())
        val maximum=Harness();val write=maximum.write(revision=ULong.MAX_VALUE-1u)
        assertNotNull(maximum.applied(write).receipt)
        assertNull(maximum.transaction.beginWrite(name(),ULong.MAX_VALUE))
    }

    @Test fun nameTypeRejectsSurrogatesAndPreservesExactUnicodeAtBounds() {
        for(text in listOf("\uD800","\uDC00","a\uD800b","x".repeat(33),"😀".repeat(17),"界".repeat(33)," x","x ","a\u0080")) {
            assertNull(V1DeviceName.create(text),text)
        }
        for(text in listOf("界".repeat(32),"😀".repeat(16),"e\u0301","é")) {
            assertEquals(text,name(text).value)
            assertNotNull(CompanionNamePayloadCodec.encode(CompanionNamePayload(CompanionNameKind.WRITE,name=text)))
        }
    }

    @Test fun requestsOwnBytesAndDiagnosticStringsRedactValues() {
        val h=Harness();val request=h.write("PrivateName");val bytes=request.encodedPayload();bytes.fill(0)
        assertNotEquals(0,request.encodedPayload()[0].toInt())
        val result=h.applied(request);val receipt=assertNotNull(result.receipt)
        for(value in listOf(request,h.context,result,receipt)) {
            assertFalse(value.toString().contains("PrivateName"));assertFalse(value.toString().contains("7K3P9Q"))
        }
    }

    @Test fun constructedButUnregisteredReceiptCannotAdvanceSetup() {
        val h=Harness();val request=h.write();val real=assertNotNull(h.applied(request).receipt)
        assertNull(h.progress().nameDevice(name(),V1NameReadbackReceipt(h.transaction)))
        assertNotNull(h.progress().nameDevice(name(),real))
    }

    @Test fun matchedMalformedResponseAndRejectionRequireReadAgain() {
        for(bytes in listOf(byteArrayOf(0),wire(CompanionNamePayload(CompanionNameKind.REJECTED,
            reason=CompanionNameReason.STALE_REVISION)))) {
            val h=Harness();val request=h.write()
            assertNull(h.transaction.receive(h.context,request.exchangeId,bytes).receipt)
            assertTrue(h.transaction.reconciliationRequired)
            assertNull(h.transaction.beginWrite(name(),0u))
            assertNull(h.applied(request).receipt)
            assertNotNull(h.transaction.beginRead())
        }
    }

    @Test fun staleTimeoutCannotCancelNewRequestInSameOrReplacementSession() {
        for(reconnect in listOf(false,true)) {
            val h=Harness();val old=h.write();val oldContext=h.context
            assertTrue(h.transaction.lostResult(oldContext,old.exchangeId))
            if(reconnect) h.authority=V1NameAuthority(V1NamePhase.READY,oldContext.copy(transportGeneration=oldContext.transportGeneration+1))
            val fresh=assertNotNull(h.transaction.beginRead())
            assertFalse(h.transaction.lostResult(oldContext,old.exchangeId))
            assertEquals(V1NameResultCode.SNAPSHOT,h.transaction.receive(h.context,fresh.exchangeId,
                wire(CompanionNamePayload(CompanionNameKind.SNAPSHOT))).code)
        }
    }

    @Test fun baselineCannotCrossAuthorityChangeDuringAdmission() {
        val a=V1NameContext(1,2,3,4,5,6,7u,label(),token());val b=a.copy(transportGeneration=9)
        var switched=false;var calls=0
        val transaction=V1NameTransaction(V1NameAuthoritySource {
            calls++
            V1NameAuthority(V1NamePhase.READY,if(switched && calls>1)b else a)
        })
        val read=assertNotNull(transaction.beginRead())
        transaction.receive(a,read.exchangeId,wire(CompanionNamePayload(CompanionNameKind.SNAPSHOT)))
        switched=true;calls=0
        val write=transaction.beginWrite(name(),0u)
        if(write!=null) {
            assertEquals(a,write.context)
            assertNull(transaction.receive(a,write.exchangeId,wire(CompanionNamePayload(CompanionNameKind.APPLIED,1u,"Camp"))).receipt)
        }
        assertNull(transaction.beginWrite(name(),0u))
        assertTrue(transaction.reconciliationRequired)
    }

    @Test fun malformedReadyCannotUnblockRevokedOwner() {
        val h=Harness();h.read();val old=h.context
        assertTrue(h.transaction.lifecycle(old,V1NameLifecycle.REVOKED))
        h.authority=V1NameAuthority(V1NamePhase.READY,old.copy(ownerGeneration=old.ownerGeneration+1,transportGeneration=0))
        assertFalse(h.transaction.observe())
        h.authority=V1NameAuthority(V1NamePhase.READY,old)
        assertNull(h.transaction.beginRead())
    }

    @Test fun sourceExceptionInvalidatesReceiptAndCannotReviveOldReady() {
        val context=V1NameContext(1,2,3,4,5,6,7u,label(),token());var fail=false
        val transaction=V1NameTransaction(V1NameAuthoritySource {
            if(fail) error("simulated unavailable authority")
            V1NameAuthority(V1NamePhase.READY,context)
        })
        val read=assertNotNull(transaction.beginRead())
        transaction.receive(context,read.exchangeId,wire(CompanionNamePayload(CompanionNameKind.SNAPSHOT)))
        val write=assertNotNull(transaction.beginWrite(name(),0u))
        val receipt=assertNotNull(transaction.receive(context,write.exchangeId,wire(CompanionNamePayload(CompanionNameKind.APPLIED,1u,"Camp"))).receipt)
        fail=true;assertFalse(transaction.observe());fail=false
        val progress=assertNotNull(V1SetupProgress.empty().matchDevice(label(),token()).authorize(V1AuthorizationReceipt(label(),token())))
        assertNull(progress.nameDevice(name(),receipt));assertNull(transaction.beginRead())
    }

    @Test fun setupMetadataChangeCannotRestartProtocolExchangeSequence() {
        for(changeLabel in listOf(false,true)) {
            val h=Harness();val first=assertNotNull(h.transaction.beginRead());val old=h.context
            h.authority=V1NameAuthority(V1NamePhase.READY,if(changeLabel)
                old.copy(setupLabel=assertNotNull(V1SetupLabel.create("Trail-8M4R2T"))) else old.copy(sessionToken=token(6)))
            val next=assertNotNull(h.transaction.beginRead())
            assertEquals(first.exchangeId+1u,next.exchangeId)
            assertNull(h.transaction.receive(old,first.exchangeId,wire(CompanionNamePayload(CompanionNameKind.SNAPSHOT))).receipt)
        }
        val h=Harness();h.read();val old=h.context
        assertTrue(h.transaction.lifecycle(old,V1NameLifecycle.DISCONNECTED))
        h.authority=V1NameAuthority(V1NamePhase.READY,old.copy(setupLabel=assertNotNull(V1SetupLabel.create("Trail-8M4R2T"))))
        assertNull(h.transaction.beginRead())
    }
}
