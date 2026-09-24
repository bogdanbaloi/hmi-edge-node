param(
    [Parameter(Mandatory = $true)]
    [string]$Image,                 # a real firmware .bin
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    # The version the image reports in INFO once it runs. It goes into BEGIN
    # so the host can tell, after the switch, whether the new image booted.
    [int]$Version = 2,
    [int]$WaitMs = 2000
)

# Sends a REAL firmware image to the board over the UART flash protocol:
# BEGIN, then DATA until the image is in the spare bank, then COMMIT.
#
# What a good run looks like depends on how the board was built:
#
#   NAK 05 at COMMIT   the image is in flash and verified, and this build
#                      refuses the bank switch (no OTA_BANK_SWITCH_ARMED)
#   ACK at COMMIT      the board wrote its option bytes and is about to reset
#                      into the new image
#
# NAK 06 at COMMIT means the image in flash does not match the CRC32 announced
# in BEGIN, which is a transfer or a flash problem, not a refusal.
#
# Making the .bin, from the repo root:
#   arm-none-eabi-objcopy -O binary Debug/hmi-edge-node.elf image.bin
#
# Close serial-monitor.ps1 first: a COM port has one owner at a time.

$FRAME_START = 0xA5
$MSG_BEGIN   = 0x02
$MSG_DATA    = 0x03
$MSG_COMMIT  = 0x04
$MSG_ACK     = 0x82
$MSG_NAK     = 0x83
# The most image bytes one DATA frame can carry (260 payload, 4 for the offset).
$CHUNK = 256
# Flash writes double words, so every DATA carries a multiple of 8 image bytes.
$WRITE_GRANULARITY = 8
# Erased flash reads as 0xFF, so padding with it costs nothing to write.
$PAD = 0xFF

function Get-Crc16([byte[]]$Bytes) {
    $crc = 0xFFFF
    foreach ($b in $Bytes) {
        $crc = $crc -bxor ([int]$b -shl 8)
        for ($i = 0; $i -lt 8; $i++) {
            if ($crc -band 0x8000) { $crc = (($crc -shl 1) -bxor 0x1021) } else { $crc = $crc -shl 1 }
            $crc = $crc -band 0xFFFF
        }
    }
    return $crc
}

function Get-Crc32([byte[]]$Bytes) {
    $crc = [uint32]::MaxValue
    foreach ($b in $Bytes) {
        $crc = $crc -bxor [uint32]$b
        for ($i = 0; $i -lt 8; $i++) {
            if ($crc -band 1) { $crc = ([uint32](($crc -shr 1) -bxor 0xEDB88320)) } else { $crc = [uint32]($crc -shr 1) }
        }
    }
    return [uint32]($crc -bxor [uint32]::MaxValue)
}

function Get-Le32([uint32]$Value) {
    return [byte[]]@(($Value -band 0xFF), (($Value -shr 8) -band 0xFF),
                     (($Value -shr 16) -band 0xFF), (($Value -shr 24) -band 0xFF))
}

function New-Frame([int]$Type, [int]$Seq, [byte[]]$Payload) {
    $body = New-Object System.Collections.Generic.List[byte]
    $body.Add([byte]$Type)
    $body.Add([byte]($Seq -band 0xFF)); $body.Add([byte](($Seq -shr 8) -band 0xFF))
    $body.Add([byte]($Payload.Length -band 0xFF)); $body.Add([byte](($Payload.Length -shr 8) -band 0xFF))
    if ($Payload.Length -gt 0) { $body.AddRange($Payload) }
    $crc = Get-Crc16 $body.ToArray()
    $frame = New-Object System.Collections.Generic.List[byte]
    $frame.Add([byte]$FRAME_START)
    $frame.AddRange($body)
    $frame.Add([byte]($crc -band 0xFF)); $frame.Add([byte](($crc -shr 8) -band 0xFF))
    return $frame.ToArray()
}

function ConvertTo-HexText([byte[]]$Bytes) {
    ($Bytes | ForEach-Object { $_.ToString("X2") }) -join " "
}

function Invoke-Exchange([System.IO.Ports.SerialPort]$Serial, [string]$What,
                         [byte[]]$Frame, [int]$Wait, [switch]$Quiet) {
    $Serial.DiscardInBuffer()
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $Serial.Write($Frame, 0, $Frame.Length)
    $got = New-Object System.Collections.Generic.List[byte]
    $end = (Get-Date).AddMilliseconds($Wait)
    while ((Get-Date) -lt $end) {
        while ($Serial.BytesToRead -gt 0) { $got.Add([byte]$Serial.ReadByte()) }
        if ($got.Count -ge 8) { break }
        Start-Sleep -Milliseconds 2
    }
    $clock.Stop()
    $bytes = $got.ToArray()
    $answer = "nothing"
    if ($bytes.Length -ge 2 -and $bytes[0] -eq $FRAME_START) {
        if ($bytes[1] -eq $MSG_ACK) { $answer = "ACK" }
        elseif ($bytes[1] -eq $MSG_NAK -and $bytes.Length -ge 7) {
            $answer = "NAK " + $bytes[6].ToString("X2")
        }
    }
    if (-not $Quiet -or $answer -ne "ACK") {
        Write-Host ("{0,-22} {1,-10} {2,6} ms  {3}" -f $What, $answer, $clock.ElapsedMilliseconds, (ConvertTo-HexText $bytes))
    }
    return $answer
}

