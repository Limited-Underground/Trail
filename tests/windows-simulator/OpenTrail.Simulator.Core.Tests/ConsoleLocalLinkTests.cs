using System.Buffers.Binary;

using OpenTrail.Simulator.Core;

internal static class ConsoleLocalLinkTests
{
    private static readonly Guid Attachment = new("04030201-0605-0807-090a-0b0c0d0e0f10");

    internal static int Run(Action<bool, string> expect)
    {
        var groups = 0;
        void Verify(string name, Action run)
        {
            groups++;
            try { run(); }
            catch (Exception error) { expect(false, $"Console link {name}: {error.Message}"); }
        }

        Verify("wire golden and maximum", () =>
        {
            Require(LocalLinkCodec.Crc("123456789"u8) == 0xcbf43926, "CRC check");
            var hello = LocalLinkCodec.Encode(new(LocalLinkKind.Hello, Guid.Empty, 0, [1, 1, 0, 6, 148, 0, 7, 0]));
            // T-0073 archived hello golden vector.
            Require(Convert.ToHexString(hello).Equals(
                "434c4c3101010000080000000000000000000000000000000000000000000000000001010006940007006072c7a4",
                StringComparison.OrdinalIgnoreCase), "HELLO differs from proposal golden");
            var frame = new LocalLinkFrame(LocalLinkKind.Data, Attachment, 1, TextRecord());
            var wire = LocalLinkCodec.Encode(frame);
            Require(wire.Length == 186, "maximum frame size");
            for (var split = 0; split <= wire.Length; split++)
            {
                var reader = new LocalLinkReader();
                LocalLinkFrame? decoded = null;
                bool Collect(LocalLinkFrame item) { decoded = item; return true; }
                foreach (var b in wire.AsSpan(0, split)) reader.Push(b, 0, Collect);
                foreach (var b in wire.AsSpan(split)) reader.Push(b, 1, Collect);
                Require(decoded is not null && decoded.Kind == frame.Kind && decoded.Attachment == Attachment &&
                    decoded.Sequence == 1 && decoded.Payload.SequenceEqual(frame.Payload), "split round trip");
            }
        });
        Verify("deterministic fuzz resync", () =>
        {
            var random = new Random(39481);
            for (var trial = 0; trial < 1500; trial++)
            {
                var reader = new LocalLinkReader();
                var junk = new byte[random.Next(0, 500)];
                random.NextBytes(junk);
                // Garbage has no magic opener. Explicit false candidates cover size and CRC separately.
                for (var i = 0; i < junk.Length; i++) if (junk[i] == 0x43) junk[i] = 0;
                var payload = new byte[random.Next(0, 149)];
                random.NextBytes(payload);
                var frame = new LocalLinkFrame(LocalLinkKind.Data, Attachment, 9, payload);
                var wire = LocalLinkCodec.Encode(frame);
                var corrupt = LocalLinkCodec.Encode(new(LocalLinkKind.Data, Attachment, 8, new byte[4]));
                corrupt[^1] ^= 1;
                var oversize = "CLL1"u8.ToArray().Concat(new byte[] { 1, 5, 0, 0, 149, 0 }).ToArray();
                var found = new List<LocalLinkFrame>();
                var bytes = junk.Concat(oversize).Concat(corrupt).Concat(wire);
                foreach (var value in bytes)
                {
                    Require(reader.Push(value, 0, item => { found.Add(item); return true; }), "fuzz framing");
                    Require(reader.BufferedBytes <= LocalLinkCodec.MaxFrame, "fuzz buffer bound");
                }
                Require(found.Count == 1 && found[0].Sequence == 9 && found[0].Payload.SequenceEqual(payload), "fuzz recovery");
            }
        });
        Verify("admission policy and capability", () =>
        {
            var panel = new ConsoleLocalLink(LocalLinkRole.Panel);
            var tracker = new ConsoleLocalLink(LocalLinkRole.Tracker);
            Require(panel.Begin(0), "panel HELLO");
            Relay(panel, tracker, 0);
            Require(tracker.LastReset == LocalLinkReset.AdmissionRefused && tracker.TakeOutput(0) is null, "default refusal");
            var pair = Connect(typedPanel: true, typedTracker: false);
            Require(!pair.Panel.TypedTextNegotiated && !pair.Tracker.TypedTextNegotiated, "intersection");
            Require(pair.Panel.TryQueueRecord(TextRecord(), 1) == LocalLinkWrite.InvalidRecord, "text gate");
            var reset = Request();
            reset[26] = 5;
            Require(pair.Panel.TryQueueRecord(reset, 1) == LocalLinkWrite.InvalidRecord, "non-quick action gate");
            pair.Policy.Current = false;
            Require(pair.Panel.TryQueueRecord(Request(), 1) == LocalLinkWrite.Unavailable &&
                pair.Panel.LastReset == LocalLinkReset.AdmissionRetired && pair.Policy.Retired == 1, "recheck and retirement");
        });
        Verify("duplex, sequence and backpressure", () =>
        {
            var pair = Connect();
            var request = Request();
            var response = Record(0x81, "OTN0"u8, 32);
            Require(pair.Panel.TryQueueRecord(request, 1) == LocalLinkWrite.Queued, "queue");
            Require(pair.Panel.TryQueueRecord(request, 1) == LocalLinkWrite.Busy, "one queue slot");
            Require(pair.Tracker.TryQueueRecord(response, 1) == LocalLinkWrite.Queued, "reverse queue");
            var packet = pair.Panel.TakeOutput(1)!;
            var reverse = pair.Tracker.TakeOutput(1)!;
            pair.Tracker.Receive(packet, 1, pair.Tracker.CallbackGeneration);
            pair.Panel.Receive(reverse, 1, pair.Panel.CallbackGeneration);
            Require(pair.Panel.Transfer == LocalLinkTransfer.AwaitingBufferAck, "no premature acknowledgement");
            Require(pair.Panel.TakeRecord(1)!.SequenceEqual(response), "reverse payload");
            Require(pair.Tracker.TakeRecord(1)!.SequenceEqual(request), "payload");
            Relay(pair.Tracker, pair.Panel, 1);
            Relay(pair.Panel, pair.Tracker, 1);
            Require(pair.Panel.Transfer == LocalLinkTransfer.BufferAccepted &&
                pair.Tracker.Transfer == LocalLinkTransfer.BufferAccepted, "both buffer ACKs");
            pair.Tracker.Receive(packet, 2, pair.Tracker.CallbackGeneration);
            Require(pair.Tracker.TakeRecord(2) is null && pair.Tracker.TakeOutput(2) is null, "old sequence dropped");
            Require(pair.Panel.TryQueueRecord(request, 2) == LocalLinkWrite.Queued, "released sender");
        });
        Verify("cut cable and stale data after reconnect", () =>
        {
            var pair = Connect();
            pair.Panel.TryQueueRecord(Request(), 10);
            var old = pair.Panel.TakeOutput(10)!;
            pair.Tracker.Receive(old.AsSpan(0, 20), 10, pair.Tracker.CallbackGeneration);
            pair.Tracker.Receive(old.AsSpan(20, 1), 109, pair.Tracker.CallbackGeneration);
            Require(!pair.Tracker.Advance(110) && pair.Tracker.LastReset == LocalLinkReset.PartialFrame, "trickle extended deadline");
            Require(!pair.Panel.Advance(510) && pair.Panel.Transfer == LocalLinkTransfer.Unknown, "missing buffer ACK");
            var previous = pair.Tracker.CallbackGeneration - 1;
            Handshake(pair.Panel, pair.Tracker, 511);
            var staleHello = LocalLinkCodec.Encode(new(LocalLinkKind.Hello, Guid.Empty, 0, [1, 1, 0, 6, 148, 0, 5, 0]));
            pair.Tracker.Receive(staleHello, 512, previous);
            pair.Tracker.Disconnect(previous);
            Require(pair.Tracker.State == LocalLinkState.LinkReady, "retired callback reset the replacement");
            pair.Tracker.Receive(old, 512, pair.Tracker.CallbackGeneration);
            Require(pair.Tracker.TakeRecord(512) is null, "retired data delivered");
            pair.Panel.TryQueueRecord(Request(), 512);
            pair.Panel.Disconnect(pair.Panel.CallbackGeneration);
            Require(pair.Panel.Transfer == LocalLinkTransfer.Canceled, "unsent data ambiguous");
        });
        Verify("malformed header, sequence gap and wrong ACK", () =>
        {
            var pair = Connect();
            pair.Tracker.Receive(LocalLinkCodec.Encode(new(LocalLinkKind.Data, Attachment, 2, Request())), 1, pair.Tracker.CallbackGeneration);
            Require(pair.Tracker.LastReset == LocalLinkReset.SequenceGap, "sequence gap");
            pair = Connect();
            pair.Panel.TryQueueRecord(Request(), 1);
            _ = pair.Panel.TakeOutput(1);
            var wrongAck = new byte[8];
            BinaryPrimitives.WriteUInt64LittleEndian(wrongAck, 2);
            pair.Panel.Receive(LocalLinkCodec.Encode(new(LocalLinkKind.LinkAck, Attachment, 1, wrongAck)), 1, pair.Panel.CallbackGeneration);
            Require(pair.Panel.LastReset == LocalLinkReset.UnexpectedControl && pair.Panel.Transfer == LocalLinkTransfer.Unknown, "bad ACK");
            pair = Connect();
            var bad = LocalLinkCodec.Encode(new(LocalLinkKind.Data, Attachment, 1, Request()));
            bad[4] = 2;
            BinaryPrimitives.WriteUInt32LittleEndian(bad.AsSpan(bad.Length - 4), LocalLinkCodec.Crc(bad.AsSpan(0, bad.Length - 4)));
            pair.Tracker.Receive(bad, 1, pair.Tracker.CallbackGeneration);
            Require(pair.Tracker.LastReset == LocalLinkReset.InvalidFrame, "unsupported framing version");
        });
        Verify("bounded input and scanner", () =>
        {
            var pair = Connect();
            pair.Panel.TryQueueRecord(Request(), 1);
            Relay(pair.Panel, pair.Tracker, 1);
            Relay(pair.Tracker, pair.Panel, 1);
            pair.Panel.TryQueueRecord(Request(), 2);
            Relay(pair.Panel, pair.Tracker, 2);
            Require(pair.Tracker.LastReset == LocalLinkReset.ReceiveFull && pair.Tracker.TakeRecord(2) is null, "full receiver containment");
            pair = Connect();
            pair.Panel.Receive(new byte[1025], 1001, pair.Panel.CallbackGeneration);
            Require(pair.Panel.LastReset == LocalLinkReset.ScanLimit && pair.Panel.BufferedBytes == 0, "bounded scanning");
        });
        Verify("heartbeat, deadlines and stale handshake", () =>
        {
            var pair = Connect();
            Relay(pair.Panel, pair.Tracker, 1000);
            Relay(pair.Tracker, pair.Panel, 1000);
            Require(pair.Panel.Advance(2000), "matching PONG retired link");
            Relay(pair.Panel, pair.Tracker, 2000);
            Require(!pair.Panel.Advance(5000) && pair.Panel.LastReset == LocalLinkReset.HeartbeatTimeout, "heartbeat silence");
            var panel = new ConsoleLocalLink(LocalLinkRole.Panel, new FakeAdmission());
            panel.Begin(1);
            _ = panel.TakeOutput(1);
            Require(!panel.Advance(1001) && panel.LastReset == LocalLinkReset.HandshakeTimeout, "handshake expires");
            pair = Connect();
            var oldOffer = LocalLinkCodec.Encode(new(LocalLinkKind.Offer, Attachment, 0, [1, 1, 0, 6, 148, 0, 5, 0]));
            pair.Panel.Disconnect(pair.Panel.CallbackGeneration);
            pair.Panel.Begin(7);
            _ = pair.Panel.TakeOutput(7);
            pair.Panel.Receive(oldOffer, 7, pair.Panel.CallbackGeneration);
            Require(pair.Panel.State == LocalLinkState.WaitingForOffer, "retired offer accepted");
            Require(!pair.Panel.Advance(6) && pair.Panel.LastReset == LocalLinkReset.ClockRollback, "rollback");
        });
        return groups;
    }

