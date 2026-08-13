namespace OpenTrail.Loader;

internal enum LoaderMaintenanceProbeStatus
{
    NotOffered,
    AwaitingOperatorConfirmation,
    ReadyForSingleAttempt,
    RuntimeRecoveryRequired,
    SessionAttemptConsumed,
}

internal readonly record struct LoaderMaintenanceProbeDecision(
    LoaderMaintenanceProbeStatus Status,
    bool CanStartProbe,
    bool RuntimeRecoveryMustBeVerified);

internal static class LoaderMaintenanceProbePolicy
{
    internal const int MaximumAttemptsPerSession = 1;

    internal static LoaderMaintenanceProbeDecision Evaluate(
        bool maintenanceRequired,
        bool operatorConfirmedDisruption,
        int attemptsThisSession,
        bool normalRuntimeRecoveredAfterAttempt)
    {
        if (!maintenanceRequired)
        {
            return new(
                LoaderMaintenanceProbeStatus.NotOffered,
                CanStartProbe: false,
                RuntimeRecoveryMustBeVerified: false);
        }

        if (attemptsThisSession < 0 || attemptsThisSession > MaximumAttemptsPerSession)
        {
            throw new ArgumentOutOfRangeException(nameof(attemptsThisSession));
        }

        if (attemptsThisSession == MaximumAttemptsPerSession)
        {
            return normalRuntimeRecoveredAfterAttempt
                ? new(
                    LoaderMaintenanceProbeStatus.SessionAttemptConsumed,
                    CanStartProbe: false,
                    RuntimeRecoveryMustBeVerified: false)
                : new(
                    LoaderMaintenanceProbeStatus.RuntimeRecoveryRequired,
                    CanStartProbe: false,
                    RuntimeRecoveryMustBeVerified: true);
        }

        if (!operatorConfirmedDisruption)
        {
            return new(
                LoaderMaintenanceProbeStatus.AwaitingOperatorConfirmation,
                CanStartProbe: false,
                RuntimeRecoveryMustBeVerified: false);
        }

        return new(
            LoaderMaintenanceProbeStatus.ReadyForSingleAttempt,
            CanStartProbe: true,
            RuntimeRecoveryMustBeVerified: true);
    }
}
