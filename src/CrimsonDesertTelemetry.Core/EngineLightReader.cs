using System.ComponentModel;

namespace CrimsonDesertTelemetry.Core;

/// <summary>
/// Read-only reader for the build-bound CPU light-source array. It performs no
/// discovery scan and never reuses scene pointers between snapshots.
/// </summary>
public sealed class EngineLightReader
{
    public const string SourceName = "engine-light-source-array";
    public const int RecordSize = 0xB8;
    public const int MaximumRecords = 8192;
    private const float TechnicalMagnitudeLimit = 10_000_000f;
    private readonly IReadOnlyProcessMemory _reader;
    private readonly string _layout;
    private readonly ulong _rootGlobal;
    private readonly ulong _sceneGlobal;
    private readonly ulong _sceneVtable;
    private long _walkChanged;
    private long _walkRetrySucceeded;
    private long _walkUnavailable;

    public EngineLightReader(IReadOnlyProcessMemory reader, ulong moduleBase, EngineLightsDefinition definition)
    {
        if (definition.Layout is not ("light-source-array-v1" or "light-source-array-scene-global-v1"))
            throw new InvalidDataException("Unsupported engine-light layout.");
        _reader = reader;
        _layout = definition.Layout;
        if (_layout == "light-source-array-v1")
            _rootGlobal = At(moduleBase, definition.RootGlobalRva);
        else
        {
            _sceneGlobal = At(moduleBase, definition.SceneGlobalRva);
            _sceneVtable = At(moduleBase, definition.SceneVtableRva);
        }
    }

    public EngineLightsSnapshot Capture((float X, float Y, float Z) playerPosition, float nearbyRadius)
    {
        ValidatePlayerAndRadius(playerPosition, nearbyRadius);
        for (var attempt = 0; attempt < 2; attempt++)
        {
            try
            {
                var before = ReadWalk();
                var bytes = before.Count == 0
                    ? Array.Empty<byte>()
                    : _reader.Read(Pointer(before.Base), checked((int)before.Count * RecordSize));
                var after = ReadWalk();
                if (before != after)
                {
                    _walkChanged++;
                    throw new InvalidDataException("Engine-light walk changed during capture.");
                }

                var decoded = Decode(bytes, checked((int)before.Count), playerPosition, nearbyRadius);
                if (attempt != 0) _walkRetrySucceeded++;
                return new EngineLightsSnapshot("available", SourceName, nearbyRadius, decoded.Sources,
                    Diagnostics(decoded));
            }
            catch (Exception exception) when (exception is InvalidDataException or Win32Exception or
                                               OverflowException or ArgumentException)
            {
                if (attempt == 0) continue;
            }
        }

        _walkUnavailable++;
        return Unavailable(nearbyRadius, "walk-unavailable");
    }

    public EngineLightsSnapshot Unavailable(float nearbyRadius, string reason) => new(
        "unavailable", SourceName, nearbyRadius, null,
        new EngineLightDiagnosticsSnapshot(0, 0, 0, 0, 0, 0, 0,
            _walkChanged, _walkRetrySucceeded, _walkUnavailable), reason);

    public static EngineLightDecodeResult Decode(byte[] bytes, int count,
        (float X, float Y, float Z) playerPosition, float nearbyRadius)
    {
        ValidatePlayerAndRadius(playerPosition, nearbyRadius);
        if (count is < 0 or > MaximumRecords || bytes.Length != checked(count * RecordSize))
            throw new InvalidDataException("Invalid engine-light record buffer.");

        var sources = new List<EngineLightSnapshot>();
        var malformed = 0;
        var outsideRadius = 0;
        var unsupportedKind = 0;
        var rendererDataUnavailable = 0;
        var nonPositiveRendererScale = 0;
        var radiusSquared = (double)nearbyRadius * nearbyRadius;

        for (var index = 0; index < count; index++)
        {
            var record = bytes.AsSpan(index * RecordSize, RecordSize);
            var position = new CameraVector3(Float(record, 0x30), Float(record, 0x34), Float(record, 0x38));
            var color = new CameraVector3(Float(record, 0x3C), Float(record, 0x40), Float(record, 0x44));
            var active = record[0x63];
            var selected = record[0x64];
            if (!Plausible(position) || !Plausible(color) || active > 1 || selected > 1)
            {
                malformed++;
                continue;
            }

            var cone = Float(record, 0x54);
            string? kind;
            if (BitConverter.SingleToInt32Bits(cone) == BitConverter.SingleToInt32Bits(-1f))
                kind = "point";
            else if (float.IsFinite(cone) && cone > 0 && cone <= MathF.PI / 2)
                kind = "spot";
            else
            {
                kind = null;
                unsupportedKind++;
            }

            float? rendererScale = null;
            CameraVector3? rendererRgb = null;
            if (selected == 0)
            {
                rendererDataUnavailable++;
            }
            else
            {
                var scale = Float(record, 0x4C);
                if (!float.IsFinite(scale) || Math.Abs(scale) > TechnicalMagnitudeLimit)
                {
                    rendererDataUnavailable++;
                }
                else if (scale <= 0)
                {
                    nonPositiveRendererScale++;
                }
                else
                {
                    var final = new CameraVector3(color.X * scale, color.Y * scale, color.Z * scale);
                    if (Plausible(final))
                    {
                        rendererScale = scale;
                        rendererRgb = final;
                    }
                    else rendererDataUnavailable++;
                }
            }

            var dx = (double)position.X - playerPosition.X;
            var dy = (double)position.Y - playerPosition.Y;
            var dz = (double)position.Z - playerPosition.Z;
            if (dx * dx + dy * dy + dz * dz > radiusSquared)
            {
                outsideRadius++;
                continue;
            }

            sources.Add(new EngineLightSnapshot(position, kind, color, active != 0, selected != 0,
                rendererScale, rendererRgb));
        }

        return new EngineLightDecodeResult(sources, count, malformed, outsideRadius, unsupportedKind,
            rendererDataUnavailable, nonPositiveRendererScale);
    }

