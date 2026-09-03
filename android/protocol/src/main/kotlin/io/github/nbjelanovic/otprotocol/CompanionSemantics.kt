package io.github.nbjelanovic.otprotocol

const val COMPANION_SNAPSHOT_REQUEST_BYTES = 8
const val COMPANION_STATUS_SNAPSHOT_BYTES = 32
const val COMPANION_ACTION_REQUEST_BYTES = 20
const val COMPANION_ACTION_RESULT_BYTES = 20

enum class CompanionRadioState(val wireValue: Int) {
    UNKNOWN(0), UNAVAILABLE(1), READY(2), DEGRADED(3), FAULT(4);

    companion object { fun fromWire(value: Int) = entries.firstOrNull { it.wireValue == value } }
}

enum class CompanionGnssState(val wireValue: Int) {
    UNKNOWN(0), UNAVAILABLE(1), SEARCHING(2), CURRENT(3), STALE(4), FAULT(5);

    companion object { fun fromWire(value: Int) = entries.firstOrNull { it.wireValue == value } }
}

enum class CompanionPowerState(val wireValue: Int) {
    UNKNOWN(0), EXTERNAL(1), NORMAL(2), LOW(3), CRITICAL(4), FAULT(5);

    companion object { fun fromWire(value: Int) = entries.firstOrNull { it.wireValue == value } }
}

enum class CompanionPositionSharingState(val wireValue: Int) {
    STOPPED(0), WAITING_FOR_FIX(1), ACTIVE(2), DEFERRED(3), FAULT(4);

    companion object { fun fromWire(value: Int) = entries.firstOrNull { it.wireValue == value } }
}

enum class CompanionQuickStatus(val wireValue: Int) {
    OK(1), NEED_ASSISTANCE(2), ANYONE_ONLINE(3), AVAILABLE_TO_HELP(4);

    companion object { fun fromWire(value: Int) = entries.firstOrNull { it.wireValue == value } }
}

enum class CompanionActionKind(val wireValue: Int) {
    QUICK_STATUS(1), ACKNOWLEDGE_CRITICAL_ALERT(2), START_POSITION_SHARING(3), STOP_POSITION_SHARING(4), FACTORY_RESET(5);

    companion object { fun fromWire(value: Int) = entries.firstOrNull { it.wireValue == value } }
}

enum class CompanionActionDisposition(val wireValue: Int) {
    ADMITTED(1), QUEUED(2), REJECTED(3);

    companion object { fun fromWire(value: Int) = entries.firstOrNull { it.wireValue == value } }
}

enum class CompanionActionRejectReason(val wireValue: Int) {
    NONE(0), UNSUPPORTED_ACTION(1), STALE_ALERT(2), UNAVAILABLE(3), QUEUE_FULL(4), POLICY_DENIED(5), INTERNAL_FAILURE(6);

    companion object { fun fromWire(value: Int) = entries.firstOrNull { it.wireValue == value } }
}

enum class CompanionSemanticCodecError {
    NONE,
    MALFORMED,
    UNSUPPORTED_VERSION,
    UNKNOWN_ENUM,
    INVALID_REVISION,
    INVALID_ALERT_ID,
    INVALID_QUICK_STATUS,
    INCOHERENT_ACTION,
    INCOHERENT_RESULT,
    RESERVED_BITS_SET,
    UNSUPPORTED_FRAME_KIND,
}

data class SemanticCodecResult<T>(
    val value: T? = null,
    val error: CompanionSemanticCodecError = CompanionSemanticCodecError.NONE,
) {
    val isSuccess: Boolean get() = error == CompanionSemanticCodecError.NONE && value != null

    companion object {
        fun <T> success(value: T) = SemanticCodecResult(value)
        fun <T> failure(error: CompanionSemanticCodecError) = SemanticCodecResult<T>(error = error)
    }
}

data object CompanionSnapshotRequest

