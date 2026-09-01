using System.Text;
using CrimsonDesertTelemetry.Core;

internal static class BuildCompatibilityTests
{
    public static void Relocation()
    {
        foreach (var shift in new[] { 0u, 0x10000u })
        {
            using var fixture = new Fixture(shift);
            var resolved = fixture.Resolve();
            var camera = resolved.Definition.EngineCamera!;
            Check(resolved.Compatibility.Mode == "automatic" && resolved.GameBuild == "unknown",
                "Unknown executable was labelled tested or inherited the reference build ID.");
            Check(resolved.Compatibility.ExecutableSha256 == GameDiscovery.ComputeSha256(fixture.Path), "Wrong actual hash.");
            Check(camera.MainRootReferenceRva == fixture.Code + 0x100 &&
                camera.CameraReferenceRva == fixture.Code + 0x180 &&
                camera.MainRootGlobalRva == fixture.Data + 0x100 &&
                camera.CameraGlobalRva == fixture.Data + 0x108 &&
                camera.ContextVtableRva == fixture.Tables + 0x100 &&
                camera.CameraVtableRva == fixture.Tables + 0x200, "Relocation used the reference RVAs.");
            Check(fixture.Definition.EngineCamera!.CameraVtableRva == 0xDEAD,
                "Automatic resolution mutated the trusted definition.");
        }
    }

    public static void KnownHash()
    {
        using var fixture = new Fixture();
        fixture.Save();
        fixture.Definition.ExecutableSha256 = GameDiscovery.ComputeSha256(fixture.Path);
        fixture.Definition.EngineCamera!.ContextVtableFingerprints.Clear();
        var result = BuildCompatibility.Resolve(fixture.Path, [fixture.Definition]);
        Check(result.Compatibility.Mode == "tested", "Known EXE did not take the existing path.");
        fixture.Bytes[0x70] = 1; // A harmless DOS-header byte changes the identity, not the game build string.
        fixture.Save();
        Reject(() => BuildCompatibility.Resolve(fixture.Path, [fixture.Definition]), "Changed hash bypassed validation.");
    }

    public static void MissingAndAmbiguousCode()
    {
        using var missing = new Fixture();
        missing.Bytes[0x500] ^= 1;
        Reject(() => missing.Resolve(), "Missing root signature accepted.");
        using var ambiguous = new Fixture();
        ambiguous.Bytes.AsSpan(0x500, 14).CopyTo(ambiguous.Bytes.AsSpan(0x700));
        Reject(() => ambiguous.Resolve(), "Duplicate root signature accepted.");
        using var ambiguousFunction = new Fixture();
        ambiguousFunction.Bytes.AsSpan(0xC00, 16).CopyTo(ambiguousFunction.Bytes.AsSpan(0xD00));
        Reject(() => ambiguousFunction.Resolve(), "Duplicate table function accepted.");
    }

    public static void DataTargets()
    {
        using var codeTarget = new Fixture();
        codeTarget.I32(0x503, (int)(codeTarget.Code + 0x300) - (int)(codeTarget.Code + 0x107));
        Reject(() => codeTarget.Resolve(), "Executable RIP target accepted as data.");
        using var outside = new Fixture();
        outside.I32(0x503, int.MinValue);
        Reject(() => outside.Resolve(), "Out-of-image RIP target accepted.");
        using var readOnly = new Fixture();
        readOnly.U32(Fixture.SectionTable + 80 + 36, 0x40000040);
        Reject(() => readOnly.Resolve(), "Read-only global accepted.");
        using var independent = new Fixture();
        independent.I32(0x583, (int)(independent.Data + 0x100) - (int)(independent.Code + 0x187));
        Reject(() => independent.Resolve(), "Identical camera roots accepted.");
    }

