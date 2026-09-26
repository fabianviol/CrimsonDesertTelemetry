using System.Buffers.Binary;
using System.Numerics;

namespace CrimsonDesertTelemetry.Core;

/// <summary>
/// Current ManyLights INPUT: the engine's light records BEFORE ProcessManyLightsCS applies
/// its view-dependent range, frustum, HiZ and brightness selection. The native bridge copies
/// this full-capacity buffer with every filtered sample, on the same command list and fence.
///
/// Decoded with the captured shader's own rules (PIX PSO475 ProcessManyLightsCS; producers
/// InjectLightsCS, InjectLightGroupsCS, GPUSpawnPointUpdateCS, GPUParticleUpdateCS):
/// DWORD0 of the paired consumer structure counter bounds the current records; the tail
/// beyond it is retained history. A record whose int32 at +40 is -2 heads a group of the
/// next count records (the 16 flame particles of a fire bowl); members carry negative
/// color.w. Standalone threads skip w&lt;0 records whose half at +46 is negative. RGB gets
/// the shader's 5% luminance floor and exact 3x3 matrix, so a renderer-selected record
/// reproduces its filtered-output colour and can be matched to it.
///
/// A group position is the DERIVED member mean; the renderer picks a noisy,
/// luminance-weighted member each frame, which is not reproducible. Records with negative
/// RGB are exposure-dependent specials whose x/y InjectLightsCS stores in VIEW space: they
/// are excluded and counted, never published at a false world position.
/// </summary>
public static class UpstreamLightDecoder
{
    public const string SourceName = "manylights-input";
    private const int Count = RenderLightReader.RawCount, Stride = RenderLightReader.Stride;
    // Exact literals from the captured shader (listing lines 725-728 and 1086-1105).
    private static readonly float WeightR = L(0x3FCB38CDA0000000), WeightG = L(0x3FE6E29740000000),
        WeightB = L(0x3FB279AAE0000000), Floor = L(0x3FA99999A0000000);
    private static readonly float[] Matrix =
    [
        L(0x3FE39EADE0000000), L(0x3FD5BA8820000000), L(0x3FA840E180000000),
        L(0x3FB1F8A0A0000000), L(0x3FED52D240000000), L(0x3F8B8BAC80000000),
        L(0x3F951D68C0000000), L(0x3FBC0D6F60000000), L(0x3FEBD566C0000000)
    ];

    public static UpstreamLightsSnapshot Unavailable(string reason) => new(
        "unavailable", SourceName, null, null, null, null, null, null, new(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0), reason);

    /// <summary>Shader colour conversion for ordinary non-negative RGB.</summary>
    public static CameraVector3 ConvertColor(float r, float g, float b)
    {
        var floor = (r * WeightR + g * WeightG + b * WeightB) * Floor;
        r = MathF.Max(floor, r); g = MathF.Max(floor, g); b = MathF.Max(floor, b);
        return new(r * Matrix[0] + g * Matrix[1] + b * Matrix[2],
            r * Matrix[3] + g * Matrix[4] + b * Matrix[5],
            r * Matrix[6] + g * Matrix[7] + b * Matrix[8]);
    }