    private static (ConsoleLocalLink Panel, ConsoleLocalLink Tracker, FakeAdmission Policy) Connect(
        bool typedPanel = false, bool typedTracker = false)
    {
        var next = Attachment;
        var policy = new FakeAdmission();
        var panel = new ConsoleLocalLink(LocalLinkRole.Panel, policy, typedPanel);
        var tracker = new ConsoleLocalLink(LocalLinkRole.Tracker, policy, typedTracker, () =>
        {
            var id = next;
            next = Guid.NewGuid();
            return id;
        });
        Handshake(panel, tracker, 0);
        return (panel, tracker, policy);
    }

    private static void Handshake(ConsoleLocalLink panel, ConsoleLocalLink tracker, long time)
    {
        Require(panel.Begin(time), "begin");
        Relay(panel, tracker, time);
        Relay(tracker, panel, time);
        Relay(panel, tracker, time);
        Require(panel.State != LocalLinkState.LinkReady, "panel ready before READY");
        Relay(tracker, panel, time);
        Require(panel.State == LocalLinkState.LinkReady && tracker.State == LocalLinkState.LinkReady, "handshake");
    }

    private static void Relay(ConsoleLocalLink source, ConsoleLocalLink target, long time) =>
        target.Receive(source.TakeOutput(time) ?? throw new InvalidOperationException("missing frame"), time, target.CallbackGeneration);

