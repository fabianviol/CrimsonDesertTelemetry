using System.Diagnostics;

namespace CrimsonDesertTelemetry.Core;

public sealed record RenderCameraFrame(
    DateTimeOffset Timestamp,
    RenderCameraConstantsCandidate Camera,
    int ConsensusCopies,
    int ValidCopies,
    int DistinctStates,
    bool Rediscovered);

public sealed class RenderCameraTracker
{
    private readonly IReadOnlyProcessMemory _reader;
    private readonly RenderCameraTemporalSelector _selector = new();
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private ulong[] _addresses;

    public RenderCameraTracker(IReadOnlyProcessMemory reader, IEnumerable<RenderCameraConstantsCandidate> discovered)
    {
        _reader = reader;
        _addresses = discovered.Select(static candidate => candidate.Address).Distinct().ToArray();
        if (_addresses.Length == 0)
            throw new ArgumentException("No camera constants addresses were provided.", nameof(discovered));
    }

    public int AddressCount => _addresses.Length;
    public int RediscoveryCount { get; private set; }

    public RenderCameraFrame Capture((float X, float Y, float Z) playerPosition)
    {
        var candidates = RenderCameraConstantsScanner.Refresh(_reader, _addresses, playerPosition);
        var consensus = _selector.Select(candidates, _clock.Elapsed);
        var rediscovered = false;
        if (consensus is null)
        {
            var discovered = RenderCameraConstantsScanner.Find(_reader, playerPosition);
            _addresses = discovered.Select(static candidate => candidate.Address).Distinct().ToArray();
            RediscoveryCount++;
            rediscovered = true;
            candidates = RenderCameraConstantsScanner.Refresh(_reader, _addresses, playerPosition);
            _selector.Reset();
            consensus = _selector.Select(candidates, _clock.Elapsed);
        }
        if (consensus is null)
            throw new InvalidDataException("No valid camera copy remains after rediscovery.");
        return new RenderCameraFrame(DateTimeOffset.UtcNow, consensus.Camera, consensus.CopyCount,
            consensus.ValidCopyCount, consensus.DistinctStateCount, rediscovered);
    }
}
