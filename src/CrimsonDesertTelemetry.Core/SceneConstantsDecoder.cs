using System.Numerics;

namespace CrimsonDesertTelemetry.Core;

/// <summary>
/// Measured SceneConstantBuffer layout for build 25116796. Shared by the direct
/// CPU camera source and the native bridge's same-event ManyLights capture.
/// These offsets are a guarded layout, not a promise about future game builds.
/// </summary>
public static class SceneConstantsDecoder
{
    public const int SourceLength = 0xB00;
    public const int FrameNumberOffset = 0x20;

    public static SceneConstantsSnapshot Decode(byte[] bytes, ulong address = 0,
        (float X, float Y, float Z)? player = null)
    {
        if (bytes.Length != SourceLength)
            throw new InvalidDataException("Scene constants must contain exactly 2816 bytes.");
        float F(int offset) => BitConverter.ToSingle(bytes, offset);
        Vector3 V(int offset) => new(F(offset), F(offset + 4), F(offset + 8));

        var width = F(0x30);
        var height = F(0x34);
        if (!ScreenDimension(width, F(0x38)) || !ScreenDimension(height, F(0x3C)) ||
            F(0xAC0) != 6360000f)
            throw new InvalidDataException("Scene constants resolution or layout signature is invalid.");

        // This directly identified renderer object also works without a player
        // anchor (for example a scripted camera). The player, if supplied, only
        // supplies the legacy distance diagnostic; it does not select the source.
        var camera = EngineCameraReader.DecodeCamera(bytes, address, player, requirePlayerProximity: false);
        var position = V(0x80);
        var right = new Vector3(camera.Right.X, camera.Right.Y, camera.Right.Z);
        var up = new Vector3(camera.Up.X, camera.Up.Y, camera.Up.Z);
        var forward = new Vector3(camera.Forward.X, camera.Forward.Y, camera.Forward.Z);

        // HLSL column-major memory corresponds to these System.Numerics row-vector
        // coefficients. _view translates world position into camera coordinates;
        // _viewRelative has the same rotation and zero translation. In particular,
        // ManyLights adds _viewPos, never _renderingOriginPos, to relative positions.
        for (var i = 0; i < 16; i++)
            if (!float.IsFinite(F(0x3E0 + i * 4)) || !float.IsFinite(F(0x420 + i * 4)))
                throw new InvalidDataException("Scene view matrix is non-finite.");
        var worldTranslation = new Vector3(F(0x410), F(0x414), F(0x418));
        var expectedTranslation = -new Vector3(Vector3.Dot(position, right),
            Vector3.Dot(position, up), Vector3.Dot(position, forward));
        if (Vector3.Distance(worldTranslation, expectedTranslation) > .05f ||
            Vector3.Distance(new Vector3(F(0x3E8), F(0x3F8), F(0x408)), forward) > .001f ||
            Math.Abs(F(0x3EC)) > .0001f || Math.Abs(F(0x3FC)) > .0001f || Math.Abs(F(0x40C)) > .0001f ||
            Vector3.Distance(V(0x450), Vector3.Zero) > .001f ||
            Math.Abs(F(0x41C) - 1) > .0001f || Math.Abs(F(0x45C) - 1) > .0001f)
            throw new InvalidDataException("Scene world and relative view matrices disagree with the camera.");
        for (var i = 0; i < 12; i++)
            if (Math.Abs(F(0x3E0 + i * 4) - F(0x420 + i * 4)) > .0001f)
                throw new InvalidDataException("Scene world and relative rotations disagree.");

        return new SceneConstantsSnapshot(BitConverter.ToUInt32(bytes, FrameNumberOffset), camera,
            checked((int)width), checked((int)height));
    }

    private static bool ScreenDimension(float size, float reciprocal) =>
        float.IsFinite(size) && size is >= 1 and <= 32768 && size == MathF.Truncate(size) &&
        float.IsFinite(reciprocal) && Math.Abs(reciprocal * size - 1) <= .00001f;
}

public sealed record SceneConstantsSnapshot(uint FrameNumber, RenderCameraConstantsCandidate Camera,
    int ScreenWidth, int ScreenHeight);
