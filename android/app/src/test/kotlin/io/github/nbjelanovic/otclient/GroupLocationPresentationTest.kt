package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionActionKind
import io.github.nbjelanovic.otprotocol.CompanionActionRequest
import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState
import java.io.File
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNotEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class GroupLocationPresentationTest {
    @Test
    fun deterministicFixturesAreBoundedMarkedFakeAndStable() {
        val first = assertNotNull(DeterministicGroupLocationFixtures.forEndpoint("fake-a"))
        val repeated = assertNotNull(DeterministicGroupLocationFixtures.forEndpoint("fake-a"))
        val second = assertNotNull(DeterministicGroupLocationFixtures.forEndpoint("fake-b"))

        assertEquals(first, repeated)
        assertNotEquals(first, second)
        assertEquals(GroupLocationProvenance.LOCAL_TEST_FIXTURE, first.provenance)
        assertEquals(GroupPositionState.CURRENT, first.selfPosition.state)
        assertEquals(listOf("Bench Partner B"), first.peers.map { it.displayAlias.value })
        assertNull(DeterministicGroupLocationFixtures.forEndpoint("unknown"))
    }

    @Test
    fun coordinatesAndStringSurfacesAreBoundedAndRedacted() {
        assertNotNull(GroupLocationCoordinate.create(-900_000_000, -1_800_000_000))
        assertNotNull(GroupLocationCoordinate.create(900_000_000, 1_800_000_000))
        assertNull(GroupLocationCoordinate.create(-900_000_001, 0))
        assertNull(GroupLocationCoordinate.create(0, 1_800_000_001))

        val fixture = assertNotNull(DeterministicGroupLocationFixtures.forEndpoint("fake-a"))
        val renderedState = fixture.toString() + fixture.selfPosition + fixture.selfPosition.coordinate
        assertFalse(renderedState.contains("44.1"))
        assertFalse(renderedState.contains("-70.2"))
        assertFalse(renderedState.contains("fake-a"))
        assertTrue(renderedState.contains("redacted"))
    }

    @Test
    fun explicitCurrentAndStalePositionsRequireBoundedSourceAgeAndOptionalAccuracy() {
        val point = assertNotNull(GroupLocationCoordinate.create(100, -200))
        assertNotNull(GroupLocationPosition.available(GroupPositionState.CURRENT, point, 0, null))
        assertNotNull(
            GroupLocationPosition.available(
                GroupPositionState.STALE,
                point,
                CURRENT_POSITION_MAX_AGE_SECONDS + 1,
                1,
            ),
        )
        assertNull(
            GroupLocationPosition.available(
                GroupPositionState.CURRENT,
                point,
                CURRENT_POSITION_MAX_AGE_SECONDS + 1,
                1,
            ),
        )
        assertNull(
            GroupLocationPosition.available(
                GroupPositionState.STALE,
                point,
                CURRENT_POSITION_MAX_AGE_SECONDS,
                1,
            ),
        )
        assertNull(GroupLocationPosition.available(GroupPositionState.UNAVAILABLE, point, 0, 1))
        assertNull(GroupLocationPosition.available(GroupPositionState.CURRENT, point, -1, 1))
        assertNull(
            GroupLocationPosition.available(
                GroupPositionState.STALE,
                point,
                MAX_DEVICE_REPORTED_POSITION_AGE_SECONDS + 1,
                1,
            ),
        )
        assertNull(GroupLocationPosition.available(GroupPositionState.CURRENT, point, 1, 0))
        assertNull(
            GroupLocationPosition.available(
                GroupPositionState.CURRENT,
                point,
                1,
                MAX_DEVICE_REPORTED_ACCURACY_METERS + 1,
            ),
        )
        val unavailable = GroupLocationPosition.unavailable()
        assertEquals(GroupPositionState.UNAVAILABLE, unavailable.state)
        assertNull(unavailable.coordinate)
        assertNull(unavailable.ageSeconds)
        assertNull(unavailable.accuracyMeters)
    }

    @Test
    fun publicAliasesRejectIdentifierLikeOrAmbiguousInputsAndDuplicates() {
        assertNotNull(StablePeerDisplayAlias.create("Ridge Scout 2"))
        val syntheticMacSegments = listOf("A0", "B1", "C2", "D3", "E4", "F5")
        listOf(
            "",
            " leading",
            "trailing ",
            syntheticMacSegments.joinToString(":"),
            syntheticMacSegments.joinToString(""),
            syntheticMacSegments.joinToString(".").lowercase(),
            syntheticMacSegments.joinToString(" "),
            "123e4567-e89b-12d3-a456-426614174000",
            "0123456789abcdef0123456789abcdef",
            "bad\nname",
            "x".repeat(MAX_PEER_DISPLAY_ALIAS_CHARS + 1),
        ).forEach { assertNull(StablePeerDisplayAlias.create(it), it) }

        val peerA = peer("Camp One")
        val peerADuplicate = peer("camp one")
        assertNull(
            GroupLocationSnapshot.create(
                GroupLocationProvenance.DEVICE_AUTHORITATIVE_SNAPSHOT,
                1,
                CompanionPositionSharingState.ACTIVE,
                GroupLocationPosition.unavailable(),
                listOf(peerA, peerADuplicate),
            ),
        )
    }

    @Test
    fun snapshotBoundsRevisionPeerCountAndCopiesCallerList() {
        assertNull(
            GroupLocationSnapshot.authoritativeUnavailable(0, CompanionPositionSharingState.STOPPED),
        )
        assertNull(
            GroupLocationSnapshot.authoritativeUnavailable(0x1_0000_0000L, CompanionPositionSharingState.STOPPED),
        )
        val mutablePeers = MutableList(MAX_GROUP_LOCATION_PEERS) { peer("Unit ${it + 1}") }
        val accepted = assertNotNull(
            GroupLocationSnapshot.create(
                GroupLocationProvenance.DEVICE_AUTHORITATIVE_SNAPSHOT,
                1,
                CompanionPositionSharingState.ACTIVE,
                GroupLocationPosition.unavailable(),
                mutablePeers,
            ),
        )
        mutablePeers.clear()
        assertEquals(MAX_GROUP_LOCATION_PEERS, accepted.peers.size)
        assertNull(
            GroupLocationSnapshot.create(
                GroupLocationProvenance.DEVICE_AUTHORITATIVE_SNAPSHOT,
                1,
                CompanionPositionSharingState.ACTIVE,
                GroupLocationPosition.unavailable(),
                List(MAX_GROUP_LOCATION_PEERS + 1) { peer("Peer ${it + 1}") },
            ),
        )
    }

    @Test
    fun realSnapshotFoundationIsUnavailableUntilDeviceSuppliesLocationPayload() {
        val snapshot = assertNotNull(
            GroupLocationSnapshot.authoritativeUnavailable(9, CompanionPositionSharingState.WAITING_FOR_FIX),
        )
        assertEquals(GroupLocationProvenance.DEVICE_AUTHORITATIVE_SNAPSHOT, snapshot.provenance)
        assertEquals(GroupPositionState.UNAVAILABLE, snapshot.selfPosition.state)
        assertTrue(snapshot.peers.isEmpty())
        assertEquals(CompanionPositionSharingState.WAITING_FOR_FIX, snapshot.sharingState)
    }

    @Test
    fun statusOnlyAuthoritativeUpdateDegradesPositionsInsteadOfRefreshingThem() {
        val point = assertNotNull(GroupLocationCoordinate.create(100, 200))
        val current = assertNotNull(
            GroupLocationPosition.available(GroupPositionState.CURRENT, point, 20, 4),
        )
        val snapshot = assertNotNull(
            GroupLocationSnapshot.create(
                GroupLocationProvenance.DEVICE_AUTHORITATIVE_SNAPSHOT,
                7,
                CompanionPositionSharingState.ACTIVE,
                current,
                listOf(assertNotNull(GroupLocationPeer.create("Ridge Scout", current))),
            ),
        )

        val statusOnly = assertNotNull(snapshot.withAuthoritativeStatus(8, CompanionPositionSharingState.DEFERRED))
        assertEquals(8, statusOnly.sourceSnapshotRevision)
        assertEquals(GroupPositionState.UNAVAILABLE, statusOnly.selfPosition.state)
        assertEquals(GroupPositionState.UNAVAILABLE, statusOnly.peers.single().position.state)
        assertNull(snapshot.withFakeStatus(8, CompanionPositionSharingState.ACTIVE))
    }

    @Test
    fun localActionsAdvanceOnlyDeterministicFixtureContextAndDisconnectDropsPresentationState() {
        val controller = CompanionAppController(FakeCompanionTransport())
        controller.chooseDevice()
        controller.connect("fake-a")
        var connected = assertIs<CompanionUiState.Connected>(controller.state)
        val originalCoordinate = connected.connection.groupLocation.selfPosition.coordinate

        controller.submitAction(CompanionActionRequest(CompanionActionKind.START_POSITION_SHARING))
        connected = assertIs<CompanionUiState.Connected>(controller.state)
        assertEquals(2, connected.connection.groupLocation.sourceSnapshotRevision)
        assertEquals(CompanionPositionSharingState.WAITING_FOR_FIX, connected.connection.groupLocation.sharingState)
        assertEquals(originalCoordinate, connected.connection.groupLocation.selfPosition.coordinate)
        assertEquals(GroupLocationProvenance.LOCAL_TEST_FIXTURE, connected.connection.groupLocation.provenance)

        controller.disconnect()
        assertIs<CompanionUiState.Disconnected>(controller.state)
    }

    @Test
    fun fakeTransportRejectsDeviceProvenanceInsteadOfRelabelingIt() {
        val deviceOnly = assertNotNull(
            GroupLocationSnapshot.authoritativeUnavailable(1, CompanionPositionSharingState.STOPPED),
        )
        val transport = FakeCompanionTransport(initialGroupLocations = mapOf("fake-a" to deviceOnly))
        assertEquals(
            "The deterministic fake location fixture is unavailable.",
            assertIs<ConnectionAttempt.Failed>(transport.connect("fake-a")).publicReason,
        )
    }

    @Test
    fun uiIsScrollableAccessibleAndContainsNoPhoneGpsOrMapDependency() {
        val source = projectFile("src/main/kotlin/io/github/nbjelanovic/otclient/MainActivity.kt").readText()
        val build = projectFile("build.gradle.kts").readText()
        val manifest = projectFile("src/main/AndroidManifest.xml").readText()
        assertTrue(source.contains("verticalScroll(rememberScrollState())"))
        assertTrue(source.contains("Open Group / Location"))
        assertTrue(source.contains("Close Group / Location"))
        assertTrue(source.contains("Modifier.semantics { heading() }"))
        assertTrue(source.contains("Device-authoritative snapshot only"))
        assertTrue(source.contains("Deterministic fake location fixture"))
        assertTrue(source.contains("val sourceLabel = if (isFake) \"Fake fixture\""))
        assertTrue(source.contains("\$sourceLabel position sharing"))
        assertTrue(source.contains("\$sourceLabel position"))
        assertTrue(source.contains("\$sourceLabel age"))
        assertTrue(source.contains("\$sourceLabel accuracy"))
        listOf("LocationManager", "FusedLocationProviderClient", "GoogleMap", "MapView").forEach {
            assertFalse(source.contains(it))
            assertFalse(build.contains(it))
        }
        assertFalse(manifest.contains("android.permission.ACCESS_FINE_LOCATION"))
        assertFalse(manifest.contains("android.permission.ACCESS_COARSE_LOCATION"))
        assertFalse(manifest.contains("android.permission.INTERNET"))
    }

    private fun peer(alias: String): GroupLocationPeer = assertNotNull(
        GroupLocationPeer.create(alias, GroupLocationPosition.unavailable()),
    )

    private fun projectFile(path: String): File {
        val direct = File(path)
        if (direct.isFile) return direct
        val fromAndroid = File("app", path)
        require(fromAndroid.isFile) { "Required Android source file is missing: $path" }
        return fromAndroid
    }
}
