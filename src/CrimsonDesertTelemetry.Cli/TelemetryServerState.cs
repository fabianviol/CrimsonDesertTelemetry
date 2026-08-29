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
    string? Error);

internal sealed class TelemetryServerState(JsonSerializerOptions jsonOptions, int sampleRateHz)
{
    private readonly object _gate = new();
    private readonly ConcurrentDictionary<Guid, Channel<byte[]>> _subscribers = new();
    private TelemetrySnapshot? _latest;
    private byte[]? _latestBytes;
    private TelemetryHealth _health = new("1.0", "waiting-for-game", false, null, null,
        sampleRateHz, null, null, 0, 0, null, null);

    public TelemetrySnapshot? Latest
    {
        get { lock (_gate) return _latest; }
    }

    public byte[]? LatestBytes
    {
        get { lock (_gate) return _latestBytes; }
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
                Error = error
            };
        }
    }

    public void Publish(TelemetrySnapshot snapshot, int discoveredCopies, double? discoveryMilliseconds)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(snapshot, jsonOptions);
        var gameRunning = snapshot.Game.State != "stopped";
        lock (_gate)
        {
            _latest = snapshot;
            _latestBytes = bytes;
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
        }
        foreach (var channel in _subscribers.Values) channel.Writer.TryWrite(bytes);
    }

    public TelemetrySubscription Subscribe()
    {
        var id = Guid.NewGuid();
        var channel = Channel.CreateBounded<byte[]>(new BoundedChannelOptions(1)
        {
            SingleReader = true,
            SingleWriter = false,
            FullMode = BoundedChannelFullMode.DropOldest
        });
        if (!_subscribers.TryAdd(id, channel)) throw new InvalidOperationException("Could not add subscriber.");
        return new TelemetrySubscription(id, channel.Reader, this);
    }

    private void Unsubscribe(Guid id)
    {
        if (_subscribers.TryRemove(id, out var channel)) channel.Writer.TryComplete();
    }

    internal sealed class TelemetrySubscription(
        Guid id, ChannelReader<byte[]> reader, TelemetryServerState owner) : IDisposable
    {
        public ChannelReader<byte[]> Reader { get; } = reader;
        public void Dispose() => owner.Unsubscribe(id);
    }
}
