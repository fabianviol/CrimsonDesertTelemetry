using System.Diagnostics;
using System.Numerics;
using System.Text;

namespace CrimsonDesertTelemetry.Core;

public readonly record struct PlayerOrientationAddresses(ulong WorldSystemGlobalAddress);

/// <summary>
/// Reads the validated player physics-root basis for the supported build.
/// A failed or changing chain returns no orientation; it never publishes a stale value.
/// </summary>
public sealed class PlayerOrientationReader
{
    private readonly PlayerOrientationAddresses _addresses;
    private readonly PlayerRootDefinition _definition;

    private PlayerOrientationReader(PlayerOrientationAddresses addresses, PlayerRootDefinition definition)
    {
        _addresses = addresses;
        _definition = definition;
    }

    public static PlayerOrientationReader? Resolve(Process process, string executable, BuildDefinition definition)
    {
        var root = definition.PlayerRoot;
        if (root is null || root.WorldSystemPattern.Purpose != "world-system") return null;
        if (root.WorldSystemPattern.Confidence != "locally-validated")
            throw new InvalidDataException("Player orientation is not validated for this build.");
        var moduleBase = checked((ulong)process.MainModule!.BaseAddress.ToInt64());
        var relative = StaticPositionProbe.ResolveUniqueRipTarget(executable, root.WorldSystemPattern);
        return new PlayerOrientationReader(new PlayerOrientationAddresses(checked(moduleBase + relative)), root);
    }

    public PlayerOrientationSnapshot? Read(IReadOnlyProcessMemory reader, (float X, float Y, float Z) worldPosition)
    {
        try
        {
            var first = ReadChain(reader);
            var transform = ReadTransform(reader, first.PhysicsAddress, worldPosition);
            // The game can replace the actor/physics object while a sample is in flight.
            // Confirm the same leaf before publishing the basis.
            var second = ReadChain(reader);
            if (second.PhysicsAddress != first.PhysicsAddress) return null;
            return ToSnapshot(transform);
        }
        catch (Exception exception) when (exception is InvalidDataException or OverflowException or
            System.ComponentModel.Win32Exception)
        {
            return null;
        }
    }

    private ChainResult ReadChain(IReadOnlyProcessMemory reader)
    {
        var current = ReadPointer(reader, _addresses.WorldSystemGlobalAddress);
        var manager = ReadNext(reader, current, _definition.WorldSystemToActorManagerOffset);
        CheckType(reader, manager, 0);
        var actor = ReadNext(reader, manager, _definition.ActorManagerToPlayerActorOffset);
        CheckType(reader, actor, 1);
        var intermediate = ReadNext(reader, actor, _definition.PlayerActorToIntermediateOffset);
        var control = ReadNext(reader, intermediate, _definition.IntermediateToControlOffset);
        CheckType(reader, control, 2);
        var owner = ReadNext(reader, control, _definition.ControlToOwnerOffset);
        var physics = ReadNext(reader, owner, _definition.OwnerToPhysicsOffset);
        return new ChainResult(physics);
    }