data class CompanionStatusSnapshot(
    val revision: Long,
    val radio: CompanionRadioState,
    val gnss: CompanionGnssState,
    val power: CompanionPowerState,
    val positionSharing: CompanionPositionSharingState,
    val queuedActionCount: Int,
    val pendingCriticalAlertId: ULong = 0uL,
)

data class CompanionActionRequest(
    val kind: CompanionActionKind,
    val quickStatus: CompanionQuickStatus = CompanionQuickStatus.OK,
    val criticalAlertId: ULong = 0uL,
    val factoryResetReceipt: ULong = 0uL,
)

data class CompanionActionResult(
    val kind: CompanionActionKind,
    val quickStatus: CompanionQuickStatus = CompanionQuickStatus.OK,
    val criticalAlertId: ULong = 0uL,
    val factoryResetReceipt: ULong = 0uL,
    val disposition: CompanionActionDisposition,
    val rejectReason: CompanionActionRejectReason = CompanionActionRejectReason.NONE,
)

object CompanionSemanticCodec {
    private val snapshotRequestMagic = byteArrayOf(0x4f, 0x54, 0x58, 0x30)
    private val statusSnapshotMagic = byteArrayOf(0x4f, 0x54, 0x4e, 0x30)
    private val actionRequestMagic = byteArrayOf(0x4f, 0x54, 0x41, 0x30)
    private val actionResultMagic = byteArrayOf(0x4f, 0x54, 0x52, 0x30)

    fun encodeSnapshotRequest(): ByteArray = byteArrayOf(0x4f, 0x54, 0x58, 0x30, 0, 0, 0, 0)

    fun decodeSnapshotRequest(encoded: ByteArray): SemanticCodecResult<CompanionSnapshotRequest> {
        validatePrefix(encoded, snapshotRequestMagic, COMPANION_SNAPSHOT_REQUEST_BYTES)?.let {
            return SemanticCodecResult.failure(it)
        }
        if (encoded[6].toInt() != 0 || encoded[7].toInt() != 0) {
            return SemanticCodecResult.failure(CompanionSemanticCodecError.RESERVED_BITS_SET)
        }
        return SemanticCodecResult.success(CompanionSnapshotRequest)
    }

    fun encodeStatusSnapshot(snapshot: CompanionStatusSnapshot): SemanticCodecResult<ByteArray> {
        validateStatus(snapshot)?.let { return SemanticCodecResult.failure(it) }
        val output = ByteArray(COMPANION_STATUS_SNAPSHOT_BYTES)
        statusSnapshotMagic.copyInto(output)
        output[6] = snapshot.radio.wireValue.toByte()
        output[7] = snapshot.gnss.wireValue.toByte()
        output[8] = snapshot.power.wireValue.toByte()
        output[9] = snapshot.positionSharing.wireValue.toByte()
        writeU16Le(output, 10, snapshot.queuedActionCount)
        writeU32Le(output, 12, snapshot.revision)
        writeU64Le(output, 16, snapshot.pendingCriticalAlertId)
        return SemanticCodecResult.success(output)
    }

    fun decodeStatusSnapshot(encoded: ByteArray): SemanticCodecResult<CompanionStatusSnapshot> {
        validatePrefix(encoded, statusSnapshotMagic, COMPANION_STATUS_SNAPSHOT_BYTES)?.let {
            return SemanticCodecResult.failure(it)
        }
        if ((24 until COMPANION_STATUS_SNAPSHOT_BYTES).any { encoded[it].toInt() != 0 }) {
            return SemanticCodecResult.failure(CompanionSemanticCodecError.RESERVED_BITS_SET)
        }
        val radio = CompanionRadioState.fromWire(encoded.u8(6))
            ?: return SemanticCodecResult.failure(CompanionSemanticCodecError.UNKNOWN_ENUM)
        val gnss = CompanionGnssState.fromWire(encoded.u8(7))
            ?: return SemanticCodecResult.failure(CompanionSemanticCodecError.UNKNOWN_ENUM)
        val power = CompanionPowerState.fromWire(encoded.u8(8))
            ?: return SemanticCodecResult.failure(CompanionSemanticCodecError.UNKNOWN_ENUM)
        val position = CompanionPositionSharingState.fromWire(encoded.u8(9))
            ?: return SemanticCodecResult.failure(CompanionSemanticCodecError.UNKNOWN_ENUM)
        val snapshot = CompanionStatusSnapshot(
            revision = encoded.readU32Le(12),
            radio = radio,
            gnss = gnss,
            power = power,
            positionSharing = position,
            queuedActionCount = encoded.readU16Le(10),
            pendingCriticalAlertId = encoded.readU64Le(16),
        )
        validateStatus(snapshot)?.let { return SemanticCodecResult.failure(it) }
        return SemanticCodecResult.success(snapshot)
    }

