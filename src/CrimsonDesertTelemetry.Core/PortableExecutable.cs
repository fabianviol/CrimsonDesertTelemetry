using System.Text;

namespace CrimsonDesertTelemetry.Core;

public sealed record PeSection(string Name, uint VirtualAddress, byte[] Bytes);
public sealed record PeSectionInfo(string Name, uint VirtualAddress, uint RawSize, uint RawOffset, uint Characteristics,
    uint VirtualSize = 0)
{
    public bool Executable => (Characteristics & 0x20000000) != 0;
    public bool Writable => (Characteristics & 0x80000000) != 0;
    public bool Readable => (Characteristics & 0x40000000) != 0;
}

public static class PortableExecutable
{
    public static PeSection ReadSection(string path, string requestedName)
    {
        var info = ReadSectionHeaders(path).SingleOrDefault(section => section.Name == requestedName)
            ?? throw new InvalidDataException($"PE section '{requestedName}' is missing.");
        return ReadSection(path, info);
    }

    public static PeSection ReadSection(string path, PeSectionInfo info)
    {
        using var stream = File.OpenRead(path);
        using var reader = new BinaryReader(stream, Encoding.ASCII, leaveOpen: true);
        if ((ulong)info.RawOffset + info.RawSize > (ulong)stream.Length)
            throw new InvalidDataException("Truncated PE section.");
        stream.Position = info.RawOffset;
        return new PeSection(info.Name, info.VirtualAddress, reader.ReadBytes(checked((int)info.RawSize)));
    }

    public static IReadOnlyList<PeSectionInfo> ReadSectionHeaders(string path)
    {
        using var stream = File.OpenRead(path);
        using var reader = new BinaryReader(stream, Encoding.ASCII, leaveOpen: true);
        if (reader.ReadUInt16() != 0x5A4D) throw new InvalidDataException("Invalid DOS header.");
        stream.Position = 0x3C;
        var peOffset = reader.ReadUInt32();
        stream.Position = peOffset;
        if (reader.ReadUInt32() != 0x00004550) throw new InvalidDataException("Invalid PE header.");
        _ = reader.ReadUInt16();
        var sectionCount = reader.ReadUInt16();
        stream.Position += 12;
        var optionalHeaderSize = reader.ReadUInt16();
        stream.Position += 2 + optionalHeaderSize;

        var result = new List<PeSectionInfo>(sectionCount);
        for (var index = 0; index < sectionCount; index++)
        {
            var name = Encoding.ASCII.GetString(reader.ReadBytes(8)).TrimEnd('\0');
            var virtualSize = reader.ReadUInt32();
            var virtualAddress = reader.ReadUInt32();
            var rawSize = reader.ReadUInt32();
            var rawOffset = reader.ReadUInt32();
            stream.Position += 12;
            var characteristics = reader.ReadUInt32();
            result.Add(new PeSectionInfo(name, virtualAddress, rawSize, rawOffset, characteristics, virtualSize));
        }
        return result;
    }
}
