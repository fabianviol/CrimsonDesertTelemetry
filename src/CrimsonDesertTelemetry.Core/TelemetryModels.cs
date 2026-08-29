namespace CrimsonDesertTelemetry.Core;

public sealed record TelemetrySnapshot(
    string SchemaVersion,
    string GameBuild,
    long Sequence,
    DateTimeOffset Timestamp,
    PlayerSnapshot Player,
    CameraSnapshot Camera);

public sealed record PlayerSnapshot(CameraVector3 Position);

public sealed record CameraSnapshot(
    CameraVector3 Position,
    CameraVector3 Up,
    CameraVector3 Right,
    CameraVector3 Forward,
    float NearPlane,
    float FarPlane,
    float FieldOfViewDegrees,
    float AspectRatio,
    int ConsensusCopies,
    int ValidCopies,
    int DistinctStates,
    bool Rediscovered);
