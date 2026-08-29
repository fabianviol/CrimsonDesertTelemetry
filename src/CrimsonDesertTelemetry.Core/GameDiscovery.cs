using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace CrimsonDesertTelemetry.Core;

public sealed record GameInstallation(string ExecutablePath, string? SteamBuildId);

public static partial class GameDiscovery
{
    public static Process? FindRunningProcess() => Process.GetProcessesByName("CrimsonDesert")
        .OrderByDescending(static process => process.MainWindowHandle != IntPtr.Zero)
        .FirstOrDefault();

    public static GameInstallation? FindInstallation()
    {
        const string uninstallKey = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall";
        foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
        {
            using var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view);
            using var root = baseKey.OpenSubKey(uninstallKey);
            if (root is null) continue;
            foreach (var name in root.GetSubKeyNames())
            {
                using var item = root.OpenSubKey(name);
                if (item is null) continue;
                if (!(item.GetValue("DisplayName") as string ?? "").StartsWith(
                        "Crimson Desert", StringComparison.OrdinalIgnoreCase)) continue;
                var install = item.GetValue("InstallLocation") as string;
                if (string.IsNullOrWhiteSpace(install)) continue;
                var executable = Path.Combine(install, "bin64", "CrimsonDesert.exe");
                if (!File.Exists(executable)) continue;
                return new GameInstallation(executable, ReadSteamBuildId(install));
            }
        }
        return null;
    }

    public static string ComputeSha256(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream));
    }

    private static string? ReadSteamBuildId(string installDirectory)
    {
        var steamApps = Directory.GetParent(installDirectory)?.Parent?.FullName;
        var manifest = steamApps is null ? null : Path.Combine(steamApps, "appmanifest_3321460.acf");
        if (manifest is null || !File.Exists(manifest)) return null;
        var match = BuildIdRegex().Match(File.ReadAllText(manifest));
        return match.Success ? match.Groups[1].Value : null;
    }

    [GeneratedRegex("\\\"buildid\\\"\\s+\\\"([0-9]+)\\\"", RegexOptions.CultureInvariant)]
    private static partial Regex BuildIdRegex();
}
