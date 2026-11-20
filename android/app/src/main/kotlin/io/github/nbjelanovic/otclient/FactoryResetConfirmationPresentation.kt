package io.github.nbjelanovic.otclient

internal const val APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT =
    "This erases the device's phone pairing, name, settings, groups, and messages.\n" +
        "Saved locations and offline maps are also erased.\n" +
        "Your phone and other devices keep their data.\n" +
        "The device software and factory data stay.\n" +
        "You can't undo this."

/** Only the current session's validated name readback identifies the board being erased. */
internal fun factoryResetBoardName(session: BleActiveSession?): String =
    session?.configuration?.takeIf { it.nameRevision != null }?.deviceName
        ?.let(V1DeviceName::create)?.value ?: "this MeshTrail board"