    fun encodeActionRequest(request: CompanionActionRequest): SemanticCodecResult<ByteArray> {
        validateAction(request)?.let { return SemanticCodecResult.failure(it) }
        val output = ByteArray(COMPANION_ACTION_REQUEST_BYTES)
        actionRequestMagic.copyInto(output)
        output[6] = request.kind.wireValue.toByte()
        output[7] = if (request.kind == CompanionActionKind.QUICK_STATUS) request.quickStatus.wireValue.toByte() else 0
        val subject = when (request.kind) {
            CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT -> request.criticalAlertId
            CompanionActionKind.FACTORY_RESET -> request.factoryResetReceipt
            else -> 0uL
        }
        writeU64Le(output, 8, subject)
        return SemanticCodecResult.success(output)
    }

    fun decodeActionRequest(encoded: ByteArray): SemanticCodecResult<CompanionActionRequest> {
        validatePrefix(encoded, actionRequestMagic, COMPANION_ACTION_REQUEST_BYTES)?.let {
            return SemanticCodecResult.failure(it)
        }
        if ((16 until COMPANION_ACTION_REQUEST_BYTES).any { encoded[it].toInt() != 0 }) {
            return SemanticCodecResult.failure(CompanionSemanticCodecError.RESERVED_BITS_SET)
        }
        val kind = CompanionActionKind.fromWire(encoded.u8(6))
            ?: return SemanticCodecResult.failure(CompanionSemanticCodecError.UNKNOWN_ENUM)
        if (kind != CompanionActionKind.QUICK_STATUS && encoded.u8(7) != 0) {
            return SemanticCodecResult.failure(CompanionSemanticCodecError.RESERVED_BITS_SET)
        }
        val quickStatus = CompanionQuickStatus.fromWire(encoded.u8(7))
            ?: if (kind == CompanionActionKind.QUICK_STATUS) {
                return SemanticCodecResult.failure(CompanionSemanticCodecError.INVALID_QUICK_STATUS)
            } else CompanionQuickStatus.OK
        val subject = encoded.readU64Le(8)
        val request = CompanionActionRequest(
            kind = kind,
            quickStatus = quickStatus,
            criticalAlertId = if (kind == CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT) subject else 0uL,
            factoryResetReceipt = if (kind == CompanionActionKind.FACTORY_RESET) subject else 0uL,
        )
        validateAction(request)?.let { return SemanticCodecResult.failure(it) }
        return SemanticCodecResult.success(request)
    }

    fun encodeActionResult(result: CompanionActionResult): SemanticCodecResult<ByteArray> {
        validateResult(result)?.let { return SemanticCodecResult.failure(it) }
        val output = ByteArray(COMPANION_ACTION_RESULT_BYTES)
        actionResultMagic.copyInto(output)
        output[6] = result.disposition.wireValue.toByte()
        output[7] = result.rejectReason.wireValue.toByte()
        output[8] = result.kind.wireValue.toByte()
        output[9] = if (result.kind == CompanionActionKind.QUICK_STATUS) result.quickStatus.wireValue.toByte() else 0
        val subject = when (result.kind) {
            CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT -> result.criticalAlertId
            CompanionActionKind.FACTORY_RESET -> result.factoryResetReceipt
            else -> 0uL
        }
        writeU64Le(output, 12, subject)
        return SemanticCodecResult.success(output)
    }

