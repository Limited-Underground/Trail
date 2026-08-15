package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState
import java.util.Locale

internal const val MAX_GROUP_LOCATION_PEERS = 8
internal const val MAX_PEER_DISPLAY_ALIAS_CHARS = 40
internal const val MAX_DEVICE_REPORTED_POSITION_AGE_SECONDS = 604_800L
internal const val MAX_DEVICE_REPORTED_ACCURACY_METERS = 100_000
internal const val CURRENT_POSITION_MAX_AGE_SECONDS = 300L

/** Identifies the only two presentation inputs accepted by this host build. */
enum class GroupLocationProvenance {
    DEVICE_AUTHORITATIVE_SNAPSHOT,
    LOCAL_TEST_FIXTURE,
}

/** This is copied from the source snapshot; the phone never infers CURRENT from connectivity. */
enum class GroupPositionState {
    CURRENT,
    STALE,
    UNAVAILABLE,
}

/**
 * Renderer-neutral fixed-point coordinate. Its string form is deliberately redacted so normal
 * state/error logging cannot disclose a location accidentally. Coordinates are exposed only to
 * the explicit Group / Location renderer.
 */
class GroupLocationCoordinate private constructor(
    val latitudeE7: Int,
    val longitudeE7: Int,
) {
    fun latitudeDegrees(): Double = latitudeE7 / 10_000_000.0
    fun longitudeDegrees(): Double = longitudeE7 / 10_000_000.0

    override fun equals(other: Any?): Boolean =
        other is GroupLocationCoordinate && latitudeE7 == other.latitudeE7 && longitudeE7 == other.longitudeE7

    override fun hashCode(): Int = 31 * latitudeE7 + longitudeE7

    override fun toString(): String = "GroupLocationCoordinate(redacted)"

    companion object {
        fun create(latitudeE7: Int, longitudeE7: Int): GroupLocationCoordinate? {
            if (latitudeE7 !in -900_000_000..900_000_000) return null
            if (longitudeE7 !in -1_800_000_000..1_800_000_000) return null
            return GroupLocationCoordinate(latitudeE7, longitudeE7)
        }
    }
}

class GroupLocationPosition private constructor(
    val state: GroupPositionState,
    val coordinate: GroupLocationCoordinate?,
    /** Age supplied by the device snapshot or deterministic fixture; never computed from wall time. */
    val ageSeconds: Long?,
    /** Optional source-reported horizontal accuracy; null means the source did not provide it. */
    val accuracyMeters: Int?,
) {
    override fun equals(other: Any?): Boolean =
        other is GroupLocationPosition &&
            state == other.state &&
            coordinate == other.coordinate &&
            ageSeconds == other.ageSeconds &&
            accuracyMeters == other.accuracyMeters

    override fun hashCode(): Int {
        var result = state.hashCode()
        result = 31 * result + (coordinate?.hashCode() ?: 0)
        result = 31 * result + (ageSeconds?.hashCode() ?: 0)
        return 31 * result + (accuracyMeters ?: 0)
    }

    override fun toString(): String =
        "GroupLocationPosition(state=$state, coordinate=${if (coordinate == null) "none" else "redacted"}, " +
            "ageSeconds=$ageSeconds, accuracyMeters=$accuracyMeters)"

    companion object {
        fun available(
            state: GroupPositionState,
            coordinate: GroupLocationCoordinate,
            ageSeconds: Long,
            accuracyMeters: Int?,
        ): GroupLocationPosition? {
            if (state == GroupPositionState.UNAVAILABLE) return null
            if (ageSeconds !in 0..MAX_DEVICE_REPORTED_POSITION_AGE_SECONDS) return null
            if (state == GroupPositionState.CURRENT && ageSeconds > CURRENT_POSITION_MAX_AGE_SECONDS) return null
            if (state == GroupPositionState.STALE && ageSeconds <= CURRENT_POSITION_MAX_AGE_SECONDS) return null
            if (accuracyMeters != null && accuracyMeters !in 1..MAX_DEVICE_REPORTED_ACCURACY_METERS) return null
            return GroupLocationPosition(state, coordinate, ageSeconds, accuracyMeters)
        }

        fun unavailable(): GroupLocationPosition =
            GroupLocationPosition(GroupPositionState.UNAVAILABLE, null, null, null)
    }
}

