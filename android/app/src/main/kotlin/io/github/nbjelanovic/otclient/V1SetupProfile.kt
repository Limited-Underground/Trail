package io.github.nbjelanovic.otclient

internal const val V1_SETUP_CODE_LENGTH = 6
internal const val V1_DEVICE_NAME_MAX_CHARS = 32
internal const val V1_PUBLIC_NAME_MAX_CHARS = 40

enum class V1SetupStage {
    MATCH_DEVICE,
    SECURE_PAIR_AND_AUTHORIZE,
    NAME_DEVICE,
    CONFIRM_RADIO_REGION,
    PUBLIC_PROFILE,
    COMPLETE,
}

class V1SetupLabel private constructor(val value: String) {
    override fun equals(other: Any?): Boolean = other is V1SetupLabel && value == other.value
    override fun hashCode(): Int = value.hashCode()
    override fun toString(): String = "V1SetupLabel(redacted)"

    companion object {
        private const val PREFIX = "Trail-"
        private val alphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ".toSet()

        fun create(value: String): V1SetupLabel? {
            if (!value.startsWith(PREFIX)) return null
            val code = value.removePrefix(PREFIX)
            if (code.length != V1_SETUP_CODE_LENGTH || code.any { it !in alphabet }) return null
            return V1SetupLabel(value)
        }
    }
}

class V1DeviceName private constructor(val value: String) {
    override fun equals(other: Any?): Boolean = other is V1DeviceName && value == other.value
    override fun hashCode(): Int = value.hashCode()
    override fun toString(): String = "V1DeviceName(redacted)"

    companion object {
        fun create(value: String): V1DeviceName? =
            value.takeIf {
                it.length in 1..V1_DEVICE_NAME_MAX_CHARS &&
                    it == it.trim() &&
                    it.none(Char::isISOControl)
            }?.let(::V1DeviceName)
    }
}

class V1PublicName private constructor(val value: String) {
    override fun equals(other: Any?): Boolean = other is V1PublicName && value == other.value
    override fun hashCode(): Int = value.hashCode()
    override fun toString(): String = "V1PublicName(redacted)"

    companion object {
        fun create(value: String): V1PublicName? =
            StablePeerDisplayAlias.create(value)
                ?.takeIf { it.value.length <= V1_PUBLIC_NAME_MAX_CHARS }
                ?.let { V1PublicName(it.value) }
    }
}

enum class V1RadioRegion(val publicCode: String) {
    US915("US915"),
}

class V1SetupSessionToken private constructor(private val bytes: ByteArray) {
    override fun equals(other: Any?): Boolean =
        other is V1SetupSessionToken && bytes.contentEquals(other.bytes)

    override fun hashCode(): Int = bytes.contentHashCode()
    override fun toString(): String = "V1SetupSessionToken(redacted)"

    companion object {
        const val SIZE_BYTES = 16

        fun create(value: ByteArray): V1SetupSessionToken? =
            value.takeIf { it.size == SIZE_BYTES && it.any { byte -> byte.toInt() != 0 } }
                ?.copyOf()
                ?.let(::V1SetupSessionToken)
    }
}

class V1AuthorizationReceipt internal constructor(
    internal val setupLabel: V1SetupLabel,
    internal val sessionToken: V1SetupSessionToken,
) {
    override fun toString(): String = "V1AuthorizationReceipt(redacted)"
}

/**
 * Future authenticated device adapter evidence, issued only after durable write/readback.
 * Current firmware has no name capability: phone-local drafts must never mint this receipt.
 * Like region receipts, this model checks binding; construction is not hardware acceptance.
 */
class V1NameReadbackReceipt internal constructor(
    internal val setupLabel: V1SetupLabel,
    internal val sessionToken: V1SetupSessionToken,
    internal val name: V1DeviceName,
) {
    override fun toString(): String = "V1NameReadbackReceipt(name=redacted, identity=redacted)"
}

class V1RegionReadbackReceipt internal constructor(
    internal val setupLabel: V1SetupLabel,
    internal val sessionToken: V1SetupSessionToken,
    internal val region: V1RadioRegion,
) {
    override fun toString(): String = "V1RegionReadbackReceipt(region=${region.publicCode}, identity=redacted)"
}

data class V1PublicProfile(
    val name: V1PublicName,
    val publiclyDiscoverable: Boolean = true,
)

class V1SetupProgress private constructor(
    val setupLabel: V1SetupLabel?,
    private val sessionToken: V1SetupSessionToken?,
    val authorized: Boolean,
    val deviceName: V1DeviceName?,
    val radioRegion: V1RadioRegion?,
    val radioRegionVerified: Boolean,
    val publicProfile: V1PublicProfile?,
) {
    val stage: V1SetupStage
        get() = when {
            setupLabel == null -> V1SetupStage.MATCH_DEVICE
            !authorized -> V1SetupStage.SECURE_PAIR_AND_AUTHORIZE
            deviceName == null -> V1SetupStage.NAME_DEVICE
            radioRegion == null || !radioRegionVerified -> V1SetupStage.CONFIRM_RADIO_REGION
            publicProfile == null -> V1SetupStage.PUBLIC_PROFILE
            else -> V1SetupStage.COMPLETE
        }

    val radioTransmissionAllowed: Boolean
        get() = authorized && deviceName != null && radioRegion != null && radioRegionVerified

    fun matchDevice(label: V1SetupLabel, session: V1SetupSessionToken): V1SetupProgress =
        V1SetupProgress(label, session, false, null, null, false, null)

    fun authorize(receipt: V1AuthorizationReceipt): V1SetupProgress? =
        takeIf { setupLabel == receipt.setupLabel && sessionToken == receipt.sessionToken }
            ?.let { V1SetupProgress(setupLabel, sessionToken, true, null, null, false, null) }

    fun nameDevice(name: V1DeviceName, receipt: V1NameReadbackReceipt): V1SetupProgress? =
        takeIf {
            authorized && setupLabel == receipt.setupLabel &&
                sessionToken == receipt.sessionToken && name == receipt.name
        }?.let {
            V1SetupProgress(setupLabel, sessionToken, true, name, null, false, null)
        }

    fun confirmRadioRegion(receipt: V1RegionReadbackReceipt): V1SetupProgress? =
        takeIf {
            deviceName != null &&
                setupLabel == receipt.setupLabel &&
                sessionToken == receipt.sessionToken
        }?.let {
            V1SetupProgress(setupLabel, sessionToken, true, deviceName, receipt.region, true, null)
        }

    fun setPublicProfile(profile: V1PublicProfile): V1SetupProgress? =
        takeIf { radioRegion != null && radioRegionVerified }?.let {
            V1SetupProgress(setupLabel, sessionToken, true, deviceName, radioRegion, true, profile)
        }

    fun clearAfterFactoryReset(): V1SetupProgress = empty()

    override fun toString(): String =
        "V1SetupProgress(stage=$stage, setupLabel=redacted, deviceName=redacted, " +
            "radioRegion=${radioRegion?.publicCode ?: "none"}, radioRegionVerified=$radioRegionVerified, " +
            "publicProfile=redacted)"

    companion object {
        fun empty(): V1SetupProgress = V1SetupProgress(null, null, false, null, null, false, null)
    }
}