    fun decodeActionResult(encoded: ByteArray): SemanticCodecResult<CompanionActionResult> {
        validatePrefix(encoded, actionResultMagic, COMPANION_ACTION_RESULT_BYTES)?.let {
            return SemanticCodecResult.failure(it)
        }
        if (encoded.u8(10) != 0 || encoded.u8(11) != 0) {
            return SemanticCodecResult.failure(CompanionSemanticCodecError.RESERVED_BITS_SET)
        }
        val disposition = CompanionActionDisposition.fromWire(encoded.u8(6))
            ?: return SemanticCodecResult.failure(CompanionSemanticCodecError.UNKNOWN_ENUM)
        val rejectReason = CompanionActionRejectReason.fromWire(encoded.u8(7))
            ?: return SemanticCodecResult.failure(CompanionSemanticCodecError.UNKNOWN_ENUM)
        val kind = CompanionActionKind.fromWire(encoded.u8(8))
            ?: return SemanticCodecResult.failure(CompanionSemanticCodecError.UNKNOWN_ENUM)
        if (kind != CompanionActionKind.QUICK_STATUS && encoded.u8(9) != 0) {
            return SemanticCodecResult.failure(CompanionSemanticCodecError.RESERVED_BITS_SET)
        }
        val quickStatus = CompanionQuickStatus.fromWire(encoded.u8(9))
            ?: if (kind == CompanionActionKind.QUICK_STATUS) {
                return SemanticCodecResult.failure(CompanionSemanticCodecError.INVALID_QUICK_STATUS)
            } else CompanionQuickStatus.OK
        val subject = encoded.readU64Le(12)
        val result = CompanionActionResult(
            kind = kind,
            quickStatus = quickStatus,
            criticalAlertId = if (kind == CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT) subject else 0uL,
            factoryResetReceipt = if (kind == CompanionActionKind.FACTORY_RESET) subject else 0uL,
            disposition = disposition,
            rejectReason = rejectReason,
        )
        validateResult(result)?.let { return SemanticCodecResult.failure(it) }
        return SemanticCodecResult.success(result)
    }

    fun validateSemanticFragment(fragment: CompanionFragment): CompanionSemanticCodecError {
        if (fragment.fragmentIndex != 0 || fragment.fragmentCount != 1 || fragment.payload.size > COMPANION_MAX_FRAGMENT_PAYLOAD_BYTES) {
            return CompanionSemanticCodecError.MALFORMED
        }
        return when (fragment.kind) {
            CompanionFrameKind.SNAPSHOT_REQUEST -> decodeSnapshotRequest(fragment.payload).error
            CompanionFrameKind.ACTION_REQUEST -> decodeActionRequest(fragment.payload).error
            CompanionFrameKind.SNAPSHOT -> decodeStatusSnapshot(fragment.payload).error
            CompanionFrameKind.ACTION_RESULT -> decodeActionResult(fragment.payload).error
            CompanionFrameKind.EVENT,
            CompanionFrameKind.AUTHORIZATION_CLAIM_START,
            CompanionFrameKind.AUTHORIZATION_CLAIM_STATUS,
            CompanionFrameKind.AUTHORIZATION_CLAIM_RESULT,
            -> CompanionSemanticCodecError.UNSUPPORTED_FRAME_KIND
        }
    }

    private fun validatePrefix(encoded: ByteArray, magic: ByteArray, expectedBytes: Int): CompanionSemanticCodecError? {
        if (encoded.size != expectedBytes || !encoded.hasMagic(magic)) return CompanionSemanticCodecError.MALFORMED
        if (encoded.u8(4) != 0 || encoded.u8(5) != 0) return CompanionSemanticCodecError.UNSUPPORTED_VERSION
        return null
    }

    private fun validateStatus(snapshot: CompanionStatusSnapshot): CompanionSemanticCodecError? {
        if (snapshot.revision !in 1..0xffff_ffffL) return CompanionSemanticCodecError.INVALID_REVISION
        if (snapshot.queuedActionCount !in 0..0xffff) return CompanionSemanticCodecError.MALFORMED
        return null
    }

