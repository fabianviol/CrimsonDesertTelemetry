using CrimsonDesertTelemetry.Core;

static class CameraSelectionTests
{
    public static void Bootstrap()
    {
        var result = new RenderCameraTemporalSelector().Select(
            [Candidate(1, 160), Candidate(10, 20), Candidate(11, 20), Candidate(12, 20)], TimeSpan.Zero);
        Require(result is { CopyCount: 3 } && Near(Yaw(result.Camera), 20),
            "Bootstrap selected an arbitrary historical address instead of the consensus.");
    }

    public static void HistoricalMajority()
    {
        var selector = new RenderCameraTemporalSelector();
        for (var step = 0; step < 60; step++)
        {
            var copies = History().Append(Candidate(100, step)).Append(Candidate(200, step < 2 ? 0 : 150)).ToArray();
            var selected = selector.Select(copies, Tick(step));
            if (step < 3) continue;
            Require(selected is { Camera.Address: 100 }, "An old majority or one-off change displaced the active camera.");
            Require(selected!.CopyCount == 1 && selected.ValidCopyCount == copies.Length,
                "Quality counts do not describe the selected state and all valid candidates.");
        }
    }

    public static void RotatingBuffers()
    {
        var selector = new RenderCameraTemporalSelector();
        var buffers = new[] { Candidate(100, 0), Candidate(101, 0), Candidate(102, 0) };
        for (var step = 0; step < 180; step++)
        {
            buffers[step % 3] = Candidate((ulong)(100 + step % 3), step);
            var result = selector.Select(History().Concat(buffers).ToArray(), Tick(step));
            if (step < 12) continue;
            Require(result is not null && Near(Yaw(result.Camera), step),
                "The selector skipped the newest rotating buffer or stayed on an old copy.");
        }
    }

    public static void ReversalAndWrap()
    {
        var selector = new RenderCameraTemporalSelector();
        float[] angles = [350, 351, 352, 353, 359, 0, 1, 0, 359, 358, 180, 20];
        for (var step = 0; step < angles.Length; step++)
        {
            var selected = selector.Select(History().Append(Candidate(100, angles[step])).ToArray(), Tick(step));
            if (step < 3) continue;
            Require(selected is not null && Near(Wrap(Yaw(selected.Camera) - angles[step]), 0),
                "A genuine reversal, wrap or abrupt camera change was clamped or smoothed.");
        }
    }

    public static void PositionAndProjection()
    {
        var selector = new RenderCameraTemporalSelector();
        for (var step = 0; step < 12; step++)
        {
            var moving = Candidate(100, 0) with { Position = new(10 + step, 20, 30) };
            var selected = selector.Select(History().Append(moving).ToArray(), Tick(step));
            if (step >= 3) Require(selected?.Camera == moving, "Position-only movement did not establish a source.");
        }
        selector.Reset();
        for (var step = 0; step < 12; step++)
        {
            var changingFov = Candidate(100, 0) with { FieldOfViewRadians = 1 + step * .01f };
            var selected = selector.Select(History().Append(changingFov).ToArray(), Tick(step));
            if (step >= 3) Require(selected?.Camera == changingFov, "Projection-only changes were ignored.");
        }
    }

    public static void Stationary()
    {
        var selector = new RenderCameraTemporalSelector();
        Train(selector);
        var still = Candidate(100, 9);
        for (var step = 10; step < 400; step++)
        {
            // Changing player distance does not mean the camera record was updated.
            var history = History().Select(c => c with { DistanceFromPlayer = step });
            var result = selector.Select(history.Append(still).ToArray(), Tick(step));
            Require(result?.Camera == still, "Stillness or player distance restored a historical winner.");
        }
    }

    public static void LostSource()
    {
        var selector = new RenderCameraTemporalSelector();
        Train(selector);
        Require(selector.Select(History(), Tick(10)) is null,
            "Losing the learned source silently fell back to historical records.");
        selector.Reset();
        Require(selector.Select([Candidate(300, 80)], Tick(11))?.Camera.Address == 300,
            "A rediscovery reset did not bootstrap the relocated source.");

        selector.Reset();
        for (var step = 0; step < 10; step++)
            selector.Select([Candidate(100, step), Candidate(101, step)], Tick(step));
        // Both sources learned the same state before a long period of stillness.
        for (var step = 10; step < 200; step++)
            selector.Select([Candidate(100, 9), Candidate(101, 9)], Tick(step));
        Require(selector.Select([Candidate(101, 9)], Tick(200))?.Camera.Address == 101,
            "A surviving learned source was ignored after stillness.");
    }

    public static void FrozenSource()
    {
        var selector = new RenderCameraTemporalSelector();
        for (var step = 0; step < 15; step++)
            selector.Select([Candidate(100, step), Candidate(200, 0)], Tick(step));
        for (var step = 15; step < 40; step++)
        {
            var result = selector.Select([Candidate(100, 14), Candidate(200, step)], Tick(step));
            if (step >= 30) Require(result?.Camera.Address == 200,
                "The selector remained stuck on a valid but frozen source.");
        }
    }

    public static void LowRate()
    {
        var selector = new RenderCameraTemporalSelector();
        for (var step = 0; step < 10; step++)
        {
            var result = selector.Select(History().Append(Candidate(100, step)).ToArray(),
                TimeSpan.FromSeconds(step * 1.01));
            if (step >= 3) Require(result?.Camera.Address == 100, "The source never qualified at 1 Hz.");
        }
    }

    private static void Train(RenderCameraTemporalSelector selector)
    {
        for (var step = 0; step < 10; step++)
            selector.Select(History().Append(Candidate(100, step)).ToArray(), Tick(step));
    }

    private static RenderCameraConstantsCandidate[] History() =>
        Enumerable.Range(1, 8).Select(id => Candidate((ulong)id, 0)).ToArray();

    private static RenderCameraConstantsCandidate Candidate(ulong address, float yaw)
    {
        var radians = yaw * MathF.PI / 180;
        return new(address, new(10, 20, 30), new(0, 1, 0),
            new(MathF.Cos(radians), 0, -MathF.Sin(radians)),
            new(MathF.Sin(radians), 0, MathF.Cos(radians)),
            .2f, float.MaxValue, 1, 16f / 9, 4);
    }

    private static TimeSpan Tick(int step) => TimeSpan.FromSeconds(step / 60d);
    private static double Yaw(RenderCameraConstantsCandidate c) => Math.Atan2(c.Forward.X, c.Forward.Z) * 180 / Math.PI;
    private static double Wrap(double value) => ((value + 540) % 360) - 180;
    private static bool Near(double a, double b) => Math.Abs(a - b) < .001;
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