/** Public, displayable alias supplied independently of any private peer/device identifier. */
class StablePeerDisplayAlias private constructor(val value: String) {
    override fun equals(other: Any?): Boolean = other is StablePeerDisplayAlias && value == other.value
    override fun hashCode(): Int = value.hashCode()
    override fun toString(): String = "StablePeerDisplayAlias(public-redacted)"

    companion object {
        private val allowed = Regex("^[\\p{L}\\p{N}][\\p{L}\\p{N} ._'-]{0,39}$")
        private val macAddress = Regex("(?i)^(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}$")
        private val normalizedMacAddress = Regex("(?i)^[0-9a-f]{12}$")
        private val uuid = Regex("(?i)^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")
        private val longHexIdentifier = Regex("(?i)^(?:0x)?[0-9a-f]{16,}$")

        fun create(value: String): StablePeerDisplayAlias? {
            if (value.length !in 1..MAX_PEER_DISPLAY_ALIAS_CHARS || value != value.trim()) return null
            val withoutCommonMacSeparators = value.replace(Regex("[. :_-]"), "")
            if (
                !allowed.matches(value) || macAddress.matches(value) ||
                normalizedMacAddress.matches(withoutCommonMacSeparators) ||
                uuid.matches(value) || longHexIdentifier.matches(value)
            ) {
                return null
            }
            return StablePeerDisplayAlias(value)
        }
    }
}

class GroupLocationPeer private constructor(
    val displayAlias: StablePeerDisplayAlias,
    val position: GroupLocationPosition,
) {
    override fun equals(other: Any?): Boolean =
        other is GroupLocationPeer && displayAlias == other.displayAlias && position == other.position

    override fun hashCode(): Int = 31 * displayAlias.hashCode() + position.hashCode()
    override fun toString(): String = "GroupLocationPeer(displayAlias=$displayAlias, position=$position)"

    companion object {
        fun create(displayAlias: String, position: GroupLocationPosition): GroupLocationPeer? =
            StablePeerDisplayAlias.create(displayAlias)?.let { GroupLocationPeer(it, position) }
    }
}

/**
 * Immutable, renderer-neutral presentation snapshot. It contains no endpoint token, radio address,
 * private peer identifier, phone location, wall-clock timestamp, or map/tile dependency.
 */
