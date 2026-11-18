package io.github.nbjelanovic.otclient

/** The live route handles device confirmation, with group setup still unavailable. */
internal fun v1LiveGroupScreenState(
    connected: Boolean,
    confirmation: V1GroupConfirmationState,
) = V1GroupScreenState(
    setupAvailable = false,
    statusMessage = "Creating and joining groups, including QR invitations, is coming in a future update.",
    confirmation = if (connected) confirmation else V1GroupConfirmationState.Unsupported,
)
