using System.Diagnostics;
using System.Text.Json;
using CrimsonDesertTelemetry.Core;

const string schemaVersion = "1.0";
var jsonOptions = new JsonSerializerOptions
{
    PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    WriteIndented = false
};

try
{
    var command = args.FirstOrDefault()?.ToLowerInvariant() ?? "diagnose";
    return command switch
    {
        "diagnose" => Diagnose(),
        "discover" => Discover(),
        "snapshot" => Snapshot(),
        "track" => Track(args.Skip(1).ToArray()),
        "--version" or "-v" or "version" => Version(),
        "--help" or "-h" or "help" => Help(),
        _ => UsageError($"Unknown command: {command}")
    };
}
catch (InvalidOperationException exception)
{
    Console.Error.WriteLine(exception.Message);
    return 3;
}
catch (InvalidDataException exception)
{
    Console.Error.WriteLine(exception.Message);
    return 4;
}
catch (Exception exception)
{
    Console.Error.WriteLine($"Unexpected failure: {exception.Message}");
    return 1;
}

int Diagnose()
{
    using var process = GameDiscovery.FindRunningProcess();
    if (process is null)
    {
        Console.WriteLine("Crimson Desert is not running.");
        return 3;
    }
    var executable = process.MainModule?.FileName
        ?? throw new InvalidOperationException("The Crimson Desert executable path is unavailable.");
    var hash = GameDiscovery.ComputeSha256(executable);
    var definition = FindDefinition(hash);
    Console.WriteLine($"Process: {process.Id}");
    Console.WriteLine($"Executable: {executable}");
    Console.WriteLine($"SHA-256: {hash}");
    Console.WriteLine(definition is null
        ? "Support: unsupported build"
        : $"Support: build {definition.SteamBuildId} ({definition.Status})");
    return definition is null ? 4 : 0;
}

int Discover()
{
    using var runtime = OpenRuntime();
    var player = StaticPositionProbe.Read(runtime.Reader, runtime.PositionAddresses);
    var watch = Stopwatch.StartNew();
    var discovered = RenderCameraConstantsScanner.Find(runtime.Reader, player);
    watch.Stop();
    var refreshed = RenderCameraConstantsScanner.Refresh(runtime.Reader,
        discovered.Select(static candidate => candidate.Address), player);
    var consensus = RenderCameraConstantsScanner.SelectConsensus(refreshed)
        ?? throw new InvalidDataException("No valid camera consensus was found.");
    Console.WriteLine(JsonSerializer.Serialize(new
    {
        build = runtime.Definition.SteamBuildId,
        discoveryMilliseconds = watch.Elapsed.TotalMilliseconds,
        discoveredCopies = discovered.Count,
        validCopies = refreshed.Count,
        consensusCopies = consensus.CopyCount,
        distinctStates = consensus.DistinctStateCount,
        camera = ToCameraConsensus(consensus)
    }, jsonOptions));
    return 0;
}

int Snapshot()
{
    using var runtime = OpenRuntime();
    var player = StaticPositionProbe.Read(runtime.Reader, runtime.PositionAddresses);
    var discovered = RenderCameraConstantsScanner.Find(runtime.Reader, player);
    var tracker = new RenderCameraTracker(runtime.Reader, discovered);
    var frame = tracker.Capture(player);
    var snapshot = new TelemetrySnapshot(schemaVersion, runtime.Definition.SteamBuildId, 0,
        frame.Timestamp, new PlayerSnapshot(ToVector(player)), ToCamera(frame));
    Console.WriteLine(JsonSerializer.Serialize(snapshot, jsonOptions));
    return 0;
}

