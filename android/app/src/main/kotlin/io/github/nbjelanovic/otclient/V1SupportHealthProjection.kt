package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionGnssState
import io.github.nbjelanovic.otprotocol.CompanionPowerState
import io.github.nbjelanovic.otprotocol.CompanionRadioState

data class V1SupportHealthProjection(
    val connection: V1SupportPhoneConnection,
    val radio: V1SupportRadioHealth,
    val gps: V1SupportGpsHealth,
    val power: V1SupportPowerHealth,
    val lastCode: V1SupportCode?,
)

internal object V1SupportHealthProjector {
    fun from(state: TrailAppUiState): V1SupportHealthProjection = when (state) {
        TrailAppUiState.ChooseMode -> unknown(V1SupportPhoneConnection.DISCONNECTED)
        is TrailAppUiState.LocalTest -> unknown(V1SupportPhoneConnection.UNKNOWN)
        is TrailAppUiState.BluetoothDevice -> from(state.runtimeState)
    }

    private fun from(state: BleRuntimeState): V1SupportHealthProjection {
        val snapshot = when (state) {
            is BleRuntimeState.Ready -> state.session.snapshot
            is BleRuntimeState.FactoryResetRequesting -> state.session.snapshot
            else -> null
        }
        if (snapshot != null) {
            return V1SupportHealthProjection(
                connection = V1SupportPhoneConnection.READY,
                radio = snapshot.radio.toSupportHealth(),
                gps = snapshot.gnss.toSupportHealth(),
                power = snapshot.power.toSupportHealth(),
                lastCode = null,
            )
        }
        return unknown(
            connection = when (state) {
                BleRuntimeState.Inactive,
                BleRuntimeState.Idle,
                is BleRuntimeState.ScanComplete,
                is BleRuntimeState.FactoryResetErasing,
                is BleRuntimeState.FactoryResetVerifying,
                is BleRuntimeState.FactoryResetComplete,
                BleRuntimeState.Closed,
                -> V1SupportPhoneConnection.DISCONNECTED
                is BleRuntimeState.Blocked,
                is BleRuntimeState.FactoryResetNotVerified,
                is BleRuntimeState.Failed,
                -> V1SupportPhoneConnection.FAILED
                is BleRuntimeState.Scanning,
                BleRuntimeState.FindingReturningOwner,
                is BleRuntimeState.Connecting,
                is BleRuntimeState.Negotiating,
                -> V1SupportPhoneConnection.CONNECTING
                is BleRuntimeState.AwaitingAuthorization -> V1SupportPhoneConnection.AUTHORIZING
                is BleRuntimeState.Reconnecting -> V1SupportPhoneConnection.RECONNECTING
                is BleRuntimeState.Ready,
                is BleRuntimeState.FactoryResetRequesting,
                -> error("session states were projected above")
            },
            lastCode = (state as? BleRuntimeState.Failed)?.reason?.toSupportCode(),
        )
    }

    private fun unknown(
        connection: V1SupportPhoneConnection,
        lastCode: V1SupportCode? = null,
    ) = V1SupportHealthProjection(
        connection = connection,
        radio = V1SupportRadioHealth.UNKNOWN,
        gps = V1SupportGpsHealth.UNKNOWN,
        power = V1SupportPowerHealth.UNKNOWN,
        lastCode = lastCode,
    )

    private fun CompanionRadioState.toSupportHealth(): V1SupportRadioHealth = when (this) {
        CompanionRadioState.UNKNOWN -> V1SupportRadioHealth.UNKNOWN
        CompanionRadioState.UNAVAILABLE -> V1SupportRadioHealth.UNAVAILABLE
        CompanionRadioState.READY -> V1SupportRadioHealth.READY
        CompanionRadioState.DEGRADED -> V1SupportRadioHealth.DEGRADED
        CompanionRadioState.FAULT -> V1SupportRadioHealth.FAULT
    }

    private fun CompanionGnssState.toSupportHealth(): V1SupportGpsHealth = when (this) {
        CompanionGnssState.UNKNOWN -> V1SupportGpsHealth.UNKNOWN
        CompanionGnssState.UNAVAILABLE -> V1SupportGpsHealth.UNAVAILABLE
        CompanionGnssState.SEARCHING -> V1SupportGpsHealth.SEARCHING
        CompanionGnssState.CURRENT -> V1SupportGpsHealth.CURRENT
        CompanionGnssState.STALE -> V1SupportGpsHealth.STALE
        CompanionGnssState.FAULT -> V1SupportGpsHealth.FAULT
    }

    private fun CompanionPowerState.toSupportHealth(): V1SupportPowerHealth = when (this) {
        CompanionPowerState.UNKNOWN -> V1SupportPowerHealth.UNKNOWN
        CompanionPowerState.EXTERNAL -> V1SupportPowerHealth.EXTERNAL
        CompanionPowerState.NORMAL -> V1SupportPowerHealth.NORMAL
        CompanionPowerState.LOW -> V1SupportPowerHealth.LOW
        CompanionPowerState.CRITICAL -> V1SupportPowerHealth.CRITICAL
        CompanionPowerState.FAULT -> V1SupportPowerHealth.FAULT
    }

    private fun BleRuntimeFailure.toSupportCode(): V1SupportCode = when (this) {
        BleRuntimeFailure.SECURITY_REQUIREMENT_FAILED,
        BleRuntimeFailure.AUTHORIZATION_CONNECTION_LOST,
        BleRuntimeFailure.AUTHORIZATION_UNSUPPORTED,
        BleRuntimeFailure.AUTHORIZATION_UNAVAILABLE,
        -> V1SupportCode.BLE_AUTHORIZATION
        BleRuntimeFailure.PROTOCOL_VIOLATION,
        BleRuntimeFailure.SESSION_COUNTER_EXHAUSTED,
        -> V1SupportCode.APP
        BleRuntimeFailure.SCAN_START_FAILED,
        BleRuntimeFailure.RETURNING_OWNER_AMBIGUOUS,
        BleRuntimeFailure.CONNECTION_START_FAILED,
        BleRuntimeFailure.MTU_NEGOTIATION_FAILED,
        BleRuntimeFailure.PROTOCOL_INFO_FAILED,
        BleRuntimeFailure.STREAM_SUBSCRIPTION_FAILED,
        BleRuntimeFailure.INITIAL_SNAPSHOT_FAILED,
        BleRuntimeFailure.NEGOTIATION_TIMEOUT,
        BleRuntimeFailure.RECONNECT_EXHAUSTED,
        BleRuntimeFailure.ACTION_WRITE_FAILED,
        BleRuntimeFailure.ACTION_RESULT_TIMEOUT,
        -> V1SupportCode.BLE_RECONNECT
    }
}