    private Walk ReadWalk()
    {
        if (_layout == "light-source-array-scene-global-v1")
        {
            var directScene = ReadPointer(_sceneGlobal);
            if (ReadPointer(directScene) != _sceneVtable)
                throw new InvalidDataException("Engine-light scene type mismatch.");
            return ReadArrayWalk(0, 0, directScene);
        }

        var root = ReadPointer(_rootGlobal);
        var container = ReadPointer(At(root, 0x658));
        if (ReadPointer(At(container, 0x10)) != root)
            throw new InvalidDataException("Engine-light container backlink mismatch.");
        var scene = ReadPointer(At(container, 0x08));
        return ReadArrayWalk(root, container, scene);
    }

    private Walk ReadArrayWalk(ulong root, ulong container, ulong scene)
    {
        var array = ReadPointer(At(scene, 0xF08));
        var descriptor = _reader.Read(Pointer(At(array, 0x10)), 0x10);
        var recordBase = BitConverter.ToUInt64(descriptor, 0);
        var count = BitConverter.ToUInt32(descriptor, 8);
        var capacity = BitConverter.ToUInt32(descriptor, 12);
        if (count > capacity || capacity > MaximumRecords)
            throw new InvalidDataException("Implausible engine-light vector size.");
        if (capacity != 0) _ = Pointer(recordBase);
        if (count != 0 && recordBase == 0)
            throw new InvalidDataException("Engine-light vector has records but no storage.");
        return new Walk(root, container, scene, array, recordBase, count, capacity);
    }

    private ulong ReadPointer(ulong address)
    {
        var value = BitConverter.ToUInt64(_reader.Read(Pointer(address), sizeof(ulong)));
        return checked((ulong)Pointer(value).ToInt64());
    }

    private EngineLightDiagnosticsSnapshot Diagnostics(EngineLightDecodeResult decoded) => new(
        decoded.SourceRecords, decoded.Sources.Count, decoded.Malformed, decoded.OutsideRadius,
        decoded.UnsupportedKind, decoded.RendererDataUnavailable, decoded.NonPositiveRendererScale,
        _walkChanged, _walkRetrySucceeded, _walkUnavailable);

    private static void ValidatePlayerAndRadius((float X, float Y, float Z) player, float radius)
    {
        if (!float.IsFinite(player.X) || !float.IsFinite(player.Y) || !float.IsFinite(player.Z) ||
            !float.IsFinite(radius) || radius <= 0 || radius > 100_000)
            throw new InvalidDataException("Invalid player position or engine-light radius.");
    }

    private static bool Plausible(CameraVector3 value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z) &&
        Math.Abs(value.X) <= TechnicalMagnitudeLimit && Math.Abs(value.Y) <= TechnicalMagnitudeLimit &&
        Math.Abs(value.Z) <= TechnicalMagnitudeLimit;

    private static float Float(ReadOnlySpan<byte> bytes, int offset) =>
        BitConverter.ToSingle(bytes.Slice(offset, sizeof(float)));

    private static ulong At(ulong address, ulong offset)
    {
        _ = Pointer(address);
        var result = checked(address + offset);
        _ = Pointer(result);
        return result;
    }

    private static IntPtr Pointer(ulong address) => address is >= 0x10000 and <= 0x00007FFFFFFFFFFF
        ? new IntPtr(checked((long)address))
        : throw new InvalidDataException("Implausible engine-light pointer.");

    private readonly record struct Walk(ulong Root, ulong Container, ulong Scene, ulong Array,
        ulong Base, uint Count, uint Capacity);
}

public sealed record EngineLightDecodeResult(
    IReadOnlyList<EngineLightSnapshot> Sources,
    int SourceRecords,
    int Malformed,
    int OutsideRadius,
    int UnsupportedKind,
    int RendererDataUnavailable,
    int NonPositiveRendererScale);
