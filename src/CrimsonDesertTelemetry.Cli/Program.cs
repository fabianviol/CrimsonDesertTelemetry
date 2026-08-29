using System.Diagnostics;
using System.ComponentModel;
using System.Net.WebSockets;
using System.Runtime.InteropServices;
using System.Text.Json;
using CrimsonDesertTelemetry.Cli;
using CrimsonDesertTelemetry.Core;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

const string schemaVersion = "1.0";
var capabilities = new[] { "player.position", "camera.transform", "camera.projection" };
var coordinateSystem = new CoordinateSystemSnapshot("game-unit", "right", "y");
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
        "serve" => Serve(args.Skip(1).ToArray()),
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
    var snapshot = ToSnapshot(runtime.Definition.SteamBuildId, 0, player, frame, 0);
    Console.WriteLine(JsonSerializer.Serialize(snapshot, jsonOptions));
    return 0;
}

int Track(string[] commandArgs)
{
    if (commandArgs.Length > 2 ||
        commandArgs.Length >= 1 && !int.TryParse(commandArgs[0], out _) ||
        commandArgs.Length >= 2 && !int.TryParse(commandArgs[1], out _))
        return UsageError("track expects integer arguments: [samples] [rate-hz].");
    var samples = commandArgs.Length >= 1 ? int.Parse(commandArgs[0]) : 0;
    var rateHz = commandArgs.Length >= 2 ? int.Parse(commandArgs[1]) : 60;
    if (samples is < 0 or > 100_000 || rateHz is < 1 or > 240)
        return UsageError("track expects [samples; 0 means unlimited] [rate-hz; 1-240].");

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
    var tickInterval = TimeSpan.FromSeconds(1d / rateHz);
    var nextTick = Stopwatch.GetTimestamp();
    while (!cancellation.IsCancellationRequested && (samples == 0 || sequence < samples))
    {
        var captureWatch = Stopwatch.StartNew();
        player = StaticPositionProbe.Read(runtime.Reader, runtime.PositionAddresses);
        var frame = tracker.Capture(player);
        captureWatch.Stop();
        var snapshot = ToSnapshot(runtime.Definition.SteamBuildId, sequence, player, frame,
            captureWatch.ElapsedTicks * 1_000_000 / Stopwatch.Frequency);
        Console.WriteLine(JsonSerializer.Serialize(snapshot, jsonOptions));
        sequence++;
        nextTick += (long)(tickInterval.TotalSeconds * Stopwatch.Frequency);
        var remainingTicks = nextTick - Stopwatch.GetTimestamp();
        if (remainingTicks > 0 && (samples == 0 || sequence < samples) &&
            cancellation.Token.WaitHandle.WaitOne(TimeSpan.FromSeconds((double)remainingTicks / Stopwatch.Frequency)))
            break;
    }
    Console.CancelKeyPress -= cancelHandler;
    return 0;
}

int Serve(string[] commandArgs)
{
    if (commandArgs.Length > 2 ||
        commandArgs.Length >= 1 && !int.TryParse(commandArgs[0], out _) ||
        commandArgs.Length >= 2 && !int.TryParse(commandArgs[1], out _))
        return UsageError("serve expects integer arguments: [port] [rate-hz].");
    var port = commandArgs.Length >= 1 ? int.Parse(commandArgs[0]) : 27311;
    var rateHz = commandArgs.Length >= 2 ? int.Parse(commandArgs[1]) : 60;
    if (port is < 1024 or > 65535 || rateHz is < 1 or > 240)
        return UsageError("serve expects [port; 1024-65535] [rate-hz; 1-240].");
    return RunServer(port, rateHz);
}

