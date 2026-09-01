using System.Text.Json;

namespace CrimsonDesertTelemetry.Core;

public sealed class BuildDefinition
{
    public int SchemaVersion { get; set; }
    public string SteamAppId { get; set; } = "";
    public string SteamBuildId { get; set; } = "";
    public string ExecutableVersion { get; set; } = "";
    public string ExecutableSha256 { get; set; } = "";
    public string Status { get; set; } = "research";
    public List<PatternDefinition> Patterns { get; set; } = [];
    public PlayerRootDefinition? PlayerRoot { get; set; }
    public EngineCameraDefinition? EngineCamera { get; set; }
    public bool AllowAutomaticCompatibility { get; set; }

    public static IReadOnlyList<BuildDefinition> LoadAll(string directory)
    {
        var options = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
        var definitions = new Dictionary<string, BuildDefinition>(StringComparer.OrdinalIgnoreCase);
        var assembly = typeof(BuildDefinition).Assembly;
        foreach (var resourceName in assembly.GetManifestResourceNames()
                     .Where(static name => name.Contains(".definitions.build-", StringComparison.OrdinalIgnoreCase) &&
                                           name.EndsWith(".json", StringComparison.OrdinalIgnoreCase)))
        {
            using var stream = assembly.GetManifestResourceStream(resourceName)
                               ?? throw new InvalidDataException($"Missing embedded build definition: {resourceName}");
            var definition = JsonSerializer.Deserialize<BuildDefinition>(stream, options)
                             ?? throw new InvalidDataException($"Empty embedded build definition: {resourceName}");
            definitions[definition.ExecutableSha256] = definition;
        }

        if (Directory.Exists(directory))
        {
            foreach (var path in Directory.EnumerateFiles(directory, "build-*.json"))
            {
                var definition = JsonSerializer.Deserialize<BuildDefinition>(File.ReadAllText(path), options)
                                 ?? throw new InvalidDataException($"Empty build definition: {path}");
                definitions[definition.ExecutableSha256] = definition;
            }
        }
        return definitions.Values.ToList();
    }
}

public sealed class EngineCameraDefinition
{
    public string Layout { get; set; } = "";
    public ulong MainRootReferenceRva { get; set; }
    public string MainRootReferencePattern { get; set; } = "";
    public ulong MainRootGlobalRva { get; set; }
    public ulong CameraReferenceRva { get; set; }
    public string CameraReferencePattern { get; set; } = "";
    public ulong CameraGlobalRva { get; set; }
    public ulong ContextVtableRva { get; set; }
    public ulong CameraVtableRva { get; set; }
    public List<VtableSlotFingerprint> ContextVtableFingerprints { get; set; } = [];
    public List<VtableSlotFingerprint> CameraVtableFingerprints { get; set; } = [];
}

public sealed class VtableSlotFingerprint
{
    public int Slot { get; set; }
    public string Pattern { get; set; } = "";
}

public sealed class PlayerRootDefinition
{
    public PatternDefinition WorldSystemPattern { get; set; } = new();
    public int WorldSystemToActorManagerOffset { get; set; } = 0x30;
    public int ActorManagerToPlayerActorOffset { get; set; } = 0x50;
    public int PlayerActorToIntermediateOffset { get; set; } = 0x68;
    public int IntermediateToControlOffset { get; set; } = 0x40;
    public int ControlToOwnerOffset { get; set; } = 0x140;
    public int OwnerToPhysicsOffset { get; set; } = 0x298;
    public int BasisXOffset { get; set; } = 0x60;
    public int BasisYOffset { get; set; } = 0x70;
    public int BasisZOffset { get; set; } = 0x80;
    public int PositionOffset { get; set; } = 0x90;
    public List<string> ExpectedTypeNames { get; set; } = [];
}

public sealed class PatternDefinition
{
    public string Name { get; set; } = "";
    public string Purpose { get; set; } = "";
    public string Section { get; set; } = ".text";
    public string Pattern { get; set; } = "";
    public int? RipOffset { get; set; }
    public int? RipEnd { get; set; }
    public string Confidence { get; set; } = "candidate";
    public string? Source { get; set; }
}
