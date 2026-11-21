package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionGnssState
import io.github.nbjelanovic.otprotocol.CompanionPowerState
import io.github.nbjelanovic.otprotocol.DeviceParameterGnssState
import io.github.nbjelanovic.otprotocol.DeviceParameterPowerSource

enum class V1DeviceGpsStatus {
    UNKNOWN,
    UNAVAILABLE,
    SEARCHING,
    CURRENT,
    STALE,
    FAULT,
}

data class V1DeviceStatusStripState(
    val bluetoothConnected: Boolean,
    val gps: V1DeviceGpsStatus,
    val satellites: Int?,
    val batteryPercent: Int?,
    val externalPower: Boolean?,
    val transmitting: Boolean?,
    val receiving: Boolean?,
) {
    init {
        require(satellites == null || satellites in 0..99)
        require(batteryPercent == null || batteryPercent in 0..100)
        if (!bluetoothConnected) {
            require(gps == V1DeviceGpsStatus.UNKNOWN)
            require(satellites == null)
            require(batteryPercent == null)
            require(externalPower == null)
            require(transmitting == null)
            require(receiving == null)
        }
    }

    companion object {
        fun disconnected() = V1DeviceStatusStripState(
            bluetoothConnected = false,
            gps = V1DeviceGpsStatus.UNKNOWN,
            satellites = null,
            batteryPercent = null,
            externalPower = null,
            transmitting = null,
            receiving = null,
        )
    }
}

internal object V1DeviceStatusStripProjector {
    fun from(state: TrailAppUiState): V1DeviceStatusStripState {
        val ready = (state as? TrailAppUiState.BluetoothDevice)?.runtimeState as? BleRuntimeState.Ready
            ?: return V1DeviceStatusStripState.disconnected()
        val parameters = ready.session.deviceParameters?.snapshot
        val gnss = parameters?.gnss?.value
        val gps = ready.session.snapshot.gnss.toDeviceStatus()
        return V1DeviceStatusStripState(
            bluetoothConnected = true,
            gps = gps,
            satellites = gnss?.satellites?.takeIf { gnss.state.toDeviceStatus() == gps },
            batteryPercent = parameters?.battery?.value?.percent,
            externalPower = when (parameters?.power?.value) {
                DeviceParameterPowerSource.EXTERNAL, DeviceParameterPowerSource.CHARGING -> true
                DeviceParameterPowerSource.BATTERY_ONLY -> false
                else -> true.takeIf { ready.session.snapshot.power == CompanionPowerState.EXTERNAL }
            },
            transmitting = null,
            receiving = null,
        )
    }

    private fun DeviceParameterGnssState.toDeviceStatus(): V1DeviceGpsStatus = when (this) {
        DeviceParameterGnssState.UNKNOWN -> V1DeviceGpsStatus.UNKNOWN
        DeviceParameterGnssState.UNAVAILABLE -> V1DeviceGpsStatus.UNAVAILABLE
        DeviceParameterGnssState.SEARCHING -> V1DeviceGpsStatus.SEARCHING
        DeviceParameterGnssState.FIX -> V1DeviceGpsStatus.CURRENT
        DeviceParameterGnssState.STALE -> V1DeviceGpsStatus.STALE
        DeviceParameterGnssState.INVALID -> V1DeviceGpsStatus.FAULT
    }

    private fun CompanionGnssState.toDeviceStatus(): V1DeviceGpsStatus = when (this) {
        CompanionGnssState.UNKNOWN -> V1DeviceGpsStatus.UNKNOWN
        CompanionGnssState.UNAVAILABLE -> V1DeviceGpsStatus.UNAVAILABLE
        CompanionGnssState.SEARCHING -> V1DeviceGpsStatus.SEARCHING
        CompanionGnssState.CURRENT -> V1DeviceGpsStatus.CURRENT
        CompanionGnssState.STALE -> V1DeviceGpsStatus.STALE
        CompanionGnssState.FAULT -> V1DeviceGpsStatus.FAULT
    }
}

internal fun statusAgeLabel(readAtMillis: Long?, nowMillis: Long): String? =
    if (readAtMillis == null || readAtMillis < 0 || nowMillis < readAtMillis) null
    else "Board status updated ${(nowMillis - readAtMillis) / 1000} s ago"
