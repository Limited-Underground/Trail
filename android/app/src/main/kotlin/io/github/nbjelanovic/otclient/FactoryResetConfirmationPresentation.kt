package io.github.nbjelanovic.otclient

internal const val APP_FACTORY_RESET_CONFIRMATION_PUBLIC_TEXT =
    "This permanently erases this board's phone pairing, name, settings, groups, messages, " +
        "saved locations and offline maps. It does not erase your phone or other boards. " +
        "Firmware and factory hardware data remain. This cannot be undone."

/** Only the current session's validated name readback identifies the board being erased. */
internal fun factoryResetBoardName(session: BleActiveSession?): String =
    session?.configuration?.takeIf { it.nameRevision != null }?.deviceName
        ?.let(V1DeviceName::create)?.value ?: "this MeshTrail board"