int RunServer(int port, int rateHz)
{
    using var cancellation = new CancellationTokenSource();
    using var timerResolution = WindowsTimerResolution.RequestFor(rateHz);
    ConsoleCancelEventHandler cancelHandler = (_, eventArgs) =>
    {
        eventArgs.Cancel = true;
        cancellation.Cancel();
    };
    Console.CancelKeyPress += cancelHandler;
    try
    {
        var state = new TelemetryServerState(jsonOptions, rateHz);
        var builder = WebApplication.CreateSlimBuilder();
        builder.Logging.ClearProviders();
        builder.WebHost.UseUrls($"http://127.0.0.1:{port}");
        var app = builder.Build();
        app.UseWebSockets();
        app.Use(async (context, next) =>
        {
            var origin = context.Request.Headers.Origin.ToString();
            if (IsAllowedBrowserOrigin(origin))
            {
                context.Response.Headers.AccessControlAllowOrigin = origin;
                context.Response.Headers.Vary = "Origin";
            }
            await next();
        });
        app.MapGet("/", () => Results.Json(new
        {
            name = "Crimson Desert Telemetry",
            schemaVersion,
            endpoints = new[] { "/v1/health", "/v1/snapshot", "/v1/schema", "/v1/stream" }
        }, jsonOptions));
        app.MapGet("/v1/health", () => Results.Json(state.Health, jsonOptions));
        app.MapGet("/v1/snapshot", () => state.Latest is { } snapshot
            ? Results.Json(snapshot, jsonOptions)
            : Results.Json(state.Health, jsonOptions, statusCode: StatusCodes.Status503ServiceUnavailable));
        app.MapGet("/v1/schema", () => Results.File(
            Path.Combine(AppContext.BaseDirectory, "schema", "telemetry-v1.schema.json"),
            "application/schema+json"));
        app.Map("/v1/stream", context => StreamWebSocket(context, state, cancellation.Token));

        Console.Error.WriteLine($"Listening on http://127.0.0.1:{port} at {rateHz} Hz.");
        var samplingTask = Task.Run(() => SampleContinuously(state, rateHz, cancellation.Token),
            cancellation.Token);
        try
        {
            app.StartAsync(cancellation.Token).GetAwaiter().GetResult();
            app.WaitForShutdownAsync(cancellation.Token).GetAwaiter().GetResult();
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { }
        finally
        {
            cancellation.Cancel();
            try { app.StopAsync().GetAwaiter().GetResult(); }
            catch (OperationCanceledException) { }
            try { samplingTask.GetAwaiter().GetResult(); }
            catch (OperationCanceledException) { }
        }
        return 0;
    }
    finally
    {
        Console.CancelKeyPress -= cancelHandler;
    }
}

async Task StreamWebSocket(HttpContext context, TelemetryServerState state, CancellationToken cancellationToken)
{
    var origin = context.Request.Headers.Origin.ToString();
    if (!string.IsNullOrEmpty(origin) && !IsAllowedBrowserOrigin(origin))
    {
        context.Response.StatusCode = StatusCodes.Status403Forbidden;
        return;
    }
    if (!context.WebSockets.IsWebSocketRequest)
    {
        context.Response.StatusCode = StatusCodes.Status400BadRequest;
        await context.Response.WriteAsync("A WebSocket connection is required.", cancellationToken);
        return;
    }
    using var socket = await context.WebSockets.AcceptWebSocketAsync();
    using var subscription = state.Subscribe();
    using var connectionCancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
    try
    {
        var sendTask = SendSnapshots(socket, subscription, state.LatestBytes, connectionCancellation.Token);
        var receiveTask = WaitForWebSocketClose(socket, connectionCancellation.Token);
        await Task.WhenAny(sendTask, receiveTask);
        connectionCancellation.Cancel();
        try { await Task.WhenAll(sendTask, receiveTask); }
        catch (OperationCanceledException) { }
        if (socket.State is WebSocketState.Open or WebSocketState.CloseReceived)
            await socket.CloseAsync(WebSocketCloseStatus.NormalClosure, "Closing", CancellationToken.None);
    }
    catch (Exception exception) when (exception is OperationCanceledException or WebSocketException)
    {
        // Disconnects and server shutdown are normal for streaming clients.
    }
}

static bool IsAllowedBrowserOrigin(string origin) =>
    Uri.TryCreate(origin, UriKind.Absolute, out var uri) && uri.IsLoopback;

async Task SendSnapshots(WebSocket socket, TelemetryServerState.TelemetrySubscription subscription,
    byte[]? initialSnapshot, CancellationToken cancellationToken)
{
    if (initialSnapshot is not null)
        await socket.SendAsync(initialSnapshot, WebSocketMessageType.Text, true, cancellationToken);
    await foreach (var bytes in subscription.Reader.ReadAllAsync(cancellationToken))
    {
        if (socket.State != WebSocketState.Open) break;
        await socket.SendAsync(bytes, WebSocketMessageType.Text, true, cancellationToken);
    }
}

async Task WaitForWebSocketClose(WebSocket socket, CancellationToken cancellationToken)
{
    var buffer = new byte[256];
    while (socket.State == WebSocketState.Open)
    {
        var result = await socket.ReceiveAsync(buffer, cancellationToken);
        if (result.MessageType == WebSocketMessageType.Close) break;
        while (!result.EndOfMessage)
        {
            result = await socket.ReceiveAsync(buffer, cancellationToken);
        }
    }
}

async Task SampleContinuously(TelemetryServerState state, int rateHz, CancellationToken cancellationToken)
{
    long sequence = 0;
    while (!cancellationToken.IsCancellationRequested)
    {
        RuntimeContext? runtime = null;
        try
        {
            state.SetHealth("waiting-for-game", false, null, null, 0, null, null);
            try { runtime = OpenRuntime(); }
            catch (InvalidOperationException)
            {
                await Task.Delay(1000, cancellationToken);
                continue;
            }
            catch (InvalidDataException exception)
            {
                state.SetHealth("unsupported-build", true, false, null, 0, null, exception.Message);
                await Task.Delay(5000, cancellationToken);
                continue;
            }

            var initialPlayer = await WaitForPlayerPosition(runtime, state,
                () => ToUnavailableSnapshot(runtime.Definition.SteamBuildId, sequence++, "loading"),
                cancellationToken);
            if (initialPlayer is null) continue;
            var player = initialPlayer.Value;
            state.SetHealth("discovering", true, true, runtime.Definition.SteamBuildId, 0, null, null);
            var discoveryWatch = Stopwatch.StartNew();
            var discovered = RenderCameraConstantsScanner.Find(runtime.Reader, player);
            discoveryWatch.Stop();
            var tracker = new RenderCameraTracker(runtime.Reader, discovered);
            var discoveryMilliseconds = discoveryWatch.Elapsed.TotalMilliseconds;
            var tickTicks = Math.Max(1, Stopwatch.Frequency / rateHz);
            var nextTick = Stopwatch.GetTimestamp();

            while (!cancellationToken.IsCancellationRequested && !runtime.Process.HasExited)
            {
                try
                {
                    var captureWatch = Stopwatch.StartNew();
                    player = StaticPositionProbe.Read(runtime.Reader, runtime.PositionAddresses);
                    var frame = tracker.Capture(player);
                    captureWatch.Stop();
                    var snapshot = ToSnapshot(runtime.Definition.SteamBuildId, sequence++, player, frame,
                        captureWatch.ElapsedTicks * 1_000_000 / Stopwatch.Frequency);
                    state.Publish(snapshot, tracker.AddressCount, discoveryMilliseconds);
                }
                catch (Exception exception) when (exception is InvalidDataException or Win32Exception)
                {
                    if (state.Health.Status != "loading")
                        state.Publish(ToUnavailableSnapshot(runtime.Definition.SteamBuildId, sequence++, "loading"),
                            tracker.AddressCount, discoveryMilliseconds);
                    state.SetHealth("loading", true, true, runtime.Definition.SteamBuildId,
                        tracker.AddressCount, discoveryMilliseconds, exception.Message);
                }

                nextTick += tickTicks;
                var remaining = nextTick - Stopwatch.GetTimestamp();
                if (remaining > 0)
                    await Task.Delay(TimeSpan.FromSeconds((double)remaining / Stopwatch.Frequency), cancellationToken);
                else
                    nextTick = Stopwatch.GetTimestamp();
            }
            if (!cancellationToken.IsCancellationRequested && runtime.Process.HasExited)
                state.Publish(ToUnavailableSnapshot(runtime.Definition.SteamBuildId, sequence++, "stopped"),
                    tracker.AddressCount, discoveryMilliseconds);
        }
        catch (Exception exception) when (exception is InvalidDataException or Win32Exception or
                                           ArgumentException or InvalidOperationException)
        {
            state.SetHealth("error", runtime is not null, runtime is not null,
                runtime?.Definition.SteamBuildId, 0, null, exception.Message);
            await Task.Delay(2000, cancellationToken);
        }
        finally
        {
            runtime?.Dispose();
        }
    }
}

async Task<(float X, float Y, float Z)?> WaitForPlayerPosition(RuntimeContext runtime,
    TelemetryServerState state, Func<TelemetrySnapshot> createLoadingSnapshot,
    CancellationToken cancellationToken)
{
    while (!cancellationToken.IsCancellationRequested && !runtime.Process.HasExited)
    {
        try
        {
            return StaticPositionProbe.Read(runtime.Reader, runtime.PositionAddresses);
        }
        catch (Exception exception) when (exception is InvalidDataException or Win32Exception)
        {
            if (state.Health.Status != "loading")
                state.Publish(createLoadingSnapshot(), 0, null);
            state.SetHealth("loading", true, true, runtime.Definition.SteamBuildId,
                0, null, exception.Message);
            await Task.Delay(250, cancellationToken);
        }
    }
    return null;
}

RuntimeContext OpenRuntime()
{
    var process = GameDiscovery.FindRunningProcess()
        ?? throw new InvalidOperationException("Crimson Desert is not running.");
    try
    {
        var executable = process.MainModule?.FileName
            ?? throw new InvalidOperationException("The Crimson Desert executable path is unavailable.");
        var hash = GameDiscovery.ComputeSha256(executable);
        var definition = FindDefinition(hash)
            ?? throw new InvalidDataException($"Unsupported Crimson Desert build (SHA-256 {hash}).");
        var addresses = StaticPositionProbe.Resolve(process, executable, definition);
        return new RuntimeContext(process, new ReadOnlyProcess(process), definition, addresses);
    }
    catch
    {
        process.Dispose();
        throw;
    }
}

BuildDefinition? FindDefinition(string hash) =>
    BuildDefinition.LoadAll(Path.Combine(AppContext.BaseDirectory, "definitions"))
        .SingleOrDefault(candidate =>
            string.Equals(candidate.ExecutableSha256, hash, StringComparison.OrdinalIgnoreCase));

CameraSnapshot ToCameraConsensus(RenderCameraConsensus consensus)
{
    var camera = consensus.Camera;
    return new CameraSnapshot(camera.Position, camera.Up, camera.Right, camera.Forward,
        camera.NearPlane, camera.FarPlane == float.MaxValue ? null : camera.FarPlane,
        camera.FieldOfViewRadians * 180f / MathF.PI, camera.AspectRatio);
}

TelemetrySnapshot ToSnapshot(string build, long sequence, (float X, float Y, float Z) player,
    RenderCameraFrame frame, long captureDurationMicroseconds) => new(
    schemaVersion,
    sequence,
    frame.Timestamp,
    new GameSnapshot(build, "playing"),
    coordinateSystem,
    capabilities,
    new PlayerSnapshot(ToVector(player)),
    ToCameraConsensus(new RenderCameraConsensus(
        frame.Camera, frame.ConsensusCopies, frame.ValidCopies, frame.DistinctStates)),
    new QualitySnapshot(frame.ConsensusCopies, frame.ValidCopies, frame.DistinctStates,
        frame.Rediscovered, captureDurationMicroseconds));

TelemetrySnapshot ToUnavailableSnapshot(string build, long sequence, string state) => new(
    schemaVersion,
    sequence,
    DateTimeOffset.UtcNow,
    new GameSnapshot(build, state),
    coordinateSystem,
    capabilities,
    null,
    null,
    null);

static CameraVector3 ToVector((float X, float Y, float Z) value) => new(value.X, value.Y, value.Z);

int Help()
{
    Console.WriteLine("Crimson Desert Telemetry");
    Console.WriteLine("  diagnose                 Check the running game and build support");
    Console.WriteLine("  discover                 Discover and summarize camera copies");
    Console.WriteLine("  snapshot                 Emit one JSON telemetry snapshot");
    Console.WriteLine("  track [samples] [hz]     Emit JSON Lines at 1-240 Hz (0 samples = unlimited)");
    Console.WriteLine("  serve [port] [hz]        Serve HTTP and WebSocket telemetry (default 27311, 60 Hz)");
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

sealed class WindowsTimerResolution : IDisposable
{
    private readonly uint _period;

    private WindowsTimerResolution(uint period)
    {
        _period = period;
        if (_period != 0 && TimeBeginPeriod(_period) != 0)
            throw new InvalidOperationException("Windows did not grant the requested timer resolution.");
    }

    public static WindowsTimerResolution RequestFor(int rateHz) => new(rateHz > 120 ? 1u : 0u);

    public void Dispose()
    {
        if (_period != 0) _ = TimeEndPeriod(_period);
    }

    [DllImport("winmm.dll", EntryPoint = "timeBeginPeriod")]
    private static extern uint TimeBeginPeriod(uint period);

    [DllImport("winmm.dll", EntryPoint = "timeEndPeriod")]
    private static extern uint TimeEndPeriod(uint period);
}