    public static void VtableGuards()
    {
        using var wrong = new Fixture();
        wrong.Q(0x2500 + 16, wrong.ImageBase + wrong.Code + 0x900);
        Reject(() => wrong.Resolve(), "Incorrect second vtable fingerprint accepted.");
        using var duplicate = new Fixture();
        duplicate.Bytes.AsSpan(0x2500, 24).CopyTo(duplicate.Bytes.AsSpan(0x2700));
        Reject(() => duplicate.Resolve(), "Ambiguous vtable accepted.");
        using var writable = new Fixture();
        writable.U32(Fixture.SectionTable + 40 + 36, 0xC0000040);
        Reject(() => writable.Resolve(), "Writable table accepted.");
        using var weak = new Fixture();
        weak.Definition.EngineCamera!.ContextVtableFingerprints.RemoveAt(1);
        Reject(() => weak.Resolve(), "Single-slot table fingerprint accepted.");
    }

    public static void MalformedImages()
    {
        using var truncated = new Fixture();
        File.WriteAllBytes(truncated.Path, [0x4D, 0x5A]);
        Reject(() => BuildCompatibility.CheckAutomatic(truncated.Path, [truncated.Definition]), "Truncated image accepted.");
        using var wrongArchitecture = new Fixture();
        wrongArchitecture.U16(0x84, 0x14C);
        Reject(() => wrongArchitecture.Resolve(), "Non-AMD64 image accepted.");
        using var overlap = new Fixture();
        overlap.U32(Fixture.SectionTable + 40 + 12, overlap.Code);
        Reject(() => overlap.Resolve(), "Overlapping sections accepted.");
        using var rawBounds = new Fixture();
        rawBounds.U32(Fixture.SectionTable + 16, uint.MaxValue);
        Reject(() => rawBounds.Resolve(), "Invalid raw bounds accepted.");
    }

    public static void TemplateGuards()
    {
        using var disabled = new Fixture();
        disabled.Definition.AllowAutomaticCompatibility = false;
        Reject(() => disabled.Resolve(), "Non-opted-in layout used automatically.");
        using var types = new Fixture();
        types.Definition.PlayerRoot!.ExpectedTypeNames.Clear();
        Reject(() => types.Resolve(), "Missing player type guards accepted.");
        using var multiple = new Fixture();
        multiple.Save();
        Reject(() => BuildCompatibility.CheckAutomatic(multiple.Path, [multiple.Definition, multiple.Definition]),
            "Multiple matching templates accepted.");
    }

    private sealed class Fixture : IDisposable
    {
        public const int SectionTable = 0x188;
        public string Path { get; } = System.IO.Path.Combine(System.IO.Path.GetTempPath(), $"cdt-compat-{Guid.NewGuid():N}.exe");
        public byte[] Bytes { get; } = new byte[0x4400];
        public uint Code { get; }
        public uint Tables { get; }
        public uint Data { get; }
        public ulong ImageBase { get; }
        public BuildDefinition Definition { get; }

