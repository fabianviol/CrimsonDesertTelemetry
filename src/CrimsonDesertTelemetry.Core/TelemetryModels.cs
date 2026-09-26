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
    string? UnavailableReason = null,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    RenderLightsSnapshot? Rendered = null,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    UpstreamLightsSnapshot? Upstream = null);

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
    CameraVector3? RendererRgbLinear,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    CameraVector3? Direction = null,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    float? ConeHalfAngleDegrees = null,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    SourceVisibilitySnapshot? SourceVisibility = null);

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

public sealed record RenderLightsSnapshot(
    string Status,
    string Source,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] ulong? CaptureSequence,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] uint? FrameNumber,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] DateTimeOffset? CapturedAt,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] long? AgeMilliseconds,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] CameraSnapshot? Camera,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] IReadOnlyList<RenderedLightSnapshot>? Sources,
    RenderLightDiagnosticsSnapshot Diagnostics,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] string? UnavailableReason = null);

public sealed record RenderedLightSnapshot(
    int SampleIndex,
    CameraVector3 Position,
    CameraVector3 ColorLinear,
    float LuminanceLinear,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] string? Kind,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] CameraVector3? Direction,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] float? ConeHalfAngleDegrees,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] SourceVisibilitySnapshot? SourceVisibility = null);

/// <summary>
/// Geometric estimate. Method=physics-ray-fan records actual camera/capture provenance,
/// may be older than the rendered frame, and has no SDF volume. Its age is measurement
/// age at publication; counts are NOT transmission. Binary attenuation is retain/hide
/// policy only. Unknown never supplies a factor or removes original light data.
/// </summary>
public sealed record SourceVisibilitySnapshot(
    string Status, double? AttenuationFactor, string? Reason,
    CameraVector3 ReferencePosition, ulong LightCaptureSequence,
    ulong? VolumeSequence, uint? ContextFrame, long? VolumeAgeMillisecondsAtCapture,
    double? ClosestApproach,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] string? Method = null,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] int? SampleCount = null,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] int? ClearSampleCount = null,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] ulong? MeasurementSequence = null,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] long? MeasuredAtTickMilliseconds = null);

public sealed record RenderLightDiagnosticsSnapshot(
    int ActiveRecords, int PublishedRecords, int Malformed, int OutsideRadius);

/// <summary>
/// Current engine light records before the renderer's view selection, paired with the
/// filtered output of the SAME capture (sequence, frame, camera, fence). Includes sources
/// behind the camera. Not a persistent object registry: sample indices change per frame.
/// </summary>
public sealed record UpstreamLightsSnapshot(
    string Status,
    string Source,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] ulong? CaptureSequence,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] uint? FrameNumber,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] DateTimeOffset? CapturedAt,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] long? AgeMilliseconds,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] uint? InputRecords,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] IReadOnlyList<UpstreamLightSnapshot>? Sources,
    UpstreamLightDiagnosticsSnapshot Diagnostics,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] string? UnavailableReason = null);

/// <summary>
/// One current light: a standalone record or an engine group (e.g. a fire's flame particles).
/// A group position is the derived member mean, not the renderer's noisy per-frame choice.
/// RendererSelected means THIS capture's filtered output contains the same contribution.
/// </summary>
public sealed record UpstreamLightSnapshot(
    int SampleIndex,
    string Type,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] int? MemberCount,
    CameraVector3 Position,
    CameraVector3 ColorLinear,
    float LuminanceLinear,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] string? Kind,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] CameraVector3? Direction,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] float? ConeHalfAngleDegrees,
    bool RendererSelected,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] int? RenderedSampleIndex,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] SourceVisibilitySnapshot? SourceVisibility = null);

public sealed record UpstreamLightDiagnosticsSnapshot(
    int Groups, int GroupMembers, int Standalone, int GlobalRecords, int SkippedMemberRecords,
    int SpecialExcluded, int ZeroColor, int Malformed, int OutsideRadius, int PublishedRecords,
    int RendererSelected, int RenderedUnmatched);