    private TransformResult ReadTransform(IReadOnlyProcessMemory reader, ulong physics,
        (float X, float Y, float Z) worldPosition)
    {
        var length = Math.Max(_definition.PositionOffset + 0x10,
            Math.Max(_definition.BasisZOffset + 0x0C, _definition.BasisYOffset + 0x0C));
        var bytes = reader.Read(ToIntPtr(physics), length);
        var x = Vector(bytes, _definition.BasisXOffset);
        var y = Vector(bytes, _definition.BasisYOffset);
        var z = Vector(bytes, _definition.BasisZOffset);
        var position = Vector(bytes, _definition.PositionOffset);
        if (!Finite(x) || !Finite(y) || !Finite(z) || !Finite(position))
            throw new InvalidDataException("Player physics basis is not finite.");
        if (Math.Abs(x.LengthSquared() - 1) > .002f || Math.Abs(y.LengthSquared() - 1) > .002f ||
            Math.Abs(z.LengthSquared() - 1) > .002f || Math.Abs(Vector3.Dot(x, y)) > .002f ||
            Math.Abs(Vector3.Dot(x, z)) > .002f || Math.Abs(Vector3.Dot(y, z)) > .002f)
            throw new InvalidDataException("Player physics basis is not orthonormal.");
        var determinant = Vector3.Dot(Vector3.Cross(x, y), z);
        if (Math.Abs(determinant - 1) > .003f)
            throw new InvalidDataException("Player physics basis has an invalid handedness.");
        var delta = new Vector3(worldPosition.X, worldPosition.Y, worldPosition.Z) - position;
        if (!Finite(delta) || Math.Abs(delta.Y) > 3 || Math.Abs(delta.X) > 1_000_000 || Math.Abs(delta.Z) > 1_000_000 ||
            Math.Abs(delta.X - MathF.Round(delta.X / 1000) * 1000) > .15f ||
            Math.Abs(delta.Z - MathF.Round(delta.Z / 1000) * 1000) > .15f)
            throw new InvalidDataException("Player physics root does not match the player position.");
        return new TransformResult(-z, y);
    }

    private PlayerOrientationSnapshot ToSnapshot(TransformResult transform)
    {
        var forward = transform.Forward;
        var horizontalLength = MathF.Sqrt(forward.X * forward.X + forward.Z * forward.Z);
        float? heading = horizontalLength < .1f
            ? null
            : NormalizeDegrees(MathF.Atan2(forward.X, forward.Z) * 180 / MathF.PI);
        return new PlayerOrientationSnapshot("player-physics-root", ToVector(forward),
            ToVector(transform.Up), heading);
    }

    private void CheckType(IReadOnlyProcessMemory reader, ulong address, int expectedIndex)
    {
        if (_definition.ExpectedTypeNames.Count <= expectedIndex) return;
        var actual = TypeName(reader, address);
        if (!string.Equals(actual, _definition.ExpectedTypeNames[expectedIndex], StringComparison.Ordinal))
            throw new InvalidDataException("Player object chain has an unexpected RTTI type.");
    }

    private static ulong ReadNext(IReadOnlyProcessMemory reader, ulong address, int offset) =>
        ReadPointer(reader, checked(address + checked((ulong)offset)));

    private static ulong ReadPointer(IReadOnlyProcessMemory reader, ulong address) =>
        BitConverter.ToUInt64(reader.Read(ToIntPtr(address), sizeof(ulong)));

    private static string? TypeName(IReadOnlyProcessMemory reader, ulong address)
    {
        var vtable = ReadPointer(reader, address);
        if (vtable < 8) return null;
        var locator = ReadPointer(reader, vtable - 8);
        var bytes = reader.Read(ToIntPtr(locator), 24);
        if (BitConverter.ToUInt32(bytes, 0) != 1) return null;
        var imageBase = checked(locator - BitConverter.ToUInt32(bytes, 20));
        var nameAddress = checked(imageBase + BitConverter.ToUInt32(bytes, 12) + 16);
        var name = Encoding.ASCII.GetString(reader.Read(ToIntPtr(nameAddress), 160));
        var end = name.IndexOf('\0');
        return end > 0 ? name[..end] : null;
    }

    private static Vector3 Vector(byte[] bytes, int offset) => new(
        BitConverter.ToSingle(bytes, offset), BitConverter.ToSingle(bytes, offset + 4),
        BitConverter.ToSingle(bytes, offset + 8));

    private static bool Finite(Vector3 value) => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);
    private static CameraVector3 ToVector(Vector3 value) => new(value.X, value.Y, value.Z);
    private static float NormalizeDegrees(float value) => (value % 360 + 360) % 360;

    private static IntPtr ToIntPtr(ulong address)
    {
        if (address is < 0x10000 or > 0x00007FFFFFFFFFFF)
            throw new InvalidDataException($"Implausible player pointer 0x{address:X}.");
        return new IntPtr(unchecked((long)address));
    }

    private readonly record struct ChainResult(ulong PhysicsAddress);
    private readonly record struct TransformResult(Vector3 Forward, Vector3 Up);
}
