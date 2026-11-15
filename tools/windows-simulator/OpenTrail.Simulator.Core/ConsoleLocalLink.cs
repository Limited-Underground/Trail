using System.Buffers.Binary;

namespace OpenTrail.Simulator.Core;

internal enum LocalLinkRole { Panel, Tracker }
internal enum LocalLinkState { Down, WaitingForOffer, WaitingForAccept, WaitingForReady, SendingReady, LinkReady }
internal enum LocalLinkReset
{
    None, Disconnected, PartialFrame, ScanLimit, InvalidFrame, HandshakeTimeout, PeerRestart,
    AdmissionRefused, AdmissionRetired, SequenceGap, SequenceExhausted, ReceiveFull,
    UnexpectedControl, AckTimeout, HeartbeatTimeout, ClockRollback,
}

internal enum LocalLinkWrite { Unavailable, InvalidRecord, Busy, Queued }
internal enum LocalLinkTransfer { None, Queued, AwaitingBufferAck, BufferAccepted, Canceled, Unknown }

// The external owner supplies and rechecks admission for the specific attachment.
// Wire bytes never substitute for that decision. Implementations run on the single link owner.
internal interface ILocalLinkAdmission
{
    bool TryAdmit(LocalLinkRole role, Guid attachment);
    bool IsCurrent(LocalLinkRole role, Guid attachment);
    void Retire(LocalLinkRole role, Guid attachment);
}

