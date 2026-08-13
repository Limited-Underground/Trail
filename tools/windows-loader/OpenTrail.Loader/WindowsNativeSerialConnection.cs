using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace OpenTrail.Loader;

/// <summary>
/// Minimal synchronous Windows serial transport for bounded inspection
/// commands. It deliberately exposes no line-control or reset operation.
/// </summary>
internal sealed class WindowsNativeSerialConnection : IDisposable
{
    private const uint GenericRead = 0x80000000;
    private const uint GenericWrite = 0x40000000;
    private const uint OpenExisting = 3;
    private const uint FileAttributeNormal = 0x00000080;
    private const uint PurgeRxAbort = 0x0002;
    private const uint PurgeRxClear = 0x0008;
    private const uint DcbBinary = 0x00000001;
    private const uint DcbDtrMask = 0x00000030;
    private const uint DcbDtrEnable = 0x00000010;
    private const uint DcbRtsMask = 0x00003000;
    private const uint DcbRtsEnable = 0x00001000;
    private readonly SafeFileHandle _handle;

    private WindowsNativeSerialConnection(SafeFileHandle handle)
    {
        _handle = handle;
    }

    internal static WindowsNativeSerialConnection Open(
        string privatePortName,
        bool rtsEnabled)
    {
        if (!WindowsSerialInspectionProvider.TryNormalizePortName(
                privatePortName,
                out var normalizedPortName))
        {
            throw new ArgumentException(
                "The private serial port name is invalid.",
                nameof(privatePortName));
        }

        var handle = CreateFileW(
            $@"\\.\{normalizedPortName}",
            GenericRead | GenericWrite,
            0,
            IntPtr.Zero,
            OpenExisting,
            FileAttributeNormal,
            IntPtr.Zero);
        if (handle.IsInvalid)
        {
            var error = Marshal.GetLastWin32Error();
            handle.Dispose();
            throw new Win32Exception(error, "The serial interface could not be opened.");
        }

        try
        {
            var dcb = new DeviceControlBlock
            {
                Length = (uint)Marshal.SizeOf<DeviceControlBlock>(),
            };
            if (!BuildCommDCBW(
                    "baud=115200 parity=n data=8 stop=1",
                    ref dcb))
            {
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "The inspection serial settings are invalid.");
            }

            // Binary 8N1 with the fixed line state required by the selected
            // USB CDC runtime. The loader provides no API that can toggle these
            // lines into a reset/download sequence.
            dcb.Flags |= DcbBinary;
            dcb.Flags &= ~(DcbDtrMask | DcbRtsMask);
            dcb.Flags |= DcbDtrEnable;
            if (rtsEnabled)
            {
                dcb.Flags |= DcbRtsEnable;
            }
            if (!SetCommState(handle, ref dcb))
            {
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "The inspection serial settings could not be applied.");
            }

            var timeouts = new CommTimeouts
            {
                ReadIntervalTimeout = 50,
                ReadTotalTimeoutMultiplier = 0,
                ReadTotalTimeoutConstant = 100,
                WriteTotalTimeoutMultiplier = 0,
                WriteTotalTimeoutConstant = 1000,
            };
            if (!SetCommTimeouts(handle, ref timeouts))
            {
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "The inspection serial timeouts could not be applied.");
            }

            return new WindowsNativeSerialConnection(handle);
        }
        catch
        {
            handle.Dispose();
            throw;
        }
    }

    internal void DiscardInput()
    {
        if (!PurgeComm(_handle, PurgeRxAbort | PurgeRxClear))
        {
            throw new Win32Exception(
                Marshal.GetLastWin32Error(),
                "The serial input could not be prepared for inspection.");
        }
    }

    internal void Write(ReadOnlySpan<byte> data)
    {
        if (data.IsEmpty)
        {
            throw new ArgumentException("Inspection serial writes cannot be empty.", nameof(data));
        }

        var buffer = data.ToArray();
        var offset = 0;
        while (offset < buffer.Length)
        {
            var remaining = new byte[buffer.Length - offset];
            Buffer.BlockCopy(buffer, offset, remaining, 0, remaining.Length);
            if (!WriteFile(
                    _handle,
                    remaining,
                    (uint)remaining.Length,
                    out var written,
                    IntPtr.Zero))
            {
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "The read-only runtime query could not be sent.");
            }

            if (written == 0)
            {
                throw new IOException("The serial interface accepted no query bytes.");
            }

            offset += checked((int)written);
        }
    }

    internal int Read(Span<byte> destination)
    {
        if (destination.IsEmpty)
        {
            return 0;
        }

        var buffer = new byte[destination.Length];
        if (!ReadFile(
                _handle,
                buffer,
                (uint)buffer.Length,
                out var read,
                IntPtr.Zero))
        {
            throw new Win32Exception(
                Marshal.GetLastWin32Error(),
                "The runtime response could not be read.");
        }

        var count = checked((int)read);
        buffer.AsSpan(0, count).CopyTo(destination);
        return count;
    }

    public void Dispose()
    {
        _handle.Dispose();
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct DeviceControlBlock
    {
        public uint Length;
        public uint BaudRate;
        public uint Flags;
        public ushort Reserved;
        public ushort XonLimit;
        public ushort XoffLimit;
        public byte ByteSize;
        public byte Parity;
        public byte StopBits;
        public byte XonChar;
        public byte XoffChar;
        public byte ErrorChar;
        public byte EofChar;
        public byte EventChar;
        public ushort Reserved1;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct CommTimeouts
    {
        public uint ReadIntervalTimeout;
        public uint ReadTotalTimeoutMultiplier;
        public uint ReadTotalTimeoutConstant;
        public uint WriteTotalTimeoutMultiplier;
        public uint WriteTotalTimeoutConstant;
    }

    [DllImport("kernel32.dll", EntryPoint = "CreateFileW",
        SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern SafeFileHandle CreateFileW(
        string fileName,
        uint desiredAccess,
        uint shareMode,
        IntPtr securityAttributes,
        uint creationDisposition,
        uint flagsAndAttributes,
        IntPtr templateFile);

    [DllImport("kernel32.dll", EntryPoint = "BuildCommDCBW",
        SetLastError = true, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool BuildCommDCBW(
        string definition,
        ref DeviceControlBlock dcb);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetCommState(
        SafeFileHandle file,
        ref DeviceControlBlock dcb);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetCommTimeouts(
        SafeFileHandle file,
        ref CommTimeouts timeouts);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PurgeComm(SafeFileHandle file, uint flags);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool WriteFile(
        SafeFileHandle file,
        [In] byte[] buffer,
        uint bytesToWrite,
        out uint bytesWritten,
        IntPtr overlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ReadFile(
        SafeFileHandle file,
        [Out] byte[] buffer,
        uint bytesToRead,
        out uint bytesRead,
        IntPtr overlapped);
}
