package io.github.nbjelanovic.otclient

import io.github.nbjelanovic.otprotocol.CompanionPositionSharingState

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

/** Deterministic fake actions update their fixed fixture context; this is never used for real data. */
internal fun GroupLocationSnapshot.withFakeStatus(
    revision: Long,
    sharingState: CompanionPositionSharingState,
): GroupLocationSnapshot? {
    if (provenance != GroupLocationProvenance.LOCAL_TEST_FIXTURE) return null
    return GroupLocationSnapshot.create(provenance, revision, sharingState, selfPosition, peers)
}
