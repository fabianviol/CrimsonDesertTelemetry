using CrimsonDesertTelemetry.Core;

internal static class EngineCameraTests
{
    public static void Mapping()
    {
        var fixture = new Fixture();
        var camera = fixture.Reader.Capture((10, 20, 30));
        Check(camera.Camera.Position == new CameraVector3(10, 20, 30) &&
            camera.Camera.Right == new CameraVector3(1, 0, 0) && camera.Camera.Up == new CameraVector3(0, 1, 0) &&
            camera.Camera.Forward == new CameraVector3(0, 0, 1), "Wrong native vector mapping.");
        Check(Math.Abs(camera.Camera.FieldOfViewRadians * 180 / MathF.PI - 50) < .001f &&
            Math.Abs(camera.Camera.AspectRatio - 16f / 9) < .0001f, "Wrong native projection mapping.");
        Check(camera.Camera.FarPlane == float.MaxValue && camera.ConsensusCopies == 1 &&
            camera.ValidCopies == 1 && camera.DistinctStates == 1 && !camera.Rediscovered,
            "Far plane or single-source quality mismatch.");
        F(fixture.Source, 0x864, 12345);
        Check(fixture.Reader.Capture((10, 20, 30)).Camera.FarPlane == float.MaxValue,
            "Unvalidated raw far field was exported.");
    }

    public static void RebasedColdStart()
    {
        var first = new Fixture();
        var second = new Fixture(0x180000000, 0x900000);
        Check(first.Reader.Capture((10, 20, 30)).Camera.Address != second.Reader.Capture((10, 20, 30)).Camera.Address,
            "Camera reader reused the previous process address.");
    }

    public static void InstructionGuards()
    {
        var fixture = new Fixture();
        var definition = fixture.Definition;
        var code = fixture.Memory.Blocks[fixture.Base + definition.CameraReferenceRva];
        code[7] ^= 1;
        Reject(() => fixture.CreateReader(), "Wrong camera instruction accepted.");
        code[7] ^= 1;
        var displacement = BitConverter.ToInt32(code, 3);
        BitConverter.GetBytes(displacement + 8).CopyTo(code, 3);
        Reject(() => fixture.CreateReader(), "Wrong RIP target accepted.");
        BitConverter.GetBytes(displacement).CopyTo(code, 3);
        definition.Layout = "unknown";
        Reject(() => fixture.CreateReader(), "Unsupported native layout accepted.");
    }

    public static void RootsAndTypes()
    {
        var fixture = new Fixture();
        fixture.Memory.Q(fixture.Base + fixture.Definition.CameraGlobalRva, fixture.Camera + 8);
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Conflicting roots accepted.");
        fixture.Memory.Q(fixture.Base + fixture.Definition.CameraGlobalRva, fixture.Camera);
        fixture.Memory.Q(fixture.Camera, fixture.Base + fixture.Definition.CameraVtableRva + 8);
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Wrong camera vtable accepted.");
        fixture.Memory.Q(fixture.Camera, fixture.Base + fixture.Definition.CameraVtableRva);
        fixture.Memory.Q(fixture.Context, fixture.Base + fixture.Definition.ContextVtableRva + 8);
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Wrong context vtable accepted.");
        fixture.Memory.Q(fixture.Context, fixture.Base + fixture.Definition.ContextVtableRva);
        fixture.Memory.Q(fixture.Camera + 0x428, 0);
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Null source pointer accepted.");
    }

    public static void InvalidData()
    {
        void Bad(Action<byte[]> mutate, string message)
        {
            var bytes = SourceBytes(); mutate(bytes);
            Reject(() => EngineCameraReader.Decode(bytes, 0x500000, (10, 20, 30)), message);
        }
        Bad(bytes => F(bytes, 0x3F4, 0), "Non-unit basis accepted.");
        Bad(bytes => F(bytes, 0x98, -1), "Reflected basis accepted.");
        Bad(bytes => F(bytes, 0x80, float.NaN), "Non-finite position accepted.");
        Bad(bytes => F(bytes, 0x80, 1000), "Distant camera accepted.");
        Bad(bytes => F(bytes, 0x860, 0), "Invalid near plane accepted.");
        Bad(bytes => F(bytes, 0x50C, 0), "Orthographic projection accepted.");
        Bad(bytes => F(bytes, 0x500, .3f), "Off-center projection accepted.");
        Bad(bytes => F(bytes, 0x4E0, -1), "Negative projection scale accepted.");
        Bad(bytes => F(bytes, 0x508, float.NaN), "Non-finite projection accepted.");
        Reject(() => EngineCameraReader.Decode(new byte[10], 0x500000, (10, 20, 30)), "Truncated source accepted.");
        Reject(() => EngineCameraReader.Decode(SourceBytes(), 0x500000, (float.NaN, 20, 30)), "Non-finite player accepted.");
    }

    public static void ConcurrentUpdates()
    {
        var fixture = new Fixture();
        var sourceReads = 0;
        fixture.Memory.BeforeRead = (address, _) =>
        {
            if (address == fixture.SourceAddress) F(fixture.Source, 0x80, 10 + ++sourceReads * .01f);
        };
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Changing fields accepted as coherent.");
        Check(sourceReads == 6, "Native capture exceeded its three-attempt budget.");
        fixture.Memory.BeforeRead = (address, _) =>
        {
            if (address == fixture.SourceAddress) fixture.Memory.U(fixture.Context + 0x40, ++fixture.Counter);
        };
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Changing render counter accepted.");
        fixture.Memory.BeforeRead = null;
        _ = fixture.Reader.Capture((10, 20, 30));
    }

