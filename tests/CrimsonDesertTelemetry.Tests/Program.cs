using CrimsonDesertTelemetry.Core;

var tests = new (string Name, Action Run)[]
{
    ("signature exact", SignatureExact),
    ("signature wildcard", SignatureWildcard),
    ("signature rejects invalid input", SignatureRejectsInvalidInput),
    ("camera constants", CameraConstants),
    ("camera rejects invalid basis", CameraRejectsInvalidBasis),
    ("camera rejects wrong sentinel", CameraRejectsWrongSentinel),
    ("camera rejects distant position", CameraRejectsDistantPosition),
    ("camera consensus", CameraConsensus),
    ("tracker rediscovers camera", TrackerRediscoversCamera)
};
var failures = 0;
foreach (var test in tests)
{
    try { test.Run(); Console.WriteLine($"PASS {test.Name}"); }
    catch (Exception exception) { failures++; Console.Error.WriteLine($"FAIL {test.Name}: {exception.Message}"); }
}
return failures == 0 ? 0 : 1;

static void SignatureExact() => Assert(
    SignaturePattern.Parse("48 8B 01 FF").FindAll([0, 0x48, 0x8B, 0x01, 0xFF, 0]).SequenceEqual([1]),
    "Exact signature mismatch.");

static void SignatureWildcard() => Assert(
    SignaturePattern.Parse("48 8B ?? FF").FindAll([0x48, 0x8B, 0xAC, 0xFF]).SequenceEqual([0]),
    "Wildcard signature mismatch.");

static void SignatureRejectsInvalidInput()
{
    AssertThrows<FormatException>(() => SignaturePattern.Parse("GG"), "Invalid hex was accepted.");
    AssertThrows<InvalidOperationException>(() => SignaturePattern.Parse("?? ??").FindAll([0, 0]),
        "All-wildcard signature was accepted.");
}

static void CameraConstants()
{
    var results = RenderCameraConstantsScanner.FindInBuffer(CameraBuffer(), 0x100000, (10f, 20f, 30f));
    Assert(results is [{ Address: 0x100028 }], "Camera constants were not uniquely recognized.");
    Assert(Math.Abs(results[0].FieldOfViewRadians - MathF.PI / 3) < 0.0001f, "Camera FOV mismatch.");
}

static void CameraRejectsInvalidBasis()
{
    var bytes = CameraBuffer();
    WriteVector(bytes, 0x40, 0, 1, 0);
    Assert(RenderCameraConstantsScanner.FindInBuffer(bytes, 0x100000, (10f, 20f, 30f)).Count == 0,
        "Non-orthogonal basis was accepted.");
}

static void CameraRejectsWrongSentinel()
{
    var bytes = CameraBuffer();
    WriteSingle(bytes, 0x24, 0);
    Assert(RenderCameraConstantsScanner.FindInBuffer(bytes, 0x100000, (10f, 20f, 30f)).Count == 0,
        "Invalid sentinel was accepted.");
}

static void CameraRejectsDistantPosition() => Assert(
    RenderCameraConstantsScanner.FindInBuffer(CameraBuffer(), 0x100000, (1000f, 2000f, 3000f)).Count == 0,
    "A camera far away from the player was accepted.");

static void CameraConsensus()
{
    RenderCameraConstantsCandidate Candidate(ulong address, float x) => new(address,
        new CameraVector3(x, 20, 30), new CameraVector3(0, 1, 0), new CameraVector3(1, 0, 0),
        new CameraVector3(0, 0, 1), 0.2f, float.MaxValue, MathF.PI / 3, 16f / 9f, 4);
    var consensus = RenderCameraConstantsScanner.SelectConsensus([
        Candidate(0x1000, 10.001f), Candidate(0x2000, 10.004f), Candidate(0x3000, 12f)
    ]);
    Assert(consensus is { CopyCount: 2, ValidCopyCount: 3, DistinctStateCount: 2 },
        "Camera consensus mismatch.");
}

static void TrackerRediscoversCamera()
{
    const ulong regionBase = 0x200000;
    var memory = new BufferMemory(regionBase, CameraBuffer());
    var stale = new RenderCameraConstantsCandidate(0x300028,
        new CameraVector3(10, 20, 30), new CameraVector3(0, 1, 0), new CameraVector3(1, 0, 0),
        new CameraVector3(0, 0, 1), 0.2f, float.MaxValue, MathF.PI / 3, 16f / 9f, 4);
    var tracker = new RenderCameraTracker(memory, [stale]);
    var frame = tracker.Capture((10f, 20f, 30f));
    Assert(frame.Rediscovered && tracker.RediscoveryCount == 1 && frame.Camera.Address == regionBase + 0x28,
        "Tracker did not rediscover the relocated camera record.");
}

static byte[] CameraBuffer()
{
    var bytes = new byte[0x100];
    const int sentinel = 0x20;
    WriteSingle(bytes, sentinel, float.MaxValue);
    WriteSingle(bytes, sentinel + 4, float.MaxValue);
    var record = sentinel + 8;
    WriteVector(bytes, record, 10, 20, 30);
    WriteVector(bytes, record + 0x0C, 0, 1, 0);
    WriteVector(bytes, record + 0x18, 1, 0, 0);
    WriteVector(bytes, record + 0x24, 0, 0, 1);
    WriteSingle(bytes, record + 0x30, 0.2f);
    WriteSingle(bytes, record + 0x34, float.MaxValue);
    WriteSingle(bytes, record + 0x38, MathF.PI / 3);
    WriteSingle(bytes, record + 0x3C, 16f / 9f);
    WriteSingle(bytes, record + 0x40, float.MaxValue);
    return bytes;
}

static void WriteVector(byte[] bytes, int offset, float x, float y, float z)
{
    WriteSingle(bytes, offset, x);
    WriteSingle(bytes, offset + 4, y);
    WriteSingle(bytes, offset + 8, z);
}

static void WriteSingle(byte[] bytes, int offset, float value) =>
    BitConverter.GetBytes(value).CopyTo(bytes, offset);

static void Assert(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

static void AssertThrows<T>(Action action, string message) where T : Exception
{
    try { action(); }
    catch (T) { return; }
    throw new InvalidOperationException(message);
}

sealed class BufferMemory(ulong baseAddress, byte[] bytes) : IReadOnlyProcessMemory
{
    public byte[] Read(IntPtr address, int length)
    {
        var offset = checked((long)address - (long)baseAddress);
        if (offset < 0 || offset + length > bytes.Length) throw new InvalidDataException("Outside test memory.");
        return bytes.AsSpan(checked((int)offset), length).ToArray();
    }

    public float ReadSingle(ulong address) => BitConverter.ToSingle(Read(new IntPtr((long)address), sizeof(float)));
    public IReadOnlyList<MemoryRegion> GetWritableRegions() => [new(baseAddress, (ulong)bytes.Length)];
}