    private fun validateAction(request: CompanionActionRequest): CompanionSemanticCodecError? = when (request.kind) {
        CompanionActionKind.QUICK_STATUS ->
            if (request.criticalAlertId == 0uL && request.factoryResetReceipt == 0uL) null
            else CompanionSemanticCodecError.INCOHERENT_ACTION
        CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT ->
            if (request.criticalAlertId != 0uL && request.factoryResetReceipt == 0uL) null
            else CompanionSemanticCodecError.INVALID_ALERT_ID
        CompanionActionKind.START_POSITION_SHARING,
        CompanionActionKind.STOP_POSITION_SHARING,
        -> if (request.criticalAlertId == 0uL && request.factoryResetReceipt == 0uL) null
        else CompanionSemanticCodecError.INCOHERENT_ACTION
        CompanionActionKind.FACTORY_RESET ->
            if (request.criticalAlertId == 0uL && request.factoryResetReceipt != 0uL) null
            else CompanionSemanticCodecError.INCOHERENT_ACTION
    }

    private fun validateResult(result: CompanionActionResult): CompanionSemanticCodecError? {
        validateAction(
            CompanionActionRequest(
                result.kind,
                result.quickStatus,
                result.criticalAlertId,
                result.factoryResetReceipt,
            ),
        )?.let { return it }
        val outbound = result.kind == CompanionActionKind.QUICK_STATUS || result.kind == CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT
        if (result.disposition == CompanionActionDisposition.REJECTED) {
            if (result.rejectReason == CompanionActionRejectReason.NONE) return CompanionSemanticCodecError.INCOHERENT_RESULT
            if (result.rejectReason == CompanionActionRejectReason.STALE_ALERT && result.kind != CompanionActionKind.ACKNOWLEDGE_CRITICAL_ALERT) {
                return CompanionSemanticCodecError.INCOHERENT_RESULT
            }
            if (result.rejectReason == CompanionActionRejectReason.QUEUE_FULL && !outbound) {
                return CompanionSemanticCodecError.INCOHERENT_RESULT
            }
            return null
        }
        if (result.rejectReason != CompanionActionRejectReason.NONE) return CompanionSemanticCodecError.INCOHERENT_RESULT
        if (outbound && result.disposition != CompanionActionDisposition.QUEUED) return CompanionSemanticCodecError.INCOHERENT_RESULT
        if (!outbound && result.disposition != CompanionActionDisposition.ADMITTED) return CompanionSemanticCodecError.INCOHERENT_RESULT
        return null
    }
}

private fun ByteArray.hasMagic(magic: ByteArray): Boolean = size >= magic.size && magic.indices.all { this[it] == magic[it] }
private fun ByteArray.u8(offset: Int): Int = this[offset].toInt() and 0xff
private fun ByteArray.readU16Le(offset: Int): Int = u8(offset) or (u8(offset + 1) shl 8)
private fun ByteArray.readU32Le(offset: Int): Long =
    u8(offset).toLong() or (u8(offset + 1).toLong() shl 8) or
        (u8(offset + 2).toLong() shl 16) or (u8(offset + 3).toLong() shl 24)

private fun ByteArray.readU64Le(offset: Int): ULong {
    var value = 0uL
    repeat(8) { index -> value = value or (u8(offset + index).toULong() shl (index * 8)) }
    return value
}

private fun writeU16Le(output: ByteArray, offset: Int, value: Int) {
    output[offset] = value.toByte()
    output[offset + 1] = (value ushr 8).toByte()
}

private fun writeU32Le(output: ByteArray, offset: Int, value: Long) {
    repeat(4) { index -> output[offset + index] = (value ushr (index * 8)).toByte() }
}

private fun writeU64Le(output: ByteArray, offset: Int, value: ULong) {
    repeat(8) { index -> output[offset + index] = (value shr (index * 8)).toByte() }
}
