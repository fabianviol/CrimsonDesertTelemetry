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

    public static IReadOnlyList<BuildDefinition> LoadAll(string directory)
    {
        if (!Directory.Exists(directory)) return [];
        var options = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
        return Directory.EnumerateFiles(directory, "build-*.json")
            .Select(path => JsonSerializer.Deserialize<BuildDefinition>(File.ReadAllText(path), options)
                ?? throw new InvalidDataException($"Empty build definition: {path}"))
            .ToList();
    }
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
