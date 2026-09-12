namespace CrimsonDesertTelemetry.Core;

/// <summary>Opens the native ASI's per-process, one-shot playable-world gate.</summary>
public static class NativeCaptureReadySignal
{
    public static string Name(int processId) =>
        $"Local\\CrimsonDesertTelemetry.CaptureReady.{processId}";

    public static bool TrySet(int processId)
    {
        try
        {
            using var ready = EventWaitHandle.OpenExisting(Name(processId));
            return ready.Set();
        }
        catch (WaitHandleCannotBeOpenedException) { return false; }
        catch (UnauthorizedAccessException) { return false; }
    }
}
