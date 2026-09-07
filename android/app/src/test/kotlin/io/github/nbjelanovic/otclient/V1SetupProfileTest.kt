package io.github.nbjelanovic.otclient

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue
import io.github.nbjelanovic.otprotocol.CompanionNameKind
import io.github.nbjelanovic.otprotocol.CompanionNamePayload
import io.github.nbjelanovic.otprotocol.CompanionNamePayloadCodec

class V1SetupProfileTest {
    @Test
    fun setupUsesOneFixedResumableOrderAndGroupIsNotASetupStage() {
        var progress = V1SetupProgress.empty()
        assertEquals(V1SetupStage.MATCH_DEVICE, progress.stage)
        progress = progress.matchDevice(label(), session())
        assertEquals(V1SetupStage.SECURE_PAIR_AND_AUTHORIZE, progress.stage)
        progress = assertNotNull(progress.authorize(authorization()))
        assertEquals(V1SetupStage.NAME_DEVICE, progress.stage)
        progress = assertNotNull(progress.nameDevice(deviceName(), nameReceipt()))
        assertEquals(V1SetupStage.CONFIRM_RADIO_REGION, progress.stage)
        progress = assertNotNull(progress.confirmRadioRegion(regionReceipt()))
        assertEquals(V1SetupStage.PUBLIC_PROFILE, progress.stage)
        progress = assertNotNull(progress.setPublicProfile(V1PublicProfile(publicName())))
        assertEquals(V1SetupStage.COMPLETE, progress.stage)
        assertFalse(progress.radioTransmissionAllowed)
        assertFalse(V1SetupStage.entries.any { it.name.contains("GROUP") })
    }

    @Test
    fun operationsFailClosedWhenAttemptedOutOfOrder() {
        val empty = V1SetupProgress.empty()
        assertNull(empty.authorize(authorization()))
        assertNull(empty.nameDevice(deviceName(), nameReceipt()))
        assertNull(empty.confirmRadioRegion(regionReceipt()))
        assertNull(empty.setPublicProfile(V1PublicProfile(publicName())))
    }

    @Test
    fun regionMustHaveMatchingReadbackBeforeProfileCompletionOrRadioTransmission() {
        val named = assertNotNull(
            assertNotNull(V1SetupProgress.empty().matchDevice(label(), session()).authorize(authorization()))
                .nameDevice(deviceName(), nameReceipt()),
        )
        val stale = V1RegionReadbackReceipt(label(), otherSession(), V1RadioRegion.US915)
        assertNull(named.confirmRadioRegion(stale))
        assertEquals(V1SetupStage.CONFIRM_RADIO_REGION, named.stage)
        assertFalse(named.radioTransmissionAllowed)
        assertNull(named.setPublicProfile(V1PublicProfile(publicName())))
        val verified = assertNotNull(named.confirmRadioRegion(regionReceipt()))
        assertEquals(V1SetupStage.PUBLIC_PROFILE, verified.stage)
        assertFalse(verified.radioTransmissionAllowed)
    }

    @Test
    fun publicDiscoveryDefaultsOnButCanBeDisabled() {
        assertTrue(V1PublicProfile(publicName()).publiclyDiscoverable)
        assertFalse(V1PublicProfile(publicName(), publiclyDiscoverable = false).publiclyDiscoverable)
    }

    @Test
    fun setupLabelsAreHumanReadableBoundedAndAvoidAmbiguousCharacters() {
        assertNotNull(V1SetupLabel.create("Trail-7K3P9Q"))
        listOf(
            "Trail-",
            "trail-7K3P9Q",
            "Trail-12345",
            "Trail-1234567",
            "Trail-10OILQ",
            "Trail-ABC-12",
        ).forEach { assertNull(V1SetupLabel.create(it), it) }
    }

    @Test
    fun publicAndDeviceNamesAreBoundedAndIdentifierLikePublicNamesAreRejected() {
        assertNotNull(V1DeviceName.create("Brian's Trail"))
        assertNotNull(V1PublicName.create("Brian"))
        assertNull(V1DeviceName.create(" x"))
        assertNull(V1DeviceName.create("x".repeat(V1_DEVICE_NAME_MAX_CHARS + 1)))
        assertNull(V1PublicName.create("123e4567-e89b-12d3-a456-426614174000"))
    }

