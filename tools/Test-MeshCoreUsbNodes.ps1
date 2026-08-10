[CmdletBinding()]
param(
    [string[]] $Port,
    [ValidateRange(1, 30)]
    [int] $ScanTimeoutSeconds = 3
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-MeshCli {
    $command = Get-Command meshcli -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $fallback = Join-Path $env:APPDATA 'Python\Python314\Scripts\meshcli.exe'
    if (Test-Path -LiteralPath $fallback) {
        return $fallback
    }

    throw 'meshcli was not found on PATH or in the expected Python user Scripts directory.'
}

function Invoke-MeshCoreJson {
    param(
        [Parameter(Mandatory)]
        [string] $MeshCli,
        [Parameter(Mandatory)]
        [string] $SerialPort,
        [Parameter(Mandatory)]
        [string[]] $Command
    )

    $output = & $MeshCli -j -s $SerialPort @Command 2>&1
    $text = ($output | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
    if ($LASTEXITCODE -ne 0 -or $text -match '(?m)^Error:') {
        throw "MeshCLI could not query $SerialPort. Make sure the MeshCore browser is disconnected. $text"
    }

    try {
        return $text | ConvertFrom-Json
    }
    catch {
        throw "MeshCLI returned unexpected output for $SerialPort. $text"
    }
}

$meshCli = Resolve-MeshCli

if (-not $Port) {
    $listOutput = & $meshCli -l -T $ScanTimeoutSeconds 2>&1
    $Port = @(
        $listOutput |
            ForEach-Object { $_.ToString() } |
            ForEach-Object {
                if ($_ -match '^\s*(COM\d+)\s+USB Serial Device.*VID:PID=303A:0002') {
                    $Matches[1]
                }
            } |
            Sort-Object -Unique
    )
}

if (-not $Port) {
    throw 'No Espressif USB Companion serial ports were found.'
}

foreach ($serialPort in $Port) {
    try {
        $version = Invoke-MeshCoreJson -MeshCli $meshCli -SerialPort $serialPort -Command @('ver')
        $info = Invoke-MeshCoreJson -MeshCli $meshCli -SerialPort $serialPort -Command @('infos')
        $core = Invoke-MeshCoreJson -MeshCli $meshCli -SerialPort $serialPort -Command @('get', 'stats_core')
        $radio = Invoke-MeshCoreJson -MeshCli $meshCli -SerialPort $serialPort -Command @('get', 'stats_radio')
        $packets = Invoke-MeshCoreJson -MeshCli $meshCli -SerialPort $serialPort -Command @('get', 'stats_packets')

        [pscustomobject]@{
            Port             = $serialPort
            Model            = $version.model
            Firmware         = $version.ver
            FirmwareDate     = $version.fw_build
            RepeatEnabled    = [bool] $version.repeat
            BatteryVolts     = [math]::Round($core.battery_mv / 1000, 3)
            UptimeSeconds    = $core.uptime_secs
            Errors           = $core.errors
            QueueLength      = $core.queue_len
            FrequencyMHz     = $info.radio_freq
            BandwidthKHz     = $info.radio_bw
            SpreadingFactor  = $info.radio_sf
            CodingRate       = $info.radio_cr
            TxPowerDbm       = $info.tx_power
            MaxTxPowerDbm    = $info.max_tx_power
            NoiseFloorDbm    = $radio.noise_floor
            TxAirtimeSeconds = $radio.tx_air_secs
            RxAirtimeSeconds = $radio.rx_air_secs
            PacketsSent      = $packets.sent
            PacketsReceived  = $packets.recv
            ReceiveErrors    = $packets.recv_errors
        }
    }
    catch {
        Write-Error -ErrorRecord $_ -ErrorAction Continue
    }
}
