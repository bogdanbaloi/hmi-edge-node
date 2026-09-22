param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    # Bytes of the fake image. A multiple of 8, the flash write granularity.
    [int]$ImageBytes = 256,
    # Send a wrong CRC32 in BEGIN, to see VERIFY_FAILED instead.
    [switch]$Corrupt,
    # Check this script's own checksums against the standard values and exit.
    [switch]$SelfTest,
    [int]$WaitMs = 2000
)

# Drives a whole update session against the board: BEGIN, DATA, COMMIT.
# It is the host side of industrial-hmi uart-flash-v1.md, written small: it
# builds every frame itself, checksums included, and decodes the answers.
#
# WHAT A PASS LOOKS LIKE WHILE PIECE 7 IS MISSING
#   BEGIN   -> ACK           the spare bank was erased
#   DATA    -> ACK           the bytes went into flash
#   COMMIT  -> NAK 05        FLASH_ERROR: the image verified, and the board
#                            then refused to switch banks, which is piece 7.
# So NAK 05 at COMMIT is the proof that the CRC32 over what is IN FLASH
# matched what BEGIN announced. Run it with -Corrupt and the same session must
# end in NAK 06, VERIFY_FAILED: that is the control, and without it a board
# that stored nothing could not be told from one that stored everything.
#
# Close serial-monitor.ps1 first: a COM port has one owner at a time.

$FRAME_START = 0xA5
$MSG_BEGIN   = 0x02
$MSG_DATA    = 0x03
$MSG_COMMIT  = 0x04
$MSG_ACK     = 0x82
$MSG_NAK     = 0x83
# The board writes image bytes in chunks of 256, the most a DATA frame carries.
$DATA_CHUNK  = 256

function Get-Crc16([byte[]]$Bytes) {
    # CRC-16/CCITT-FALSE, over TYPE..PAYLOAD, never over the 0xA5.
    $crc = 0xFFFF
    foreach ($b in $Bytes) {
        $crc = $crc -bxor ([int]$b -shl 8)
        for ($i = 0; $i -lt 8; $i++) {
            if ($crc -band 0x8000) { $crc = (($crc -shl 1) -bxor 0x1021) }
            else { $crc = $crc -shl 1 }
            $crc = $crc -band 0xFFFF
        }
    }
    return $crc
}

function Get-Crc32([byte[]]$Bytes) {
    # CRC-32/ISO-HDLC, the variant pinned in section 4 of the spec.
    $crc = [uint32]::MaxValue
    foreach ($b in $Bytes) {
        $crc = $crc -bxor [uint32]$b
        for ($i = 0; $i -lt 8; $i++) {
            if ($crc -band 1) { $crc = ([uint32](($crc -shr 1) -bxor 0xEDB88320)) }
            else { $crc = [uint32]($crc -shr 1) }
        }
    }
    return [uint32]($crc -bxor [uint32]::MaxValue)
}

function New-Frame([int]$Type, [int]$Seq, [byte[]]$Payload) {
    $body = New-Object System.Collections.Generic.List[byte]
    $body.Add([byte]$Type)
    $body.Add([byte]($Seq -band 0xFF)); $body.Add([byte](($Seq -shr 8) -band 0xFF))
    $len = $Payload.Length
    $body.Add([byte]($len -band 0xFF)); $body.Add([byte](($len -shr 8) -band 0xFF))
    if ($len -gt 0) { $body.AddRange($Payload) }
    $crc = Get-Crc16 $body.ToArray()
    $frame = New-Object System.Collections.Generic.List[byte]
    $frame.Add([byte]$FRAME_START)
    $frame.AddRange($body)
    # Little-endian, like LEN and every other field: low byte first. The
    # spec's worked example ends "E9 CD", which is the value 0xCDE9.
    $frame.Add([byte]($crc -band 0xFF)); $frame.Add([byte](($crc -shr 8) -band 0xFF))
    return $frame.ToArray()
}

function ConvertTo-HexText([byte[]]$Bytes) {
    ($Bytes | ForEach-Object { $_.ToString("X2") }) -join " "
}

# Every element is parenthesised: a comma binds tighter than -band, so
# "$Value -band 0xFF, ..." would make the FIRST element an array.
function Get-Le32([uint32]$Value) {
    return [byte[]]@(($Value -band 0xFF),
                     (($Value -shr 8) -band 0xFF),
                     (($Value -shr 16) -band 0xFF),
                     (($Value -shr 24) -band 0xFF))
}

