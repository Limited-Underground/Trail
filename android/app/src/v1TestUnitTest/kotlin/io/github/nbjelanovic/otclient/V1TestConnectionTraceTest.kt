package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionGnssState
import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState
import io.github.nbjelanovic.otprotocol.CompanionPowerState
import io.github.nbjelanovic.otprotocol.CompanionProtocolInfo
import io.github.nbjelanovic.otprotocol.CompanionRadioState
import io.github.nbjelanovic.otprotocol.CompanionStatusSnapshot
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class V1TestConnectionTraceTest {
    @Test
    fun everyRuntimeFailureRoundTripsAsAnAllowlistedReasonWithoutDeviceLabels() {
        for (failure in BleRuntimeFailure.entries) {
            val machine = V1TestConnectionTraceMachine()
            val companion = BleDiscoveredCompanion("private-scan-endpoint", "Trail-7K3P9Q")
            machine.observe(GENERATION, 0, state(BleRuntimeState.Connecting(companion)))
            val emission = machine.observe(GENERATION, 1, state(BleRuntimeState.Failed(failure))).single()
            val expectedReason = when (failure) {
                BleRuntimeFailure.SETUP_LABEL_AMBIGUOUS,
                BleRuntimeFailure.SETUP_LABEL_CHANGED -> V1TestTraceReason.SCAN_START_FAILED.name
                else -> failure.name
            }
            assertEquals(expectedReason, emission.reason.name)
            val storage = Storage()
            val log = V1TestConnectionLog(storage)
            assertTrue(log.startSession())
            assertTrue(log.recordTrace(1, emission))
            val reloaded = V1TestConnectionLog(storage)
            assertEquals(log.snapshot(), reloaded.snapshot())
            val exported = reloaded.exportText()
            assertTrue(exported.contains(expectedReason))
            assertFalse(exported.contains(companion.publicLabel))
            assertFalse(exported.contains(companion.endpointToken))
            assertFalse(requireNotNull(storage.bytes).decodeToString().contains(companion.publicLabel))
            assertFalse(requireNotNull(storage.bytes).decodeToString().contains(companion.endpointToken))
        }
    }

    @Test
    fun promotedClaimIsRecordedBeforeProtectedNormalProfileReread() {
        val machine=V1TestConnectionTraceMachine()
        machine.observe(GENERATION,0,state(BleRuntimeState.Connecting(COMPANION)))
        machine.observe(GENERATION,1,negotiating(BleNegotiationPhase.AUTHORIZATION_CLAIM))
        val reread=machine.observe(GENERATION,2,negotiating(BleNegotiationPhase.PROTOCOL_INFO))
        assertEquals(1,reread.count { it.stage==V1TestTraceStage.AUTHORIZATION_ACCEPTED })
        val snapshot=machine.observe(GENERATION,3,negotiating(BleNegotiationPhase.INITIAL_SNAPSHOT))
        assertFalse(snapshot.any { it.stage==V1TestTraceStage.AUTHORIZATION_ACCEPTED })
        assertTrue(snapshot.any { it.stage==V1TestTraceStage.SNAPSHOT_REQUESTED })
    }
    @Test
    fun failedRuntimeKeepsEveryExactDiagnosticAndNullSeparateFromReasonThroughReloadAndExport() {
        (BleConnectionDiagnostic.entries + listOf(null)).forEach { diagnostic ->
            val machine = V1TestConnectionTraceMachine()
            machine.observe(GENERATION, 0, state(BleRuntimeState.Connecting(COMPANION)))
            machine.observe(GENERATION, 1, negotiating(BleNegotiationPhase.AUTHORIZATION_CLAIM))
            machine.observe(GENERATION, 2, negotiating(BleNegotiationPhase.INITIAL_SNAPSHOT))
            val failed = state(BleRuntimeState.Failed(
                BleRuntimeFailure.AUTHORIZATION_CONNECTION_LOST,
                connectionDiagnostic = diagnostic,
            ))
            val emission = machine.observe(GENERATION, 3, failed).single()
            assertEquals(V1TestTraceStage.SNAPSHOT_REJECTED, emission.stage)
            assertEquals(V1TestTraceReason.AUTHORIZATION_CONNECTION_LOST, emission.reason)
            assertEquals(diagnostic, emission.connectionDiagnostic)
            assertEquals(1L, emission.transaction)
            assertTrue(machine.observe(GENERATION, 4, failed).isEmpty())
            val storage = Storage()
            val log = V1TestConnectionLog(storage)
            assertTrue(log.startSession())
            assertTrue(log.recordTrace(3, emission))
            val reloaded = V1TestConnectionLog(storage)
            assertEquals(log.snapshot(), reloaded.snapshot())
            assertEquals(emission, (reloaded.snapshot().single().event as V1TestConnectionLogEvent.Trace).emission)
            val export = reloaded.exportText()
            assertTrue(export.contains("AUTHORIZATION_CONNECTION_LOST\t${diagnostic?.name ?: "UNAVAILABLE"}\n"))
            assertTrue(export.contains("not proof of explicit device rejection"))
        }
    }

    @Test
    fun version2MigrationNeverInventsAMissingDiagnosticEvenFromALegacyReason() {
        val storage = Storage().apply {
            bytes = ("OTCL\t2\t1\n" +
                "1\t3\tS\tSNAPSHOT_REJECTED\t1\t1\t7\t0\t0\tAUTHORIZATION_CONNECTION_LOST\n" +
                "1\t4\tS\tDISCONNECT_OBSERVED\t1\t2\t7\t0\t0\tGATT_AUTHORIZATION_REJECTED\n").toByteArray()
        }
        val log = V1TestConnectionLog(storage)
        assertEquals(2, log.loadedVersion)
        val before = log.snapshot()
        before.forEach { assertNull((it.event as V1TestConnectionLogEvent.Trace).emission.connectionDiagnostic) }
        assertTrue(log.exportText().contains("GATT_AUTHORIZATION_REJECTED\tUNAVAILABLE\n"))
        val oldBytes = storage.bytes!!.copyOf()
        storage.writeResult = false
        assertFalse(log.startSession())
        assertContentEquals(oldBytes, storage.bytes)
        assertEquals(before, log.snapshot())
        storage.writeResult = true
        assertTrue(log.startSession())
        assertTrue(storage.bytes!!.decodeToString().startsWith("OTCL\t3\t2\n"))
        val reloaded = V1TestConnectionLog(storage)
        assertEquals(3, reloaded.loadedVersion)
        assertEquals(before, reloaded.snapshot())
    }

    @Test
    fun version3RejectsMissingExtraAndUnknownDiagnosticFieldsAndOverflowingVersions() {
        val row = "1\t3\tS\tSNAPSHOT_REJECTED\t1\t1\t7\t0\t0\tAUTHORIZATION_CONNECTION_LOST"
        val malformed = listOf(
            "OTCL\t3\t1\n$row\n",
            "OTCL\t3\t1\n$row\t\n",
            "OTCL\t3\t1\n$row\tNONE\n",
            "OTCL\t3\t1\n$row\tUNKNOWN\n",
            "OTCL\t3\t1\n$row\tGATT_AUTHORIZATION_REJECTED \n",
            "OTCL\t3\t1\n$row\tGATT_AUTHORIZATION_REJECTED\tEXTRA\n",
            "OTCL\t2\t1\n$row\tUNAVAILABLE\n",
            "OTCL\t4294967299\t1\n",
        )
        malformed.forEach { value ->
            val storage = Storage().apply { bytes = value.toByteArray() }
            val log = V1TestConnectionLog(storage)
            assertEquals(V1TestConnectionLogStatus.CORRUPT, log.status, value)
            assertFalse(log.startSession())
            assertEquals(0, storage.writes)
        }
    }

    @Test
    fun nearlyFullVersion2MigrationEvictsOldestWithinTheVersion3ByteBudget() {
        val rows = mutableListOf<String>()
        var encoded = "OTCL\t2\t1\n"
        for (ordinal in 1..512) {
            val row = "1\t$ordinal\tS\tPROTECTED_PROTOCOL_INFO_REJECTED\t${Long.MAX_VALUE}\t$ordinal\t${Long.MAX_VALUE}\t${Long.MAX_VALUE}\t${Int.MAX_VALUE}\tAUTHORIZATION_CONNECTION_LOST\n"
            if ((encoded + row).length > V1_TEST_CONNECTION_LOG_MAX_BYTES) break
            rows += row
            encoded += row
        }
        val storage = Storage().apply { bytes = encoded.toByteArray() }
        val log = V1TestConnectionLog(storage)
        assertEquals(V1TestConnectionLogStatus.READY, log.status)
        val before = log.snapshot()
        assertEquals(rows.size, before.size)
        assertTrue(log.startSession())
        val after = log.snapshot()
        assertTrue(after.size < before.size)
        assertEquals(before.takeLast(after.size), after)
        assertTrue(storage.bytes!!.size <= V1_TEST_CONNECTION_LOG_MAX_BYTES)
        assertEquals(after, V1TestConnectionLog(storage).snapshot())
    }

    @Test
    fun savedOwnerFlowRecordsTheExactObservedStageOrder() {
        val machine = V1TestConnectionTraceMachine()
        val stages = savedOwnerConnection(machine, GENERATION, from = 0)
        assertEquals(
            listOf(
                V1TestTraceStage.CONNECTION_ATTEMPT_STARTED,
                V1TestTraceStage.GATT_LINK_ESTABLISHED,
                V1TestTraceStage.PROTECTED_PROTOCOL_INFO_REQUESTED,
                V1TestTraceStage.PROTECTED_PROTOCOL_INFO_ACCEPTED,
                V1TestTraceStage.ATT_MTU_NEGOTIATED,
                V1TestTraceStage.STREAM_SUBSCRIPTION_ESTABLISHED,
                V1TestTraceStage.AUTHORIZATION_STARTED,
                V1TestTraceStage.AUTHORIZATION_ACCEPTED,
                V1TestTraceStage.SNAPSHOT_REQUESTED,
                V1TestTraceStage.SNAPSHOT_ACCEPTED,
                V1TestTraceStage.READY_REACHED,
            ),
            stages.map { it.stage },
        )
        // One transaction, strictly increasing ordinals, and a correlated elapsed distance.
        assertEquals(setOf(1L), stages.map { it.transaction }.toSet())
        assertContentEquals((1L..11L).toList(), stages.map { it.ordinal })
        assertEquals(setOf(GENERATION), stages.map { it.generation }.toSet())
        assertEquals(0L, stages.first().sinceMillis)
        assertTrue(stages.any { it.sinceMillis > 0 })
        assertEquals(setOf(V1TestTraceReason.NONE), stages.map { it.reason }.toSet())
        assertNull(machine.failure)
    }

    @Test
    fun connectedIsNotAuthorizedAndAuthorizedIsNotReady() {
        val machine = V1TestConnectionTraceMachine()
        assertEquals(
            listOf(V1TestTraceStage.CONNECTION_ATTEMPT_STARTED),
            machine.observe(GENERATION, 0, state(BleRuntimeState.Connecting(COMPANION))).map { it.stage },
        )
        // A GATT link plus an accepted protected read is still not authorization.
        val linked = machine.observe(GENERATION, 10, negotiating(BleNegotiationPhase.PROTOCOL_INFO)) +
            machine.observe(GENERATION, 20, negotiating(BleNegotiationPhase.ATT_MTU))
        assertFalse(linked.any { it.stage == V1TestTraceStage.AUTHORIZATION_ACCEPTED })
        assertFalse(linked.any { it.stage == V1TestTraceStage.READY_REACHED })
        // Reaching the claim phase is started, never accepted.
        val started = machine.observe(GENERATION, 30, negotiating(BleNegotiationPhase.STREAM_SUBSCRIPTION)) +
            machine.observe(GENERATION, 40, negotiating(BleNegotiationPhase.AUTHORIZATION_CLAIM))
        assertTrue(started.any { it.stage == V1TestTraceStage.AUTHORIZATION_STARTED })
        assertFalse(started.any { it.stage == V1TestTraceStage.AUTHORIZATION_ACCEPTED })
        assertFalse(started.any { it.stage == V1TestTraceStage.READY_REACHED })
        // Accepted authorization is still not Ready until the Snapshot is decoded.
        val accepted = machine.observe(GENERATION, 50, negotiating(BleNegotiationPhase.INITIAL_SNAPSHOT))
        assertEquals(
            listOf(V1TestTraceStage.AUTHORIZATION_ACCEPTED, V1TestTraceStage.SNAPSHOT_REQUESTED),
            accepted.map { it.stage },
        )
        assertFalse(accepted.any { it.stage == V1TestTraceStage.READY_REACHED })
    }

    @Test
    fun authorizationRejectionAndSnapshotFailureStayDistinguishable() {
        val denied = V1TestConnectionTraceMachine()
        denied.observe(GENERATION, 0, state(BleRuntimeState.Connecting(COMPANION)))
        denied.observe(GENERATION, 1, negotiating(BleNegotiationPhase.PROTOCOL_INFO))
        denied.observe(GENERATION, 2, negotiating(BleNegotiationPhase.STREAM_SUBSCRIPTION))
        denied.observe(GENERATION, 3, negotiating(BleNegotiationPhase.AUTHORIZATION_CLAIM))
        val authorizationFailure = denied.observe(
            GENERATION,
            4,
            state(BleRuntimeState.Failed(BleRuntimeFailure.AUTHORIZATION_CONNECTION_LOST)),
        )
        assertEquals(
            listOf(V1TestTraceStage.AUTHORIZATION_UNCERTAIN),
            authorizationFailure.map { it.stage },
        )
        assertEquals(V1TestTraceReason.AUTHORIZATION_CONNECTION_LOST, authorizationFailure.single().reason)

        val snapshot = V1TestConnectionTraceMachine()
        snapshot.observe(GENERATION, 0, state(BleRuntimeState.Connecting(COMPANION)))
        snapshot.observe(GENERATION, 1, negotiating(BleNegotiationPhase.PROTOCOL_INFO))
        snapshot.observe(GENERATION, 2, negotiating(BleNegotiationPhase.STREAM_SUBSCRIPTION))
        snapshot.observe(GENERATION, 3, negotiating(BleNegotiationPhase.AUTHORIZATION_CLAIM))
        snapshot.observe(GENERATION, 4, negotiating(BleNegotiationPhase.INITIAL_SNAPSHOT))
        val snapshotFailure = snapshot.observe(
            GENERATION,
            5,
            state(BleRuntimeState.Failed(BleRuntimeFailure.INITIAL_SNAPSHOT_FAILED)),
        )
        assertEquals(listOf(V1TestTraceStage.SNAPSHOT_REJECTED), snapshotFailure.map { it.stage })
        assertEquals(V1TestTraceReason.INITIAL_SNAPSHOT_FAILED, snapshotFailure.single().reason)

        val protectedRead = V1TestConnectionTraceMachine()
        protectedRead.observe(GENERATION, 0, state(BleRuntimeState.Connecting(COMPANION)))
        protectedRead.observe(GENERATION, 1, negotiating(BleNegotiationPhase.PROTOCOL_INFO))
        val readFailure = protectedRead.observe(
            GENERATION,
            2,
            state(BleRuntimeState.Failed(BleRuntimeFailure.PROTOCOL_INFO_FAILED)),
        )
        assertEquals(
            listOf(V1TestTraceStage.PROTECTED_PROTOCOL_INFO_REJECTED),
            readFailure.map { it.stage },
        )

        // Every explicit-claim authorization outcome maps to its own typed stage.
        listOf(
            DeviceAuthorizationUiState.Denied(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE) to
                V1TestTraceStage.AUTHORIZATION_DENIED,
            DeviceAuthorizationUiState.Unavailable(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE) to
                V1TestTraceStage.AUTHORIZATION_UNAVAILABLE,
            DeviceAuthorizationUiState.Unsupported(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE) to
                V1TestTraceStage.AUTHORIZATION_UNAVAILABLE,
            DeviceAuthorizationUiState.Expired(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE) to
                V1TestTraceStage.AUTHORIZATION_UNCERTAIN,
            DeviceAuthorizationUiState.InvalidResult(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE) to
                V1TestTraceStage.AUTHORIZATION_UNCERTAIN,
            DeviceAuthorizationUiState.AuthorityUnknown(DeviceAuthorizationPurpose.AUTHORIZE_THIS_PHONE) to
                V1TestTraceStage.AUTHORIZATION_UNCERTAIN,
        ).forEach { (authorization, expected) ->
            val outcome = V1TestConnectionTraceMachine()
            outcome.observe(GENERATION, 0, state(BleRuntimeState.Connecting(COMPANION)))
            val emissions = outcome.observe(GENERATION, 1, state(BleRuntimeState.Connecting(COMPANION), authorization))
            assertEquals(listOf(expected), emissions.map { it.stage }, authorization.toString())
        }
    }

    @Test
    fun staleGenerationCannotProduceSuccessOrAdvanceTheTrace() {
        val machine = V1TestConnectionTraceMachine()
        savedOwnerConnection(machine, generation = 9, from = 0)
        // A late callback from the released service generation is refused outright.
        assertTrue(machine.observe(8, 200, state(BleRuntimeState.Connecting(COMPANION))).isEmpty())
        assertTrue(machine.observe(8, 201, readyState()).isEmpty())
        assertEquals(2, machine.staleObservations)
        assertNull(machine.failure)
        // A newer generation closes the stale transaction before anything new is recorded.
        val next = machine.observe(10, 210, state(BleRuntimeState.Connecting(COMPANION)))
        assertEquals(
            listOf(V1TestTraceStage.DISCONNECT_OBSERVED, V1TestTraceStage.CONNECTION_ATTEMPT_STARTED),
            next.map { it.stage },
        )
        assertEquals(V1TestTraceReason.SERVICE_GENERATION_CHANGED, next.first().reason)
        assertEquals(1L, next.first().transaction)
        assertEquals(2L, next.last().transaction)
    }

    @Test
    fun duplicateCallbacksNeverFabricateDuplicateSuccessfulTransactions() {
        val machine = V1TestConnectionTraceMachine()
        savedOwnerConnection(machine, GENERATION, from = 0)
        // Repeated Ready snapshots and repeated identical phases add nothing.
        assertTrue(machine.observe(GENERATION, 120, readyState()).isEmpty())
        assertTrue(machine.observe(GENERATION, 130, readyState()).isEmpty())
        assertTrue(machine.observe(GENERATION, 140, readyState()).isEmpty())
        // The runtime republishes Reconnecting once when scheduling and once when starting.
        val first = machine.observe(GENERATION, 150, reconnecting(1))
        assertEquals(
            listOf(V1TestTraceStage.DISCONNECT_OBSERVED, V1TestTraceStage.RECONNECT_ATTEMPT_STARTED),
            first.map { it.stage },
        )
        assertTrue(machine.observe(GENERATION, 151, reconnecting(1)).isEmpty())
        assertTrue(machine.observe(GENERATION, 152, reconnecting(1)).isEmpty())
    }

    @Test
    fun disconnectClosesTheActiveTransaction() {
        val machine = V1TestConnectionTraceMachine()
        savedOwnerConnection(machine, GENERATION, from = 0)
        val closed = machine.observe(GENERATION, 200, state(BleRuntimeState.Idle))
        assertEquals(listOf(V1TestTraceStage.DISCONNECT_OBSERVED), closed.map { it.stage })
        assertEquals(1L, closed.single().transaction)
        assertEquals(V1TestTraceReason.LINK_CLOSED, closed.single().reason)
        // A second quiescent observation cannot close an already closed transaction.
        assertTrue(machine.observe(GENERATION, 210, state(BleRuntimeState.Inactive)).isEmpty())
        // Ready without an open transaction is never back-filled into a success.
        assertTrue(machine.observe(GENERATION, 220, readyState()).isEmpty())

        val released = V1TestConnectionTraceMachine()
        savedOwnerConnection(released, GENERATION, from = 0)
        val binding = released.observeServiceDisconnected(300)
        assertEquals(listOf(V1TestTraceStage.DISCONNECT_OBSERVED), binding.map { it.stage })
        assertEquals(V1TestTraceReason.SERVICE_BINDING_RELEASED, binding.single().reason)
    }

    @Test
    fun reconnectStartsANewCorrelatedTransaction() {
        val machine = V1TestConnectionTraceMachine()
        savedOwnerConnection(machine, GENERATION, from = 0)
        val lost = machine.observe(
            GENERATION,
            200,
            state(
                BleRuntimeState.Reconnecting(
                    COMPANION,
                    attempt = 2,
                    maximumAttempts = 3,
                    connectionDiagnostic = BleConnectionDiagnostic.GATT_TRANSIENT_LINK,
                ),
            ),
        )
        assertEquals(V1TestTraceReason.GATT_TRANSIENT_LINK, lost.first().reason)
        assertEquals(BleConnectionDiagnostic.GATT_TRANSIENT_LINK, lost.first().connectionDiagnostic)
        assertNull(lost.last().connectionDiagnostic)
        assertEquals(1L, lost.first().transaction)
        val started = lost.last()
        assertEquals(V1TestTraceStage.RECONNECT_ATTEMPT_STARTED, started.stage)
        assertEquals(2L, started.transaction)
        assertEquals(2, started.attempt)
        assertEquals(0L, started.sinceMillis)
        // The second transaction repeats the complete protected sequence with its own correlation.
        val second = savedOwnerConnection(machine, GENERATION, from = 210, alreadyStarted = true)
        assertEquals(setOf(2L), second.map { it.transaction }.toSet())
        assertEquals(V1TestTraceStage.READY_REACHED, second.last().stage)
        assertTrue(second.first().ordinal > started.ordinal)
        // A periodic saved-owner cycle is a discovery boundary, then a fresh transaction.
        val discovery = machine.observe(GENERATION, 300, state(BleRuntimeState.FindingReturningOwner))
        assertEquals(
            listOf(V1TestTraceStage.DISCONNECT_OBSERVED, V1TestTraceStage.RETURNING_OWNER_DISCOVERY_STARTED),
            discovery.map { it.stage },
        )
        assertEquals(0L, discovery.last().transaction)
        val third = machine.observe(GENERATION, 310, reconnecting(3, periodic = true))
        assertEquals(listOf(V1TestTraceStage.RECONNECT_ATTEMPT_STARTED), third.map { it.stage })
        assertEquals(3L, third.single().transaction)
    }

    @Test
    fun elapsedRollbackFailsClosed() {
        val machine = V1TestConnectionTraceMachine()
        machine.observe(GENERATION, 100, state(BleRuntimeState.Connecting(COMPANION)))
        assertTrue(machine.observe(GENERATION, 99, negotiating(BleNegotiationPhase.PROTOCOL_INFO)).isEmpty())
        assertEquals(V1TestTraceFailure.ELAPSED_ROLLBACK, machine.failure)
        // Latched: no later observation, including a Ready one, can be recorded.
        assertTrue(machine.observe(GENERATION, 500, readyState()).isEmpty())
        assertTrue(machine.observeServiceDisconnected(600).isEmpty())
        assertEquals(V1TestTraceFailure.ELAPSED_ROLLBACK, machine.failure)
        machine.reset()
        assertNull(machine.failure)
        assertEquals(
            listOf(V1TestTraceStage.CONNECTION_ATTEMPT_STARTED),
            machine.observe(GENERATION, 0, state(BleRuntimeState.Connecting(COMPANION))).map { it.stage },
        )
        val negative = V1TestConnectionTraceMachine()
        assertTrue(negative.observe(GENERATION, -1, state(BleRuntimeState.Connecting(COMPANION))).isEmpty())
        assertEquals(V1TestTraceFailure.ELAPSED_ROLLBACK, negative.failure)
    }

    @Test
    fun version3RoundTripsCanonicallyAndVersion1RemainsReadable() {
        val version1 = Storage().apply {
            bytes = "OTCL\t1\t2\n1\t5\tC\tREADY\n2\t7\tL\tFOREGROUND\n".toByteArray()
        }
        val upgraded = V1TestConnectionLog(version1)
        assertEquals(V1TestConnectionLogStatus.READY, upgraded.status)
        assertEquals(1, upgraded.loadedVersion)
        assertEquals(2, upgraded.snapshot().size)
        assertTrue(upgraded.startSession())
        // The first write upgrades the durable file; the version-1 records are preserved exactly.
        assertTrue(requireNotNull(version1.bytes).decodeToString().startsWith("OTCL\t3\t3\n"))
        assertEquals(2, upgraded.snapshot().size)

        val machine = V1TestConnectionTraceMachine()
        val emissions = savedOwnerConnection(machine, GENERATION, from = 0)
        val storage = Storage()
        val log = V1TestConnectionLog(storage)
        assertEquals(3, log.loadedVersion)
        assertTrue(log.startSession())
        emissions.forEachIndexed { index, emission ->
            assertTrue(log.recordTrace(index.toLong(), emission))
        }
        val encoded = requireNotNull(storage.bytes).decodeToString()
        assertTrue(encoded.startsWith("OTCL\t3\t1\n"))
        assertTrue(
            encoded.contains("1\t0\tS\tCONNECTION_ATTEMPT_STARTED\t1\t1\t$GENERATION\t0\t0\tNONE\tUNAVAILABLE\n"),
            encoded,
        )
        val reloaded = V1TestConnectionLog(storage)
        assertEquals(3, reloaded.loadedVersion)
        assertEquals(log.snapshot(), reloaded.snapshot())
        assertEquals(
            emissions,
            reloaded.snapshot().mapNotNull { (it.event as? V1TestConnectionLogEvent.Trace)?.emission },
        )
        // Re-encoding the reloaded history is byte-identical, so the stored form is canonical.
        val second = Storage()
        val rewritten = V1TestConnectionLog(second)
        assertTrue(rewritten.startSession())
        reloaded.snapshot().forEach {
            val trace = (it.event as V1TestConnectionLogEvent.Trace).emission
            assertTrue(rewritten.recordTrace(it.elapsedMillis, trace))
        }
        assertContentEquals(storage.bytes, second.bytes)
        assertTrue(reloaded.exportText().contains("format 3, loaded 3"))
        assertTrue(reloaded.exportText().contains("never from a UI label"))
    }

    @Test
    fun malformedInconsistentAndUnsupportedTraceDataIsRejected() {
        val malformed = listOf(
            // Unsupported and impossible versions.
            "OTCL\t4\t1\n",
            "OTCL\t0\t1\n",
            // A stage record inside a version-1 file.
            "OTCL\t1\t1\n1\t0\tS\tREADY_REACHED\t1\t1\t1\t0\t0\tNONE\n",
            // Wrong field count.
            "OTCL\t2\t1\n1\t0\tS\tREADY_REACHED\t1\t1\t1\t0\t0\n",
            "OTCL\t2\t1\n1\t0\tS\tREADY_REACHED\t1\t1\t1\t0\t0\tNONE\tEXTRA\n",
            "OTCL\t2\t1\n1\t0\tC\tREADY\tEXTRA\n",
            // Unknown stage and unknown reason.
            "OTCL\t2\t1\n1\t0\tS\tNOT_A_STAGE\t1\t1\t1\t0\t0\tNONE\n",
            "OTCL\t2\t1\n1\t0\tS\tREADY_REACHED\t1\t1\t1\t0\t0\tNOT_A_REASON\n",
            // Reordered and repeated ordinals.
            "OTCL\t2\t1\n1\t0\tS\tREADY_REACHED\t1\t2\t1\t0\t0\tNONE\n1\t1\tS\tDISCONNECT_OBSERVED\t1\t1\t1\t0\t0\tNONE\n",
            "OTCL\t2\t1\n1\t0\tS\tREADY_REACHED\t1\t1\t1\t0\t0\tNONE\n1\t1\tS\tDISCONNECT_OBSERVED\t1\t1\t1\t0\t0\tNONE\n",
            // Zero and negative ordinals.
            "OTCL\t2\t1\n1\t0\tS\tREADY_REACHED\t1\t0\t1\t0\t0\tNONE\n",
            "OTCL\t2\t1\n1\t0\tS\tREADY_REACHED\t1\t-1\t1\t0\t0\tNONE\n",
            // Non-canonical numbers.
            "OTCL\t2\t1\n1\t0\tS\tREADY_REACHED\t01\t1\t1\t0\t0\tNONE\n",
            // Inconsistent correlation: no transaction cannot carry a correlated distance.
            "OTCL\t2\t1\n1\t0\tS\tREADY_REACHED\t0\t1\t1\t5\t0\tNONE\n",
        )
        malformed.forEach { value ->
            val storage = Storage().apply { bytes = value.toByteArray() }
            val log = V1TestConnectionLog(storage)
            assertEquals(V1TestConnectionLogStatus.CORRUPT, log.status, value)
            assertFalse(log.startSession())
            assertEquals(0, storage.writes)
        }
        // Ordinals restart with each recording session, so a lower ordinal in a later session is valid.
        val valid = Storage().apply {
            bytes = (
                "OTCL\t2\t2\n" +
                    "1\t0\tS\tREADY_REACHED\t1\t4\t1\t0\t0\tNONE\n" +
                    "2\t0\tS\tCONNECTION_ATTEMPT_STARTED\t1\t1\t1\t0\t0\tNONE\n"
                ).toByteArray()
        }
        assertEquals(V1TestConnectionLogStatus.READY, V1TestConnectionLog(valid).status)
    }

    @Test
    fun storageFailurePreventsPublicationOfARecordedStage() {
        val machine = V1TestConnectionTraceMachine()
        val emissions = savedOwnerConnection(machine, GENERATION, from = 0)
        val storage = Storage()
        val log = V1TestConnectionLog(storage)
        assertTrue(log.startSession())
        assertTrue(log.recordTrace(0, emissions.first()))
        val published = log.snapshot()
        val durable = requireNotNull(storage.bytes).copyOf()
        storage.writeResult = false
        assertFalse(log.recordTrace(1, emissions[1]))
        assertEquals(V1TestConnectionLogStatus.STORAGE_FAILURE, log.status)
        assertEquals(published, log.snapshot())
        assertFalse(log.exportText().contains(emissions[1].stage.name))
        assertContentEquals(durable, storage.bytes)
        storage.throwErrors = true
        assertFalse(log.recordTrace(2, emissions[1]))
        assertEquals(V1TestConnectionLogStatus.STORAGE_FAILURE, log.status)
        storage.throwErrors = false
        storage.writeResult = true
        assertTrue(log.recordTrace(3, emissions[1]))
        assertTrue(log.exportText().contains(emissions[1].stage.name))
        assertEquals(V1TestConnectionLogStatus.READY, log.status)
    }

    @Test
    fun theRecordAndByteLimitsRemainEnforcedForStageRecords() {
        val storage = Storage()
        val log = V1TestConnectionLog(storage)
        assertTrue(log.startSession())
        val wide = V1TestTraceEmission(
            stage = V1TestTraceStage.PROTECTED_PROTOCOL_INFO_REQUESTED,
            transaction = Long.MAX_VALUE,
            ordinal = 1,
            generation = Long.MAX_VALUE,
            sinceMillis = Long.MAX_VALUE,
            attempt = Int.MAX_VALUE,
            reason = V1TestTraceReason.AUTHORIZATION_AUTHORITY_UNKNOWN,
            connectionDiagnostic = BleConnectionDiagnostic.DISCONNECTED_BEFORE_PROFILE,
        )
        repeat(600) { index ->
            assertTrue(log.recordTrace(index.toLong(), wide.copy(ordinal = index + 1L)))
        }
        val records = log.snapshot()
        val bytes = requireNotNull(storage.bytes)
        assertTrue(records.size <= V1_TEST_CONNECTION_LOG_MAX_RECORDS, "records=${records.size}")
        assertTrue(bytes.size <= V1_TEST_CONNECTION_LOG_MAX_BYTES, "bytes=${bytes.size}")
        // These wide records exceed the byte budget before the record cap, so the byte budget
        // is the binding limit here and eviction is proven, not incidental.
        assertTrue(records.size < V1_TEST_CONNECTION_LOG_MAX_RECORDS, "records=${records.size}")
        assertTrue(bytes.size > V1_TEST_CONNECTION_LOG_MAX_BYTES - 400, "bytes=${bytes.size}")
        // Oldest-first eviction: the newest terminal evidence always survives.
        assertEquals(600L, (records.last().event as V1TestConnectionLogEvent.Trace).emission.ordinal)
        assertEquals(V1TestConnectionLogStatus.READY, log.status)
        assertEquals(records, V1TestConnectionLog(storage).snapshot())
        // Narrow records stay bound by the 512-record cap instead.
        val narrowStorage = Storage()
        val narrow = V1TestConnectionLog(narrowStorage)
        assertTrue(narrow.startSession())
        repeat(600) { index ->
            assertTrue(
                narrow.recordTrace(
                    index.toLong(),
                    V1TestTraceEmission(
                        stage = V1TestTraceStage.READY_REACHED,
                        transaction = 1,
                        ordinal = index + 1L,
                        generation = 1,
                        sinceMillis = 0,
                        attempt = 0,
                        reason = V1TestTraceReason.NONE,
                    ),
                ),
            )
        }
        assertEquals(V1_TEST_CONNECTION_LOG_MAX_RECORDS, narrow.snapshot().size)
        assertTrue(requireNotNull(narrowStorage.bytes).size <= V1_TEST_CONNECTION_LOG_MAX_BYTES)
    }

    private fun savedOwnerConnection(
        machine: V1TestConnectionTraceMachine,
        generation: Long,
        from: Long,
        alreadyStarted: Boolean = false,
    ): List<V1TestTraceEmission> {
        val emissions = mutableListOf<V1TestTraceEmission>()
        if (!alreadyStarted) {
            emissions += machine.observe(generation, from, state(BleRuntimeState.Connecting(COMPANION)))
        }
        emissions += machine.observe(generation, from + 10, negotiating(BleNegotiationPhase.PROTOCOL_INFO))
        emissions += machine.observe(generation, from + 20, negotiating(BleNegotiationPhase.ATT_MTU))
        emissions += machine.observe(generation, from + 30, negotiating(BleNegotiationPhase.STREAM_SUBSCRIPTION))
        emissions += machine.observe(generation, from + 40, negotiating(BleNegotiationPhase.AUTHORIZATION_CLAIM))
        emissions += machine.observe(generation, from + 50, negotiating(BleNegotiationPhase.INITIAL_SNAPSHOT))
        emissions += machine.observe(generation, from + 60, readyState())
        return emissions
    }

    private fun negotiating(phase: BleNegotiationPhase) =
        state(BleRuntimeState.Negotiating(COMPANION, phase))

    private fun reconnecting(attempt: Int, periodic: Boolean = false) = state(
        BleRuntimeState.Reconnecting(COMPANION, attempt, 3, null, periodic),
    )

    private fun readyState() = state(
        BleRuntimeState.Ready(
            BleActiveSession(
                companion = COMPANION,
                sessionNonce = 1,
                snapshot = CompanionStatusSnapshot(
                    revision = 1,
                    radio = CompanionRadioState.UNKNOWN,
                    gnss = CompanionGnssState.UNKNOWN,
                    power = CompanionPowerState.UNKNOWN,
                    positionSharing = CompanionPositionSharingState.STOPPED,
                    queuedActionCount = 0,
                ),
                protocolInfo = CompanionProtocolInfo(capabilities = 0),
                groupLocation = requireNotNull(
                    GroupLocationSnapshot.authoritativeUnavailable(1, CompanionPositionSharingState.STOPPED),
                ),
            ),
        ),
    )

    private fun state(
        runtime: BleRuntimeState,
        authorization: DeviceAuthorizationUiState = DeviceAuthorizationUiState.None,
    ) = TrailAppUiState.BluetoothDevice(
        runtimeState = runtime,
        permissionState = NearbyDevicesPermissionState.GRANTED,
        permissionRequestInFlight = false,
        permissionWasDenied = false,
        authorizationState = authorization,
    )

    private class Storage : V1TestConnectionLogStorage {
        var bytes: ByteArray? = null
        var writes = 0
        var writeResult = true
        var throwErrors = false
        override fun read(): ByteArray? { check(!throwErrors); return bytes?.copyOf() }
        override fun write(encoded: ByteArray): Boolean {
            check(!throwErrors)
            if (!writeResult) return false
            bytes = encoded.copyOf(); writes++; return true
        }
        override fun clear(): Boolean { check(!throwErrors); bytes = null; return true }
    }

    private companion object {
        const val GENERATION = 7L
        val COMPANION = BleDiscoveredCompanion("opaque-test-token", "Test companion")
    }
}
