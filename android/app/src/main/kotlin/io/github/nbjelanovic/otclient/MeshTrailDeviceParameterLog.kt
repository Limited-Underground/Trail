package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionDeviceParameters
import io.github.nbjelanovic.otprotocol.DeviceParameterSample

/** Numeric readings and closed enums only. Even the wire-validated firmware version token is omitted. */
internal fun meshTrailParameterEntries(snapshot: CompanionDeviceParameters): List<Pair<MeshTrailLogKind, String>> =
    buildList {
        add(MeshTrailLogKind.PARAMETERS to
            "sequence=${snapshot.sequence} age_ms=${snapshot.snapshotAgeMillis} uptime_ms=${snapshot.collectionUptimeMillis}")
        fun <T> addSample(kind: MeshTrailLogKind, sample: DeviceParameterSample<T>?, fields: (T) -> String) {
            if (sample != null) add(kind to "age_ms=${sample.ageMillis(snapshot.snapshotAgeMillis)} ${fields(sample.value)}")
        }
        addSample(MeshTrailLogKind.BATTERY, snapshot.battery) { "millivolts=${it.millivolts} percent=${it.percent}" }
        addSample(MeshTrailLogKind.POWER, snapshot.power) { "source=${it.name}" }
        addSample(MeshTrailLogKind.PACKET, snapshot.lastPacket) {
            "rssi_dbm=${it.rssiDbm} snr_quarter_db=${it.snrQuarterDb} frequency_hz=${it.frequencyHz}"
        }
        addSample(MeshTrailLogKind.RADIO, snapshot.radioCounters) {
            "tx_attempts=${it.txAttempts} tx_completed=${it.txCompleted} rx_frames=${it.rxFrames} " +
                "rx_errors=${it.rxErrors} rx_dropped=${it.rxDropped} tx_refused=${it.txRefused} " +
                "tx_busy_dropped=${it.txBusyDropped} tx_prelaunch_dropped=${it.txPrelaunchDropped}"
        }
        addSample(MeshTrailLogKind.CAD, snapshot.cadDeferrals) { "count=$it" }
        addSample(MeshTrailLogKind.GNSS, snapshot.gnss) { "state=${it.state.name} satellites=${it.satellites ?: "unknown"}" }
        addSample(MeshTrailLogKind.RESET, snapshot.reset) { "reason=${it.name}" }
        addSample(MeshTrailLogKind.HEAP, snapshot.heap) {
            "free_bytes=${it.freeBytes} minimum_free_bytes=${it.minimumFreeBytes}"
        }
        addSample(MeshTrailLogKind.MAIN_STACK, snapshot.mainStack) { "minimum_free_bytes=$it" }
        addSample(MeshTrailLogKind.BLE_STACK, snapshot.bleStack) { "minimum_free_bytes=$it" }
        addSample(MeshTrailLogKind.STORAGE, snapshot.storage) {
            "used_entries=${it.usedEntries} free_entries=${it.freeEntries} total_entries=${it.totalEntries}"
        }
        addSample(MeshTrailLogKind.FIRMWARE, snapshot.firmware) {
            "release_generation=${it.releaseGeneration} partition_offset=${it.applicationPartitionOffset}"
        }
    }