int Track(string[] commandArgs)
{
    if (commandArgs.Length > 2 ||
        commandArgs.Length >= 1 && !int.TryParse(commandArgs[0], out _) ||
        commandArgs.Length >= 2 && !int.TryParse(commandArgs[1], out _))
        return UsageError("track expects integer arguments: [samples] [interval-ms].");
    var samples = commandArgs.Length >= 1 ? int.Parse(commandArgs[0]) : 0;
    var intervalMilliseconds = commandArgs.Length >= 2 ? int.Parse(commandArgs[1]) : 100;
    if (samples is < 0 or > 100_000 || intervalMilliseconds is < 10 or > 60_000)
        return UsageError("track expects [samples; 0 means unlimited] [interval-ms].");

    using var runtime = OpenRuntime();
    var player = StaticPositionProbe.Read(runtime.Reader, runtime.PositionAddresses);
    var watch = Stopwatch.StartNew();
    var discovered = RenderCameraConstantsScanner.Find(runtime.Reader, player);
    watch.Stop();
    var tracker = new RenderCameraTracker(runtime.Reader, discovered);
    Console.Error.WriteLine($"Ready: {tracker.AddressCount} copies discovered in {watch.Elapsed.TotalSeconds:0.###} s.");

    using var cancellation = new CancellationTokenSource();
    ConsoleCancelEventHandler cancelHandler = (_, eventArgs) =>
    {
        eventArgs.Cancel = true;
        cancellation.Cancel();
    };
    Console.CancelKeyPress += cancelHandler;
    long sequence = 0;
    while (!cancellation.IsCancellationRequested && (samples == 0 || sequence < samples))
    {
        player = StaticPositionProbe.Read(runtime.Reader, runtime.PositionAddresses);
        var frame = tracker.Capture(player);
        var snapshot = new TelemetrySnapshot(schemaVersion, runtime.Definition.SteamBuildId, sequence,
            frame.Timestamp, new PlayerSnapshot(ToVector(player)), ToCamera(frame));
        Console.WriteLine(JsonSerializer.Serialize(snapshot, jsonOptions));
        sequence++;
        if ((samples == 0 || sequence < samples) &&
            cancellation.Token.WaitHandle.WaitOne(intervalMilliseconds)) break;
    }
    Console.CancelKeyPress -= cancelHandler;
    return 0;
}

RuntimeContext OpenRuntime()
{
    var process = GameDiscovery.FindRunningProcess()
        ?? throw new InvalidOperationException("Crimson Desert is not running.");
    var executable = process.MainModule?.FileName
        ?? throw new InvalidOperationException("The Crimson Desert executable path is unavailable.");
    var hash = GameDiscovery.ComputeSha256(executable);
    var definition = FindDefinition(hash)
        ?? throw new InvalidDataException($"Unsupported Crimson Desert build (SHA-256 {hash}).");
    var addresses = StaticPositionProbe.Resolve(process, executable, definition);
    return new RuntimeContext(process, new ReadOnlyProcess(process), definition, addresses);
}

BuildDefinition? FindDefinition(string hash) =>
    BuildDefinition.LoadAll(Path.Combine(AppContext.BaseDirectory, "definitions"))
        .SingleOrDefault(candidate =>
            string.Equals(candidate.ExecutableSha256, hash, StringComparison.OrdinalIgnoreCase));

CameraSnapshot ToCamera(RenderCameraFrame frame)
{
    var camera = frame.Camera;
    return new CameraSnapshot(camera.Position, camera.Up, camera.Right, camera.Forward,
        camera.NearPlane, camera.FarPlane, camera.FieldOfViewRadians * 180f / MathF.PI,
        camera.AspectRatio, frame.ConsensusCopies, frame.ValidCopies, frame.DistinctStates,
        frame.Rediscovered);
}

CameraSnapshot ToCameraConsensus(RenderCameraConsensus consensus)
{
    var camera = consensus.Camera;
    return new CameraSnapshot(camera.Position, camera.Up, camera.Right, camera.Forward,
        camera.NearPlane, camera.FarPlane, camera.FieldOfViewRadians * 180f / MathF.PI,
        camera.AspectRatio, consensus.CopyCount, consensus.ValidCopyCount, consensus.DistinctStateCount, false);
}

static CameraVector3 ToVector((float X, float Y, float Z) value) => new(value.X, value.Y, value.Z);

int Help()
{
    Console.WriteLine("Crimson Desert Telemetry");
    Console.WriteLine("  diagnose                 Check the running game and build support");
    Console.WriteLine("  discover                 Discover and summarize camera copies");
    Console.WriteLine("  snapshot                 Emit one JSON telemetry snapshot");
    Console.WriteLine("  track [samples] [ms]     Emit JSON Lines continuously (0 samples = unlimited)");
    Console.WriteLine("  version                  Show the program version");
    return 0;
}

int Version()
{
    Console.WriteLine(typeof(RuntimeContext).Assembly.GetName().Version?.ToString(3) ?? "unknown");
    return 0;
}

int UsageError(string message)
{
    Console.Error.WriteLine(message);
    Console.Error.WriteLine("Run with --help for usage.");
    return 2;
}

sealed class RuntimeContext(
    Process process,
    ReadOnlyProcess reader,
    BuildDefinition definition,
    StaticPositionAddresses positionAddresses) : IDisposable
{
    public Process Process { get; } = process;
    public ReadOnlyProcess Reader { get; } = reader;
    public BuildDefinition Definition { get; } = definition;
    public StaticPositionAddresses PositionAddresses { get; } = positionAddresses;

    public void Dispose()
    {
        Reader.Dispose();
        Process.Dispose();
    }
}
