using System.IO.MemoryMappedFiles;
using System.Numerics;

namespace CrimsonDesertTelemetry.Core;

/// <summary>Consumes fenced, camera-paired samples from the unified in-process ASI.</summary>
public sealed class RenderLightReader(int processId, long processStartFileTime) : IDisposable
{
    public const string SourceName = "filtered-manylights";
    public const int HeaderBytes = 256;
    public const int SceneBytes = 2816;
    public const int RawCount = 32768;
    public const int Stride = 48;
    public const int TotalBytes = HeaderBytes + SceneBytes + RawCount * Stride;
    public const long MaximumAgeMilliseconds = 500;
    private MemoryMappedFile? _mapping;
    private MemoryMappedViewAccessor? _view;
    private byte[]? _lastBytes;
    private ulong _lastLock;
    private long _retryAfter;

    public RenderLightsSnapshot Capture((float X, float Y, float Z) player, float radius)
    {
        try
        {
            if (_view is null)
            {
                if (Environment.TickCount64 < _retryAfter) return Unavailable("bridge-missing");
                _mapping = MemoryMappedFile.OpenExisting($"Local\\CrimsonDesertTelemetry.Render.{processId}",
                    MemoryMappedFileRights.Read);
                _view = _mapping.CreateViewAccessor(0, TotalBytes, MemoryMappedFileAccess.Read);
            }
            for (var attempt = 0; attempt < 3; attempt++)
            {
                var before = _view.ReadUInt64(16);
                if ((before & 1) != 0) continue;
                Thread.MemoryBarrier();
                if (_lastBytes is not null && before == _lastLock)
                    return Decode(_lastBytes, processId, processStartFileTime, Environment.TickCount64, player, radius);
                var bytes = new byte[TotalBytes];
                if (_view.ReadArray(0, bytes, 0, bytes.Length) != bytes.Length)
                    throw new InvalidDataException("Truncated native render bridge.");
                Thread.MemoryBarrier();
                var after = _view.ReadUInt64(16);
                if (before != after || (after & 1) != 0 || BitConverter.ToUInt64(bytes, 16) != after)
                    continue;
                var decoded = Decode(bytes, processId, processStartFileTime, Environment.TickCount64, player, radius);
                _lastBytes = bytes;
                _lastLock = after;
                return decoded;
            }
            return Unavailable("bridge-changing");
        }
        catch (FileNotFoundException)
        {
            Reset();
            _retryAfter = Environment.TickCount64 + 500;
            return Unavailable("bridge-missing");
        }
        catch (Exception exception) when (exception is InvalidDataException or IOException or UnauthorizedAccessException or
                                           ArgumentException or OverflowException)
        {
            Reset();
            _retryAfter = Environment.TickCount64 + 500;
            return Unavailable("bridge-invalid");
        }
    }

    public static RenderLightsSnapshot Unavailable(string reason) => new(
        "unavailable", SourceName, null, null, null, null, null, null, new(0, 0, 0, 0), reason);