    public static UpstreamLightsSnapshot Decode(ReadOnlySpan<byte> input, uint inputBound,
        ReadOnlySpan<byte> output, uint outputCount, CameraVector3 camera, (float X, float Y, float Z) player,
        float radius, ulong captureSequence, uint frameNumber, DateTimeOffset capturedAt, long ageMilliseconds)
    {
        if (input.Length != Count * Stride || output.Length != Count * Stride)
            throw new InvalidDataException("ManyLights input/output capacity disagrees with the bridge layout.");
        if (inputBound > Count || outputCount > Count)
            throw new InvalidDataException("ManyLights input bound or output count exceeds capacity.");
        var radiusSquared = (double)radius * radius;
        var lights = new List<Candidate>();
        int groups = 0, members = 0, standalone = 0, global = 0, skipped = 0, special = 0, zero = 0,
            malformed = 0, outside = 0;
        for (var slot = 0; slot < inputBound; slot++)
        {
            var record = input.Slice(slot * Stride, Stride);
            var count = Math.Clamp(I32(record, 44), 0, 32767);
            if (I32(record, 40) == -2 && count > 0)
            {
                // The shader reads the following records without the dispatch bound;
                // D3D12 returns zero beyond the resource, which contributes nothing.
                float r = 0, g = 0, b = 0, x = 0, y = 0, z = 0, lookX = 0, lookY = 0, lookZ = 0, cone = float.NaN;
                int live = 0;
                var finite = true;
                var memberPositions = new List<Vector3>();
                for (var i = 0; i < count && slot + 1 + i < Count; i++)
                {
                    var member = input.Slice((slot + 1 + i) * Stride, Stride);
                    if (!(F(member, 28) < 0)) continue;
                    var position = new Vector3(F(member, 0), F(member, 4), F(member, 8));
                    float mr = F(member, 16), mg = F(member, 20), mb = F(member, 24);
                    if (!Finite(position) || !float.IsFinite(mr) || !float.IsFinite(mg) || !float.IsFinite(mb))
                    { finite = false; break; }
                    r = mr + r; g = mg + g; b = mb + b;
                    x = position.X + x; y = position.Y + y; z = position.Z + z;
                    lookX = H(member, 40) + lookX; lookY = H(member, 42) + lookY; lookZ = H(member, 44) + lookZ;
                    cone = H(member, 38);
                    memberPositions.Add(position);
                    live++;
                }
                if (!finite) { malformed++; continue; }
                if (r < 0 || g < 0 || b < 0) { special++; continue; }
                if (live == 0 || r == 0 && g == 0 && b == 0) { zero++; continue; }
                groups++;
                members += live;
                var inverse = 1f / Math.Max(1, live);
                var center = new Vector3(inverse * x, inverse * y, inverse * z);
                var spread = memberPositions.Max(p => Vector3.Distance(p, center));
                var look = new Vector3(lookX, lookY, lookZ);
                lights.Add(new(slot, true, live, center, ConvertColor(r, g, b), cone,
                    look.LengthSquared() > 0 ? Vector3.Normalize(look) : Vector3.UnitZ, spread));
                continue;
            }
            if (F(record, 28) < 0 && H(record, 46) < 0) { skipped++; continue; }
            var world = new Vector3(F(record, 0), F(record, 4), F(record, 8));
            float red = F(record, 16), green = F(record, 20), blue = F(record, 24);
            if (!Finite(world) || !float.IsFinite(red) || !float.IsFinite(green) || !float.IsFinite(blue))
            { malformed++; continue; }
            if (red < 0 || green < 0 || blue < 0) { special++; continue; }
            if (red == 0 && green == 0 && blue == 0) { zero++; continue; }
            standalone++;
            if (Math.Abs(F(record, 28)) > 99999) global++;
            lights.Add(new(slot, false, 0, world, ConvertColor(red, green, blue), H(record, 38),
                new Vector3(H(record, 40), H(record, 42), H(record, 44)), 0));
        }

        var published = new List<Candidate>();
        foreach (var light in lights)
        {
            var dx = (double)light.Position.X - player.X;
            var dy = (double)light.Position.Y - player.Y;
            var dz = (double)light.Position.Z - player.Z;
            if (dx * dx + dy * dy + dz * dz > radiusSquared) outside++;
            else published.Add(light);
        }

        // Match against THIS sample's filtered output. RGB after the shader conversion is
        // effectively a fingerprint; position bounds prevent coincidental colours matching.
        var outputs = new List<(int Index, Vector3 World, Vector3 Rgb)>();
        var matchRadius = radius + 2.0;
        for (var index = 0; index < outputCount; index++)
        {
            var record = output.Slice(index * Stride, Stride);
            var world = new Vector3(camera.X + F(record, 0), camera.Y + F(record, 4), camera.Z + F(record, 8));
            var rgb = new Vector3(F(record, 16), F(record, 20), F(record, 24));
            if (!Finite(world) || !Finite(rgb)) continue;
            var dx = (double)world.X - player.X;
            var dy = (double)world.Y - player.Y;
            var dz = (double)world.Z - player.Z;
            if (dx * dx + dy * dy + dz * dz <= matchRadius * matchRadius) outputs.Add((index, world, rgb));
        }
        var claimed = new bool[outputs.Count];
        var result = new List<UpstreamLightSnapshot>(published.Count);
        var selected = 0;
        foreach (var light in published)
        {
            var color = new Vector3(light.Color.X, light.Color.Y, light.Color.Z);
            var best = -1;
            var bestError = float.MaxValue;
            for (var i = 0; i < outputs.Count; i++)
            {
                if (claimed[i]) continue;
                var error = ColorError(color, outputs[i].Rgb);
                if (error > 1e-4f) continue;
                var distance = Vector3.Distance(outputs[i].World, light.Position);
                if (light.Group ? distance > light.Spread + .5f : distance > .05f) continue;
                if (error < bestError) { best = i; bestError = error; }
            }
            int? renderedIndex = null;
            if (best >= 0) { claimed[best] = true; renderedIndex = outputs[best].Index; selected++; }
            var (kind, direction, halfAngle) = RenderLightReader.DecodeKind(light.Cone, light.Look);
            result.Add(new(light.Slot, light.Group ? "group" : "standalone", light.Group ? light.Members : null,
                new(light.Position.X, light.Position.Y, light.Position.Z), light.Color,
                light.Color.X * .212671f + light.Color.Y * .71516f + light.Color.Z * .07216f,
                kind, direction, halfAngle, renderedIndex is not null, renderedIndex));
        }
        var unmatched = 0;
        for (var i = 0; i < outputs.Count; i++)
        {
            var dx = (double)outputs[i].World.X - player.X;
            var dy = (double)outputs[i].World.Y - player.Y;
            var dz = (double)outputs[i].World.Z - player.Z;
            if (!claimed[i] && dx * dx + dy * dy + dz * dz <= radiusSquared) unmatched++;
        }
        return new("available", SourceName, captureSequence, frameNumber, capturedAt, ageMilliseconds, inputBound,
            result, new(groups, members, standalone, global, skipped, special, zero, malformed, outside,
                result.Count, selected, unmatched));
    }

