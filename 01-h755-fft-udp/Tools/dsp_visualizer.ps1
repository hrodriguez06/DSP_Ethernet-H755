param(
    [string]$Ip = "192.168.1.172",
    [int]$Port = 5002
)

$client = New-Object System.Net.Sockets.UdpClient(0)
$client.Client.ReceiveTimeout = 2500
$ep = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Parse($Ip), $Port)
$sender = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)

Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host " STM32H755 DUAL-CORE DSP + UDP ETHERNET TELEMETRY VISUALIZER" -ForegroundColor Cyan
Write-Host " Conectando a $Ip`:$Port..." -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan

try {
    $cmd = [System.Text.Encoding]::ASCII.GetBytes("STREAM")
    $null = $client.Send($cmd, $cmd.Length, $ep)
    $resp = $client.Receive([ref]$sender)
    Write-Host "Estado servidor: " -NoNewline
    Write-Host ([System.Text.Encoding]::ASCII.GetString($resp).Trim()) -ForegroundColor Green
} catch {
    Write-Host "Aviso: No se recibio confirmacion inmediata de STREAM. Iniciando escucha..." -ForegroundColor Yellow
}

Write-Host ""
Write-Host ("{0,8} | {1,-10} | {2,8} | {3,8} | {4,8} | {5,8} | {6,7} | {7,7} | {8,14}" -f "Bloque", "Estado", "f0 (Hz)", "Vpk (mV)", "Vdc (mV)", "Vpp (mV)", "SNR(dB)", "THD (%)", "H1..H5 (mV)") -ForegroundColor Yellow
Write-Host ("-" * 96)

try {
    while ($true) {
        try {
            $recv = $client.Receive([ref]$sender)
            if ($recv.Length -eq 60) {
                $magic = [System.BitConverter]::ToUInt32($recv, 0)
                if ($magic -eq 0x44535032) {
                    $block   = [System.BitConverter]::ToUInt32($recv, 4)
                    $ts      = [System.BitConverter]::ToUInt32($recv, 8)
                    $fs      = [System.BitConverter]::ToUInt32($recv, 12)
                    $nfft    = [System.BitConverter]::ToUInt32($recv, 16)
                    $f0_10   = [System.BitConverter]::ToUInt32($recv, 20)
                    $mag     = [System.BitConverter]::ToUInt32($recv, 24)
                    $noise   = [System.BitConverter]::ToUInt32($recv, 28)
                    $snr_10  = [System.BitConverter]::ToUInt32($recv, 32)
                    $thd_10  = [System.BitConverter]::ToUInt32($recv, 36)
                    $flags   = [System.BitConverter]::ToUInt32($recv, 40)
                    
                    $h1 = [System.BitConverter]::ToUInt16($recv, 44)
                    $h2 = [System.BitConverter]::ToUInt16($recv, 46)
                    $h3 = [System.BitConverter]::ToUInt16($recv, 48)
                    $h4 = [System.BitConverter]::ToUInt16($recv, 50)
                    $h5 = [System.BitConverter]::ToUInt16($recv, 52)
                    $vdc = [System.BitConverter]::ToUInt16($recv, 54)
                    $vpp = [System.BitConverter]::ToUInt16($recv, 56)

                    $f0  = $f0_10 / 10.0
                    $snr = $snr_10 / 10.0
                    $thd = $thd_10 / 10.0
                    $det = ($flags -band 0x02) -ne 0

                    $statusStr = if ($det) { "PERIODICO " } else { "  RUIDO   " }
                    $statusColor = if ($det) { "Green" } else { "DarkGray" }

                    $harmStr = "{0},{1},{2},{3},{4}" -f $h1, $h2, $h3, $h4, $h5

                    $line = "{0,8} | " -f $block
                    Write-Host $line -NoNewline
                    Write-Host $statusStr -ForegroundColor $statusColor -NoNewline
                    $lineRest = " | {0,8:N1} | {1,8} | {2,8} | {3,8} | {4,7:N1} | {5,7:N1} | {6,14}" -f $f0, $mag, $vdc, $vpp, $snr, $thd, $harmStr
                    Write-Host $lineRest
                }
            }
        } catch [System.Net.Sockets.SocketException] {
            $cmd = [System.Text.Encoding]::ASCII.GetBytes("STREAM")
            $null = $client.Send($cmd, $cmd.Length, $ep)
        }
    }
} finally {
    Write-Host "`nDeteniendo transmision UDP..." -ForegroundColor Cyan
    try {
        $cmd = [System.Text.Encoding]::ASCII.GetBytes("STOP")
        $null = $client.Send($cmd, $cmd.Length, $ep)
        $resp = $client.Receive([ref]$sender)
        Write-Host ([System.Text.Encoding]::ASCII.GetString($resp).Trim()) -ForegroundColor Green
    } catch {}
    $client.Close()
    Write-Host "Conexion cerrada." -ForegroundColor Cyan
}
