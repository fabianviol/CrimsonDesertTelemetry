using System.Collections.Concurrent;
using System.Runtime.InteropServices;

namespace CrimsonDesertTelemetry.Core;

public readonly record struct CameraVector3(float X, float Y, float Z);

public sealed record RenderCameraConstantsCandidate(
    ulong Address,
    CameraVector3 Position,
    CameraVector3 Up,
    CameraVector3 Right,
    CameraVector3 Forward,
    float NearPlane,
    float FarPlane,
    float FieldOfViewRadians,
    float AspectRatio,
    double DistanceFromPlayer);

public sealed record RenderCameraConsensus(
    RenderCameraConstantsCandidate Camera, int CopyCount, int ValidCopyCount, int DistinctStateCount);

public static class RenderCameraConstantsScanner
{
    private const int ChunkBytes = 4 * 1024 * 1024;
    private const int SentinelToEndBytes = 0x4C;
    private const int PositionOffset = 0x08;
    private const uint FloatMaxBits = 0x7F7FFFFF;

    public static IReadOnlyList<RenderCameraConstantsCandidate> Find(
        IReadOnlyProcessMemory reader, (float X, float Y, float Z) playerPosition)
    {
        var regions = reader.GetWritableRegions();
        var preferred = regions.Where(static region => region.Size <= 64UL * 1024 * 1024).ToList();
        var results = ScanRegions(reader, preferred, playerPosition);
        if (results.Count != 0) return results;

        return ScanRegions(reader, regions.Where(static region => region.Size > 64UL * 1024 * 1024),
            playerPosition);
    }

    public static IReadOnlyList<RenderCameraConstantsCandidate> Refresh(
        IReadOnlyProcessMemory reader, IEnumerable<ulong> addresses, (float X, float Y, float Z) playerPosition)
    {
        var results = new List<RenderCameraConstantsCandidate>();
        foreach (var address in addresses.Distinct())
        {
            if (address < PositionOffset) continue;
            try
            {
                var bufferBase = address - PositionOffset;
                var bytes = reader.Read(new IntPtr(checked((long)bufferBase)), SentinelToEndBytes);
                results.AddRange(FindInBuffer(bytes, bufferBase, playerPosition)
                    .Where(candidate => candidate.Address == address));
            }
            catch (Exception exception) when (exception is InvalidDataException or
                                               System.ComponentModel.Win32Exception or OverflowException)
            {
                // Renderer copies are intentionally short-lived; invalidated addresses are ignored.
            }
        }
        return results;
    }

    public static RenderCameraConsensus? SelectConsensus(
        IReadOnlyList<RenderCameraConstantsCandidate> candidates)
    {
        if (candidates.Count == 0) return null;
        var groups = candidates.GroupBy(StateKey.From)
            .OrderByDescending(static group => group.Count())
            .ThenBy(static group => group.Min(candidate => candidate.DistanceFromPlayer))
            .ToList();
        var winningGroup = groups[0].ToList();
        var representative = winningGroup
            .OrderBy(candidate => candidate.DistanceFromPlayer)
            .ThenBy(candidate => candidate.Address)
            .First();
        return new RenderCameraConsensus(representative, winningGroup.Count, candidates.Count, groups.Count);
    }

    internal static RenderCameraConsensus DescribeSelection(
        IReadOnlyList<RenderCameraConstantsCandidate> candidates, RenderCameraConstantsCandidate selected)
    {
        var selectedKey = StateKey.From(selected);
        return new RenderCameraConsensus(selected,
            candidates.Count(candidate => StateKey.From(candidate) == selectedKey), candidates.Count,
            candidates.Select(StateKey.From).Distinct().Count());
    }

    public static IReadOnlyList<RenderCameraConstantsCandidate> FindInBuffer(
        byte[] bytes, ulong bufferBase, (float X, float Y, float Z) playerPosition)
    {
        var results = new List<RenderCameraConstantsCandidate>();
        var firstAlignedOffset = checked((int)((4 - (bufferBase & 3)) & 3));
        var alignedLength = (bytes.Length - firstAlignedOffset) & ~3;
        if (alignedLength < SentinelToEndBytes) return results;
        var words = MemoryMarshal.Cast<byte, uint>(bytes.AsSpan(firstAlignedOffset, alignedLength));
        var wordIndex = 0;
        while (wordIndex < words.Length)
        {
            var relativeIndex = words[wordIndex..].IndexOf(FloatMaxBits);
            if (relativeIndex < 0) break;
            wordIndex += relativeIndex;
            var offset = firstAlignedOffset + wordIndex * sizeof(uint);
            wordIndex++;
            if (offset > bytes.Length - SentinelToEndBytes ||
                BitConverter.ToUInt32(bytes, offset + 4) != FloatMaxBits) continue;

            var recordOffset = offset + PositionOffset;
            var position = ReadVector(bytes, recordOffset);
            var up = ReadVector(bytes, recordOffset + 0x0C);
            var right = ReadVector(bytes, recordOffset + 0x18);
            var forward = ReadVector(bytes, recordOffset + 0x24);
            var nearPlane = BitConverter.ToSingle(bytes, recordOffset + 0x30);
            var farPlane = BitConverter.ToSingle(bytes, recordOffset + 0x34);
            var fieldOfView = BitConverter.ToSingle(bytes, recordOffset + 0x38);
            var aspectRatio = BitConverter.ToSingle(bytes, recordOffset + 0x3C);
            var trailingSentinel = BitConverter.ToInt32(bytes, recordOffset + 0x40);

            if (!IsNearPlayer(position, playerPosition, out var distance) ||
                !IsUnit(up) || !IsUnit(right) || !IsUnit(forward) ||
                Math.Abs(Dot(up, right)) > 0.01 || Math.Abs(Dot(up, forward)) > 0.01 ||
                Math.Abs(Dot(right, forward)) > 0.01 || Math.Abs(Determinant(right, up, forward)) < 0.99 ||
                !float.IsFinite(nearPlane) || nearPlane is < 0.001f or > 10f ||
                !(farPlane == float.MaxValue || float.IsFinite(farPlane) && farPlane > nearPlane) ||
                !float.IsFinite(fieldOfView) || fieldOfView is < 0.2f or > 3f ||
                !float.IsFinite(aspectRatio) || aspectRatio is < 0.5f or > 5f ||
                trailingSentinel != unchecked((int)FloatMaxBits)) continue;

            results.Add(new RenderCameraConstantsCandidate(
                bufferBase + (ulong)recordOffset, position, up, right, forward, nearPlane, farPlane,
                fieldOfView, aspectRatio, distance));
        }
        return results;
    }

