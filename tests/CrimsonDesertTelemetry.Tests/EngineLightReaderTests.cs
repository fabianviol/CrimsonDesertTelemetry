using System.Text.Json;
using CrimsonDesertTelemetry.Core;

internal static class EngineLightReaderTests
{
    public static void DecodeFields()
    {
        var bytes = Records(
            Record((1, 2, 3), (.5f, .25f, .125f), .47119140625f, 2, active: 1, selected: 1),
            Record((2, 2, 3), (.2f, .3f, .4f), -1, 0, active: 0, selected: 0));
        var result = EngineLightReader.Decode(bytes, 2, (0, 0, 0), 10);
        Check(result.Sources.Count == 2 && result.Sources[0].Kind == "spot" &&
              result.Sources[1].Kind == "point", "Point/spot decoding failed.");
        Check(result.Sources[0].RendererScale == 2 &&
              result.Sources[0].RendererRgbLinear == new CameraVector3(1, .5f, .25f),
            "Renderer RGB was not derived from the proven fields.");
        Check(result.Sources[1].RendererScale is null && result.Sources[1].RendererRgbLinear is null &&
              !result.Sources[1].RecordActive && !result.Sources[1].RendererSelected,
            "Unselected renderer fields were invented.");
        Check(result.RendererDataUnavailable == 1, "Unselected renderer diagnostics mismatch.");
    }

    public static void Validation()
    {
        var bytes = Records(
            Record((1, 0, 0), (1, 1, 1), float.NaN, 1, 1, 1),
            Record((2, 0, 0), (1, 1, 1), -1, 0, 1, 1),
            Record((100, 0, 0), (1, 1, 1), -1, 1, 1, 1),
            Record((float.NaN, 0, 0), (1, 1, 1), -1, 1, 1, 1),
            Record((3, 0, 0), (1, 1, 1), -1, float.NaN, 1, 1),
            Record((4, 0, 0), (1, 1, 1), -1, -2, 1, 1));
        var result = EngineLightReader.Decode(bytes, 6, (0, 0, 0), 10);
        Check(result.Sources.Count == 4 && result.Malformed == 1 && result.OutsideRadius == 1,
            "Malformed and outside-radius records were conflated.");
        Check(result.UnsupportedKind == 1 && result.Sources[0].Kind is null,
            "Unknown cone encoding was assigned a kind.");
        Check(result.RendererDataUnavailable == 1 && result.NonPositiveRendererScale == 2,
            "Renderer field diagnostics mismatch.");
        Check(result.Sources.Skip(1).All(source => source.RendererScale is null),
            "Invalid renderer scales were published.");
    }

    public static void WalkRetry()
    {
        var fixture = new Fixture();
        var alternate = Fixture.RecordBase + 0x10000;
        fixture.Memory.Blocks[alternate] = fixture.Memory.Blocks[Fixture.RecordBase].ToArray();
        var descriptorReads = 0;
        fixture.Memory.BeforeRead = (address, _) =>
        {
            if (address == Fixture.Array + 0x10 && ++descriptorReads == 2)
                fixture.SetDescriptor(alternate, 1, 1);
        };
        var snapshot = fixture.Reader.Capture((0, 0, 0), 10);
        Check(snapshot.Status == "available" && snapshot.Sources?.Count == 1 &&
              snapshot.Diagnostics.WalkChanged == 1 && snapshot.Diagnostics.WalkRetrySucceeded == 1,
            "A changed walk was not retried from the root.");
    }

    public static void WalkUnavailable()
    {
        var fixture = new Fixture();
        var alternate = Fixture.RecordBase + 0x10000;
        fixture.Memory.Blocks[alternate] = fixture.Memory.Blocks[Fixture.RecordBase].ToArray();
        var descriptorReads = 0;
        fixture.Memory.BeforeRead = (address, _) =>
        {
            if (address != Fixture.Array + 0x10) return;
            descriptorReads++;
            if (descriptorReads % 2 == 0)
                fixture.SetDescriptor(descriptorReads == 2 ? alternate : Fixture.RecordBase, 1, 1);
        };
        var snapshot = fixture.Reader.Capture((0, 0, 0), 10);
        Check(snapshot.Status == "unavailable" && snapshot.Sources is null &&
              snapshot.UnavailableReason == "walk-unavailable" && snapshot.Diagnostics.WalkUnavailable == 1,
            "Repeated walk races did not fail closed.");

        fixture.Memory.BeforeRead = null;
        fixture.SetDescriptor(Fixture.RecordBase, 2, 1);
        snapshot = fixture.Reader.Capture((0, 0, 0), 10);
        Check(snapshot.Status == "unavailable", "Count greater than capacity was accepted.");
    }

