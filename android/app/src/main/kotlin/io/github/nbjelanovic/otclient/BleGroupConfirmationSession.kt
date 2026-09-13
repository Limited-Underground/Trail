package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionConfigurationCodec
import io.github.nbjelanovic.otprotocol.CompanionConfigurationFrame
import io.github.nbjelanovic.otprotocol.CompanionConfirmationCodec as Wire
import io.github.nbjelanovic.otprotocol.CompanionConfirmationPayload

/** One explicitly negotiated evaluation session; this object never establishes BLE authority. */
internal class BleGroupConfirmationSession(
    private val authority: V1GroupConfirmationAuthority,
    private val sessionNonce: UInt,
    private val isCurrent: () -> Boolean,
    private val allocate: () -> UInt?,
    private val send: (ByteArray) -> Boolean,
    private val clockMillis: () -> Long,
) : V1GroupConfirmationSource {
    private var observer: (() -> Unit)? = null
    private var pending: UInt? = null
    private var pendingOp = 0
    private var readStarted = 0L
    private var lastClock: Long? = null
    private var descriptor: CompanionConfirmationPayload? = null
    private var offer: V1GroupConfirmationOffer? = null
    private var delivered = false
    private var decision: V1GroupConfirmationDecision? = null
    private var result: V1GroupConfirmationResult? = null
    private var unavailable = false
    private var failed = false
    private var fenced = false
    private var sourceClosed = false
    private var ended = false
    val busy: Boolean get() = pending != null || fenced
    fun pendingExchange(): UInt? = pending

    override fun pollOffer(): V1GroupConfirmationOffer? {
        checkSource()
        val current = offer
        if (current != null) {
            if (now() >= current.deadlineMillis) fail()
            checkSource()
            if (delivered) return null
            delivered = true
            return current
        }
        if (pending == null && decision == null) {
            val started = now()
            if (started > Long.MAX_VALUE - 60_000L) fail()
            checkSource()
            val id = allocate() ?: return null
            readStarted = started
            transmit(id, Wire.read())
        }
        checkSource()
        return null
    }

    override fun submit(expected: V1GroupConfirmationOffer, selected: V1GroupConfirmationDecision): Boolean {
        checkSource()
        if (!delivered || expected !== offer || decision != null || busy || now() >= expected.deadlineMillis) return false
        checkSource()
        val id = allocate() ?: return false
        decision = selected
        val operation = if (selected == V1GroupConfirmationDecision.CONFIRM) Wire.CONFIRM else Wire.CANCEL
        return transmit(id, checkNotNull(checkNotNull(descriptor).withOperation(operation)))
    }

    override fun pollResult(): V1GroupConfirmationResult? {
        checkSource()
        return result.also { result = null }
    }

    override fun setChangedObserver(observer: (() -> Unit)?) {
        if (sourceClosed || ended) return
        this.observer = observer
        if (observer != null && (offer != null || result != null || failed || unavailable)) notifyChanged()
    }

    private fun transmit(id: UInt, payload: CompanionConfirmationPayload): Boolean {
        pending = id
        pendingOp = payload.op
        val bytes = checkNotNull(CompanionConfigurationCodec.encodeFrame(
            CompanionConfigurationFrame(Wire.REQUEST, sessionNonce, id, payload.encoded(), Wire.PROFILE)))
        val sent = try { send(bytes) } catch (_: Exception) { false }
        // A synchronous exact result is authoritative even if the platform then reports false.
        if (!sent && pending == id) fail()
        return (sent || pending != id) && !failed && !ended && !sourceClosed
    }

    /** Returns false only for a result outside the exact currently pending exchange. */
    fun receive(frame: CompanionConfigurationFrame): Boolean {
        if (ended || !isCurrent() || frame.minorVersion != Wire.PROFILE || frame.kind != Wire.RESPONSE ||
            frame.sessionNonce != sessionNonce || frame.exchangeId != pending) return false
        val payload = Wire.decode(frame.payload)
        val currentOp = pendingOp
        pending = null
        if (payload == null) { fail(); return true }
        val received = now()
        if (failed) return true
        if (currentOp == Wire.READ) {
            if (payload.op == Wire.RESULT && payload.status == Wire.WAITING) return true
            if (payload.op == Wire.RESULT && payload.status == Wire.UNAVAILABLE) {
                unavailable = true
                notifyChanged()
                return true
            }
            if (payload.op != Wire.OFFER || payload.sessionNonce != sessionNonce ||
                received < readStarted || received - readStarted >= payload.remainingMillis) {
                fail(); return true
            }
            descriptor = payload
            offer = V1GroupConfirmationOffer(authority, payload.attempt,
                if (payload.role == 1) V1GroupConfirmationRole.INVITER else V1GroupConfirmationRole.JOINER,
                payload.group.toString(16).padStart(16, '0'), payload.epoch, hex(payload.peer),
                hex(payload.transcript), readStarted + payload.remainingMillis)
        } else {
            val expected = descriptor
            val offered = offer
            val selected = decision
            if (payload.op != Wire.RESULT || expected == null || offered == null || selected == null ||
                !expected.sameDescriptor(payload) || received >= offered.deadlineMillis ||
                !(payload.status == Wire.REFUSED || payload.status == Wire.LOCAL_CONFIRMED && currentOp == Wire.CONFIRM ||
                    payload.status == Wire.CANCELLED && currentOp == Wire.CANCEL)) {
                fail(); return true
            }
            result = V1GroupConfirmationResult(offered, selected, when (payload.status) {
                Wire.LOCAL_CONFIRMED -> V1GroupConfirmationDeviceOutcome.LOCAL_CONFIRMED
                Wire.CANCELLED -> V1GroupConfirmationDeviceOutcome.CANCELLED
                else -> V1GroupConfirmationDeviceOutcome.REFUSED
            })
        }
        notifyChanged()
        return true
    }

    fun lost(exchange: UInt) { if (pending == exchange) { pending = null; fail() } }

    /** Local UI closure cannot release an uncertain shared operation slot. */
    override fun close() {
        sourceClosed = true
        observer = null
        result = null
        if (pending == null) offer = null
    }

    fun endSession() {
        ended = true
        close()
        pending = null
        fenced = false
        offer = null
        descriptor = null
    }

    private fun checkSource() {
        if (sourceClosed || ended || failed || unavailable || !isCurrent())
            throw IllegalStateException("Device confirmation unavailable")
    }
    private fun now(): Long {
        val value = try { clockMillis() } catch (_: Exception) { -1L }
        if (value < 0 || value == Long.MAX_VALUE || lastClock?.let { value < it } == true) fail()
        else lastClock = value
        return value
    }
    private fun fail() { failed = true; fenced = true; notifyChanged() }
    private fun notifyChanged() {
        if (sourceClosed || ended) return
        try { observer?.invoke() } catch (_: Exception) { observer = null }
    }
    private fun hex(bytes: ByteArray): String = bytes.joinToString("") { (it.toInt() and 255).toString(16).padStart(2, '0') }
}

class RuntimeV1GroupConfirmationAdapter(private val runtime: BleCompanionRuntime) : V1GroupConfirmationAdapter {
    override fun open(authority: V1GroupConfirmationAuthority): V1GroupConfirmationSource? =
        runtime.createGroupConfirmationSource(authority)
}

/** Implemented only by the V1-Test Application, never normal/release composition. */
interface EvaluationGroupConfirmationOptIn
