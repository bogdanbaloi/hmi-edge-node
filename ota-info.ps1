param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    # How long to collect the answer. The host's own timeout is 2 s (spec
    # section 6), so an answer later than this is a failure anyway.
    [int]$WaitMs = 2000
)

# Asks the board what it runs: sends one INFO_REQ and shows every byte that
# comes back. The first proof that the board RECEIVES, before any flash code
# exists.
#
# The request is the worked example of industrial-hmi uart-flash-v1.md,
# section 3, byte for byte, so this checks the board against the spec rather
# than against this repo's own encoder:
#
#     A5  01  01 00  00 00  E9 CD      INFO_REQ, SEQ 1, no payload
#
# The expected answer, built by this repo's encoder (Src/ota_frame.c):
#
#     A5  81  01 00  06 00  01 00 00 00  01  00  F3 1C
#         INFO SEQ 1  LEN 6  version 1   bank 1 CONFIRMED  CRC16
#
# Close serial-monitor.ps1 first: a COM port has one owner at a time.

$request  = [byte[]](0xA5, 0x01, 0x01, 0x00, 0x00, 0x00, 0xE9, 0xCD)
$expected = [byte[]](0xA5, 0x81, 0x01, 0x00, 0x06, 0x00,
                     0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0xF3, 0x1C)

function ConvertTo-HexText([byte[]]$Bytes) {
    ($Bytes | ForEach-Object { $_.ToString("X2") }) -join " "
}

# Index of $Needle inside $Haystack, or -1.
function Find-Bytes([byte[]]$Haystack, [byte[]]$Needle) {
    for ($i = 0; $i -le $Haystack.Length - $Needle.Length; $i++) {
        $match = $true
        for ($j = 0; $j -lt $Needle.Length; $j++) {
            if ($Haystack[$i + $j] -ne $Needle[$j]) { $match = $false; break }
        }
        if ($match) { return $i }
    }
    return -1
}

$serial = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
$serial.ReadTimeout = 50
try {
    $serial.Open()
} catch {
    Write-Host "Cannot open $Port. Is serial-monitor.ps1 still running?" -ForegroundColor Red
    exit 1
}

try {
    $serial.DiscardInBuffer()
    Write-Host ("sent     " + (ConvertTo-HexText $request))
    $serial.Write($request, 0, $request.Length)

    $received = New-Object System.Collections.Generic.List[byte]
    $deadline = (Get-Date).AddMilliseconds($WaitMs)
    while ((Get-Date) -lt $deadline) {
        while ($serial.BytesToRead -gt 0) { $received.Add([byte]$serial.ReadByte()) }
        Start-Sleep -Milliseconds 10
    }
    $bytes = $received.ToArray()

    if ($bytes.Length -eq 0) {
        Write-Host "received nothing in $WaitMs ms" -ForegroundColor Red
        exit 1
    }
    Write-Host ("received " + (ConvertTo-HexText $bytes))

    if ((Find-Bytes $bytes $expected) -ge 0) {
        Write-Host "OK: the exact INFO expected: version 1, bank 1, CONFIRMED" -ForegroundColor Green
        exit 0
    }
    Write-Host "NOT the expected INFO. Compare with:" -ForegroundColor Yellow
    Write-Host ("expected " + (ConvertTo-HexText $expected))
    exit 1
} finally {
    $serial.Close()
}
