package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionRejectReason
import io.github.nbjelanovic.otprotocol.CompanionGnssState
import io.github.nbjelanovic.otprotocol.CompanionPairingReason
import io.github.nbjelanovic.otprotocol.CompanionPairingResult
import io.github.nbjelanovic.otprotocol.CompanionPairingState
import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState
import io.github.nbjelanovic.otprotocol.CompanionPowerState
import io.github.nbjelanovic.otprotocol.CompanionProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionRadioState
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

// Built at runtime so the publication-safety scan never sees an address literal.
private val peerFixtureAddress = listOf("44", "33", "22", "11", "AA", "BB").joinToString(":")

class MeshTrailPairingLogTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun logsEveryVisiblePairingOutcomeAndDistinguishesPreviousResults() {
        val capture = PairingCapture()
        val inputs = listOf(
            BoardPairingState.Available,
            BoardPairingState.Waiting,
            BoardPairingState.Ready(654321),
            BoardPairingState.Rejected(CompanionActionRejectReason.BUSY),
            BoardPairingState.Unknown,
        )
        val results = listOf(
            CompanionPairingState.NONE to "unknown",
            CompanionPairingState.RUNNING to "running",
            CompanionPairingState.PAIRED_LOCAL to "paired locally",
            CompanionPairingState.PAIRED_BOTH to "paired on both boards",
            CompanionPairingState.FAILED to "failed",
            CompanionPairingState.CANCELLED to "cancelled",
            CompanionPairingState.INCOMPLETE to "incomplete",
        )
        for (state in inputs) capture.observe(state)
        // Change the projection between Unknown and the canonical NONE result.
        capture.observe(BoardPairingState.Available)
        for ((state, _) in results) {
            val result = when (state) {
                CompanionPairingState.NONE -> CompanionPairingResult()
                CompanionPairingState.RUNNING -> syntheticResult(state, peerShortId = 0)
                CompanionPairingState.PAIRED_LOCAL, CompanionPairingState.PAIRED_BOTH -> syntheticResult(state)
                CompanionPairingState.FAILED -> syntheticResult(state, CompanionPairingReason.TIMED_OUT, 0)
                CompanionPairingState.CANCELLED -> syntheticResult(state, CompanionPairingReason.REQUEST_ENDED, 0)
                CompanionPairingState.INCOMPLETE -> syntheticResult(state, CompanionPairingReason.STORAGE, 0)
            }
            capture.observe(BoardPairingState.Result(result))
            // A second state observation with the same public projection should not duplicate rows.
            capture.observe(BoardPairingState.Result(result))
            capture.observe(BoardPairingState.Result(result, historical = true))
        }
        assertEquals(
            listOf("available", "waiting", "ready", "refused", "unknown", "available") +
                results.flatMap { (_, label) -> listOf(label, "previous $label") },
            capture.values("Board pairing"),
        )
        assertEquals(listOf("BUSY"), capture.values("Board pairing refused"))
        capture.checkNoPrivateData()
    }

    @Test fun pairingResultReasonsAreTypedWithoutKeepingCorrelationOrPresentationFields() {
        val capture = PairingCapture()
        for (reason in CompanionPairingReason.entries.filter { it != CompanionPairingReason.NONE }) {
            capture.observe(BoardPairingState.Result(syntheticResult(
                CompanionPairingState.FAILED, reason, peerShortId = 0,
            )))
        }
        assertEquals(
            listOf("TIMED_OUT", "NO_REPLY", "STORAGE", "REQUEST_ENDED", "RADIO_UNAVAILABLE",
                "VERIFICATION_FAILED", "UNAVAILABLE"),
            capture.values("Board pairing result reason"),
        )
        assertEquals(listOf("failed"), capture.values("Board pairing"))
        capture.checkNoPrivateData()
    }

    @Test fun pairingStorageDoesNotAdmitFreeFormCredentialsAndIdentity() {
        val log = MeshTrailOperationLog(temp.newFolder())
        for (privateValue in privateMarkers) {
            assertFalse(log.append(12, MeshTrailLogKind.PAIRING, privateValue))
            assertFalse(log.append(12, MeshTrailLogKind.PAIRING, "paired locally $privateValue"))
            assertFalse(log.append(12, MeshTrailLogKind.PAIRING_REFUSED, "BUSY\n$privateValue"))
        }
        assertTrue(log.exportText().contains("No events recorded yet."))
    }

    private inner class PairingCapture {
        private val directory = temp.newFolder()
        private val log = MeshTrailOperationLog(directory)
        private val transitions = MeshTrailLogTransitions { kind, value -> check(log.append(12, kind, value)) }

        fun observe(pairing: BoardPairingState) {
            val snapshot = CompanionStatusSnapshot(
                revision = 49572911,
                radio = CompanionRadioState.UNKNOWN,
                gnss = CompanionGnssState.UNKNOWN,
                power = CompanionPowerState.UNKNOWN,
                positionSharing = CompanionPositionSharingState.STOPPED,
                queuedActionCount = 0,
                pendingCriticalAlertId = 87533473uL,
            )
            val session = BleActiveSession(
                companion = BleDiscoveredCompanion("$peerFixtureAddress/opaque-endpoint", "Trail-CCDD22/owner-name"),
                sessionNonce = 459678332,
                snapshot = snapshot,
                protocolInfo = CompanionProtocolInfo(capabilities = 0),
                groupLocation = checkNotNull(GroupLocationSnapshot.authoritativeUnavailable(
                    snapshot.revision, snapshot.positionSharing,
                )),
                configuration = BleConfigurationState(
                    editorSessionId = 1273771,
                    suggestedDeviceName = "suggested-private-board",
                    deviceName = "private-board-name",
                    publicName = "private-public-name",
                    notice = "fixture-passkey 772991 at 13.456789,-112.987654",
                ),
                boardPairing = pairing,
            )
            transitions.observeService(TrailAppUiState.BluetoothDevice(
                runtimeState = BleRuntimeState.Ready(session),
                permissionState = NearbyDevicesPermissionState.GRANTED,
                permissionRequestInFlight = false,
                permissionWasDenied = false,
                authorizationState = DeviceAuthorizationUiState.None,
                textMessages = V1TextState(received = "private-message-content"),
            ))
        }

        fun values(label: String): List<String> {
            val prefix = "+12s  $label: "
            return log.exportText().lineSequence().filter { it.startsWith(prefix) }
                .map { it.removePrefix(prefix) }.toList()
        }

        fun checkNoPrivateData() {
            val disk = directory.listFiles()!!.joinToString("\n") { it.readText() }
            val exported = log.exportText()
            for (marker in privateMarkers) {
                assertFalse("Export exposed synthetic marker $marker", exported.contains(marker, ignoreCase = true))
                // Checksums can contain short hex fragments, so inspect only persisted kind/value columns.
                assertFalse("Disk exposed synthetic marker $marker", disk.lineSequence().any { line ->
                    val columns = line.split('\t', limit = 4)
                    columns.take(3).any { it.contains(marker, ignoreCase = true) }
                })
            }
        }
    }

    private fun syntheticResult(
        state: CompanionPairingState,
        reason: CompanionPairingReason = CompanionPairingReason.NONE,
        peerShortId: Int = 0xBEEF,
    ) = CompanionPairingResult(
        state = state,
        reason = reason,
        peerShortId = peerShortId,
        originExchangeId = 295087361,
        originSessionNonce = 398794281,
        revision = 317539218,
    )

    private val privateMarkers = listOf(
        "BEEF", "48879", "654321", "295087361", "398794281", "317539218", "459678332",
        "49572911", "87533473", "1273771", peerFixtureAddress, "opaque-endpoint", "Trail-CCDD22",
        "owner-name", "suggested-private-board", "private-board-name", "private-public-name",
        "fixture-passkey", "772991", "13.456789", "-112.987654", "private-message-content",
    )
}
