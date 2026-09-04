using System.Text.Json.Serialization;

namespace CrimsonDesertTelemetry.Core;

public sealed record TelemetrySnapshot(
    string SchemaVersion,
    long Sequence,
    DateTimeOffset CapturedAt,
    GameSnapshot Game,
    CoordinateSystemSnapshot CoordinateSystem,
    IReadOnlyList<string> Capabilities,
    PlayerSnapshot? Player,
    CameraSnapshot? Camera,
    QualitySnapshot? Quality,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    EngineLightsSnapshot? Lights = null);

public sealed record GameSnapshot(string Build, string State);

public sealed record CoordinateSystemSnapshot(string Unit, string Handedness, string UpAxis);

public sealed record PlayerSnapshot(CameraVector3 Position, PlayerOrientationSnapshot? Orientation = null);

public sealed record PlayerOrientationSnapshot(
    string Source,
    CameraVector3 Forward,
    CameraVector3 Up,
    float? HeadingDegrees);

public sealed record CameraSnapshot(
    CameraVector3 Position,
    CameraVector3 Up,
    CameraVector3 Right,
    CameraVector3 Forward,
    float NearPlane,
    float? FarPlane,
    float VerticalFovDegrees,
    float AspectRatio);

public sealed record QualitySnapshot(
    int ConsensusCopies,
    int ValidCopies,
    int DistinctStates,
    bool Rediscovered,
    long CaptureDurationMicroseconds);

public sealed record EngineLightsSnapshot(
    string Status,
    string Source,
    float NearbyRadius,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    IReadOnlyList<EngineLightSnapshot>? Sources,
    EngineLightDiagnosticsSnapshot Diagnostics,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    string? UnavailableReason = null);

public sealed record EngineLightSnapshot(
    CameraVector3 Position,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    string? Kind,
    CameraVector3 ColorLinear,
    bool RecordActive,
    bool RendererSelected,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    float? RendererScale,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    CameraVector3? RendererRgbLinear);

public sealed record EngineLightDiagnosticsSnapshot(
    int SourceRecords,
    int PublishedRecords,
    int Malformed,
    int OutsideRadius,
    int UnsupportedKind,
    int RendererDataUnavailable,
    int NonPositiveRendererScale,
    long WalkChanged,
    long WalkRetrySucceeded,
    long WalkUnavailable);
