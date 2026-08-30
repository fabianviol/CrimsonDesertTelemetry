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
