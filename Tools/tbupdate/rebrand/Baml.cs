using System.Text;

namespace LauncherRebrand;

// Record layout from the WPF BamlRecordType enum; -1 = record with a 7-bit encoded size (size field included).
static class Baml
{
    const int HeaderSize = 4 + 12 + 12;
    const int Sized = -1;

    static readonly Dictionary<byte, int> Layout = new()
    {
        [1] = 6, [2] = 0, [3] = 3, [4] = 0, [5] = Sized, [6] = Sized, [7] = 2, [8] = 0, [9] = 2, [10] = 0,
        [11] = 2, [12] = 0, [13] = 2, [14] = 0, [15] = Sized, [16] = Sized, [17] = Sized, [18] = Sized,
        [20] = Sized, [25] = Sized, [26] = 0, [27] = Sized, [28] = Sized, [29] = Sized, [30] = Sized,
        [31] = Sized, [32] = Sized, [33] = 4, [34] = 4, [35] = 6, [36] = Sized, [37] = 4, [38] = Sized,
        [39] = 9, [40] = 9, [41] = 0, [42] = 0, [43] = 0, [44] = 2, [45] = 4, [46] = 2, [48] = 3, [49] = 0,
        [50] = 2, [51] = Sized, [52] = Sized, [53] = 8, [54] = 4, [55] = 3, [56] = 4,
    };

    // Offset of the value string inside the record body (after the size field).
    static readonly Dictionary<byte, int> ValueOffset = new()
    {
        [5] = 2, [15] = 0, [16] = 0, [17] = 0, [25] = 0, [32] = 2, [36] = 2, [52] = 0,
    };

    record Rec(byte Type, int Start, int End, byte[] Body, bool IsSized);

    static int ReadEncoded(byte[] d, ref int pos)
    {
        int value = 0, shift = 0;
        while (true)
        {
            byte b = d[pos++];
            value |= (b & 0x7F) << shift;
            if ((b & 0x80) == 0) return value;
            shift += 7;
        }
    }

    static byte[] Encode(int value)
    {
        var list = new List<byte>();
        uint v = (uint)value;
        while (v >= 0x80) { list.Add((byte)(v | 0x80)); v >>= 7; }
        list.Add((byte)v);
        return list.ToArray();
    }

    static List<Rec> Parse(byte[] d)
    {
        var recs = new List<Rec>();
        int pos = HeaderSize;
        while (pos < d.Length)
        {
            int start = pos;
            byte type = d[pos++];
            if (!Layout.TryGetValue(type, out int size))
                throw new InvalidDataException($"BAML record type {type} at {start} not supported");
            byte[] body;
            if (size == Sized)
            {
                int sizePos = pos;
                int total = ReadEncoded(d, ref pos);
                int bodyLen = total - (pos - sizePos);
                body = d[pos..(pos + bodyLen)];
                pos += bodyLen;
            }
            else
            {
                body = d[pos..(pos + size)];
                pos += size;
            }
            recs.Add(new Rec(type, start, pos, body, size == Sized));
            if (type == 2 && pos != d.Length)
                throw new InvalidDataException("data after DocumentEnd");
        }
        if (recs.Count == 0 || recs[^1].Type != 2)
            throw new InvalidDataException("BAML does not end with DocumentEnd");
        return recs;
    }

    static (string Value, int Length) ReadString(byte[] body, int offset)
    {
        int pos = offset;
        int len = ReadEncoded(body, ref pos);
        return (Encoding.UTF8.GetString(body, pos, len), pos - offset + len);
    }

    public static IEnumerable<string> Strings(byte[] baml)
    {
        foreach (var r in Parse(baml))
            if (ValueOffset.TryGetValue(r.Type, out int off))
                yield return ReadString(r.Body, off).Value;
    }

    public static byte[] Replace(byte[] baml, IReadOnlyDictionary<string, string> map, Dictionary<string, int> counts)
    {
        var recs = Parse(baml);
        var deferred = new List<(int From, int To)>();
        foreach (var r in recs.Where(r => r.Type == 37))
            deferred.Add((r.Start, r.End + BitConverter.ToInt32(r.Body, 0)));

        using var output = new MemoryStream();
        output.Write(baml, 0, HeaderSize);
        foreach (var r in recs)
        {
            byte[] body = r.Body;
            if (ValueOffset.TryGetValue(r.Type, out int off))
            {
                var (value, length) = ReadString(body, off);
                if (map.TryGetValue(value, out string? replacement))
                {
                    if (deferred.Any(x => r.Start >= x.From && r.Start < x.To))
                        throw new InvalidDataException($"'{value}' is inside deferred content, offsets would need fixing");
                    byte[] utf8 = Encoding.UTF8.GetBytes(replacement);
                    body = [.. body[..off], .. Encode(utf8.Length), .. utf8, .. body[(off + length)..]];
                    counts[value] = counts.GetValueOrDefault(value) + 1;
                }
            }
            output.WriteByte(r.Type);
            if (r.IsSized)
            {
                int n = 1;
                while (Encode(n + body.Length).Length != n) n++;
                output.Write(Encode(n + body.Length));
            }
            output.Write(body);
        }
        byte[] result = output.ToArray();
        Parse(result);
        return result;
    }
}