# The script's own checksums are checked against the catalogue values before
# the port is opened, so a wrong script cannot accuse a right board.
$checkString = [byte[]][char[]]"123456789"
$crc32Want = [Convert]::ToUInt32("CBF43926", 16)
if ((Get-Crc16 $checkString) -ne 0x29B1 -or (Get-Crc32 $checkString) -ne $crc32Want) {
    Write-Host "This script computes the wrong checksums, stopping." -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $Image)) {
    Write-Host "No such image: $Image" -ForegroundColor Red
    exit 1
}
$bytes = [System.IO.File]::ReadAllBytes($Image)
$size = $bytes.Length
$crc32 = Get-Crc32 $bytes
Write-Host ("image {0}, {1} bytes, CRC32 {2:X8}, announced version {3}" -f
            (Split-Path $Image -Leaf), $size, $crc32, $Version)

# Flash writes double words, so the LAST frame is padded up to a multiple of 8
# with erased-flash bytes. The CRC32 still covers only the real image bytes,
# which is what the board checks, so the padding changes nothing.
$padded = $bytes
$remainder = $size % $WRITE_GRANULARITY
if ($remainder -ne 0) {
    $padded = New-Object byte[] ($size + $WRITE_GRANULARITY - $remainder)
    [Array]::Copy($bytes, $padded, $size)
    for ($i = $size; $i -lt $padded.Length; $i++) { $padded[$i] = $PAD }
    Write-Host ("padded to {0} bytes with 0x{1:X2} for the last double word" -f $padded.Length, $PAD)
}

$serial = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
$serial.ReadTimeout = 50
try { $serial.Open() } catch {
    Write-Host "Cannot open $Port. Is serial-monitor.ps1 still running?" -ForegroundColor Red
    exit 1
}

try {
    $begin = New-Object System.Collections.Generic.List[byte]
    $begin.AddRange([byte[]](Get-Le32 ([uint32]$size)))
    $begin.AddRange([byte[]](Get-Le32 ([uint32]$crc32)))
    $begin.AddRange([byte[]](Get-Le32 ([uint32]$Version)))
    # BEGIN erases a whole bank before it answers, so it waits longer.
    if ((Invoke-Exchange $serial "BEGIN" (New-Frame $MSG_BEGIN 1 $begin.ToArray()) ($WaitMs + 1000)) -ne "ACK") {
        Write-Host "BEGIN was refused, stopping." -ForegroundColor Red
        exit 1
    }

    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $seq = 2
    for ($offset = 0; $offset -lt $padded.Length; $offset += $CHUNK) {
        $count = [Math]::Min($CHUNK, $padded.Length - $offset)
        $payload = New-Object System.Collections.Generic.List[byte]
        $payload.AddRange([byte[]](Get-Le32 ([uint32]$offset)))
        $payload.AddRange([byte[]]($padded[$offset..($offset + $count - 1)]))
        $answer = Invoke-Exchange $serial ("DATA at " + $offset) (New-Frame $MSG_DATA $seq $payload.ToArray()) $WaitMs -Quiet
        if ($answer -ne "ACK") {
            Write-Host ("DATA at {0} was refused, stopping." -f $offset) -ForegroundColor Red
            exit 1
        }
        $seq = ($seq + 1) -band 0xFFFF
    }
    $clock.Stop()
    Write-Host ("{0} bytes in {1} frames, {2:N1} s on the wire" -f $padded.Length,
                [Math]::Ceiling($padded.Length / $CHUNK), ($clock.ElapsedMilliseconds / 1000.0))

    $answer = Invoke-Exchange $serial "COMMIT" (New-Frame $MSG_COMMIT $seq @()) ($WaitMs + 2000)
    switch ($answer) {
        "ACK"    { Write-Host "OK: the board took the image and is switching banks" -ForegroundColor Green; exit 0 }
        "NAK 05" { Write-Host "OK: image verified in flash; this build refuses the bank switch" -ForegroundColor Green; exit 0 }
        "NAK 06" { Write-Host "VERIFY_FAILED: what is in flash does not match the CRC32 announced" -ForegroundColor Red; exit 1 }
        default  { Write-Host "Unexpected answer to COMMIT: $answer" -ForegroundColor Yellow; exit 1 }
    }
} finally {
    $serial.Close()
}
