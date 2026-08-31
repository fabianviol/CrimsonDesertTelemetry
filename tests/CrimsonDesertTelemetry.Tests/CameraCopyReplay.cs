using CrimsonDesertTelemetry.Core;
using System.Text.Json;

// Optional local regression: use the production selector on a controlled right-turn
// trace. No memory reads, fixed session addresses, or private fixture in the repo.
static class CameraCopyReplay
{
    public static int Run(string path)
    {
        var selector = new RenderCameraTemporalSelector();
        var options = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
        var old = new DirectionStats();
        var current = new DirectionStats();
        var individual = new Dictionary<ulong, DirectionStats>();
        var samples = 0;
        var unavailable = 0;
        foreach (var line in File.ReadLines(path))
        {
            using var json = JsonDocument.Parse(line);
            var root = json.RootElement;
            if (root.GetProperty("kind").GetString() == "camera-copy-trace") continue;
            var time = root.GetProperty("elapsedMs").GetDouble();
            var candidates = root.GetProperty("candidates").Deserialize<RenderCameraConstantsCandidate[]>(options)
                ?? throw new InvalidDataException("Missing candidates.");
            var previous = RenderCameraConstantsScanner.SelectConsensus(candidates);
            var selected = selector.Select(candidates, TimeSpan.FromMilliseconds(time));
            samples++;
            if (previous is not null) old.Add(previous.Camera, time);
            if (selected is not null) current.Add(selected.Camera, time); else unavailable++;
            foreach (var candidate in candidates)
            {
                if (!individual.TryGetValue(candidate.Address, out var stats))
                    individual[candidate.Address] = stats = new DirectionStats();
                stats.Add(candidate, time);
            }
        }
        var mostActiveCopy = individual.Values.OrderByDescending(s => s.Changes).First();
        var passed = samples > 0 && unavailable == 0 && current.Negative == 0 &&
            current.Changes >= mostActiveCopy.Changes && current.Positive > 0 &&
            current.MaximumStep <= mostActiveCopy.MaximumStep + .1 &&
            current.FirstChangeMs <= mostActiveCopy.FirstChangeMs + 100;
        Console.WriteLine(JsonSerializer.Serialize(new
        {
            passed, samples, unavailable, oldConsensus = old, temporal = current,
            mostActiveSingleCopy = mostActiveCopy,
            note = "Controlled right-turn replay, not an in-game/other-GPU validation."
        }, new JsonSerializerOptions { WriteIndented = true, PropertyNamingPolicy = JsonNamingPolicy.CamelCase }));
        return passed ? 0 : 1;
    }

    private sealed class DirectionStats
    {
        private double? _last;
        public int Changes { get; private set; }
        public int Positive { get; private set; }
        public int Negative { get; private set; }
        public double MaximumStep { get; private set; }
        public double TravelDegrees { get; private set; }
        public double? FirstChangeMs { get; private set; }
        public double? LastChangeMs { get; private set; }
        public void Add(RenderCameraConstantsCandidate camera, double time)
        {
            var value = Math.Atan2(camera.Forward.X, camera.Forward.Z) * 180 / Math.PI;
            if (_last is { } last)
            {
                var delta = ((value - last + 540) % 360) - 180;
                if (Math.Abs(delta) > .05)
                {
                    Changes++;
                    if (delta > 0) Positive++; else Negative++;
                    MaximumStep = Math.Max(MaximumStep, Math.Abs(delta));
                    TravelDegrees += delta;
                    FirstChangeMs ??= time;
                    LastChangeMs = time;
                }
            }
            _last = value;
        }
    }
}
