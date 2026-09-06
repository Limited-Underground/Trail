package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.*
import kotlin.test.Test
import kotlin.test.assertEquals

/** Pins the original support projector's connection-only categorization during isolation. */
class V1TestConnectionCategoryTest {
    private val companion = BleDiscoveredCompanion("opaque-test-token", "Test companion")

    @Test fun disconnectedAndFactoryResetPhasesKeepTheOriginalCategories() {
        listOf(BleRuntimeState.Inactive, BleRuntimeState.Idle,
            BleRuntimeState.ScanComplete(emptyList()), BleRuntimeState.FactoryResetErasing(companion),
            BleRuntimeState.FactoryResetVerifying(companion), BleRuntimeState.FactoryResetComplete(false),
            BleRuntimeState.Closed).forEach {
            assertEquals(V1TestPhoneConnectionState.DISCONNECTED, V1TestConnectionCategory.from(it))
        }
        FactoryResetNotVerifiedReason.entries.forEach {
            assertEquals(V1TestPhoneConnectionState.FAILED,
                V1TestConnectionCategory.from(BleRuntimeState.FactoryResetNotVerified(companion, it, false)))
        }
    }

    @Test fun allConnectionNegotiationPhasesRemainConnecting() {
        val states = listOf(BleRuntimeState.Scanning(emptyList()), BleRuntimeState.FindingReturningOwner,
            BleRuntimeState.Connecting(companion)) + BleNegotiationPhase.entries.map {
            BleRuntimeState.Negotiating(companion, it)
        }
        states.forEach { assertEquals(V1TestPhoneConnectionState.CONNECTING, V1TestConnectionCategory.from(it)) }
        assertEquals(V1TestPhoneConnectionState.AUTHORIZING,
            V1TestConnectionCategory.from(BleRuntimeState.AwaitingAuthorization(companion)))
        listOf(false, true).forEach { periodic ->
            assertEquals(V1TestPhoneConnectionState.RECONNECTING,
                V1TestConnectionCategory.from(BleRuntimeState.Reconnecting(companion, 1, 3, null, periodic)))
        }
    }

    @Test fun everyBlockAndFailureRemainsFailedRegardlessOfExactDiagnostic() {
        BleRuntimeBlock.entries.forEach {
            assertEquals(V1TestPhoneConnectionState.FAILED, V1TestConnectionCategory.from(BleRuntimeState.Blocked(it)))
        }
        BleRuntimeFailure.entries.forEach { failure ->
            (BleConnectionDiagnostic.entries + listOf(null)).forEach { diagnostic ->
                assertEquals(V1TestPhoneConnectionState.FAILED,
                    V1TestConnectionCategory.from(BleRuntimeState.Failed(failure, diagnostic)))
            }
        }
    }

    @Test fun readyAndResetRequestingKeepReadyWithoutReadingDeviceMetrics() {
        val session = BleActiveSession(
            companion = companion, sessionNonce = 1,
            snapshot = CompanionStatusSnapshot(revision = 1, radio = CompanionRadioState.UNKNOWN,
                gnss = CompanionGnssState.UNKNOWN, power = CompanionPowerState.UNKNOWN,
                positionSharing = CompanionPositionSharingState.STOPPED, queuedActionCount = 0),
            protocolInfo = CompanionProtocolInfo(capabilities = 0),
            groupLocation = requireNotNull(GroupLocationSnapshot.authoritativeUnavailable(
                1, CompanionPositionSharingState.STOPPED)),
        )
        listOf(BleRuntimeState.Ready(session), BleRuntimeState.FactoryResetRequesting(session)).forEach {
            assertEquals(V1TestPhoneConnectionState.READY, V1TestConnectionCategory.from(it))
        }
    }
}