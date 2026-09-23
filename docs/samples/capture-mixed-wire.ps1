param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    [int]$Seconds = 45,
    [string]$Out = "capture.txt"
)

# Records every byte that arrives with the time it arrived, while sending a
# few flash-protocol frames, so the log holds telemetry text and binary frames
# on the SAME wire, including one frame with a broken checksum.

$goodInfoReq = [byte[]](0xA5,0x01,0x01,0x00,0x00,0x00,0xE9,0xCD)
$badCrcFrame = [byte[]](0xA5,0x01,0x02,0x00,0x00,0x00,0x00,0x00)
$secondInfo  = [byte[]](0xA5,0x01,0x03,0x00,0x00,0x00,0x81,0x20)

$serial = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
$serial.ReadTimeout = 50
$serial.Open()
$serial.DiscardInBuffer()

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# raw serial capture, COM3 @ 115200 8N1, " + (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))
$lines.Add("# one line per burst of bytes: time, direction, hex, then the printable rendering")

function Log-Bytes([string]$dir, [byte[]]$bytes) {
    if ($bytes.Length -eq 0) { return }
    $hex = ($bytes | ForEach-Object { $_.ToString("X2") }) -join " "
    $txt = -join ($bytes | ForEach-Object {
        if ($_ -ge 0x20 -and $_ -le 0x7E) { [char]$_ }
        elseif ($_ -eq 0x0A) { "\n" }
        elseif ($_ -eq 0x0D) { "\r" }
        else { "." }
    })
    $script:lines.Add(("{0:HH:mm:ss.fff}  {1}  {2}  |{3}|" -f (Get-Date), $dir, $hex, $txt))
}

$end = (Get-Date).AddSeconds($Seconds)
$sent = 0
$nextSend = (Get-Date).AddSeconds(6)
while ((Get-Date) -lt $end) {
    if ($serial.BytesToRead -gt 0) {
        Start-Sleep -Milliseconds 15          # let a burst gather
        $n = $serial.BytesToRead
        $buf = New-Object byte[] $n
        [void]$serial.Read($buf, 0, $n)
        Log-Bytes "RX" $buf
    }
    if ((Get-Date) -gt $nextSend -and $sent -lt 3) {
        $frame = switch ($sent) {
            0 { $goodInfoReq }
            1 { $badCrcFrame }
            2 { $secondInfo }
        }
        Log-Bytes "TX" $frame
        $serial.Write($frame, 0, $frame.Length)
        $sent++
        $nextSend = (Get-Date).AddSeconds(6)
    }
    Start-Sleep -Milliseconds 20
}
$serial.Close()
$lines | Set-Content -Path $Out -Encoding ASCII
Write-Host ("wrote {0} lines to {1}" -f $lines.Count, $Out)
