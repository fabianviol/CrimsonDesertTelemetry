using System.Buffers.Binary;
using System.Globalization;
using System.Text.Json;
using CrimsonDesertTelemetry.Core;

internal static class UpstreamLightTests
{
    private const int HeaderBytes = 256, SceneBytes = 2816, Stride = 48, Count = 32768;
    private const int OutputOffset = HeaderBytes + SceneBytes;
    private const int CounterOffset = OutputOffset + Count * Stride;
    private const long CapturedTick = 10000;
    private const int ProcessId = 1234;
    private const long ProcessStart = 133000000000000000;
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };

    /// <summary>
    /// Offline check against a preserved private pair file (128-byte header, scene, output,
    /// counter, input). Prints what the production decoder would publish near a position.
    /// </summary>
    public static int Replay(string path, string x, string y, string z, string radius, string? jsonOutput = null)
    {
        var file = File.ReadAllBytes(path);
        const int pairHeader = 128;
        if (file.Length != pairHeader + SceneBytes + Count * Stride * 2 + 256 ||
            BinaryPrimitives.ReadUInt32LittleEndian(file) != 0x50445443)
            throw new InvalidDataException("Not a ManyLights pair file.");
        var scene = file.AsSpan(pairHeader, SceneBytes);
        var output = file.AsSpan(pairHeader + SceneBytes, Count * Stride);
        var counter = file.AsSpan(pairHeader + SceneBytes + Count * Stride, 256);
        var input = file.AsSpan(pairHeader + SceneBytes + Count * Stride + 256, Count * Stride);
        var camera = new CameraVector3(BinaryPrimitives.ReadSingleLittleEndian(scene[128..]),
            BinaryPrimitives.ReadSingleLittleEndian(scene[132..]), BinaryPrimitives.ReadSingleLittleEndian(scene[136..]));
        var player = (float.Parse(x, CultureInfo.InvariantCulture), float.Parse(y, CultureInfo.InvariantCulture),
            float.Parse(z, CultureInfo.InvariantCulture));
        var result = UpstreamLightDecoder.Decode(input, BinaryPrimitives.ReadUInt32LittleEndian(counter),
            output, BinaryPrimitives.ReadUInt32LittleEndian(counter[4..]), camera, player,
            float.Parse(radius, CultureInfo.InvariantCulture), 1, BinaryPrimitives.ReadUInt32LittleEndian(file.AsSpan(36)),
            DateTimeOffset.UnixEpoch, 0);
        Console.WriteLine(JsonSerializer.Serialize(new { result.InputRecords, result.Diagnostics }, JsonOptions));
        if (jsonOutput is not null)
        {
            using var file_ = new FileStream(jsonOutput, FileMode.CreateNew, FileAccess.Write);
            JsonSerializer.Serialize(file_, result, JsonOptions);
        }
        foreach (var light in result.Sources!.OrderBy(l => Distance(l.Position, player)))
            Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
                $"slot {light.SampleIndex,5} {light.Type,-10} n={light.MemberCount?.ToString() ?? "-",-3} d={Distance(light.Position, player),7:0.00} " +
                $"pos=({light.Position.X:0.000},{light.Position.Y:0.000},{light.Position.Z:0.000}) " +
                $"rgb=({light.ColorLinear.X:0.0000000},{light.ColorLinear.Y:0.0000000},{light.ColorLinear.Z:0.0000000}) " +
                $"{light.Kind ?? "?"} selected={light.RendererSelected} out={light.RenderedSampleIndex?.ToString() ?? "-"}"));
        return 0;
    }

    /// <summary>Shader rules: current bound, groups, member skips, specials, conversion and pairing.</summary>
    public static void DecodeRules()
    {
        var input = new byte[Count * Stride];
        var output = new byte[Count * Stride];
        var camera = new CameraVector3(10, 20, 30);
        // Slot 0: header of a 3-slot range; slot 2 inside it is not a member (w >= 0).
        // Members 1 and 3 are summed by the header and skipped as standalone threads.
        Header(input, 0, 3);
        Light(input, 1, (1, 2, 3), (1, .5f, .25f), -.05f, cone: -1, lookW: -1);
        Light(input, 2, (5, 2, 3), (7, 7, 7), .1f, cone: -1, lookW: 1);
        Light(input, 3, (3, 2, 3), (2, 1, .5f), -.05f, cone: -1, lookW: -1);
        // Slot 4: a group whose single member lives inside the dispatch bound.
        Header(input, 4, 1);
        Light(input, 5, (0, 0, 0), (0, 0, 0), -.05f, cone: -1, lookW: -1); // dead particle: zero group
        Light(input, 6, (2, 2, 2), (.5f, .5f, .5f), .2f, cone: -1, lookW: 0); // plain standalone point
        Light(input, 7, (2, 3, 2), (-1, .5f, .5f), .2f, cone: -1, lookW: 0); // exposure special: excluded
        Light(input, 8, (2, 2, 3), (.1f, .2f, .3f), -.2f, cone: -1, lookW: .5f); // w<0 but half>=0: standalone
        Light(input, 9, (500, 2, 2), (1, 1, 1), .2f, cone: -1, lookW: 0); // outside radius
        Light(input, 10, (2, 2, 4), (1, 1, 1), .2f, cone: MathF.PI / 6, lookW: 0, look: (0, -.6f, .8f)); // spot
        Light(input, 11, (4, 4, 4), (9, 9, 9), .2f, cone: -1, lookW: 0); // retained tail beyond the bound
        var sumColor = UpstreamLightDecoder.ConvertColor(1 + 2, .5f + 1, .25f + .5f);
        // Output: the group at a noisy member position (inside its spread), the plain
        // standalone at its exact position, and one unrelated output (a special).
        Out(output, 0, (3 - 10, 2 - 20, 3 - 30), sumColor);
        Out(output, 1, (2 - 10, 2 - 20, 2 - 30), UpstreamLightDecoder.ConvertColor(.5f, .5f, .5f));
        Out(output, 2, (0 - 10, 0 - 20, 0 - 30), new(4, 4, 4));
        var result = UpstreamLightDecoder.Decode(input, 11, output, 3, camera, (0, 0, 0), 100, 7, 42,
            DateTimeOffset.UnixEpoch, 12);
        var lights = result.Sources!;
        Check(result.Status == "available" && result.InputRecords == 11 && result.CaptureSequence == 7,
            "capture identity lost");
        Check(lights.Select(l => l.SampleIndex).SequenceEqual([0, 2, 6, 8, 10]),
            "group/member/special/zero/tail rules differ from ProcessManyLightsCS");
        var group = lights[0];
        Check(group is { Type: "group", MemberCount: 2, RendererSelected: true, RenderedSampleIndex: 0 } &&
            group.Position == new CameraVector3(2, 2, 3) && group.ColorLinear == sumColor && group.Kind == "point",
            "group sum/mean/conversion or noisy-position pairing wrong");
        Check(lights[1] is { Type: "standalone", RendererSelected: false, MemberCount: null },
            "non-member record inside a group range must stay standalone");
        Check(lights[2] is { RendererSelected: true, RenderedSampleIndex: 1 } &&
            Near(lights[2].LuminanceLinear, lights[2].ColorLinear.X * .212671f + lights[2].ColorLinear.Y * .71516f +
                lights[2].ColorLinear.Z * .07216f), "standalone pairing/luminance wrong");
        Check(lights[4] is { Kind: "spot", ConeHalfAngleDegrees: > 29.9f and < 30.1f, Direction: not null },
            "spot cone/direction not decoded");
        Check(result.Diagnostics is { Groups: 1, GroupMembers: 2, SpecialExcluded: 1, ZeroColor: 1, OutsideRadius: 1,
            SkippedMemberRecords: 3, PublishedRecords: 5, RendererSelected: 2, RenderedUnmatched: 1 },
            "diagnostics disagree: " + JsonSerializer.Serialize(result.Diagnostics));
        // A wrong colour or a far output must never be claimed as the renderer's.
        Out(output, 0, (3 - 10, 2 - 20, 3 - 30), new(sumColor.X * 1.01f, sumColor.Y, sumColor.Z));
        Out(output, 1, (2.2f - 10, 2 - 20, 2 - 30), UpstreamLightDecoder.ConvertColor(.5f, .5f, .5f));
        var strict = UpstreamLightDecoder.Decode(input, 11, output, 3, camera, (0, 0, 0), 100, 7, 42,
            DateTimeOffset.UnixEpoch, 12);
        Check(strict.Sources!.All(l => !l.RendererSelected) && strict.Diagnostics.RenderedUnmatched == 3,
            "colour/position tolerance matched unrelated output");
        ExpectInvalid(() => UpstreamLightDecoder.Decode(input, Count + 1, output, 3, camera, (0, 0, 0), 100, 7, 42,
            DateTimeOffset.UnixEpoch, 12), "input bound beyond capacity accepted");
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(result, JsonOptions));
        var first = json.RootElement.GetProperty("sources")[0];
        var second = json.RootElement.GetProperty("sources")[1];
        Check(first.GetProperty("memberCount").GetInt32() == 2 && first.GetProperty("renderedSampleIndex").GetInt32() == 0 &&
            !second.TryGetProperty("memberCount", out _) && !second.TryGetProperty("renderedSampleIndex", out _) &&
            second.GetProperty("rendererSelected").GetBoolean() == false, "upstream JSON contract");
    }

    /// <summary>Version-4 bridge: paired input decodes with the same sample; failures stay local.</summary>
    public static void BridgeProtocol()
    {
        var bytes = Mapping();
        Light(bytes.AsSpan(RenderLightReader.InputOffset), 0, (11, 21, 31), (1, 1, 1), .2f, cone: -1, lookW: 0);
        Light(bytes.AsSpan(RenderLightReader.InputOffset), 1, (500, 21, 31), (1, 1, 1), .2f, cone: -1, lookW: 0);
        U32(bytes, CounterOffset, 2);
        Out(bytes.AsSpan(OutputOffset), 0, (1, 1, 1), UpstreamLightDecoder.ConvertColor(1, 1, 1));
        U32(bytes, CounterOffset + 4, 1);
        var capture = Decode(bytes);
        Check(capture.Rendered.Status == "available" && capture.Upstream is { Status: "available", InputRecords: 2 } &&
            capture.Upstream.CaptureSequence == capture.Rendered.CaptureSequence &&
            capture.Upstream.FrameNumber == capture.Rendered.FrameNumber &&
            capture.Upstream.Sources!.Single() is { RendererSelected: true, RenderedSampleIndex: 0 },
            "paired input not decoded with its own filtered sample");
        foreach (var (state, reason) in new (uint, string)[] { (0, "input-disabled"), (2, "input-unavailable"),
            (3, "input-refused"), (9, "input-invalid") })
        {
            var changed = (byte[])bytes.Clone(); U32(changed, 168, state);
            var decoded = Decode(changed);
            Check(decoded.Rendered.Status == "available" && decoded.Upstream is { Status: "unavailable" } &&
                decoded.Upstream.UnavailableReason == reason && decoded.Upstream.Sources is null,
                $"input state {state} was not isolated to the input stream");
        }
        foreach (var (offset, value) in new (int, ulong)[] { (160, 0), (160, 1000), (160, 2000) })
        {
            var aliased = (byte[])bytes.Clone(); U64(aliased, offset, value);
            Check(Decode(aliased) is { Rendered.Status: "available", Upstream.UnavailableReason: "input-invalid" },
                "missing/aliased input resource accepted");
        }
        var overflow = (byte[])bytes.Clone(); U32(overflow, CounterOffset, Count + 1);
        Check(Decode(overflow) is { Rendered.Status: "available", Upstream.UnavailableReason: "input-invalid" },
            "overflowing input bound broke or leaked into the filtered stream");
        var stale = Decode(bytes, CapturedTick + 501);
        Check(stale.Rendered.UnavailableReason == "bridge-stale" && stale.Upstream?.UnavailableReason == "bridge-stale",
            "stale sample kept a live input stream");
        var waiting = (byte[])bytes.Clone(); U32(waiting, 28, 0);
        Check(Decode(waiting).Upstream?.UnavailableReason == "bridge-waiting", "native state not forwarded to input");
        var v3 = bytes[..RenderLightReader.VisibilityTotalBytes];
        U32(v3, 4, 3); U32(v3, 12, (uint)v3.Length);
        Check(Decode(v3) is { Rendered.Status: "available", Upstream: null }, "v3 bridge invented an input stream");
        var wrongLayout = (byte[])bytes.Clone(); U32(wrongLayout, 4, 3);
        ExpectInvalid(() => Decode(wrongLayout), "v3 header with v4 payload size accepted");
    }

    /// <summary>Behind-camera input lights become physics targets; filtered contributions reuse them.</summary>
    public static void PhysicsTargets()
    {
        var pid = Environment.ProcessId + 230000;
        long now = 10000;
        using var results = System.IO.MemoryMappedFiles.MemoryMappedFile.CreateNew(
            $"Local\\CrimsonDesertTelemetry.PhysicsVisibilityResultV2.{pid}", PhysicsVisibilityClient.BatchBytes);
        using var output = results.CreateViewAccessor();
        using var client = new PhysicsVisibilityClient(pid, 1, () => now, 100);
        using var query = System.IO.MemoryMappedFiles.MemoryMappedFile.OpenExisting(
            $"Local\\CrimsonDesertTelemetry.PhysicsVisibilityQueryV2.{pid}");
        using var input = query.CreateViewAccessor();
        var camera = new CameraSnapshot(new(0, 2, -5), new(0, 1, 0), new(1, 0, 0), new(0, 0, 1), .1f, null, 50, 1.7f);
        // Output contribution 3 is the group at a noisy member position 0.4gu from its centre.
        var rendered = new RenderLightsSnapshot("available", RenderLightReader.SourceName, 17, 44, DateTimeOffset.UtcNow, 10,
            camera, [new RenderedLightSnapshot(3, new(0.4f, 2, 10), new(1, 1, 1), 1, "point", null, null)], new(1, 1, 0, 0));
        var group = new UpstreamLightSnapshot(20, "group", 16, new(0, 2, 10), new(1, 1, 1), 1, "point", null, null, true, 3);
        var behind = new UpstreamLightSnapshot(30, "standalone", null, new(0, 2, -20), new(1, 1, 1), 1, "point", null, null, false, null);
        var upstream = new UpstreamLightsSnapshot("available", UpstreamLightDecoder.SourceName, 17, 44, DateTimeOffset.UtcNow, 10,
            100, [group, behind], new(1, 16, 1, 0, 16, 0, 0, 0, 0, 2, 1, 0));
        var snapshot = new EngineLightsSnapshot("available", EngineLightReader.SourceName, 100, [],
            new(0, 0, 0, 0, 0, 0, 0, 0, 0, 0), null, rendered, upstream);
        client.Apply((0, 2, 0), snapshot);
        const int header = PhysicsVisibilityClient.HeaderBytes;
        var targets = Enumerable.Range(0, (int)input.ReadUInt32(12))
            .Select(n => (input.ReadSingle(header + n * 128 + 112), input.ReadSingle(header + n * 128 + 120))).ToArray();
        Check(targets.Length == 2 && targets.Contains((0f, 10f)) && targets.Contains((0f, -20f)),
            "input centre and behind-camera light were not the ray targets");
        var bytes = new byte[PhysicsVisibilityClient.BatchBytes]; input.ReadArray(0, bytes, 0, bytes.Length);
        BitConverter.TryWriteBytes(bytes.AsSpan(0), 0x53564443u);
        for (var n = 0; n < targets.Length; n++)
        {
            var o = header + n * 128;
            BitConverter.TryWriteBytes(bytes.AsSpan(o), 0x53564443u); BitConverter.TryWriteBytes(bytes.AsSpan(o + 12), 1u);
            BitConverter.TryWriteBytes(bytes.AsSpan(o + 28), 1u); BitConverter.TryWriteBytes(bytes.AsSpan(o + 56), (ulong)now + 60);
            BitConverter.TryWriteBytes(bytes.AsSpan(o + 76), 9u);
            BitConverter.TryWriteBytes(bytes.AsSpan(o + 80), targets[n].Item2 < 0 ? 0u : 5u);
        }
        output.Write(16, 1L); output.WriteArray(0, bytes, 0, 16); output.WriteArray(24, bytes, 24, bytes.Length - 24); output.Write(16, 2L);
        now += 60;
        var applied = client.Apply((0, 2, 0), snapshot);
        Check(applied.Upstream!.Sources![0].SourceVisibility!.Status == "clear" &&
            applied.Upstream.Sources[1].SourceVisibility!.Status == "blocked",
            "input lights did not receive their own measurements");
        Check(applied.Rendered!.Sources![0].SourceVisibility!.Status == "clear",
            "filtered group contribution did not reuse its input centre's measurement");
        Check(applied.Upstream.Sources[0] with { SourceVisibility = null } == group,
            "physics changed raw input light data");
    }

    /// <summary>A configured input stream owns the smoothed feed with honest coverage labels.</summary>
    public static void Smoothing()
    {
        var smoother = new SmoothedLightProcessor();
        var captured = DateTimeOffset.UtcNow.AddMilliseconds(-10);
        var upstream = new UpstreamLightsSnapshot("available", UpstreamLightDecoder.SourceName, 5, 9, captured, 10, 2,
            [new UpstreamLightSnapshot(4, "group", 16, new(1, 2, 3), new(2, 1, .5f), 1.2f, "point", null, null, false, null)],
            new(1, 16, 0, 0, 16, 0, 0, 0, 0, 1, 0, 0));
        var smooth = smoother.ProcessUpstream(upstream, captured.AddMilliseconds(10));
        Check(smooth is { Status: "available", Source: "spatially-grouped-manylights-input",
            Coverage: "current-engine-lights-before-view-selection" } &&
            smooth.Sources!.Single().RawSumColorLinear == new CameraVector3(2, 1, .5f),
            "input stream did not feed smoothing with input coverage labels");
        var unavailable = smoother.ProcessUpstream(UpstreamLightDecoder.Unavailable("input-unavailable"), captured.AddMilliseconds(20));
        Check(unavailable is { Status: "unavailable", UnavailableReason: "input-unavailable",
            Source: "spatially-grouped-manylights-input" }, "unavailable input silently fell back or lost its reason");
        var rendered = new RenderLightsSnapshot("available", RenderLightReader.SourceName, 6, 10, captured.AddMilliseconds(30), 10,
            null, [new RenderedLightSnapshot(1, new(1, 2, 3), new(1, 1, 1), 1, "point", null, null)], new(1, 1, 0, 0));
        Check(smoother.Process(rendered, captured.AddMilliseconds(40)).Source == "spatially-grouped-filtered-manylights",
            "filtered labels not restored when input is not configured");
    }

    private static RenderCapture Decode(byte[] bytes, long now = CapturedTick + 100) =>
        RenderLightReader.DecodeAll(bytes, ProcessId, ProcessStart, now, (10, 20, 30), 10);

    private static byte[] Mapping()
    {
        var bytes = new byte[RenderLightReader.InputTotalBytes];
        U32(bytes, 0, 0x52445443); U32(bytes, 4, 4);
        U32(bytes, 8, HeaderBytes); U32(bytes, 12, (uint)bytes.Length);
        U64(bytes, 16, 2); U32(bytes, 24, ProcessId); U32(bytes, 28, 1);
        U64(bytes, 32, ProcessStart); U64(bytes, 40, 7);
        U64(bytes, 48, CapturedTick); U64(bytes, 56, CapturedTick + 10);
        U32(bytes, 64, 42); U32(bytes, 68, SceneBytes);
        U32(bytes, 72, Count); U32(bytes, 76, Stride); U32(bytes, 84, 15);
        U64(bytes, 88, 1000); U64(bytes, 96, 2000); U32(bytes, 116, 256);
        U32(bytes, 120, 1); U32(bytes, 124, 8); U32(bytes, 148, 1500); U32(bytes, 152, 256);
        for (var i = 0; i < Count; i++) U32(bytes, RenderLightReader.TotalBytes + i * 8, 11);
        U64(bytes, 160, 3000); U32(bytes, 168, 1); U32(bytes, 172, Count * Stride);
        EngineCameraTests.SceneBytes().CopyTo(bytes, HeaderBytes);
        return bytes;
    }

    private static void Header(Span<byte> records, int slot, int count)
    {
        var o = slot * Stride;
        BinaryPrimitives.WriteInt32LittleEndian(records[(o + 40)..], -2);
        BinaryPrimitives.WriteInt32LittleEndian(records[(o + 44)..], count);
    }

    private static void Light(Span<byte> records, int slot, (float X, float Y, float Z) position,
        (float R, float G, float B) color, float w, float cone, float lookW, (float X, float Y, float Z)? look = null)
    {
        var o = slot * Stride;
        F(records, o, position.X); F(records, o + 4, position.Y); F(records, o + 8, position.Z); F(records, o + 12, MathF.PI);
        F(records, o + 16, color.R); F(records, o + 20, color.G); F(records, o + 24, color.B); F(records, o + 28, w);
        H(records, o + 32, 0); H(records, o + 34, 1); H(records, o + 36, 0); H(records, o + 38, cone);
        var direction = look ?? (0, 0, 1);
        H(records, o + 40, direction.X); H(records, o + 42, direction.Y); H(records, o + 44, direction.Z); H(records, o + 46, lookW);
    }

    private static void Out(Span<byte> records, int slot, (float X, float Y, float Z) relative, CameraVector3 color)
    {
        var o = slot * Stride;
        F(records, o, relative.X); F(records, o + 4, relative.Y); F(records, o + 8, relative.Z); F(records, o + 12, MathF.PI);
        F(records, o + 16, color.X); F(records, o + 20, color.Y); F(records, o + 24, color.Z);
        H(records, o + 38, -1);
    }

    private static void F(Span<byte> bytes, int offset, float value) => BinaryPrimitives.WriteSingleLittleEndian(bytes[offset..], value);
    private static void H(Span<byte> bytes, int offset, float value) =>
        BinaryPrimitives.WriteUInt16LittleEndian(bytes[offset..], BitConverter.HalfToUInt16Bits((Half)value));
    private static void U32(byte[] bytes, int offset, uint value) => BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(offset), value);
    private static void U64(byte[] bytes, int offset, ulong value) => BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(offset), value);
    private static bool Near(float actual, float expected) => Math.Abs(actual - expected) < 1e-4f;
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
    private static void ExpectInvalid(Action action, string message)
    {
        try { action(); }
        catch (InvalidDataException) { return; }
        throw new InvalidOperationException(message);
    }
    private static double Distance(CameraVector3 a, (float X, float Y, float Z) b) =>
        Math.Sqrt(Math.Pow((double)a.X - b.X, 2) + Math.Pow((double)a.Y - b.Y, 2) + Math.Pow((double)a.Z - b.Z, 2));
}
