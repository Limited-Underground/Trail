using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace OpenTrail.Loader;

internal enum MeshCoreUsbRuntimeFamily
{
    Unknown = 0,
    HeltecV4Companion = 1,
    SenseCapSolarRepeater = 2,
    WioTrackerL1Companion = 3,
}

internal sealed record WindowsUsbSerialCandidate(
    string PrivatePortName,
    MeshCoreUsbRuntimeFamily RuntimeFamily);

/// <summary>
/// Enumerates present Windows Ports-class interfaces without CIM/WMI. Hardware
/// IDs and COM names remain private inputs to the bounded runtime probe.
/// </summary>
internal static class WindowsUsbSerialDiscovery
{
    private const uint DigcfPresent = 0x00000002;
    private const uint SpdrpHardwareId = 0x00000001;
    private const uint DicsFlagGlobal = 0x00000001;
    private const uint DiregDevice = 0x00000001;
    private const int KeyQueryValue = 0x0001;
    private const int ErrorNoMoreItems = 259;
    private const int ErrorSuccess = 0;
    private const int MaximumCandidates = 64;
    private const int MaximumRegistryBytes = 4096;
    private static readonly Guid PortsClassGuid =
        new("4d36e978-e325-11ce-bfc1-08002be10318");
    private static readonly IntPtr InvalidHandleValue = new(-1);