    public static void DirectSceneWalk()
    {
        const ulong moduleBase = 0x140000000;
        const ulong scene = 0x160000;
        const ulong array = 0x170000;
        const ulong recordBase = 0x180000;
        var definition = BuildDefinition.LoadAll(Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")))
            .Single(value => value.SteamBuildId == "25116796").EngineLights
            ?? throw new InvalidOperationException("Direct engine-light definition is not embedded.");
        var memory = new Memory();
        memory.Q(moduleBase + definition.SceneGlobalRva, scene);
        memory.Q(scene, moduleBase + definition.SceneVtableRva);
        memory.Q(scene + 0xF08, array);
        memory.Blocks[recordBase] = Record((1, 2, 3), (1, .5f, .25f), -1, 2, 1, 1);
        var descriptor = new byte[16];
        BitConverter.GetBytes(recordBase).CopyTo(descriptor, 0);
        BitConverter.GetBytes(1u).CopyTo(descriptor, 8);
        BitConverter.GetBytes(1u).CopyTo(descriptor, 12);
        memory.Blocks[array + 0x10] = descriptor;
        var reader = new EngineLightReader(memory, moduleBase, definition);
        Check(reader.Capture((0, 0, 0), 10).Sources?.Count == 1,
            "Direct scene-global light walk failed.");
        memory.Q(scene, moduleBase + definition.SceneVtableRva + 8);
        Check(reader.Capture((0, 0, 0), 10).Status == "unavailable",
            "Direct scene-global light walk accepted the wrong scene type.");
    }

    public static void JsonContract()
    {
        var source = EngineLightReader.Decode(
            Record((1, 2, 3), (1, .5f, .25f), -1, 0, 1, 0), 1, (0, 0, 0), 10).Sources[0];
        var lights = new EngineLightsSnapshot("available", EngineLightReader.SourceName, 10, [source],
            new EngineLightDiagnosticsSnapshot(1, 1, 0, 0, 0, 1, 0, 0, 0, 0));
        var snapshot = new TelemetrySnapshot("1.2", 1, DateTimeOffset.UnixEpoch,
            new GameSnapshot("25050808", "playing"), new CoordinateSystemSnapshot("game-unit", "right", "y"),
            ["player.position", "camera.transform", "camera.projection", "lights.engine"], null, null, null, lights);
        using var document = JsonDocument.Parse(JsonSerializer.Serialize(snapshot,
            new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase }));
        var light = document.RootElement.GetProperty("lights").GetProperty("sources")[0];
        Check(!light.TryGetProperty("rendererScale", out _) && !light.TryGetProperty("rendererRgbLinear", out _),
            "Unavailable renderer fields serialized as plausible values or nulls.");
        Check(document.RootElement.GetProperty("schemaVersion").GetString() == "1.2",
            "Light snapshot did not use the additive schema revision.");
    }

    private sealed class Fixture
    {
        public const ulong ModuleBase = 0x140000000;
        public const ulong Root = 0x100000;
        public const ulong Container = 0x110000;
        public const ulong Scene = 0x120000;
        public const ulong Array = 0x130000;
        public const ulong RecordBase = 0x140000;
        public Memory Memory { get; } = new();
        public EngineLightReader Reader { get; }

        public Fixture()
        {
            var definition = BuildDefinition.LoadAll(Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")))
                .Single(value => value.SteamBuildId == "25050808").EngineLights
                ?? throw new InvalidOperationException("Engine-light definition is not embedded.");
            Memory.Q(ModuleBase + definition.RootGlobalRva, Root);
            Memory.Q(Root + 0x658, Container);
            Memory.Q(Container + 0x10, Root);
            Memory.Q(Container + 0x08, Scene);
            Memory.Q(Scene + 0xF08, Array);
            Memory.Blocks[RecordBase] = Record((1, 2, 3), (1, .5f, .25f), -1, 2, 1, 1);
            SetDescriptor(RecordBase, 1, 1);
            Reader = new EngineLightReader(Memory, ModuleBase, definition);
        }

        public void SetDescriptor(ulong recordBase, uint count, uint capacity)
        {
            var bytes = new byte[16];
            BitConverter.GetBytes(recordBase).CopyTo(bytes, 0);
            BitConverter.GetBytes(count).CopyTo(bytes, 8);
            BitConverter.GetBytes(capacity).CopyTo(bytes, 12);
            Memory.Blocks[Array + 0x10] = bytes;
        }
    }

    private sealed class Memory : IReadOnlyProcessMemory
    {
        public Dictionary<ulong, byte[]> Blocks { get; } = [];
        public Action<ulong, int>? BeforeRead;
        public void Q(ulong address, ulong value) => Blocks[address] = BitConverter.GetBytes(value);
        public byte[] Read(IntPtr address, int length)
        {
            var start = checked((ulong)address.ToInt64());
            BeforeRead?.Invoke(start, length);
            foreach (var (baseAddress, bytes) in Blocks)
                if (start >= baseAddress && start - baseAddress + (ulong)length <= (ulong)bytes.Length)
                    return bytes.AsSpan((int)(start - baseAddress), length).ToArray();
            throw new InvalidDataException("Unmapped test memory.");
        }
        public float ReadSingle(ulong address) => BitConverter.ToSingle(Read(new IntPtr((long)address), 4));
        public IReadOnlyList<MemoryRegion> GetWritableRegions() =>
            throw new InvalidOperationException("Engine-light reader must not scan memory.");
    }

    private static byte[] Records(params byte[][] records)
    {
        var bytes = new byte[records.Length * EngineLightReader.RecordSize];
        for (var index = 0; index < records.Length; index++)
            records[index].CopyTo(bytes, index * EngineLightReader.RecordSize);
        return bytes;
    }

    private static byte[] Record((float X, float Y, float Z) position, (float R, float G, float B) color,
        float cone, float scale, byte active, byte selected)
    {
        var bytes = new byte[EngineLightReader.RecordSize];
        F(bytes, 0x30, position.X); F(bytes, 0x34, position.Y); F(bytes, 0x38, position.Z);
        F(bytes, 0x3C, color.R); F(bytes, 0x40, color.G); F(bytes, 0x44, color.B);
        F(bytes, 0x4C, scale); F(bytes, 0x54, cone);
        bytes[0x63] = active; bytes[0x64] = selected;
        return bytes;
    }

    private static void F(byte[] bytes, int offset, float value) => BitConverter.GetBytes(value).CopyTo(bytes, offset);
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