    public static void LoadingAndRelocation()
    {
        var fixture = new Fixture();
        _ = fixture.Reader.Capture((10, 20, 30));
        fixture.Memory.Q(fixture.Camera + 0x428, 0);
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Loading reused the last good camera.");
        var replacement = fixture.SourceAddress + 0x10000;
        fixture.Memory.Blocks[replacement] = SourceBytes();
        fixture.Memory.Q(fixture.Camera + 0x428, replacement);
        var frame = fixture.Reader.Capture((10, 20, 30));
        Check(frame.Rediscovered && frame.Camera.Address == replacement, "Replacement source was not followed.");
        Check(!fixture.Reader.Capture((10, 20, 30)).Rediscovered, "Relocation flag did not reset.");
    }

    public static void Freshness()
    {
        var fixture = new Fixture();
        _ = fixture.Reader.Capture((10, 20, 30));
        fixture.Now = TimeSpan.FromMilliseconds(900);
        _ = fixture.Reader.Capture((10, 20, 30));
        fixture.Now = TimeSpan.FromSeconds(1);
        Reject(() => fixture.Reader.Capture((10, 20, 30)), "Stalled render context was still published.");
        fixture.Memory.U(fixture.Context + 0x40, ++fixture.Counter);
        _ = fixture.Reader.Capture((10, 20, 30));
        fixture.Now = TimeSpan.FromSeconds(2);
        fixture.Memory.U(fixture.Context + 0x40, uint.MaxValue);
        _ = fixture.Reader.Capture((10, 20, 30));
        fixture.Now = TimeSpan.FromSeconds(3);
        fixture.Memory.U(fixture.Context + 0x40, 0);
        _ = fixture.Reader.Capture((10, 20, 30));
    }

    private sealed class Fixture
    {
        public ulong Base { get; }
        public ulong Context { get; }
        public ulong Camera { get; }
        public ulong SourceAddress { get; }
        public byte[] Source => Memory.Blocks[SourceAddress];
        public Memory Memory { get; } = new();
        public EngineCameraDefinition Definition { get; }
        public EngineCameraReader Reader { get; }
        public TimeSpan Now;
        public uint Counter = 100;

        public Fixture(ulong moduleBase = 0x140000000, ulong heap = 0x100000)
        {
            Base = moduleBase;
            Definition = BuildDefinition.LoadAll(Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")))
                .Single(d => d.SteamBuildId == "24994088").EngineCamera
                ?? throw new InvalidOperationException("Native definition is not embedded.");
            var application = heap + 0x10000;
            Context = heap + 0x20000; Camera = heap + 0x30000; SourceAddress = heap + 0x40000;
            Memory.Q(Base + Definition.MainRootGlobalRva, heap);
            Memory.Q(heap + 0x28, application);
            Memory.Q(application + 0x18, Context);
            Memory.Q(Context, Base + Definition.ContextVtableRva);
            Memory.U(Context + 0x40, Counter);
            Memory.Q(Context + 0xE0, Camera);
            Memory.Q(Base + Definition.CameraGlobalRva, Camera);
            Memory.Q(Camera, Base + Definition.CameraVtableRva);
            Memory.Q(Camera + 0x428, SourceAddress);
            Memory.Blocks[SourceAddress] = SourceBytes();
            Code(Definition.MainRootReferenceRva, Definition.MainRootReferencePattern, Definition.MainRootGlobalRva);
            Code(Definition.CameraReferenceRva, Definition.CameraReferencePattern, Definition.CameraGlobalRva);
            Reader = CreateReader();
        }

        public EngineCameraReader CreateReader() => new(Memory, Base, Definition, () => Now);
        private void Code(ulong rva, string pattern, ulong target)
        {
            var bytes = pattern.Split(' ').Select(token => token == "??" ? (byte)0 : Convert.ToByte(token, 16)).ToArray();
            BitConverter.GetBytes(checked((int)((long)target - (long)rva - 7))).CopyTo(bytes, 3);
            Memory.Blocks[Base + rva] = bytes;
        }
    }

    private sealed class Memory : IReadOnlyProcessMemory
    {
        public Dictionary<ulong, byte[]> Blocks { get; } = [];
        public Action<ulong, int>? BeforeRead;
        public void Q(ulong address, ulong value) => Blocks[address] = BitConverter.GetBytes(value);
        public void U(ulong address, uint value) => Blocks[address] = BitConverter.GetBytes(value);
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
        public IReadOnlyList<MemoryRegion> GetWritableRegions() => throw new InvalidOperationException("Native reader must not scan memory.");
    }

    private static byte[] SourceBytes()
    {
        var bytes = new byte[0x868];
        F(bytes, 0x80, 10); F(bytes, 0x84, 20); F(bytes, 0x88, 30);
        F(bytes, 0x98, 1); F(bytes, 0x3E0, 1); F(bytes, 0x3F4, 1);
        F(bytes, 0x4E0, 1.20628512f); F(bytes, 0x4F4, 2.14450693f);
        F(bytes, 0x50C, 1); F(bytes, 0x518, .2f); F(bytes, 0x860, .2f);
        return bytes;
    }
    private static void F(byte[] bytes, int offset, float value) => BitConverter.GetBytes(value).CopyTo(bytes, offset);
    private static void Check(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    private static void Reject(Action action, string message)
    {
        try { action(); } catch (InvalidDataException) { return; }
        throw new InvalidOperationException(message);
    }
}