    /// <summary>Upstream records in the element type the smoothing and HUD paths already consume.</summary>
    public static RenderLightsSnapshot AsRendered(UpstreamLightsSnapshot upstream, CameraSnapshot? camera) => new(
        upstream.Status, SourceName, upstream.CaptureSequence, upstream.FrameNumber, upstream.CapturedAt,
        upstream.AgeMilliseconds, camera,
        upstream.Sources?.Select(light => new RenderedLightSnapshot(light.SampleIndex, light.Position,
            light.ColorLinear, light.LuminanceLinear, light.Kind, light.Direction, light.ConeHalfAngleDegrees,
            light.SourceVisibility)).ToArray(),
        new(upstream.Diagnostics.PublishedRecords + upstream.Diagnostics.OutsideRadius,
            upstream.Diagnostics.PublishedRecords, upstream.Diagnostics.Malformed, upstream.Diagnostics.OutsideRadius),
        upstream.UnavailableReason);

    private sealed record Candidate(int Slot, bool Group, int Members, Vector3 Position, CameraVector3 Color,
        float Cone, Vector3 Look, float Spread);

    private static float ColorError(Vector3 expected, Vector3 actual)
    {
        var scale = MathF.Max(1, MathF.Max(MathF.Max(MathF.Abs(expected.X), MathF.Abs(expected.Y)), MathF.Abs(expected.Z)));
        return MathF.Max(MathF.Max(MathF.Abs(expected.X - actual.X), MathF.Abs(expected.Y - actual.Y)),
            MathF.Abs(expected.Z - actual.Z)) / scale;
    }
    private static bool Finite(Vector3 value) => float.IsFinite(value.X) && float.IsFinite(value.Y) &&
        float.IsFinite(value.Z) && MathF.Abs(value.X) <= 10_000_000 && MathF.Abs(value.Y) <= 10_000_000 &&
        MathF.Abs(value.Z) <= 10_000_000;
    private static float L(ulong bits) => (float)BitConverter.Int64BitsToDouble((long)bits);
    private static int I32(ReadOnlySpan<byte> bytes, int offset) => BinaryPrimitives.ReadInt32LittleEndian(bytes[offset..]);
    private static float F(ReadOnlySpan<byte> bytes, int offset) => BinaryPrimitives.ReadSingleLittleEndian(bytes[offset..]);
    private static float H(ReadOnlySpan<byte> bytes, int offset) =>
        (float)BitConverter.UInt16BitsToHalf(BinaryPrimitives.ReadUInt16LittleEndian(bytes[offset..]));
}
