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
    QualitySnapshot? Quality);

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
