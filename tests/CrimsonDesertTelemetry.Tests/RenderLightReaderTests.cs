using System.Buffers.Binary;
using System.IO.MemoryMappedFiles;
using System.Text.Json;
using CrimsonDesertTelemetry.Core;

internal static class RenderLightReaderTests
{
    private const int ProcessId = 1234;
    private const long ProcessStart = 133000000000000000;
    private const long CapturedTick = 10000;
    private const int HeaderBytes = 256;
    private const int SceneBytes = 2816;
    private const int RecordBytes = 48;
    private const int RecordCount = 32768;
    private const int TotalBytes = HeaderBytes + SceneBytes + RecordCount * RecordBytes;

    public static void DecodeFields()
    {
        var bytes = Snapshot();
        Record(bytes, 0, (1, 2, 3), (4, 2, 1), -1, (0, -1, 0));
        Record(bytes, 4, (2, 2, 3), (.5f, .25f, .125f), MathF.PI / 6, (0, -.6f, .8f));
        // The fourth colour component is not a second intensity multiplier.
        F(bytes, RecordOffset(0) + 28, 123);
        var result = Decode(bytes, CapturedTick + 100, (15, 20, 35), 10);
        Check(result.Status == "available" && result.Source == "filtered-manylights" &&
              result.CaptureSequence == 7 && result.FrameNumber == 42 && result.AgeMilliseconds == 100 &&
              result.CapturedAt is not null && result.UnavailableReason is null,
            "Fresh capture metadata was not preserved.");
        Check(result.Camera?.Position == new CameraVector3(10, 20, 30),
            "The light frame lost its paired camera.");
        var sources = result.Sources ?? throw new InvalidOperationException("Available capture has no sources.");
        Check(sources.Count == 2 && sources[0].SampleIndex == 0 && sources[1].SampleIndex == 4,
            "Inactive entries were published or capture-local record indices changed.");
        Check(sources[0].Position == new CameraVector3(11, 22, 33) &&
              sources[1].Position == new CameraVector3(12, 22, 33),
            "Relative light positions were not converted using the paired scene origin.");
        Check(sources[0].ColorLinear == new CameraVector3(4, 2, 1) &&
              Near(sources[0].LuminanceLinear, 4 * .212671f + 2 * .71516f + .07216f),
            "Current HDR colour/luminance was clamped or scaled by an unrelated component.");
        Check(sources[0].Kind == "point" && sources[0].Direction is null &&
              sources[0].ConeHalfAngleDegrees is null,
            "Point light was given a spot direction or cone.");
        Check(sources[1].Kind == "spot" && sources[1].Direction is CameraVector3 direction &&
              Near(direction.X, 0) && Near(direction.Y, -.6f, .001f) && Near(direction.Z, .8f, .001f) &&
              Near(direction.X * direction.X + direction.Y * direction.Y + direction.Z * direction.Z, 1) &&
              sources[1].ConeHalfAngleDegrees is float cone && Near(cone, 30, .05f),
            "Packed half-precision spotlight direction/cone was not decoded and normalized.");
        Check(result.Diagnostics.ActiveRecords == 2 && result.Diagnostics.PublishedRecords == 2 &&
              result.Diagnostics.Malformed == 0 && result.Diagnostics.OutsideRadius == 0,
            "Clean capture diagnostics mismatch.");

        using var json = JsonDocument.Parse(JsonSerializer.Serialize(result, JsonOptions));
        var jsonSources = json.RootElement.GetProperty("sources");
        Check(!jsonSources[0].TryGetProperty("direction", out _) &&
              !jsonSources[0].TryGetProperty("coneHalfAngleDegrees", out _) &&
              jsonSources[1].TryGetProperty("direction", out _) &&
              jsonSources[1].TryGetProperty("coneHalfAngleDegrees", out _),
            "Rendered-light JSON did not omit inapplicable point fields.");
    }