    private static byte[] Request()
    {
        var bytes = Record(2, "OTA0"u8, 20);
        bytes[26] = bytes[27] = 1;
        return bytes;
    }

    private static byte[] TextRecord()
    {
        var bytes = Record(0x0a, "OTXC"u8, 128);
        bytes[24] = 1;
        bytes[26] = bytes[30] = bytes[38] = bytes[42] = 1;
        "OTXT"u8.CopyTo(bytes.AsSpan(50));
        bytes[54] = 1;
        bytes[57] = 90;
        bytes.AsSpan(58).Fill((byte)'A');
        return bytes;
    }

    private static byte[] Record(byte kind, ReadOnlySpan<byte> domain, ushort payloadLength)
    {
        var bytes = new byte[20 + payloadLength];
        "OTC0"u8.CopyTo(bytes);
        bytes[5] = 6;
        bytes[6] = kind;
        bytes[8] = bytes[12] = bytes[17] = 1;
        BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(18), payloadLength);
        domain.CopyTo(bytes.AsSpan(20));
        return bytes;
    }

    private static void Require(bool condition, string reason)
    {
        if (!condition) throw new InvalidOperationException(reason);
    }

    private sealed class FakeAdmission : ILocalLinkAdmission
    {
        public bool Current { get; set; } = true;
        public int Retired { get; private set; }
        public bool TryAdmit(LocalLinkRole role, Guid attachment) => Current;
        public bool IsCurrent(LocalLinkRole role, Guid attachment) => Current;
        public void Retire(LocalLinkRole role, Guid attachment) => Retired++;
    }
}
