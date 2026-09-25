using System.IO.MemoryMappedFiles;

namespace CrimsonDesertTelemetry.Core;

/// <summary>Experimental sampled collision visibility, never an optical transmission measurement.
/// Only fresh camera-paired ManyLights are queried. Raw records/RGB remain untouched.</summary>
public sealed class PhysicsVisibilityClient : IDisposable
{
    public const string Method = "physics-ray-fan";
    public const int PacketBytes = 128, MaximumTargets = 24;
    public const long MaximumAgeMilliseconds = 2500;
    private readonly int _pid;
    private readonly ulong _born;
    private readonly Func<long> _now;
    private readonly MemoryMappedFile _query;
    private readonly MemoryMappedViewAccessor _write;
    private MemoryMappedFile? _result;
    private MemoryMappedViewAccessor? _read;
    private long _retry, _lastRequest;
    private ulong _sequence, _consumed;
    private byte[]? _pending;
    private bool _faulted;
    private readonly List<Entry> _cache = [];

    private sealed class Entry(CameraVector3 position)
    {
        public CameraVector3 Position = position, Receiver = position;
        public long Measured, LastRequested;
        public ulong Capture;
        public uint Frame, Clear;
        public int BlockedStreak;
        public string Status = "unknown", Reason = "waiting-for-physics";
    }

    public PhysicsVisibilityClient(int processId, long processStartFileTime, Func<long>? tickCount = null)
    {
        _pid = processId; _born = unchecked((ulong)processStartFileTime);
        _now = tickCount ?? (() => Environment.TickCount64);
        _query = MemoryMappedFile.CreateOrOpen($"Local\\CrimsonDesertTelemetry.PhysicsVisibilityQuery.{_pid}",
            PacketBytes, MemoryMappedFileAccess.ReadWrite);
        _write = _query.CreateViewAccessor(0, PacketBytes, MemoryMappedFileAccess.ReadWrite);
    }

    public EngineLightsSnapshot Apply((float X, float Y, float Z) player, EngineLightsSnapshot input)
    {
        var now = _now();
        ReadResponse(now);
        var rendered = input.Rendered;
        if (rendered?.Status != "available" || rendered.Camera is null || rendered.Sources is null ||
            rendered.CaptureSequence is not > 0 || rendered.AgeMilliseconds is not >= 0 or > 250)
        {
            _cache.Clear();
            return input with { Rendered = rendered is null ? null : rendered with
                { Sources = rendered.Sources?.Select(s => s with { SourceVisibility = null }).ToArray() } };
        }
        var camera = rendered.Camera.Position;
        _cache.RemoveAll(e => now - Math.Max(e.Measured, e.LastRequested) > 5000);
        var targets = new List<CameraVector3>();
        foreach (var source in rendered.Sources.OrderBy(s => DistanceSquared(s.Position, camera)))
        {
            if (DistanceSquared(source.Position, camera) > 35 * 35) continue;
            if (targets.Any(p => DistanceSquared(p, source.Position) <= .01f)) continue;
            if (targets.Count == MaximumTargets) break;
            targets.Add(source.Position);
        }
        foreach (var position in targets)
            if (Find(position) is null && _cache.Count < 64) _cache.Add(new Entry(position));

        if (!_faulted && now - _lastRequest >= 50 &&
            (_pending is null || _consumed == _sequence || now - _lastRequest > 400))
        {
            var next = targets.Select(p => (Position: p, Entry: Find(p)))
                .Where(v => v.Entry is not null).OrderBy(v => v.Entry!.LastRequested).FirstOrDefault();
            if (next.Entry is not null)
            {
                next.Entry.LastRequested = now;
                Publish(player, camera, next.Position, rendered, now);
            }
        }
        SourceVisibilitySnapshot Resolve(CameraVector3 position)
        {
            var e = Find(position);
            var reason = _faulted ? "physics-stopped-restart-required" : e is null ? "outside-physics-budget" :
                e.Measured == 0 ? "waiting-for-physics" : now - e.Measured > MaximumAgeMilliseconds ? "stale-physics" :
                DistanceSquared(camera, e.Receiver) > .0625f ? "camera-moved" : e.Reason;
            var status = reason.Length == 0 ? e!.Status : "unknown";
            // Preserve actual measurement origin/capture; never pretend an older ray
            // was executed on the current frame. HUD understands this method explicitly.
            return new SourceVisibilitySnapshot(status, status == "unknown" ? null : status == "blocked" ? 0 : 1,
                status == "unknown" ? reason : null, e?.Receiver ?? camera, e?.Capture > 0 ? e.Capture : rendered.CaptureSequence.Value,
                null, e?.Frame, e?.Measured > 0 ? now - e.Measured : null, null,
                Method, e?.Measured > 0 ? 9 : null, e?.Measured > 0 ? (int)e.Clear : null);
        }
        return input with { Rendered = rendered with
            { Sources = rendered.Sources.Select(s => s with { SourceVisibility = Resolve(s.Position) }).ToArray() } };
    }

    private Entry? Find(CameraVector3 p) => _cache.Where(e => DistanceSquared(e.Position, p) <= .0144f)
        .MinBy(e => DistanceSquared(e.Position, p));

