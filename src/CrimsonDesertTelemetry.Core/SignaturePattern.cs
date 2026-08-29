namespace CrimsonDesertTelemetry.Core;

public sealed class SignaturePattern
{
    private readonly byte[] _bytes;
    private readonly bool[] _wildcards;

    private SignaturePattern(byte[] bytes, bool[] wildcards)
    {
        _bytes = bytes;
        _wildcards = wildcards;
    }

    public int Length => _bytes.Length;

    public static SignaturePattern Parse(string value)
    {
        var tokens = value.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (tokens.Length == 0) throw new FormatException("Signature must not be empty.");
        var bytes = new byte[tokens.Length];
        var wildcards = new bool[tokens.Length];
        for (var index = 0; index < tokens.Length; index++)
        {
            if (tokens[index] is "?" or "??")
            {
                wildcards[index] = true;
                continue;
            }

            if (tokens[index].Length != 2 || !byte.TryParse(tokens[index],
                    System.Globalization.NumberStyles.HexNumber,
                    System.Globalization.CultureInfo.InvariantCulture, out bytes[index]))
            {
                throw new FormatException($"Invalid signature token '{tokens[index]}'.");
            }
        }
        return new SignaturePattern(bytes, wildcards);
    }

    public IReadOnlyList<int> FindAll(ReadOnlySpan<byte> data)
    {
        var matches = new List<int>();
        var anchorIndex = Array.FindIndex(_wildcards, static wildcard => !wildcard);
        if (anchorIndex < 0) throw new InvalidOperationException("A signature made entirely of wildcards is not allowed.");
        var searchFrom = anchorIndex;
        while (searchFrom < data.Length)
        {
            var relative = data[searchFrom..].IndexOf(_bytes[anchorIndex]);
            if (relative < 0) break;
            var anchorPosition = searchFrom + relative;
            var offset = anchorPosition - anchorIndex;
            searchFrom = anchorPosition + 1;
            if (offset < 0 || offset > data.Length - _bytes.Length) continue;
            var matchesAtOffset = true;
            for (var index = 0; index < _bytes.Length; index++)
            {
                if (!_wildcards[index] && data[offset + index] != _bytes[index])
                {
                    matchesAtOffset = false;
                    break;
                }
            }
            if (matchesAtOffset) matches.Add(offset);
        }
        return matches;
    }
}