        public Fixture(uint shift = 0)
        {
            Code = 0x1000 + shift; Tables = 0x5000 + shift; Data = 0x7000 + shift;
            ImageBase = shift == 0 ? 0x140000000UL : 0x180000000UL;
            U16(0, 0x5A4D); U32(0x3C, 0x80); U32(0x80, 0x4550); U16(0x84, 0x8664);
            U16(0x86, 3); U16(0x94, 0xF0); U16(0x98, 0x20B); Q(0x98 + 24, ImageBase);
            U32(0x98 + 56, shift + 0x9000);
            Section(0, ".text", Code, 0x2000, 0x400, 0x2000, 0x60000020);
            Section(1, ".rdata", Tables, 0x1000, 0x2400, 0x1000, 0x40000040);
            Section(2, ".data", Data, 0x1800, 0x3400, 0x1000, 0xC0000040);
            var main = Reference(0x100, Data + 0x100, 0x0D, 0x90);
            var camera = Reference(0x180, Data + 0x108, 0x05, 0xA0);
            var xy = Reference(0x200, Data + 0x300, 0x05, 0xB0);
            var z = Reference(0x280, Data + 0x310, 0x05, 0xC0);
            var world = Reference(0x300, Data + 0x200, 0x05, 0xD0);
            xy.Purpose = "static-position-xy"; z.Purpose = "static-position-z"; world.Purpose = "world-system";
            var contextFunctions = new[] { Function(0x800, 0x10), Function(0x840, 0x20) };
            var cameraFunctions = new[] { Function(0x880, 0x30), Function(0x8C0, 0x40) };
            for (var slot = 0; slot < 3; slot++)
            {
                Q(0x2500 + slot * 8, ImageBase + Code + (slot == 2 ? 0x840UL : 0x800UL));
                Q(0x2600 + slot * 8, ImageBase + Code + (slot == 2 ? 0x8C0UL : 0x880UL));
            }
            Definition = new BuildDefinition
            {
                Status = "locally-validated", AllowAutomaticCompatibility = true,
                ExecutableSha256 = new string('A', 64), SteamBuildId = "reference-only", Patterns = [xy, z],
                PlayerRoot = new PlayerRootDefinition { WorldSystemPattern = world, ExpectedTypeNames = ["manager", "actor", "control"] },
                EngineCamera = new EngineCameraDefinition
                {
                    Layout = "renderer-camera-v1", MainRootReferencePattern = main.Pattern,
                    CameraReferencePattern = camera.Pattern, CameraVtableRva = 0xDEAD,
                    ContextVtableFingerprints = [new() { Slot = 1, Pattern = contextFunctions[0] }, new() { Slot = 2, Pattern = contextFunctions[1] }],
                    CameraVtableFingerprints = [new() { Slot = 1, Pattern = cameraFunctions[0] }, new() { Slot = 2, Pattern = cameraFunctions[1] }]
                }
            };
        }

        private void Section(int index, string name, uint rva, uint virtualSize, uint raw, uint size, uint flags)
        {
            var offset = SectionTable + index * 40;
            Encoding.ASCII.GetBytes(name).CopyTo(Bytes, offset);
            U32(offset + 8, virtualSize); U32(offset + 12, rva); U32(offset + 16, size);
            U32(offset + 20, raw); U32(offset + 36, flags);
        }

        private PatternDefinition Reference(int offset, uint target, byte operand, byte suffix)
        {
            var bytes = new byte[] { 0x48, 0x8B, operand, 0, 0, 0, 0, suffix, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96 };
            var pattern = string.Join(" ", bytes.Select((value, index) => index is >= 3 and <= 6 ? "??" : value.ToString("X2")));
            bytes.CopyTo(Bytes, 0x400 + offset);
            I32(0x400 + offset + 3, checked((int)target - (int)(Code + offset + 7)));
            return new PatternDefinition { Name = suffix.ToString("X2"), Pattern = pattern, RipOffset = 3, RipEnd = 7, Confidence = "locally-validated" };
        }

        private string Function(int offset, int start)
        {
            var bytes = Enumerable.Range(start, 16).Select(value => (byte)value).ToArray();
            bytes.CopyTo(Bytes, 0x400 + offset);
            return string.Join(" ", bytes.Select(value => value.ToString("X2")));
        }

        public void Save() => File.WriteAllBytes(Path, Bytes);
        public ResolvedBuild Resolve() { Save(); return BuildCompatibility.Resolve(Path, [Definition]); }
        public void U16(int offset, ushort value) => BitConverter.GetBytes(value).CopyTo(Bytes, offset);
        public void U32(int offset, uint value) => BitConverter.GetBytes(value).CopyTo(Bytes, offset);
        public void I32(int offset, int value) => BitConverter.GetBytes(value).CopyTo(Bytes, offset);
        public void Q(int offset, ulong value) => BitConverter.GetBytes(value).CopyTo(Bytes, offset);
        public void Dispose() { if (File.Exists(Path)) File.Delete(Path); }
    }

    private static void Check(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    private static void Reject(Action action, string message)
    {
        try { action(); } catch (InvalidDataException) { return; }
        throw new InvalidOperationException(message);
    }
}