    private void Publish((float X, float Y, float Z) player, CameraVector3 camera, CameraVector3 target,
        RenderLightsSnapshot rendered, long now)
    {
        var p = new byte[PacketBytes];
        Put(p, 0, 0x50564443u); Put(p, 4, 1u); Put(p, 8, 128u); Put(p, 24, (uint)_pid);
        Put(p, 32, _born); Put(p, 40, ++_sequence); Put(p, 48, (ulong)now);
        Put(p, 64, rendered.CaptureSequence!.Value); Put(p, 72, rendered.FrameNumber ?? 0);
        Put(p, 84, (float)rendered.AgeMilliseconds!.Value);
        Vector(p, 88, new(player.X, player.Y, player.Z)); Vector(p, 100, camera); Vector(p, 112, target);
        var seq = _write.ReadInt64(16); if ((seq & 1) != 0) seq++;
        _write.Write(16, seq + 1); Thread.MemoryBarrier();
        _write.WriteArray(0, p, 0, 16); _write.WriteArray(24, p, 24, 104);
        Thread.MemoryBarrier(); _write.Write(16, seq + 2);
        _pending = p; _lastRequest = now;
    }

    private void ReadResponse(long now)
    {
        if (_pending is null) return;
        try
        {
            if (_read is null)
            {
                if (now < _retry) return;
                _result = MemoryMappedFile.OpenExisting($"Local\\CrimsonDesertTelemetry.PhysicsVisibilityResult.{_pid}", MemoryMappedFileRights.Read);
                _read = _result.CreateViewAccessor(0, PacketBytes, MemoryMappedFileAccess.Read);
            }
            var p = new byte[PacketBytes];
            var seq = _read.ReadInt64(16); Thread.MemoryBarrier(); if ((seq & 1) != 0) return;
            _read.ReadArray(0, p, 0, PacketBytes); Thread.MemoryBarrier();
            if (_read.ReadInt64(16) != seq || BitConverter.ToInt64(p, 16) != seq) return;
            if (U32(p, 0) != 0x53564443 || U32(p, 4) != 1 || U32(p, 8) != 128 ||
                U32(p, 24) != _pid || U64(p, 32) != _born || U32(p, 12) != 1 ||
                U64(p, 40) != _sequence || U64(p, 40) == _consumed) return;
            // Response must belong to the exact request, including camera and source.
            if (!p.AsSpan(64, 12).SequenceEqual(_pending.AsSpan(64, 12)) ||
                !p.AsSpan(88, 36).SequenceEqual(_pending.AsSpan(88, 36)) || U64(p, 48) != U64(_pending, 48)) return;
            var tick = U64(p, 56);
            if (tick < U64(p, 48) || tick > (ulong)now || (ulong)now - tick > MaximumAgeMilliseconds) return;
            _consumed = _sequence;
            var e = Find(Vector(p, 112)); if (e is null) return;
            var code = U32(p, 28);
            if (code == 3) _faulted = true;
            if (code != 1 || U32(p, 76) != 9 || U32(p, 80) > 9)
            { e.Status = "unknown"; e.Reason = "physics-query-unavailable"; e.BlockedStreak = 0; return; }
            var receiver = Vector(p, 100);
            var consecutive = e.Measured > 0 && now - e.Measured <= MaximumAgeMilliseconds &&
                DistanceSquared(receiver, e.Receiver) <= .0625f;
            e.Clear = U32(p, 80);
            e.BlockedStreak = e.Clear > 0 ? 0 : consecutive ? e.BlockedStreak + 1 : 1;
            e.Status = e.Clear > 0 ? "clear" : e.BlockedStreak >= 2 ? "blocked" : "unknown";
            e.Reason = e.Status == "unknown" ? "confirming-obstruction" : "";
            e.Receiver = receiver; e.Position = Vector(p, 112); e.Measured = (long)tick;
            e.Capture = U64(p, 64); e.Frame = U32(p, 72);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or ArgumentException)
        {
            _read?.Dispose(); _result?.Dispose(); _read = null; _result = null; _retry = now + 250;
        }
    }
    private static uint U32(byte[] p, int i) => BitConverter.ToUInt32(p, i);
    private static ulong U64(byte[] p, int i) => BitConverter.ToUInt64(p, i);
    private static void Put(byte[] p, int i, uint v) => BitConverter.TryWriteBytes(p.AsSpan(i), v);
    private static void Put(byte[] p, int i, ulong v) => BitConverter.TryWriteBytes(p.AsSpan(i), v);
    private static void Put(byte[] p, int i, float v) => BitConverter.TryWriteBytes(p.AsSpan(i), v);
    private static void Vector(byte[] p, int i, CameraVector3 v) { Put(p, i, v.X); Put(p, i+4, v.Y); Put(p, i+8, v.Z); }
    private static CameraVector3 Vector(byte[] p, int i) => new(BitConverter.ToSingle(p,i), BitConverter.ToSingle(p,i+4), BitConverter.ToSingle(p,i+8));
    private static float DistanceSquared(CameraVector3 a, CameraVector3 b) =>
        (a.X-b.X)*(a.X-b.X)+(a.Y-b.Y)*(a.Y-b.Y)+(a.Z-b.Z)*(a.Z-b.Z);
    public void Dispose() { _read?.Dispose(); _result?.Dispose(); _write.Dispose(); _query.Dispose(); }
}