    private static List<RenderCameraConstantsCandidate> ScanRegions(
        IReadOnlyProcessMemory reader, IEnumerable<MemoryRegion> regions, (float X, float Y, float Z) playerPosition)
    {
        var results = new ConcurrentBag<RenderCameraConstantsCandidate>();
        var options = new ParallelOptions
        {
            MaxDegreeOfParallelism = Math.Clamp(Environment.ProcessorCount, 2, 8)
        };
        Parallel.ForEach(regions, options, region =>
        {
            var overlap = (ulong)(SentinelToEndBytes - 4);
            var advance = (ulong)ChunkBytes - overlap;
            for (ulong regionOffset = 0; regionOffset < region.Size; regionOffset += advance)
            {
                var byteCount = checked((int)Math.Min((ulong)ChunkBytes, region.Size - regionOffset));
                byte[] bytes;
                try
                {
                    bytes = reader.Read(new IntPtr(checked((long)(region.BaseAddress + regionOffset))), byteCount);
                }
                catch (Exception exception) when (exception is InvalidDataException or
                                                   System.ComponentModel.Win32Exception or OverflowException)
                {
                    continue;
                }
                foreach (var candidate in FindInBuffer(bytes, region.BaseAddress + regionOffset, playerPosition))
                    results.Add(candidate);
            }
        });
        return results.DistinctBy(static result => result.Address)
            .OrderBy(static result => result.Address)
            .ToList();
    }

    private static CameraVector3 ReadVector(byte[] bytes, int offset) =>
        new(BitConverter.ToSingle(bytes, offset), BitConverter.ToSingle(bytes, offset + 4),
            BitConverter.ToSingle(bytes, offset + 8));

    private static bool IsNearPlayer(CameraVector3 camera, (float X, float Y, float Z) player, out double distance)
    {
        distance = double.PositiveInfinity;
        if (!float.IsFinite(camera.X) || !float.IsFinite(camera.Y) || !float.IsFinite(camera.Z)) return false;
        var dx = (double)camera.X - player.X;
        var dy = (double)camera.Y - player.Y;
        var dz = (double)camera.Z - player.Z;
        distance = Math.Sqrt(dx * dx + dy * dy + dz * dz);
        return distance <= 50;
    }

    private static bool IsUnit(CameraVector3 value)
    {
        if (!float.IsFinite(value.X) || !float.IsFinite(value.Y) || !float.IsFinite(value.Z)) return false;
        var lengthSquared = value.X * value.X + value.Y * value.Y + value.Z * value.Z;
        return lengthSquared is > 0.995f and < 1.005f;
    }

    private static double Dot(CameraVector3 a, CameraVector3 b) =>
        a.X * b.X + a.Y * b.Y + a.Z * b.Z;

    private static double Determinant(CameraVector3 right, CameraVector3 up, CameraVector3 forward) =>
        right.X * (up.Y * forward.Z - up.Z * forward.Y) -
        right.Y * (up.X * forward.Z - up.Z * forward.X) +
        right.Z * (up.X * forward.Y - up.Y * forward.X);

    private readonly record struct StateKey(
        int Px, int Py, int Pz,
        int Ux, int Uy, int Uz,
        int Rx, int Ry, int Rz,
        int Fx, int Fy, int Fz,
        int Near, int Far, int Fov, int Aspect)
    {
        public static StateKey From(RenderCameraConstantsCandidate value) => new(
            Quantize(value.Position.X, 100), Quantize(value.Position.Y, 100), Quantize(value.Position.Z, 100),
            Quantize(value.Up.X, 1000), Quantize(value.Up.Y, 1000), Quantize(value.Up.Z, 1000),
            Quantize(value.Right.X, 1000), Quantize(value.Right.Y, 1000), Quantize(value.Right.Z, 1000),
            Quantize(value.Forward.X, 1000), Quantize(value.Forward.Y, 1000), Quantize(value.Forward.Z, 1000),
            Quantize(value.NearPlane, 1000), BitConverter.SingleToInt32Bits(value.FarPlane),
            Quantize(value.FieldOfViewRadians, 1000), Quantize(value.AspectRatio, 1000));

        private static int Quantize(float value, float scale) => checked((int)MathF.Round(value * scale));
    }
}