internal sealed class ConsoleLocalLink(
    LocalLinkRole role, ILocalLinkAdmission? admission = null, bool supportsTypedText = false,
    Func<Guid>? newAttachment = null)
{
    private const ushort Required = 5;
    private const ushort Text = 2;
    private readonly LocalLinkReader reader = new();
    private readonly Queue<LocalLinkKind> controls = new(2);
    private readonly ushort features = supportsTypedText ? (ushort)(Required | Text) : Required;
    private Guid attachment;
    private Guid lastAttachment;
    private ushort agreed;
    private bool admitted;
    private long lastTime = -1;
    private long stageStart;
    private long scanWindow = -1;
    private int scanned;
    private ulong tx;
    private ulong rx;
    private byte[]? outgoing;
    private byte[]? incoming;
    private ulong dataAwaitingAck;
    private ulong ackToSend;
    private long dataSentAt;
    private ulong pingAwaitingPong;
    private ulong pongToSend;
    private long lastPingAt;

    internal LocalLinkState State { get; private set; }
    internal LocalLinkReset LastReset { get; private set; }
    internal LocalLinkTransfer Transfer { get; private set; }
    internal bool TypedTextNegotiated => State == LocalLinkState.LinkReady && (agreed & Text) != 0;
    // Capture this value when registering transport callbacks.
    internal ulong CallbackGeneration { get; private set; } = 1;
    internal int BufferedBytes => reader.BufferedBytes;

    internal bool Begin(long now)
    {
        if (!Advance(now) || role != LocalLinkRole.Panel || State != LocalLinkState.Down)
            return false;
        State = LocalLinkState.WaitingForOffer;
        stageStart = now;
        controls.Enqueue(LocalLinkKind.Hello);
        return true;
    }

    internal void Disconnect(ulong generation)
    {
        if (generation == CallbackGeneration) Reset(LocalLinkReset.Disconnected);
    }

    internal bool Advance(long now)
    {
        if (CallbackGeneration == ulong.MaxValue) return false;
        if (now < 0 || now < lastTime)
            return Reset(LocalLinkReset.ClockRollback);
        lastTime = now;
        if (reader.Expired(now))
            return Reset(LocalLinkReset.PartialFrame);
        if (admitted && admission?.IsCurrent(role, attachment) != true)
            return Reset(LocalLinkReset.AdmissionRetired);
        if (State == LocalLinkState.Down)
            return true;
        if (State != LocalLinkState.LinkReady)
            return now - stageStart < 1000 || Reset(LocalLinkReset.HandshakeTimeout);
        if (dataAwaitingAck != 0 && now - dataSentAt >= 500)
            return Reset(LocalLinkReset.AckTimeout);
        if (now - lastPingAt >= 3000)
            return Reset(LocalLinkReset.HeartbeatTimeout);
        return true;
    }

    internal void Receive(ReadOnlySpan<byte> bytes, long now, ulong generation)
    {
        if (generation != CallbackGeneration || !Advance(now))
            return;
        if (scanWindow < 0 || now - scanWindow >= 1000)
        {
            scanWindow = now;
            scanned = 0;
        }
        var resetDuringFrame = false;
        bool Collect(LocalLinkFrame frame)
        {
            resetDuringFrame = !Handle(frame, now);
            return !resetDuringFrame;
        }
        foreach (var value in bytes)
        {
            if (++scanned > 1024)
            {
                Reset(LocalLinkReset.ScanLimit);
                return;
            }
            if (!reader.Push(value, now, Collect))
            {
                Reset(LocalLinkReset.InvalidFrame);
                return;
            }
            if (resetDuringFrame)
                return;
        }
    }

    internal LocalLinkWrite TryQueueRecord(ReadOnlySpan<byte> record, long now)
    {
        if (!Advance(now) || State != LocalLinkState.LinkReady)
            return LocalLinkWrite.Unavailable;
        if (!LocalLinkCodec.ValidInner(record, role == LocalLinkRole.Panel, TypedTextNegotiated))
            return LocalLinkWrite.InvalidRecord;
        if (outgoing is not null || dataAwaitingAck != 0)
            return LocalLinkWrite.Busy;
        outgoing = record.ToArray();
        Transfer = LocalLinkTransfer.Queued;
        return LocalLinkWrite.Queued;
    }

    internal byte[]? TakeRecord(long now)
    {
        if (!Advance(now) || State != LocalLinkState.LinkReady)
            return null;
        var record = incoming;
        incoming = null;
        return record;
    }

    // A returned frame is handed to the transport. From that point a lost DATA is uncertain.
    internal byte[]? TakeOutput(long now)
    {
        if (!Advance(now))
            return null;
        if (controls.TryDequeue(out var kind))
        {
            if (kind <= LocalLinkKind.Ready)
            {
                var bytes = LocalLinkCodec.Encode(new(kind, attachment, 0,
                    Options(kind == LocalLinkKind.Hello ? features : agreed)));
                if (kind == LocalLinkKind.Ready)
                    SetReady(now);
                return bytes;
            }
            var correlation = kind == LocalLinkKind.LinkAck ? ackToSend : pongToSend;
            if (kind == LocalLinkKind.LinkAck) ackToSend = 0;
            else pongToSend = 0;
            return EmitControl(kind, correlation);
        }
        if (State != LocalLinkState.LinkReady)
            return null;
        if (pingAwaitingPong == 0 && now - lastPingAt >= 1000)
        {
            var bytes = EmitControl(LocalLinkKind.Ping, tx + 1);
            if (bytes is not null)
            {
                pingAwaitingPong = tx;
                lastPingAt = now;
            }
            return bytes;
        }
        if (outgoing is null)
            return null;
        var result = Emit(LocalLinkKind.Data, outgoing);
        if (result is not null)
        {
            outgoing = null;
            dataAwaitingAck = tx;
            dataSentAt = now;
            Transfer = LocalLinkTransfer.AwaitingBufferAck;
        }
        return result;
    }

    private bool Handle(LocalLinkFrame frame, long now)
    {
        if (frame.Kind <= LocalLinkKind.Ready)
            return HandleHandshake(frame, now);
        if (State != LocalLinkState.LinkReady || frame.Attachment != attachment)
            return true; // Retired frames cannot affect a replacement attachment.
        if (frame.Sequence == 0)
            return Reset(LocalLinkReset.InvalidFrame);
        if (frame.Sequence <= rx)
            return true;
        if (frame.Sequence != rx + 1)
            return Reset(LocalLinkReset.SequenceGap);
        if (frame.Sequence == ulong.MaxValue)
            return Reset(LocalLinkReset.SequenceExhausted);
        rx = frame.Sequence;

        if (frame.Kind == LocalLinkKind.Data)
        {
            if (!LocalLinkCodec.ValidInner(frame.Payload, role == LocalLinkRole.Tracker, TypedTextNegotiated))
                return Reset(LocalLinkReset.InvalidFrame);
            if (incoming is not null || ackToSend != 0 || controls.Count == 2)
                return Reset(LocalLinkReset.ReceiveFull);
            incoming = frame.Payload;
            ackToSend = rx;
            controls.Enqueue(LocalLinkKind.LinkAck);
            return true;
        }

        if (frame.Payload.Length != 8)
            return Reset(LocalLinkReset.InvalidFrame);
        var reference = BinaryPrimitives.ReadUInt64LittleEndian(frame.Payload);
        switch (frame.Kind)
        {
            case LocalLinkKind.Ping when reference == rx && pongToSend == 0 && controls.Count < 2:
                pongToSend = rx;
                controls.Enqueue(LocalLinkKind.Pong);
                return true;
            case LocalLinkKind.Pong when reference != 0 && reference == pingAwaitingPong:
                pingAwaitingPong = 0;
                return true;
            case LocalLinkKind.LinkAck when reference != 0 && reference == dataAwaitingAck:
                dataAwaitingAck = 0;
                Transfer = LocalLinkTransfer.BufferAccepted;
                return true;
            default:
                return Reset(LocalLinkReset.UnexpectedControl);
        }
    }

    private bool HandleHandshake(LocalLinkFrame frame, long now)
    {
        if (frame.Sequence != 0 || !ReadOptions(frame.Payload, out var offered) ||
            (frame.Kind == LocalLinkKind.Hello) != (frame.Attachment == Guid.Empty))
            return Reset(LocalLinkReset.InvalidFrame);

        if (frame.Kind == LocalLinkKind.Hello && role == LocalLinkRole.Tracker)
        {
            if (State != LocalLinkState.Down)
                return Reset(LocalLinkReset.PeerRestart);
            var candidate = newAttachment?.Invoke() ?? Guid.NewGuid();
            agreed = (ushort)(offered & features);
            if (!Admit(candidate))
                return false;
            State = LocalLinkState.WaitingForAccept;
            stageStart = now;
            controls.Enqueue(LocalLinkKind.Offer);
            return true;
        }

        if (frame.Kind == LocalLinkKind.Offer && State == LocalLinkState.WaitingForOffer)
        {
            if (controls.Count != 0 || (offered & features) != offered)
                return Reset(LocalLinkReset.InvalidFrame);
            if (frame.Attachment == lastAttachment)
                return true;
            agreed = offered;
            if (!Admit(frame.Attachment))
                return false;
            State = LocalLinkState.WaitingForReady;
            controls.Enqueue(LocalLinkKind.Accept);
            return true;
        }

        if (frame.Attachment != attachment || attachment == Guid.Empty)
            return true;
        if (offered != agreed)
            return Reset(LocalLinkReset.InvalidFrame);
        if (frame.Kind == LocalLinkKind.Accept && State == LocalLinkState.WaitingForAccept && controls.Count == 0)
        {
            State = LocalLinkState.SendingReady;
            controls.Enqueue(LocalLinkKind.Ready);
            return true;
        }
        if (frame.Kind == LocalLinkKind.Ready && State == LocalLinkState.WaitingForReady && controls.Count == 0)
        {
            SetReady(now);
            return true;
        }
        return Reset(LocalLinkReset.UnexpectedControl);
    }

    private bool Admit(Guid candidate)
    {
        if (candidate == Guid.Empty || candidate == lastAttachment || admission?.TryAdmit(role, candidate) != true)
            return Reset(LocalLinkReset.AdmissionRefused);
        attachment = candidate;
        lastAttachment = candidate;
        admitted = true;
        if (admission.IsCurrent(role, candidate))
            return true;
        return Reset(LocalLinkReset.AdmissionRetired);
    }

    private void SetReady(long now)
    {
        State = LocalLinkState.LinkReady;
        lastPingAt = now;
    }

    private byte[]? EmitControl(LocalLinkKind kind, ulong correlation)
    {
        var payload = new byte[8];
        BinaryPrimitives.WriteUInt64LittleEndian(payload, correlation);
        return Emit(kind, payload);
    }

    private byte[]? Emit(LocalLinkKind kind, byte[] payload)
    {
        if (tx == ulong.MaxValue - 1)
        {
            Reset(LocalLinkReset.SequenceExhausted);
            return null;
        }
        return LocalLinkCodec.Encode(new(kind, attachment, ++tx, payload));
    }

    private static byte[] Options(ushort mask) => [1, 1, 0, 6, 148, 0, (byte)mask, 0];

    private static bool ReadOptions(ReadOnlySpan<byte> bytes, out ushort mask)
    {
        mask = 0;
        if (bytes.Length != 8 || !bytes[..6].SequenceEqual(Options(0).AsSpan(0, 6)))
            return false;
        mask = BinaryPrimitives.ReadUInt16LittleEndian(bytes[6..]);
        return mask == Required || mask == (Required | Text);
    }

    private bool Reset(LocalLinkReset reason)
    {
        var retired = attachment;
        var notify = admitted;
        if (CallbackGeneration != ulong.MaxValue) CallbackGeneration++;
        State = LocalLinkState.Down;
        LastReset = reason;
        Transfer = Transfer switch
        {
            LocalLinkTransfer.Queued => LocalLinkTransfer.Canceled,
            LocalLinkTransfer.AwaitingBufferAck => LocalLinkTransfer.Unknown,
            _ => Transfer,
        };
        attachment = Guid.Empty;
        admitted = false;
        agreed = 0;
        tx = rx = dataAwaitingAck = ackToSend = pingAwaitingPong = pongToSend = 0;
        controls.Clear();
        reader.Clear();
        outgoing = incoming = null;
        if (notify)
            admission!.Retire(role, retired);
        return false;
    }
}
