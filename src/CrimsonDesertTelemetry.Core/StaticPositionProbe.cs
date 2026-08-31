using System.Diagnostics;

namespace CrimsonDesertTelemetry.Core;

public sealed record StaticPositionProbeResult(
    ulong XyAddress, ulong ZAddress, float X, float Y, float Z, string Confidence);
public readonly record struct StaticPositionAddresses(ulong XyAddress, ulong ZAddress);

public static class StaticPositionProbe
{
    public static StaticPositionProbeResult Run(Process process, string executable, BuildDefinition definition)
    {
        var addresses = Resolve(process, executable, definition);
        using var reader = new ReadOnlyProcess(process);
        var (x, y, z) = Read(reader, addresses);
        return new StaticPositionProbeResult(
            addresses.XyAddress, addresses.ZAddress, x, y, z,
            "validated by controlled movement for this build");
    }

    public static StaticPositionAddresses Resolve(Process process, string executable, BuildDefinition definition)
    {
        var xy = definition.Patterns.Single(pattern => pattern.Purpose == "static-position-xy");
        var z = definition.Patterns.Single(pattern => pattern.Purpose == "static-position-z");
        if (xy.Confidence != "locally-validated" || z.Confidence != "locally-validated")
            throw new InvalidDataException("Static position is not validated for this build.");
        var moduleBase = checked((ulong)process.MainModule!.BaseAddress.ToInt64());
        return new StaticPositionAddresses(
            checked(moduleBase + ResolveUniqueRipTarget(executable, xy)),
            checked(moduleBase + ResolveUniqueRipTarget(executable, z)));
    }

    public static (float X, float Y, float Z) Read(IReadOnlyProcessMemory reader, StaticPositionAddresses addresses)
    {
        var x = reader.ReadSingle(addresses.XyAddress);
        var y = reader.ReadSingle(addresses.XyAddress + sizeof(float));
        var z = reader.ReadSingle(addresses.ZAddress);
        if (!float.IsFinite(x) || !float.IsFinite(y) || !float.IsFinite(z) ||
            Math.Abs(x) > 100_000_000 || Math.Abs(y) > 100_000_000 || Math.Abs(z) > 100_000_000)
            throw new InvalidDataException("Static position contains implausible values.");
        var horizontalDistanceSquared = x * x + z * z;
        var nearZeroSentinel = horizontalDistanceSquared < 0.01f && Math.Abs(y) < 0.1f;
        var height1000Sentinel = horizontalDistanceSquared < 10_000f && Math.Abs(y - 1000.15f) < 1f;
        var knownLoadingSentinel = nearZeroSentinel || height1000Sentinel;
        if (knownLoadingSentinel)
            throw new InvalidDataException("Static horizontal position is not initialized yet.");
        return (x, y, z);
    }

    internal static ulong ResolveUniqueRipTarget(string executable, PatternDefinition candidate)
    {
        if (candidate.RipOffset is null || candidate.RipEnd is null)
            throw new InvalidDataException($"{candidate.Name} has no RIP metadata.");
        var targets = new List<long>();
        foreach (var info in PortableExecutable.ReadSectionHeaders(executable)
                     .Where(static section => section.Executable && section.RawSize > 0))
        {
            var section = PortableExecutable.ReadSection(executable, info);
            foreach (var match in SignaturePattern.Parse(candidate.Pattern).FindAll(section.Bytes))
            {
                var displacement = BitConverter.ToInt32(section.Bytes, match + candidate.RipOffset.Value);
                targets.Add(checked((long)section.VirtualAddress + match + candidate.RipEnd.Value + displacement));
            }
        }
        if (targets.Count != 1)
            throw new InvalidDataException($"{candidate.Name} is not unique: {targets.Count} matches.");
        return checked((ulong)targets[0]);
    }
}
