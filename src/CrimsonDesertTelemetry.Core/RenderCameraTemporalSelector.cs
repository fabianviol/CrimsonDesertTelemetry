namespace CrimsonDesertTelemetry.Core;

/// <summary>
/// Learns which renderer copies are regularly updated, then follows their most
/// recent observed change. Observation time is not an engine frame identifier.
/// </summary>
public sealed class RenderCameraTemporalSelector
{
    private const int MinimumChanges = 3;
    private const double ActiveFraction = 0.6;
    private readonly Dictionary<ulong, Activity> _activity = [];
    private readonly HashSet<ulong> _trusted = [];
    private TimeSpan? _previousTime;
    private long _sample;
    private ulong? _selectedAddress;
    private bool _confirmed;

    public void Reset()
    {
        _activity.Clear();
        _trusted.Clear();
        _previousTime = null;
        _sample = 0;
        _selectedAddress = null;
        _confirmed = false;
    }

    public RenderCameraConsensus? Select(
        IReadOnlyList<RenderCameraConstantsCandidate> candidates, TimeSpan observationTime)
    {
        if (observationTime < TimeSpan.Zero || _previousTime is { } previous && observationTime < previous)
            throw new ArgumentOutOfRangeException(nameof(observationTime), "Observation time must be monotonic.");

        // At 1 Hz, allow enough observations to distinguish regular activity from a
        // single copied record. At normal game rates the window remains two seconds.
        var interval = _previousTime is { } last ? (observationTime - last).TotalSeconds : 0;
        var window = TimeSpan.FromSeconds(Math.Clamp(interval * 4, 2, 8));
        var cutoff = observationTime - window;
        _previousTime = observationTime;
        _sample++;

        var unique = candidates.DistinctBy(static candidate => candidate.Address).ToArray();
        foreach (var candidate in unique)
        {
            var state = CameraState.From(candidate);
            if (!_activity.TryGetValue(candidate.Address, out var activity) || activity.LastSample != _sample - 1)
            {
                _activity[candidate.Address] = activity = new Activity(state);
                _trusted.Remove(candidate.Address);
                if (_selectedAddress == candidate.Address) _selectedAddress = null;
            }
            else if (activity.State != state)
            {
                activity.Changes.Enqueue(observationTime);
                activity.LastChange = observationTime;
                activity.State = state;
            }
            activity.LastSample = _sample;
            activity.LastSeen = observationTime;
            while (activity.Changes.TryPeek(out var changedAt) && changedAt < cutoff)
                activity.Changes.Dequeue();
        }
        foreach (var address in _activity.Where(pair => pair.Value.LastSeen < cutoff)
                     .Select(static pair => pair.Key).ToArray())
            _activity.Remove(address);
        if (unique.Length == 0) return null;

        var maximum = unique.Max(candidate => _activity[candidate.Address].Changes.Count);
        RenderCameraConstantsCandidate? selected;
        if (maximum >= MinimumChanges)
        {
            var active = unique.Where(candidate =>
                    _activity[candidate.Address].Changes.Count >= Math.Max(MinimumChanges, maximum * ActiveFraction))
                .ToArray();
            _trusted.Clear();
            foreach (var candidate in active) _trusted.Add(candidate.Address);
            selected = NewestObserved(active);
            _confirmed = true;
        }
        else if (!_confirmed)
        {
            // The first observation has no temporal evidence. Bootstrap from the
            // existing consensus, not the lowest address (which may be historical).
            selected = RenderCameraConstantsScanner.SelectConsensus(unique)?.Camera;
        }
        else
        {
            // A motionless camera is valid. Retain a learned source without
            // inventing motion or switching back to a larger historical group.
            selected = unique.FirstOrDefault(candidate => candidate.Address == _selectedAddress);
            selected ??= NewestObserved(unique.Where(candidate => _trusted.Contains(candidate.Address)));
            // No learned source remains: ask the tracker to rediscover, even when
            // unrelated historical records still pass structural validation.
            if (selected is null) return null;
        }
        if (selected is null) return null;
        _selectedAddress = selected.Address;
        return RenderCameraConstantsScanner.DescribeSelection(unique, selected);
    }

    private RenderCameraConstantsCandidate? NewestObserved(IEnumerable<RenderCameraConstantsCandidate> candidates) =>
        candidates.OrderByDescending(candidate => _activity[candidate.Address].LastChange)
            .ThenByDescending(candidate => _activity[candidate.Address].Changes.Count)
            .ThenByDescending(candidate => candidate.Address == _selectedAddress)
            .ThenBy(static candidate => candidate.Address)
            .FirstOrDefault();

    private sealed class Activity(CameraState state)
    {
        public CameraState State = state;
        public readonly Queue<TimeSpan> Changes = new();
        public TimeSpan LastChange = TimeSpan.MinValue;
        public TimeSpan LastSeen;
        public long LastSample;
    }

    // Ignore address and player distance: a player movement must not count as a
    // camera-buffer update. Compare the actual transform/projection, without yaw
    // smoothing, wrap assumptions, or a preferred direction of rotation.
    private readonly record struct CameraState(CameraVector3 Position, CameraVector3 Up,
        CameraVector3 Right, CameraVector3 Forward, float Near, float Far, float Fov, float Aspect)
    {
        public static CameraState From(RenderCameraConstantsCandidate candidate) => new(
            candidate.Position, candidate.Up, candidate.Right, candidate.Forward,
            candidate.NearPlane, candidate.FarPlane, candidate.FieldOfViewRadians, candidate.AspectRatio);
    }
}
