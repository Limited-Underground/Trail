using System.Buffers.Binary;

namespace OpenTrail.Simulator.Core;

internal enum LocalLinkKind : byte
{
    Hello = 1, Offer, Accept, Ready, Data, Ping, Pong, LinkAck,
}

internal sealed record LocalLinkFrame(LocalLinkKind Kind, Guid Attachment, ulong Sequence, byte[] Payload);

internal static class LocalLinkCodec
{
    internal const int HeaderBytes = 34;
    internal const int Overhead = HeaderBytes + 4;
    internal const int MaxPayload = 148;
    internal const int MaxFrame = Overhead + MaxPayload;
    internal static ReadOnlySpan<byte> Magic => "CLL1"u8;

    internal static byte[] Encode(LocalLinkFrame frame)
    {
        if (frame.Payload.Length > MaxPayload)
            throw new ArgumentOutOfRangeException(nameof(frame));

        var bytes = new byte[Overhead + frame.Payload.Length];
        Magic.CopyTo(bytes);
        bytes[4] = 1;
        bytes[5] = (byte)frame.Kind;
        BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(8), (ushort)frame.Payload.Length);
        frame.Attachment.TryWriteBytes(bytes.AsSpan(10, 16));
        BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(26), frame.Sequence);
        frame.Payload.CopyTo(bytes, HeaderBytes);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(bytes.Length - 4), Crc(bytes.AsSpan(0, bytes.Length - 4)));
        return bytes;
    }

    internal static uint Crc(ReadOnlySpan<byte> bytes)
    {
        var crc = uint.MaxValue;
        foreach (var value in bytes)
        {
            crc ^= value;
            for (var bit = 0; bit < 8; bit++)
                crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xedb88320u : 0);
        }
        return ~crc;
    }

    internal static bool ValidInner(ReadOnlySpan<byte> payload, bool fromPanel, bool typed)
    {
        if (payload.Length is < 20 or > MaxPayload || !payload[..4].SequenceEqual("OTC0"u8) ||
            payload[4] != 0 || payload[5] != 6 || payload[7] != 0 ||
            BinaryPrimitives.ReadUInt32LittleEndian(payload[8..]) == 0 ||
            BinaryPrimitives.ReadUInt32LittleEndian(payload[12..]) == 0 ||
            payload[16] != 0 || payload[17] != 1 ||
            BinaryPrimitives.ReadUInt16LittleEndian(payload[18..]) != payload.Length - 20)
            return false;

        return payload[6] switch
        {
            0x01 => fromPanel && HasPayload(payload, "OTX0"u8, 8),
            0x09 => fromPanel && HasPayload(payload, "OTY0"u8, 56),
            0x02 => fromPanel && payload.Length == 40 && payload[20..24].SequenceEqual("OTA0"u8) &&
                payload[24] == 0 && payload[25] == 0 && payload[26] == 1 && payload[27] is >= 1 and <= 4,
            0x81 => !fromPanel && HasPayload(payload, "OTN0"u8, 32),
            0x82 => !fromPanel && HasPayload(payload, "OTR0"u8, 20) && payload[28] == 1,
            0x83 => !fromPanel && HasPayload(payload, "OTE0"u8, 40),
            0x8b => !fromPanel && HasPayload(payload, "OTY0"u8, 116),
            0x0a => typed && fromPanel && payload.Length >= 59 && payload[20..24].SequenceEqual("OTXC"u8),
            0x8c => typed && !fromPanel && HasPayload(payload, "OTXC"u8, 48),
            0x8d => typed && !fromPanel && payload.Length >= 59 && payload[20..24].SequenceEqual("OTXC"u8),
            0x8e => typed && !fromPanel && HasPayload(payload, "OTXC"u8, 52),
            _ => false,
        };
    }

    private static bool HasPayload(ReadOnlySpan<byte> record, ReadOnlySpan<byte> domain, int length) =>
        record.Length == 20 + length && record[20..24].SequenceEqual(domain);
}

internal sealed class LocalLinkReader
{
    private readonly byte[] buffer = new byte[LocalLinkCodec.MaxFrame];
    private readonly long[] arrivals = new long[LocalLinkCodec.MaxFrame];
    private int length;

    internal int BufferedBytes => length;

    internal bool Expired(long now) => length != 0 && now - arrivals[0] >= 100;

    internal void Clear() => length = 0;

    // Returns false for a complete, CRC-valid frame with an unsupported outer header.
    internal bool Push(byte value, long now, Func<LocalLinkFrame, bool> receive)
    {
        buffer[length] = value;
        arrivals[length++] = now;
        while (length != 0)
        {
            var candidate = buffer.AsSpan(0, length);
            var prefix = Math.Min(length, 4);
            if (!candidate[..prefix].SequenceEqual(LocalLinkCodec.Magic[..prefix]))
            {
                Consume(1);
                continue;
            }
            if (length < 10)
                break;

            var payloadLength = BinaryPrimitives.ReadUInt16LittleEndian(candidate[8..]);
            if (payloadLength > LocalLinkCodec.MaxPayload)
            {
                Consume(1);
                continue;
            }
            var frameLength = LocalLinkCodec.Overhead + payloadLength;
            if (length < frameLength)
                break;

            var crcOffset = frameLength - 4;
            if (LocalLinkCodec.Crc(candidate[..crcOffset]) != BinaryPrimitives.ReadUInt32LittleEndian(candidate[crcOffset..]))
            {
                Consume(1);
                continue;
            }
            if (candidate[4] != 1 || candidate[5] is < 1 or > 8 || candidate[6] != 0 || candidate[7] != 0)
                return false;

            var frame = new LocalLinkFrame((LocalLinkKind)candidate[5], new Guid(candidate.Slice(10, 16)),
                BinaryPrimitives.ReadUInt64LittleEndian(candidate[26..]),
                candidate.Slice(LocalLinkCodec.HeaderBytes, payloadLength).ToArray());
            Consume(frameLength);
            if (!receive(frame))
                break;
        }
        return true;
    }

    private void Consume(int count)
    {
        length -= count;
        Array.Copy(buffer, count, buffer, 0, length);
        Array.Copy(arrivals, count, arrivals, 0, length);
    }
}