    @Test
    fun selectingAnotherDeviceClearsAllPriorAuthorityAndConfiguration() {
        val complete = complete()
        val changed = complete.matchDevice(requireNotNull(V1SetupLabel.create("Trail-8M4R2T")), otherSession())
        assertEquals(V1SetupStage.SECURE_PAIR_AND_AUTHORIZE, changed.stage)
        assertFalse(changed.authorized)
        assertNull(changed.deviceName)
        assertNull(changed.radioRegion)
        assertNull(changed.publicProfile)
        assertFalse(changed.radioTransmissionAllowed)
    }

    @Test
    fun factoryResetReturnsToEmptySetupWithoutLeakingNames() {
        val cleared = complete().clearAfterFactoryReset()
        assertEquals(V1SetupStage.MATCH_DEVICE, cleared.stage)
        assertFalse(cleared.toString().contains("Brian"))
        assertFalse(cleared.toString().contains("7K3P9Q"))
    }

    private fun complete(): V1SetupProgress {
        var progress = V1SetupProgress.empty().matchDevice(label(), session())
        progress = requireNotNull(progress.authorize(authorization()))
        progress = requireNotNull(progress.nameDevice(deviceName(), nameReceipt()))
        progress = requireNotNull(progress.confirmRadioRegion(regionReceipt()))
        return requireNotNull(progress.setPublicProfile(V1PublicProfile(publicName())))
    }

    @Test
    fun nameReadbackRequiresCurrentSessionLabelAndExactRequestedName() {
        val authorized = requireNotNull(V1SetupProgress.empty().matchDevice(label(), session()).authorize(authorization()))
        val otherLabel = requireNotNull(V1SetupLabel.create("Trail-8M4R2T"))
        val differentName = requireNotNull(V1DeviceName.create("Different name"))
        assertNull(authorized.nameDevice(deviceName(), nameReceipt(token = otherSession())))
        assertNull(authorized.nameDevice(deviceName(), nameReceipt(setupLabel = otherLabel)))
        assertNull(authorized.nameDevice(deviceName(), nameReceipt(name = differentName)))
        assertEquals(V1SetupStage.NAME_DEVICE, authorized.stage)
        assertNull(authorized.deviceName)
        assertFalse(authorized.radioTransmissionAllowed)
        val named = requireNotNull(authorized.nameDevice(deviceName(), nameReceipt()))
        assertEquals(deviceName(), named.deviceName)
        assertEquals(V1SetupStage.CONFIRM_RADIO_REGION, named.stage)
        assertFalse(named.radioTransmissionAllowed)
    }

    @Test
    fun matchingNameReceiptCannotAuthorizeUnownedSetup() {
        val matched = V1SetupProgress.empty().matchDevice(label(), session())
        assertNull(matched.nameDevice(deviceName(), nameReceipt()))
        assertEquals(V1SetupStage.SECURE_PAIR_AND_AUTHORIZE, matched.stage)
        assertFalse(nameReceipt().toString().contains("Brian"))
        assertFalse(nameReceipt().toString().contains("7K3P9Q"))
    }

    private fun nameReceipt(setupLabel: V1SetupLabel = label(), token: V1SetupSessionToken = session(),
                            name: V1DeviceName = deviceName()): V1NameReadbackReceipt {
        val context = V1NameContext(1, 2, 3, 4, 5, 6, 7u, setupLabel, token)
        val transaction = V1NameTransaction(V1NameAuthoritySource { V1NameAuthority(V1NamePhase.READY, context) })
        val read = assertNotNull(transaction.beginRead())
        transaction.receive(context, read.exchangeId, assertNotNull(CompanionNamePayloadCodec.encode(
            CompanionNamePayload(CompanionNameKind.SNAPSHOT))))
        val write = assertNotNull(transaction.beginWrite(name, 0u))
        return assertNotNull(transaction.receive(context, write.exchangeId, assertNotNull(CompanionNamePayloadCodec.encode(
            CompanionNamePayload(CompanionNameKind.APPLIED, 1u, name.value)))).receipt)
    }
    private fun authorization() = V1AuthorizationReceipt(label(), session())
    private fun regionReceipt() = V1RegionReadbackReceipt(label(), session(), V1RadioRegion.US915)
    private fun session() = requireNotNull(
        V1SetupSessionToken.create(ByteArray(V1SetupSessionToken.SIZE_BYTES) { 5 }),
    )
    private fun otherSession() = requireNotNull(
        V1SetupSessionToken.create(ByteArray(V1SetupSessionToken.SIZE_BYTES) { 6 }),
    )
    private fun label() = requireNotNull(V1SetupLabel.create("Trail-7K3P9Q"))
    private fun deviceName() = requireNotNull(V1DeviceName.create("Brian's Trail"))
    private fun publicName() = requireNotNull(V1PublicName.create("Brian"))
}