    public static RenderLightsSnapshot Decode(byte[] snapshot, int expectedPid, long expectedStartFileTime,
        long nowTickMs, (float X, float Y, float Z) player, float nearbyRadius)
    {
        if (snapshot.Length != TotalBytes || BitConverter.ToUInt32(snapshot, 0) != 0x52445443 ||
            BitConverter.ToUInt32(snapshot, 4) != 1 || BitConverter.ToUInt32(snapshot, 8) != HeaderBytes ||
            BitConverter.ToUInt32(snapshot, 12) != TotalBytes || (BitConverter.ToUInt64(snapshot, 16) & 1) != 0 ||
            BitConverter.ToUInt32(snapshot, 24) != expectedPid ||
            BitConverter.ToInt64(snapshot, 32) != expectedStartFileTime ||
            BitConverter.ToUInt32(snapshot, 68) != SceneBytes ||
            BitConverter.ToUInt32(snapshot, 72) != RawCount || BitConverter.ToUInt32(snapshot, 76) != Stride)
            throw new InvalidDataException("Native render bridge version, bounds or process identity disagree.");
        if (!float.IsFinite(player.X) || !float.IsFinite(player.Y) || !float.IsFinite(player.Z) ||
            !float.IsFinite(nearbyRadius) || nearbyRadius is <= 0 or > 100000)
            throw new InvalidDataException("Invalid rendered-light radius or player position.");

        var state = BitConverter.ToUInt32(snapshot, 28);
        if (state > 5) throw new InvalidDataException("Unknown native render bridge state.");
        if (state != 1)
            return Unavailable(state switch
            {
                0 => "bridge-waiting", 2 => "unsupported-build", 3 => "native-fault",
                4 => "legacy-plugin-conflict", 5 => "game-stopped", _ => "bridge-invalid"
            });
        if (BitConverter.ToUInt32(snapshot, 84) != 7)
            throw new InvalidDataException("Render sample lacks build, fence or scene validation.");
        var capturedTick = BitConverter.ToInt64(snapshot, 48);
        var publishedTick = BitConverter.ToInt64(snapshot, 56);
        var sequence = BitConverter.ToUInt64(snapshot, 40);
        if (sequence == 0 || capturedTick < 0 || publishedTick < capturedTick || nowTickMs < publishedTick)
            throw new InvalidDataException("Invalid native render timing or sequence.");
        var age = nowTickMs - capturedTick;
        if (age > MaximumAgeMilliseconds) return Unavailable("bridge-stale");

        var scene = SceneConstantsDecoder.Decode(snapshot.AsSpan(HeaderBytes, SceneBytes).ToArray());
        if (scene.FrameNumber != BitConverter.ToUInt32(snapshot, 64))
            throw new InvalidDataException("Render sample and paired camera frame disagree.");
        var camera = scene.Camera;
        var sources = new List<RenderedLightSnapshot>();
        var active = 0;
        var malformed = 0;
        var outside = 0;
        var radiusSquared = (double)nearbyRadius * nearbyRadius;
        for (var index = 0; index < RawCount; index++)
        {
            var record = snapshot.AsSpan(HeaderBytes + SceneBytes + index * Stride, Stride);
            var marker = F(record, 12);
            if (marker == 0) continue;
            if (!float.IsFinite(marker) || Math.Abs(marker - MathF.PI) > .0001f)
            {
                malformed++;
                continue;
            }
            active++;
            var relative = new CameraVector3(F(record, 0), F(record, 4), F(record, 8));
            var rgb = new CameraVector3(F(record, 16), F(record, 20), F(record, 24));
            var world = new CameraVector3(camera.Position.X + relative.X,
                camera.Position.Y + relative.Y, camera.Position.Z + relative.Z);
            var luminance = rgb.X * .212671f + rgb.Y * .71516f + rgb.Z * .07216f;
            if (!Plausible(relative) || !Plausible(world) || !Plausible(rgb) ||
                rgb.X < 0 || rgb.Y < 0 || rgb.Z < 0 || !float.IsFinite(luminance))
            {
                malformed++;
                continue;
            }
            var dx = (double)world.X - player.X;
            var dy = (double)world.Y - player.Y;
            var dz = (double)world.Z - player.Z;
            if (dx * dx + dy * dy + dz * dz > radiusSquared)
            {
                outside++;
                continue;
            }
            var cone = H(record, 38);
            string? kind = null;
            float? halfAngle = null;
            CameraVector3? direction = null;
            if (cone == -1) kind = "point";
            else if (float.IsFinite(cone) && cone > 0 && cone <= MathF.PI / 2)
            {
                kind = "spot";
                halfAngle = cone * 180 / MathF.PI;
                var look = new Vector3(H(record, 40), H(record, 42), H(record, 44));
                if (float.IsFinite(look.X) && float.IsFinite(look.Y) && float.IsFinite(look.Z) &&
                    Math.Abs(look.LengthSquared() - 1) <= .005f)
                {
                    look = Vector3.Normalize(look);
                    direction = new CameraVector3(look.X, look.Y, look.Z);
                }
            }
            sources.Add(new RenderedLightSnapshot(index, world, rgb, luminance, kind, direction, halfAngle));
        }
        return new RenderLightsSnapshot("available", SourceName, sequence, scene.FrameNumber,
            DateTimeOffset.UtcNow.AddMilliseconds(-age), age,
            new CameraSnapshot(camera.Position, camera.Up, camera.Right, camera.Forward,
                camera.NearPlane, camera.FarPlane == float.MaxValue ? null : camera.FarPlane,
                camera.FieldOfViewRadians * 180 / MathF.PI, camera.AspectRatio),
            sources, new(active, sources.Count, malformed, outside));
    }

    private static bool Plausible(CameraVector3 value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z) &&
        Math.Abs(value.X) <= 10_000_000 && Math.Abs(value.Y) <= 10_000_000 && Math.Abs(value.Z) <= 10_000_000;
    private static float F(ReadOnlySpan<byte> bytes, int offset) => BitConverter.ToSingle(bytes.Slice(offset, 4));
    private static float H(ReadOnlySpan<byte> bytes, int offset) =>
        (float)BitConverter.UInt16BitsToHalf(BitConverter.ToUInt16(bytes.Slice(offset, 2)));
    private void Reset()
    {
        _lastBytes = null;
        _view?.Dispose();
        _view = null;
        _mapping?.Dispose();
        _mapping = null;
    }
    public void Dispose() => Reset();
}
