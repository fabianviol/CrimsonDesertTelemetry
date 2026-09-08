using System.Collections.Concurrent;
using System.Text.Json;
using System.Threading.Channels;
using CrimsonDesertTelemetry.Core;

namespace CrimsonDesertTelemetry.Cli;

internal sealed record TelemetryHealth(
    string SchemaVersion,
    string Status,
    bool GameRunning,
    bool? SupportedBuild,
    string? GameBuild,
    int SampleRateHz,
    long? LastSequence,
    DateTimeOffset? LastCapture,
    int ConnectedClients,
    int DiscoveredCopies,
    double? DiscoveryMilliseconds,
    string? Error,
    CompatibilityInfo? Compatibility = null);

internal sealed class TelemetryServerState(JsonSerializerOptions jsonOptions, int sampleRateHz, string schemaVersion,
    LightSmoothingOptions? smoothingOptions = null)
{
    private readonly object _gate = new();
    private readonly ConcurrentDictionary<Guid, (Channel<byte[]> Channel, int Feed)> _subscribers = new();
    private SkyAmbientSnapshot _sky = SkyAmbientReader.Unavailable("waiting-for-game");
    private readonly SmoothedLightProcessor _smoother = new(smoothingOptions);
    private SmoothedLightsSnapshot? _smoothed;
    private TelemetrySnapshot? _latest;
    private byte[]? _latestBytes;
    private TelemetryHealth _health = new(schemaVersion, "waiting-for-game", false, null, null,
        sampleRateHz, null, null, 0, 0, null, null);

    public TelemetrySnapshot? Latest
    {
        get { lock (_gate) return _latest; }
    }

    public byte[]? LatestBytes
    {
        get { lock (_gate) return _latestBytes; }
    }

    public SmoothedLightsSnapshot LatestSmoothed
    {
        get
        {
            lock (_gate)
            {
                var now = DateTimeOffset.UtcNow;
                if (_smoothed?.CapturedAt is { } captured)
                {
                    var age = (now - captured).TotalMilliseconds;
                    if (age is < 0 or > RenderLightReader.MaximumAgeMilliseconds)
                        _smoothed = _smoother.Unavailable("source-stale", now);
                    else _smoothed = _smoothed with { AgeMilliseconds = Math.Max(_smoothed.AgeMilliseconds ?? 0, (long)age) };
                }
                return _smoothed ??= _smoother.Unavailable("waiting-for-game", now);
            }
        }
    }
    public byte[] LatestSmoothedBytes => JsonSerializer.SerializeToUtf8Bytes(LatestSmoothed, jsonOptions);

    public SkyAmbientSnapshot LatestSky
    {
        get
        {
            lock (_gate)
            {
                if (_sky.CapturedAt is { } captured)
                {
                    var age = (DateTimeOffset.UtcNow - captured).TotalMilliseconds;
                    _sky = age is < 0 or > SkyAmbientReader.MaximumAgeMilliseconds
                        ? SkyAmbientReader.Unavailable("source-stale")
                        : _sky with { AgeMilliseconds = Math.Max(_sky.AgeMilliseconds ?? 0, (long)age) };
                }
                return _sky;
            }
        }
    }
    public byte[] LatestSkyBytes => JsonSerializer.SerializeToUtf8Bytes(LatestSky, jsonOptions);
    public void PublishSky(SkyAmbientSnapshot snapshot)
    {
        lock (_gate)
        {
            _sky = _health.Status == "playing" ? snapshot : SkyAmbientReader.Unavailable(_health.Status);
            SendFeed(JsonSerializer.SerializeToUtf8Bytes(_sky, jsonOptions), 2);
        }
    }

    public TelemetryHealth Health
    {
        get
        {
            lock (_gate) return _health with { ConnectedClients = _subscribers.Count };
        }
    }

    public void SetHealth(string status, bool gameRunning, bool? supportedBuild, string? gameBuild,
        int discoveredCopies, double? discoveryMilliseconds, string? error)
    {
        lock (_gate)
        {
            _health = _health with
            {
                Status = status,
                GameRunning = gameRunning,
                SupportedBuild = supportedBuild,
                GameBuild = gameBuild,
                DiscoveredCopies = discoveredCopies,
                DiscoveryMilliseconds = discoveryMilliseconds,
                Error = error,
                Compatibility = !gameRunning || supportedBuild == false ? null : _health.Compatibility
            };
            if (status != "playing")
            {
                _smoothed = _smoother.Unavailable(status, DateTimeOffset.UtcNow);
                Send(JsonSerializer.SerializeToUtf8Bytes(_smoothed, jsonOptions), true);
                _sky = SkyAmbientReader.Unavailable(status);
                SendFeed(JsonSerializer.SerializeToUtf8Bytes(_sky, jsonOptions), 2);
            }
        }
    }

    public void SetCompatibility(CompatibilityInfo compatibility)
    {
        lock (_gate) _health = _health with { Compatibility = compatibility };
    }

    public void Publish(TelemetrySnapshot snapshot, int discoveredCopies, double? discoveryMilliseconds)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(snapshot, jsonOptions);
        var gameRunning = snapshot.Game.State != "stopped";
        lock (_gate)
        {
            _latest = snapshot;
            _latestBytes = bytes;
            _smoothed = snapshot.Game.State == "playing"
                ? _smoother.Process(snapshot.Lights?.Rendered, DateTimeOffset.UtcNow)
                : _smoother.Unavailable(snapshot.Game.State, DateTimeOffset.UtcNow);
            _health = _health with
            {
                Status = snapshot.Game.State,
                GameRunning = gameRunning,
                SupportedBuild = true,
                GameBuild = snapshot.Game.Build,
                LastSequence = snapshot.Sequence,
                LastCapture = snapshot.CapturedAt,
                DiscoveredCopies = discoveredCopies,
                DiscoveryMilliseconds = discoveryMilliseconds,
                Error = null
            };
            Send(JsonSerializer.SerializeToUtf8Bytes(_smoothed, jsonOptions), true);
            if (snapshot.Game.State != "playing")
            {
                _sky = SkyAmbientReader.Unavailable(snapshot.Game.State);
                SendFeed(JsonSerializer.SerializeToUtf8Bytes(_sky, jsonOptions), 2);
            }
        }
        Send(bytes, false);
    }

    private void Send(byte[] bytes, bool smoothed)
        => SendFeed(bytes, smoothed ? 1 : 0);

    private void SendFeed(byte[] bytes, int feed)
    {
        foreach (var entry in _subscribers.Values)
            if (entry.Feed == feed) entry.Channel.Writer.TryWrite(bytes);
    }

    public TelemetrySubscription Subscribe(bool smoothed = false)
        => SubscribeFeed(smoothed ? 1 : 0);
    public TelemetrySubscription SubscribeSky() => SubscribeFeed(2);
    private TelemetrySubscription SubscribeFeed(int feed)
    {
        var id = Guid.NewGuid();
        var channel = Channel.CreateBounded<byte[]>(new BoundedChannelOptions(1)
        {
            SingleReader = true,
            SingleWriter = false,
            FullMode = BoundedChannelFullMode.DropOldest
        });
        if (!_subscribers.TryAdd(id, (channel, feed))) throw new InvalidOperationException("Could not add subscriber.");
        return new TelemetrySubscription(id, channel.Reader, this);
    }

    private void Unsubscribe(Guid id)
    {
        if (_subscribers.TryRemove(id, out var entry)) entry.Channel.Writer.TryComplete();
    }

    internal sealed class TelemetrySubscription(
        Guid id, ChannelReader<byte[]> reader, TelemetryServerState owner) : IDisposable
    {
        public ChannelReader<byte[]> Reader { get; } = reader;
        public void Dispose() => owner.Unsubscribe(id);
    }
}