# Reads the answer to one frame and says in words what came back.
function Invoke-Exchange([System.IO.Ports.SerialPort]$Serial, [string]$What,
                         [byte[]]$Frame, [int]$Wait) {
    $Serial.DiscardInBuffer()
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $Serial.Write($Frame, 0, $Frame.Length)
    $got = New-Object System.Collections.Generic.List[byte]
    $end = (Get-Date).AddMilliseconds($Wait)
    while ((Get-Date) -lt $end) {
        while ($Serial.BytesToRead -gt 0) { $got.Add([byte]$Serial.ReadByte()) }
        if ($got.Count -ge 8) { break }
        Start-Sleep -Milliseconds 5
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
    # Write-Host, not bare output: anything written to the pipeline would be
    # returned together with $answer, and the caller would compare an array.
    # The time includes the frame on the wire, about 23 ms for a full DATA
    # and under 1 ms for a COMMIT, so for COMMIT it is mostly the board.
    Write-Host ("{0,-22} {1,-10} {2,6} ms  {3}" -f $What, $answer, $clock.ElapsedMilliseconds, (ConvertTo-HexText $bytes))
    return $answer
}

# Both checksums are reimplemented here, so they are checked against the
# catalogue check values rather than against the board. A script that computed
# a wrong CRC32 would accuse the board of corrupting an image it stored fine.
$checkString = [byte[]][char[]]"123456789"
$crc16Check = Get-Crc16 $checkString
$crc32Check = Get-Crc32 $checkString
# Written through Convert, not as 0xCBF43926: PowerShell 5.1 reads a hex
# literal above int32 as a NEGATIVE number, so the comparison would fail
# against a CRC that is perfectly right.
$crc32Want = [Convert]::ToUInt32("CBF43926", 16)
if ($crc16Check -ne 0x29B1 -or $crc32Check -ne $crc32Want) {
    Write-Host ("This script computes CRC16 {0:X4} (want 29B1) and CRC32 {1:X8} (want CBF43926)" -f $crc16Check, $crc32Check) -ForegroundColor Red
    exit 1
}
if ($SelfTest) {
    Write-Host ("self test OK: CRC16 {0:X4}, CRC32 {1:X8}" -f $crc16Check, $crc32Check) -ForegroundColor Green
    exit 0
}

$image = New-Object byte[] $ImageBytes
for ($i = 0; $i -lt $ImageBytes; $i++) { $image[$i] = [byte]($i % 251) }
$crc32 = Get-Crc32 $image
if ($Corrupt) { $crc32 = $crc32 -bxor 1 }
Write-Host ("image {0} bytes, CRC32 {1:X8}{2}" -f $ImageBytes, $crc32,
            $(if ($Corrupt) { " (deliberately wrong)" } else { "" }))

$serial = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
$serial.ReadTimeout = 50
try { $serial.Open() } catch {
    Write-Host "Cannot open $Port. Is serial-monitor.ps1 still running?" -ForegroundColor Red
    exit 1
}

try {
    $payload = New-Object System.Collections.Generic.List[byte]
    $payload.AddRange([byte[]](Get-Le32 ([uint32]$ImageBytes)))
    $payload.AddRange([byte[]](Get-Le32 ([uint32]$crc32)))
    $payload.AddRange([byte[]](Get-Le32 ([uint32]2)))   # the new image's version
    # BEGIN erases a whole bank before answering, so it gets the longest wait.
    $answer = Invoke-Exchange $serial "BEGIN" (New-Frame $MSG_BEGIN 1 $payload.ToArray()) ($WaitMs + 1000)
    if ($answer -ne "ACK") { Write-Host "BEGIN was refused, stopping." -ForegroundColor Red; exit 1 }

    $seq = 2
    for ($offset = 0; $offset -lt $ImageBytes; $offset += $DATA_CHUNK) {
        $count = [Math]::Min($DATA_CHUNK, $ImageBytes - $offset)
        $chunk = New-Object System.Collections.Generic.List[byte]
        $chunk.AddRange([byte[]](Get-Le32 ([uint32]$offset)))
        $chunk.AddRange([byte[]]($image[$offset..($offset + $count - 1)]))
        $answer = Invoke-Exchange $serial ("DATA at " + $offset) (New-Frame $MSG_DATA $seq $chunk.ToArray()) $WaitMs
        if ($answer -ne "ACK") { Write-Host "DATA was refused, stopping." -ForegroundColor Red; exit 1 }
        $seq++
    }

    $answer = Invoke-Exchange $serial "COMMIT" (New-Frame $MSG_COMMIT $seq @()) $WaitMs
    if ($Corrupt) {
        if ($answer -eq "NAK 06") {
            Write-Host "OK: VERIFY_FAILED, as it must be for a wrong CRC32" -ForegroundColor Green
            exit 0
        }
    } elseif ($answer -eq "NAK 05") {
        Write-Host "OK: the image verified in flash; FLASH_ERROR is the bank switch, piece 7" -ForegroundColor Green
        exit 0
    }
    Write-Host "Unexpected answer to COMMIT: $answer" -ForegroundColor Yellow
    exit 1
} finally {
    $serial.Close()
}
