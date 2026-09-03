package io.github.nbjelanovic.otclient

internal const val DEVICE_AUTHORIZATION_CLAIM_TIMEOUT_MILLIS = 30_000L
internal const val MAX_DEVICE_AUTHORIZATION_TOKEN_CHARS = 128
internal const val EXPIRED_AUTHORIZATION_PUBLIC_TEXT =
    "No authoritative result reached this phone. Current device authority is unknown; reconnect and check " +
        "the physical device before retrying."
internal const val INVALID_AUTHORIZATION_RESULT_PUBLIC_TEXT =
    "The app rejected an invalid or mismatched result and did not start a connection. Current device " +
        "authority is unknown; check the physical device before retrying."

enum class DeviceAuthorizationPurpose {
    AUTHORIZE_THIS_PHONE,
}

sealed interface DeviceAuthorizationClaimEvent {
    val claimToken: String

    /** First authoritative event. The token is lease-local; private wire correlation stays in the tracker. */
    data class Pending(override val claimToken: String) : DeviceAuthorizationClaimEvent
    data class Accepted(override val claimToken: String) : DeviceAuthorizationClaimEvent
    data class Denied(override val claimToken: String) : DeviceAuthorizationClaimEvent
    /** Local transport outcome before an authoritative Pending; never a device denial. */
    data class Unavailable(override val claimToken: String) : DeviceAuthorizationClaimEvent
    /** Exact protected ProtocolInfo did not advertise the accepted claim contract. */
    data class Unsupported(override val claimToken: String) : DeviceAuthorizationClaimEvent
    /** Transport ended after Pending; device authority may have changed and must be checked. */
    data class AuthorityUnknown(override val claimToken: String) : DeviceAuthorizationClaimEvent
}

interface DeviceAuthorizationClaimLease : AutoCloseable {
    /** A false return means the claim did not start and no callback has authority. */
    fun start(): Boolean
    override fun close()
}

/**
 * Injectable device-authority seam. Endpoint and claim tokens stay opaque; callbacks must be
 * serialized on the Trail controller owner thread and contain no platform error text or secrets.
 */
fun interface DeviceAuthorizationClaimClient {
    fun createClaim(
        endpointToken: String,
        purpose: DeviceAuthorizationPurpose,
        observer: (DeviceAuthorizationClaimEvent) -> Unit,
    ): DeviceAuthorizationClaimLease?
}

/** Explicit disabled seam for tests or builds that intentionally omit the production composition. */
class DisabledDeviceAuthorizationClaimClient : DeviceAuthorizationClaimClient {
    override fun createClaim(
        endpointToken: String,
        purpose: DeviceAuthorizationPurpose,
        observer: (DeviceAuthorizationClaimEvent) -> Unit,
    ): DeviceAuthorizationClaimLease? = null
}

/**
 * Runtime-backed claim adapter. The device-issued 128-bit wire correlation never leaves the runtime
 * tracker; controller/UI events use only a bounded lease-local operation token.
 */
class RuntimeDeviceAuthorizationClaimClient(
    private val runtime: BleCompanionRuntime,
) : DeviceAuthorizationClaimClient {
    override fun createClaim(
        endpointToken: String,
        purpose: DeviceAuthorizationPurpose,
        observer: (DeviceAuthorizationClaimEvent) -> Unit,
    ): DeviceAuthorizationClaimLease? = runtime.createAuthorizationClaim(endpointToken, purpose, observer)
}

sealed interface DeviceAuthorizationUiState {
    data object None : DeviceAuthorizationUiState
    data class Starting(
        val companion: BleDiscoveredCompanion,
        val purpose: DeviceAuthorizationPurpose,
    ) : DeviceAuthorizationUiState
    data class Pending(
        val companion: BleDiscoveredCompanion,
        val purpose: DeviceAuthorizationPurpose,
    ) : DeviceAuthorizationUiState
    data class Accepted(
        val companion: BleDiscoveredCompanion,
        val purpose: DeviceAuthorizationPurpose,
    ) : DeviceAuthorizationUiState
    data class Denied(val purpose: DeviceAuthorizationPurpose) : DeviceAuthorizationUiState
    data class InvalidResult(val purpose: DeviceAuthorizationPurpose) : DeviceAuthorizationUiState
    /** Timeout is uncertainty, never evidence that device authority was unchanged or rolled back. */
    data class Expired(val purpose: DeviceAuthorizationPurpose) : DeviceAuthorizationUiState
    data class Unavailable(val purpose: DeviceAuthorizationPurpose) : DeviceAuthorizationUiState
    data class Unsupported(val purpose: DeviceAuthorizationPurpose) : DeviceAuthorizationUiState
    data class AuthorityUnknown(val purpose: DeviceAuthorizationPurpose) : DeviceAuthorizationUiState
}