    public static void Validation()
    {
        var bytes = Snapshot();
        Record(bytes, 0, (1, 0, 0), (1, .5f, .25f), -1, (0, 0, 1));
        Record(bytes, 1, (float.NaN, 0, 0), (1, 1, 1), -1, (0, 0, 1));
        Record(bytes, 2, (1, 0, 0), (-1, 1, 1), -1, (0, 0, 1));
        Record(bytes, 3, (1, 0, 0), (float.PositiveInfinity, 1, 1), -1, (0, 0, 1));
        Record(bytes, 4, (100, 0, 0), (1, 1, 1), -1, (0, 0, 1));
        Record(bytes, 5, (2, 0, 0), (1, 1, 1), float.NaN, (0, 0, 1));
        Record(bytes, 6, (3, 0, 0), (1, 1, 1), .4f, (0, 0, 0));
        Record(bytes, 7, (float.NaN, 0, 0), (float.NaN, 1, 1), -1, (0, 0, 1));
        F(bytes, RecordOffset(7) + 12, 0);
        Record(bytes, 8, (4, 0, 0), (1, 1, 1), -1, (0, 0, 1));
        F(bytes, RecordOffset(8) + 12, 1);
        var result = Decode(bytes);
        var sources = result.Sources ?? throw new InvalidOperationException("Valid records were all discarded.");
        Check(result.Status == "available" && sources.Count == 3 &&
              result.Diagnostics.PublishedRecords == 3 && result.Diagnostics.Malformed >= 3 &&
              result.Diagnostics.OutsideRadius == 1,
            "Invalid fields, inactive markers and radius filtering were not separated.");
        Check(sources.Select(source => source.SampleIndex).SequenceEqual([0, 5, 6]),
            "A malformed/inactive record leaked into the published light list.");
        Check(sources[1].Kind is null && sources[1].Direction is null &&
              sources[1].ConeHalfAngleDegrees is null,
            "Unknown packed cone was assigned a light type or direction.");
        Check(sources[2].Kind == "spot" && sources[2].Direction is null &&
              sources[2].ConeHalfAngleDegrees is not null,
            "Invalid look vector discarded an otherwise usable spotlight.");

        var radiusBytes = Snapshot();
        Record(radiusBytes, 0, (10, 0, 0), (0, 0, 0), -1, (0, 0, 1));
        Record(radiusBytes, 1, (10.01f, 0, 0), (1, 1, 1), -1, (0, 0, 1));
        var radius = Decode(radiusBytes, nearbyRadius: 10);
        Check(radius.Sources is { Count: 1 } && radius.Sources[0].SampleIndex == 0 &&
              radius.Sources[0].LuminanceLinear == 0 && radius.Diagnostics.OutsideRadius == 1,
            "Radius boundary or a valid zero-intensity active record was mishandled.");
    }

    public static void ProtocolAndFreshness()
    {
        var bytes = Snapshot();
        Record(bytes, 0, (1, 0, 0), (1, 1, 1), -1, (0, 0, 1));
        Check(Decode(bytes, CapturedTick + 500).Status == "available", "Exactly 500ms was rejected as stale.");
        AssertUnavailable(Decode(bytes, CapturedTick + 501), "A stale render sample was published.");
        ExpectInvalid(() => Decode(bytes, CapturedTick - 1), "A future capture timestamp was accepted.");
        var republished = (byte[])bytes.Clone();
        U64(republished, 56, CapturedTick + 5000);
        AssertUnavailable(Decode(republished, CapturedTick + 5000),
            "A recent publication timestamp made an old GPU capture appear fresh.");

        foreach (var state in new uint[] { 0, 2, 3, 4, 5 })
        {
            var inactive = (byte[])bytes.Clone();
            U32(inactive, 28, state);
            AssertUnavailable(Decode(inactive), $"Native state {state} exposed a light list.");
        }

        foreach (var (offset, value, description) in new (int Offset, uint Value, string Description)[]
        {
            (0, 0, "magic"), (4, 2, "version"), (8, 255, "header length"),
            (12, TotalBytes - 1, "total length"), (24, ProcessId + 1, "PID"),
            (28, 6, "unknown native state"), (68, SceneBytes - 1, "scene length"),
            (72, RecordCount - 1, "record count"), (76, RecordBytes - 1, "record stride"),
            (84, 3, "missing coherence flags"), (84, 15, "unknown coherence flags")
        })
        {
            var corrupt = (byte[])bytes.Clone();
            U32(corrupt, offset, value);
            ExpectInvalid(() => Decode(corrupt), $"Malformed {description} was accepted.");
        }
        var differentProcess = (byte[])bytes.Clone();
        U64(differentProcess, 32, ProcessStart + 1);
        ExpectInvalid(() => Decode(differentProcess), "A recycled PID's mapping was accepted.");
        var torn = (byte[])bytes.Clone();
        U64(torn, 16, 3);
        ExpectInvalid(() => Decode(torn), "Odd/in-progress seqlock was accepted.");
        ExpectInvalid(() => Decode(bytes[..^1]), "Truncated mapping was accepted.");
        ExpectInvalid(() => Decode([.. bytes, 0]), "Oversized mapping was accepted.");

        var mismatchedFrame = (byte[])bytes.Clone();
        U32(mismatchedFrame, 64, 43);
        ExpectInvalid(() => Decode(mismatchedFrame), "Different scene/header frames were paired.");
        MappingFailureIsolation();
    }

    private static RenderLightsSnapshot Decode(byte[] bytes, long now = CapturedTick + 100,
        (float X, float Y, float Z)? player = null, float nearbyRadius = 10) =>
        RenderLightReader.Decode(bytes, ProcessId, ProcessStart, now, player ?? (10, 20, 30), nearbyRadius);

