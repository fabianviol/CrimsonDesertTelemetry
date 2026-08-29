using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace CrimsonDesertTelemetry.Core;

public interface IReadOnlyProcessMemory
{
    byte[] Read(IntPtr address, int length);
    float ReadSingle(ulong address);
    IReadOnlyList<MemoryRegion> GetWritableRegions();
}

public sealed class ReadOnlyProcess : IReadOnlyProcessMemory, IDisposable
{
    private const uint ProcessVmRead = 0x0010;
    private const uint ProcessQueryLimitedInformation = 0x1000;
    private readonly SafeFileHandle _handle;

    public ReadOnlyProcess(Process process)
    {
        _handle = OpenProcess(ProcessVmRead | ProcessQueryLimitedInformation, false, process.Id);
        if (_handle.IsInvalid)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not open the game process for read-only access.");
        }
    }

    public byte[] Read(IntPtr address, int length)
    {
        if (length is <= 0 or > 64 * 1024 * 1024) throw new ArgumentOutOfRangeException(nameof(length));
        var buffer = new byte[length];
        if (!ReadProcessMemory(_handle, address, buffer, (nuint)buffer.Length, out var read) || read != (nuint)length)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(),
                $"Could not read process memory at 0x{address.ToInt64():X}.");
        }
        return buffer;
    }

    public ulong ReadPointer(ulong address) => BitConverter.ToUInt64(Read(ToIntPtr(address), sizeof(ulong)));
    public float ReadSingle(ulong address) => BitConverter.ToSingle(Read(ToIntPtr(address), sizeof(float)));

    public IReadOnlyList<MemoryRegion> GetWritablePrivateRegions()
        => GetRegions(writableOnly: true, privateOnly: true);

    public IReadOnlyList<MemoryRegion> GetWritableRegions()
        => GetRegions(writableOnly: true, privateOnly: false);

    public MemoryRegionInfo QueryRegion(ulong address)
    {
        var queried = VirtualQueryEx(_handle, ToIntPtr(address), out var information,
            (nuint)Marshal.SizeOf<MemoryBasicInformation>());
        if (queried == 0)
            throw new Win32Exception(Marshal.GetLastWin32Error(), $"Could not query memory region at 0x{address:X}.");
        return new MemoryRegionInfo(
            checked((ulong)information.BaseAddress.ToInt64()),
            checked((ulong)information.AllocationBase.ToInt64()),
            (ulong)information.RegionSize,
            information.State,
            information.Protect,
            information.Type);
    }

    private IReadOnlyList<MemoryRegion> GetRegions(bool writableOnly, bool privateOnly)
    {
        const uint memCommit = 0x1000;
        const uint memPrivate = 0x20000;
        const uint pageGuard = 0x100;
        const uint pageNoAccess = 0x01;
        var result = new List<MemoryRegion>();
        ulong address = 0x10000;
        const ulong maximumAddress = 0x00007FFFFFFF0000;
        while (address < maximumAddress)
        {
            var queried = VirtualQueryEx(_handle, ToIntPtr(address), out var information,
                (nuint)Marshal.SizeOf<MemoryBasicInformation>());
            if (queried == 0) break;
            var regionBase = checked((ulong)information.BaseAddress.ToInt64());
            var regionSize = (ulong)information.RegionSize;
            if (information.State == memCommit && (!privateOnly || information.Type == memPrivate) &&
                (information.Protect & (pageGuard | pageNoAccess)) == 0 &&
                (!writableOnly || IsWritable(information.Protect)) && regionSize > 0)
            {
                result.Add(new MemoryRegion(regionBase, regionSize));
            }
            var next = regionBase + regionSize;
            if (next <= address) break;
            address = next;
        }
        return result;
    }

    private static bool IsWritable(uint protection)
    {
        var basic = protection & 0xFF;
        return basic is 0x04 or 0x08 or 0x40 or 0x80;
    }

    private static IntPtr ToIntPtr(ulong address)
    {
        if (address is < 0x10000 or > 0x00007FFFFFFFFFFF)
        {
            throw new InvalidDataException($"Implausible user-mode address 0x{address:X}.");
        }
        return new IntPtr(unchecked((long)address));
    }

    public void Dispose() => _handle.Dispose();

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern SafeFileHandle OpenProcess(uint desiredAccess, bool inheritHandle, int processId);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool ReadProcessMemory(SafeFileHandle process, IntPtr baseAddress, byte[] buffer,
        nuint size, out nuint bytesRead);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern nuint VirtualQueryEx(SafeFileHandle process, IntPtr address,
        out MemoryBasicInformation information, nuint length);

    [StructLayout(LayoutKind.Sequential)]
    private struct MemoryBasicInformation
    {
        public IntPtr BaseAddress;
        public IntPtr AllocationBase;
        public uint AllocationProtect;
        public nuint RegionSize;
        public uint State;
        public uint Protect;
        public uint Type;
    }
}

public readonly record struct MemoryRegion(ulong BaseAddress, ulong Size);
public readonly record struct MemoryRegionInfo(
    ulong BaseAddress, ulong AllocationBase, ulong Size, uint State, uint Protect, uint Type);