class GroupLocationSnapshot private constructor(
    val provenance: GroupLocationProvenance,
    val sourceSnapshotRevision: Long,
    val sharingState: CompanionPositionSharingState,
    val selfPosition: GroupLocationPosition,
    peers: List<GroupLocationPeer>,
) {
    val peers: List<GroupLocationPeer> = peers.toList()

    /** A status-only device update cannot refresh old position age/state, so all positions degrade. */
    fun withAuthoritativeStatus(
        revision: Long,
        sharingState: CompanionPositionSharingState,
    ): GroupLocationSnapshot? {
        if (provenance != GroupLocationProvenance.DEVICE_AUTHORITATIVE_SNAPSHOT) return null
        val unavailablePeers = peers.mapNotNull {
            GroupLocationPeer.create(it.displayAlias.value, GroupLocationPosition.unavailable())
        }
        if (unavailablePeers.size != peers.size) return null
        return create(
            provenance,
            revision,
            sharingState,
            GroupLocationPosition.unavailable(),
            unavailablePeers,
        )
    }

    /** Deterministic fake actions update their fixed fixture context; this is never used for real data. */
    fun withFakeStatus(
        revision: Long,
        sharingState: CompanionPositionSharingState,
    ): GroupLocationSnapshot? {
        if (provenance != GroupLocationProvenance.LOCAL_TEST_FIXTURE) return null
        return create(provenance, revision, sharingState, selfPosition, peers)
    }

    override fun equals(other: Any?): Boolean =
        other is GroupLocationSnapshot &&
            provenance == other.provenance &&
            sourceSnapshotRevision == other.sourceSnapshotRevision &&
            sharingState == other.sharingState &&
            selfPosition == other.selfPosition &&
            peers == other.peers

    override fun hashCode(): Int {
        var result = provenance.hashCode()
        result = 31 * result + sourceSnapshotRevision.hashCode()
        result = 31 * result + sharingState.hashCode()
        result = 31 * result + selfPosition.hashCode()
        return 31 * result + peers.hashCode()
    }

    override fun toString(): String =
        "GroupLocationSnapshot(provenance=$provenance, revision=$sourceSnapshotRevision, " +
            "sharingState=$sharingState, self=${selfPosition.state}, peerCount=${peers.size})"

    companion object {
        fun create(
            provenance: GroupLocationProvenance,
            sourceSnapshotRevision: Long,
            sharingState: CompanionPositionSharingState,
            selfPosition: GroupLocationPosition,
            peers: List<GroupLocationPeer>,
        ): GroupLocationSnapshot? {
            if (sourceSnapshotRevision !in 1..0xffff_ffffL || peers.size > MAX_GROUP_LOCATION_PEERS) return null
            val aliases = peers.map { it.displayAlias.value.lowercase(Locale.ROOT) }
            if (aliases.toSet().size != aliases.size) return null
            return GroupLocationSnapshot(provenance, sourceSnapshotRevision, sharingState, selfPosition, peers)
        }

        fun authoritativeUnavailable(
            sourceSnapshotRevision: Long,
            sharingState: CompanionPositionSharingState,
        ): GroupLocationSnapshot? = create(
            provenance = GroupLocationProvenance.DEVICE_AUTHORITATIVE_SNAPSHOT,
            sourceSnapshotRevision = sourceSnapshotRevision,
            sharingState = sharingState,
            selfPosition = GroupLocationPosition.unavailable(),
            peers = emptyList(),
        )
    }
}

/** Fixed local-only fixtures. They never consume a phone location provider or Bluetooth data. */
internal object DeterministicGroupLocationFixtures {
    fun forEndpoint(endpointToken: String): GroupLocationSnapshot? = when (endpointToken) {
        "fake-a" -> fixture(
            revision = 1,
            selfLatitudeE7 = 44_100_0000,
            selfLongitudeE7 = -70_200_0000,
            peerAlias = "Bench Partner B",
            peerLatitudeE7 = 44_100_2500,
            peerLongitudeE7 = -70_200_4200,
        )
        "fake-b" -> fixture(
            revision = 1,
            selfLatitudeE7 = 44_100_2500,
            selfLongitudeE7 = -70_200_4200,
            peerAlias = "Bench Partner A",
            peerLatitudeE7 = 44_100_0000,
            peerLongitudeE7 = -70_200_0000,
        )
        else -> null
    }

    private fun fixture(
        revision: Long,
        selfLatitudeE7: Int,
        selfLongitudeE7: Int,
        peerAlias: String,
        peerLatitudeE7: Int,
        peerLongitudeE7: Int,
    ): GroupLocationSnapshot {
        val self = requireNotNull(
            GroupLocationPosition.available(
                state = GroupPositionState.CURRENT,
                coordinate = requireNotNull(GroupLocationCoordinate.create(selfLatitudeE7, selfLongitudeE7)),
                ageSeconds = 4,
                accuracyMeters = 6,
            ),
        )
        val peer = requireNotNull(
            GroupLocationPeer.create(
                peerAlias,
                requireNotNull(
                    GroupLocationPosition.available(
                        state = GroupPositionState.STALE,
                        coordinate = requireNotNull(GroupLocationCoordinate.create(peerLatitudeE7, peerLongitudeE7)),
                        ageSeconds = 900,
                        accuracyMeters = 12,
                    ),
                ),
            ),
        )
        return requireNotNull(
            GroupLocationSnapshot.create(
                provenance = GroupLocationProvenance.LOCAL_TEST_FIXTURE,
                sourceSnapshotRevision = revision,
                sharingState = CompanionPositionSharingState.STOPPED,
                selfPosition = self,
                peers = listOf(peer),
            ),
        )
    }
}