    private static byte[] Snapshot()
    {
        var bytes = new byte[TotalBytes];
        U32(bytes, 0, 0x52445443); U32(bytes, 4, 1);
        U32(bytes, 8, HeaderBytes); U32(bytes, 12, TotalBytes);
        U64(bytes, 16, 2); U32(bytes, 24, ProcessId); U32(bytes, 28, 1);
        U64(bytes, 32, ProcessStart); U64(bytes, 40, 7);
        U64(bytes, 48, CapturedTick); U64(bytes, 56, CapturedTick + 10);
        U32(bytes, 64, 42); U32(bytes, 68, SceneBytes);
        U32(bytes, 72, RecordCount); U32(bytes, 76, RecordBytes); U32(bytes, 84, 7);
        EngineCameraTests.SceneBytes().CopyTo(bytes, HeaderBytes);
        return bytes;
    }

    private static void Record(byte[] bytes, int index, (float X, float Y, float Z) position,
        (float R, float G, float B) colour, float cone, (float X, float Y, float Z) look)
    {
        var offset = RecordOffset(index);
        F(bytes, offset, position.X); F(bytes, offset + 4, position.Y); F(bytes, offset + 8, position.Z);
        F(bytes, offset + 12, MathF.PI);
        F(bytes, offset + 16, colour.R); F(bytes, offset + 20, colour.G); F(bytes, offset + 24, colour.B);
        F(bytes, offset + 28, .005f);
        H(bytes, offset + 32, 0); H(bytes, offset + 34, 1); H(bytes, offset + 36, 0);
        H(bytes, offset + 38, cone);
        H(bytes, offset + 40, look.X); H(bytes, offset + 42, look.Y); H(bytes, offset + 44, look.Z);
    }

    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };

    private static void MappingFailureIsolation()
    {
        var bytes = Snapshot();
        U32(bytes, 24, (uint)Environment.ProcessId);
        U32(bytes, 0, 0);
        using var mapping = MemoryMappedFile.CreateNew(
            $"Local\\CrimsonDesertTelemetry.Render.{Environment.ProcessId}", TotalBytes,
            MemoryMappedFileAccess.ReadWrite);
        using var writer = mapping.CreateViewAccessor();
        writer.WriteArray(0, bytes, 0, bytes.Length);
        using (var malformedReader = new RenderLightReader(Environment.ProcessId, ProcessStart))
        {
            var result = malformedReader.Capture((10, 20, 30), 10);
            AssertUnavailable(result, "Malformed bridge prevented graceful optional-light failure.");
            Check(result.UnavailableReason == "bridge-invalid", "Malformed mapping lost its failure reason.");
        }

        U32(bytes, 0, 0x52445443);
        U64(bytes, 16, 4);
        var now = (ulong)Environment.TickCount64;
        U64(bytes, 48, now); U64(bytes, 56, now);
        writer.WriteArray(0, bytes, 0, bytes.Length);
        using var reader = new RenderLightReader(Environment.ProcessId, ProcessStart);
        Check(reader.Capture((10, 20, 30), 10).Status == "available", "Reader could not recover a valid mapping.");
        U64(bytes, 16, 6); U32(bytes, 28, 4); U32(bytes, 84, 0);
        writer.WriteArray(0, bytes, 0, bytes.Length);
        var conflict = reader.Capture((10, 20, 30), 10);
        AssertUnavailable(conflict, "A status update reused the previous cached active capture.");
        Check(conflict.UnavailableReason == "legacy-plugin-conflict", "Native conflict status was lost.");
    }

    private static int RecordOffset(int index) => HeaderBytes + SceneBytes + index * RecordBytes;
    private static void U32(byte[] bytes, int offset, uint value) => BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(offset), value);
    private static void U64(byte[] bytes, int offset, ulong value) => BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(offset), value);
    private static void F(byte[] bytes, int offset, float value) => BinaryPrimitives.WriteSingleLittleEndian(bytes.AsSpan(offset), value);
    private static void H(byte[] bytes, int offset, float value) => BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(offset), BitConverter.HalfToUInt16Bits((Half)value));
    private static bool Near(float actual, float expected, float tolerance = .0001f) => Math.Abs(actual - expected) < tolerance;
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
    private static void AssertUnavailable(RenderLightsSnapshot result, string message)
    {
        Check(result.Status == "unavailable" && result.Sources is null &&
              !string.IsNullOrWhiteSpace(result.UnavailableReason), message);
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(result, JsonOptions));
        Check(!json.RootElement.TryGetProperty("sources", out _),
            "Unavailable render capture serialized sources as a plausible empty light list.");
    }
}
