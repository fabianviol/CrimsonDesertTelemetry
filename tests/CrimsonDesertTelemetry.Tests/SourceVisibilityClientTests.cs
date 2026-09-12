using System.Diagnostics;
using System.IO.MemoryMappedFiles;
using CrimsonDesertTelemetry.Core;

internal static class SourceVisibilityClientTests
{
    public static void ExchangeAndAttach()
    {
        var pid = Environment.ProcessId;
        var start = Process.GetCurrentProcess().StartTime.ToFileTimeUtc();
        using var client = new SourceVisibilityClient(pid, start);
        var input = Snapshot();

        _ = client.Apply((10, 20, 30), input);
        using (var query = MemoryMappedFile.OpenExisting(
                   $"Local\\CrimsonDesertTelemetry.VisibilityQuery.{pid}", MemoryMappedFileRights.Read))
        using (var view = query.CreateViewAccessor(0, SourceVisibilityClient.QueryBytes, MemoryMappedFileAccess.Read))
        {
            Check(view.ReadUInt32(0) == 0x51564443 && view.ReadUInt32(4) == 1 &&
                  view.ReadUInt32(8) == SourceVisibilityClient.HeaderBytes &&
                  view.ReadUInt32(12) == SourceVisibilityClient.QueryBytes && (view.ReadUInt64(16) & 1) == 0,
                "Visibility query ABI is invalid or mid-write.");
            Check(view.ReadUInt32(24) == pid && view.ReadInt64(32) == start && view.ReadUInt32(28) == 2,
                "Visibility query identity/count mismatch or duplicate positions were not merged.");
            Check(view.ReadSingle(56) == 10 && view.ReadSingle(60) == 21 && view.ReadSingle(64) == 30,
                "Visibility receiver did not use the raised player position.");
        }

        using var result = MemoryMappedFile.CreateNew(
            $"Local\\CrimsonDesertTelemetry.VisibilityResult.{pid}", SourceVisibilityClient.ResultBytes,
            MemoryMappedFileAccess.ReadWrite);
        using (var view = result.CreateViewAccessor(0, SourceVisibilityClient.ResultBytes, MemoryMappedFileAccess.ReadWrite))
        {
            var now = unchecked((ulong)Environment.TickCount64);
            view.Write(16, 1L);
            Thread.MemoryBarrier();
            view.Write(0, 0x52564443u); view.Write(4, 1u);
            view.Write(8, (uint)SourceVisibilityClient.HeaderBytes);
            view.Write(12, (uint)SourceVisibilityClient.ResultBytes);
            view.Write(24, (uint)pid); view.Write(28, 1u); view.Write(32, unchecked((ulong)start));
            view.Write(40, 1ul); view.Write(48, now); view.Write(56, now - 10);
            view.Write(64, 9ul); view.Write(72, now - 100); view.Write(80, 77u); view.Write(84, 2u);
            Vector(view, 88, new(10, 21, 30));
            Entry(view, 0, 1, 2, .08f, new(11, 21, 30));
            Entry(view, 1, 2, 1, .6f, new(12, 21, 30));
            Thread.MemoryBarrier();
            view.Write(16, 2L);
        }

        Thread.Sleep(275);
        var actual = client.Apply((10, 20, 30), input);
        Check(actual.Sources is { Count: 2 } && actual.Sources[0].SourceVisibility is
              { Status: "blocked", AttenuationFactor: 0, ClosestApproach: .08f } &&
              actual.Sources[1].SourceVisibility is { Status: "clear", AttenuationFactor: 1 },
            "Authored lights did not receive the position-matched binary verdicts.");
        Check(actual.Rendered?.Sources is { Count: 1 } &&
              actual.Rendered.Sources[0].SourceVisibility?.Status == "clear" &&
              actual.Rendered.Sources[0].SourceVisibility?.ReferencePosition == new CameraVector3(10, 21, 30),
            "Rendered duplicate did not share the authored source verdict or player receiver.");
        var authored = actual.Sources!;
        var rendered = actual.Rendered!.Sources!;
        Check(authored[0].ColorLinear == input.Sources![0].ColorLinear &&
              rendered[0].ColorLinear == input.Rendered!.Sources![0].ColorLinear,
            "Visibility exchange changed original light data.");
    }

    private static EngineLightsSnapshot Snapshot()
    {
        var a = new EngineLightSnapshot(new(11, 21, 30), "point", new(1, .5f, .25f), true, true, 1,
            new(1, .5f, .25f));
        var b = new EngineLightSnapshot(new(12, 21, 30), "spot", new(.5f, .25f, .125f), true, true, 1,
            new(.5f, .25f, .125f), new(0, -1, 0), 27);
        var rendered = new RenderLightsSnapshot("available", RenderLightReader.SourceName, 1, 1,
            DateTimeOffset.UtcNow, 0, null,
            [new RenderedLightSnapshot(4, b.Position, b.ColorLinear, .3f, "spot", b.Direction, 27)],
            new(1, 1, 0, 0));
        return new EngineLightsSnapshot("available", EngineLightReader.SourceName, 100, [a, b],
            new(2, 2, 0, 0, 0, 0, 0, 0, 0, 0), null, rendered);
    }

    private static void Entry(MemoryMappedViewAccessor view, int index, uint id, uint code, float closest,
        CameraVector3 position)
    {
        var offset = SourceVisibilityClient.HeaderBytes + index * SourceVisibilityClient.ResultEntryBytes;
        view.Write(offset, id); view.Write(offset + 4, code); view.Write(offset + 8, closest);
        Vector(view, offset + 12, position);
    }
    private static void Vector(MemoryMappedViewAccessor view, long offset, CameraVector3 value)
    {
        view.Write(offset, value.X); view.Write(offset + 4, value.Y); view.Write(offset + 8, value.Z);
    }
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