    internal static IReadOnlyList<WindowsUsbSerialCandidate> Discover(
        CancellationToken cancellationToken)
    {
        if (!OperatingSystem.IsWindows())
        {
            throw new PlatformNotSupportedException(
                "USB serial discovery requires Windows.");
        }

        var classGuid = PortsClassGuid;
        var deviceSet = SetupDiGetClassDevsW(
            ref classGuid,
            null,
            IntPtr.Zero,
            DigcfPresent);
        if (deviceSet == InvalidHandleValue)
        {
            throw new Win32Exception(
                Marshal.GetLastWin32Error(),
                "Windows USB serial discovery could not start.");
        }

        try
        {
            var candidates = new List<WindowsUsbSerialCandidate>();
            for (uint index = 0; ; index++)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var deviceInfo = new SpDevInfoData
                {
                    Size = (uint)Marshal.SizeOf<SpDevInfoData>(),
                };
                if (!SetupDiEnumDeviceInfo(deviceSet, index, ref deviceInfo))
                {
                    var error = Marshal.GetLastWin32Error();
                    if (error == ErrorNoMoreItems)
                    {
                        break;
                    }

                    throw new Win32Exception(
                        error,
                        "Windows USB serial discovery was interrupted.");
                }

                var hardwareIds = ReadDeviceProperty(
                    deviceSet,
                    ref deviceInfo,
                    SpdrpHardwareId);
                var family = ClassifyHardwareIds(hardwareIds);
                if (family == MeshCoreUsbRuntimeFamily.Unknown)
                {
                    continue;
                }

                var privatePortName = ReadPortName(deviceSet, ref deviceInfo);
                if (!WindowsSerialInspectionProvider.TryNormalizePortName(
                        privatePortName,
                        out var normalizedPortName))
                {
                    continue;
                }

                candidates.Add(new WindowsUsbSerialCandidate(
                    normalizedPortName,
                    family));
                if (candidates.Count > MaximumCandidates)
                {
                    throw new InvalidDataException(
                        "Windows reported too many allowlisted USB serial interfaces.");
                }
            }

            return candidates
                .OrderBy(static candidate => candidate.RuntimeFamily)
                .ThenBy(static candidate => candidate.PrivatePortName,
                    StringComparer.OrdinalIgnoreCase)
                .ToArray();
        }
        finally
        {
            _ = SetupDiDestroyDeviceInfoList(deviceSet);
        }
    }

    internal static MeshCoreUsbRuntimeFamily ClassifyHardwareIds(string value)
    {
        if (ContainsExactHardwareIdPair(value, "VID_303A&PID_0002"))
        {
            return MeshCoreUsbRuntimeFamily.HeltecV4Companion;
        }

        if (ContainsExactHardwareIdPair(value, "VID_2886&PID_0059"))
        {
            return MeshCoreUsbRuntimeFamily.SenseCapSolarRepeater;
        }

        if (ContainsExactHardwareIdPair(value, "VID_2886&PID_1667"))
        {
            return MeshCoreUsbRuntimeFamily.WioTrackerL1Companion;
        }

        return MeshCoreUsbRuntimeFamily.Unknown;
    }

    private static bool ContainsExactHardwareIdPair(string value, string pair)
    {
        var searchStart = 0;
        while (searchStart < value.Length)
        {
            var index = value.IndexOf(
                pair,
                searchStart,
                StringComparison.OrdinalIgnoreCase);
            if (index < 0)
            {
                return false;
            }

            var beforePair = index == 0 || value[index - 1] is '\\' or '\0';
            var afterIndex = index + pair.Length;
            var afterPair = afterIndex == value.Length ||
                value[afterIndex] is '&' or '\0';
            if (beforePair && afterPair)
            {
                return true;
            }

            searchStart = index + 1;
        }

        return false;
    }

    private static string ReadDeviceProperty(
        IntPtr deviceSet,
        ref SpDevInfoData deviceInfo,
        uint property)
    {
        var buffer = new byte[MaximumRegistryBytes];
        if (!SetupDiGetDeviceRegistryPropertyW(
                deviceSet,
                ref deviceInfo,
                property,
                out _,
                buffer,
                (uint)buffer.Length,
                out var requiredBytes))
        {
            return string.Empty;
        }

        if (requiredBytes == 0 || requiredBytes > buffer.Length)
        {
            return string.Empty;
        }

        return Encoding.Unicode.GetString(buffer, 0, (int)requiredBytes)
            .TrimEnd('\0');
    }

    private static string? ReadPortName(
        IntPtr deviceSet,
        ref SpDevInfoData deviceInfo)
    {
        var deviceKey = SetupDiOpenDevRegKey(
            deviceSet,
            ref deviceInfo,
            DicsFlagGlobal,
            0,
            DiregDevice,
            KeyQueryValue);
        if (deviceKey == InvalidHandleValue)
        {
            return null;
        }

        try
        {
            uint byteCount = 0;
            var first = RegQueryValueExW(
                deviceKey,
                "PortName",
                IntPtr.Zero,
                out _,
                null,
                ref byteCount);
            if (first != ErrorSuccess ||
                byteCount is < 2 or > MaximumRegistryBytes)
            {
                return null;
            }

            var buffer = new byte[byteCount];
            var second = RegQueryValueExW(
                deviceKey,
                "PortName",
                IntPtr.Zero,
                out _,
                buffer,
                ref byteCount);
            if (second != ErrorSuccess || byteCount < 2)
            {
                return null;
            }

            return Encoding.Unicode.GetString(buffer, 0, (int)byteCount)
                .TrimEnd('\0');
        }
        finally
        {
            _ = RegCloseKey(deviceKey);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct SpDevInfoData
    {
        public uint Size;
        public Guid ClassGuid;
        public uint DeviceInstance;
        public IntPtr Reserved;
    }

    [DllImport("setupapi.dll", EntryPoint = "SetupDiGetClassDevsW",
        SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr SetupDiGetClassDevsW(
        ref Guid classGuid,
        string? enumerator,
        IntPtr parentWindow,
        uint flags);

    [DllImport("setupapi.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetupDiEnumDeviceInfo(
        IntPtr deviceInfoSet,
        uint memberIndex,
        ref SpDevInfoData deviceInfoData);

    [DllImport("setupapi.dll", EntryPoint = "SetupDiGetDeviceRegistryPropertyW",
        SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetupDiGetDeviceRegistryPropertyW(
        IntPtr deviceInfoSet,
        ref SpDevInfoData deviceInfoData,
        uint property,
        out uint propertyRegistryDataType,
        [Out] byte[] propertyBuffer,
        uint propertyBufferSize,
        out uint requiredSize);

    [DllImport("setupapi.dll", SetLastError = true)]
    private static extern IntPtr SetupDiOpenDevRegKey(
        IntPtr deviceInfoSet,
        ref SpDevInfoData deviceInfoData,
        uint scope,
        uint hardwareProfile,
        uint keyType,
        int desiredAccess);

    [DllImport("setupapi.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetupDiDestroyDeviceInfoList(IntPtr deviceInfoSet);

    [DllImport("advapi32.dll", EntryPoint = "RegQueryValueExW",
        CharSet = CharSet.Unicode)]
    private static extern int RegQueryValueExW(
        IntPtr key,
        string valueName,
        IntPtr reserved,
        out uint type,
        [Out] byte[]? data,
        ref uint dataSize);

    [DllImport("advapi32.dll")]
    private static extern int RegCloseKey(IntPtr key);
}
