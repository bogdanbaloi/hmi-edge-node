param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    # Seconds of telemetry recorded before the session starts, for pressing
    # the button: an update has to share the wire with this traffic.
    [int]$TelemetrySeconds = 12,
    [string]$Out = "update-session-capture.txt"
)

# Records a whole update session on the wire, with the times bytes arrived.
# What it deliberately puts in the capture, in this order:
#
#   INFO_REQ                 answered with INFO
#   BEGIN                    the board erases the spare bank, then ACK
#   DATA at 0                ACK
#   DATA at 256, bad CRC16   NAK BAD_CRC, because INSIDE a session a corrupted
#                            frame IS answered, unlike outside one
#   DATA at 256, clean       ACK, so a NAK is not remembered
#   COMMIT                   NAK FLASH_ERROR while the bank switch is piece 7
#
# The offsets matter: the board accepts DATA only at the offset it is waiting
# for, so a "repeat" of an earlier offset is refused with BAD_OFFSET. That is
# how the first version of this script was wrong, and the board was right.
#
# The protocol is industrial-hmi docs/protocols/uart-flash-v1.md.

$FRAME_START = 0xA5
# Two DATA frames worth, so the second one can be sent broken and then clean:
# a repeat only makes sense at the offset the board is waiting for.
$IMAGE_BYTES = 512
$CHUNK = 256

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

# $Spoil breaks the checksum, which is how the bad DATA frame is made.
function New-Frame([int]$Type, [int]$Seq, [byte[]]$Payload, [switch]$Spoil) {
    $body = New-Object System.Collections.Generic.List[byte]
    $body.Add([byte]$Type)
    $body.Add([byte]($Seq -band 0xFF)); $body.Add([byte](($Seq -shr 8) -band 0xFF))
    $body.Add([byte]($Payload.Length -band 0xFF)); $body.Add([byte](($Payload.Length -shr 8) -band 0xFF))
    if ($Payload.Length -gt 0) { $body.AddRange($Payload) }
    $crc = Get-Crc16 $body.ToArray()
    if ($Spoil) { $crc = $crc -bxor 0xFFFF }
    $frame = New-Object System.Collections.Generic.List[byte]
    $frame.Add([byte]$FRAME_START)
    $frame.AddRange($body)
    $frame.Add([byte]($crc -band 0xFF)); $frame.Add([byte](($crc -shr 8) -band 0xFF))
    return $frame.ToArray()
}

$lines = New-Object System.Collections.Generic.List[string]
function Log-Bytes([string]$dir, [byte[]]$bytes, [string]$note) {
    if ($bytes.Length -eq 0) { return }
    $hex = ($bytes | ForEach-Object { $_.ToString("X2") }) -join " "
    $txt = -join ($bytes | ForEach-Object {
        if ($_ -ge 0x20 -and $_ -le 0x7E) { [char]$_ }
        elseif ($_ -eq 0x0A) { "\n" } elseif ($_ -eq 0x0D) { "\r" } else { "." }
    })
    $script:lines.Add(("{0:HH:mm:ss.fff}  {1}  {2}  |{3}|{4}" -f (Get-Date), $dir, $hex, $txt,
                       $(if ($note) { "   # " + $note } else { "" })))
}

function Read-Available([System.IO.Ports.SerialPort]$Serial) {
    if ($Serial.BytesToRead -le 0) { return }
    Start-Sleep -Milliseconds 20
    $n = $Serial.BytesToRead
    $buf = New-Object byte[] $n
    [void]$Serial.Read($buf, 0, $n)
    Log-Bytes "RX" $buf ""
}

function Send-Frame([System.IO.Ports.SerialPort]$Serial, [byte[]]$Frame, [string]$Note, [int]$WaitMs) {
    Log-Bytes "TX" $Frame $Note
    $Serial.Write($Frame, 0, $Frame.Length)
    $end = (Get-Date).AddMilliseconds($WaitMs)
    while ((Get-Date) -lt $end) { Read-Available $Serial; Start-Sleep -Milliseconds 20 }
}

$serial = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
$serial.ReadTimeout = 50
$serial.Open()
$serial.DiscardInBuffer()

$lines.Add("# raw serial capture, COM3 @ 115200 8N1, " + (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))
$lines.Add("# one line per burst: time, direction, hex, printable rendering, and a note on sent frames")

Write-Host "press the USER button a few times, $TelemetrySeconds seconds of telemetry first"
$end = (Get-Date).AddSeconds($TelemetrySeconds)
while ((Get-Date) -lt $end) { Read-Available $serial; Start-Sleep -Milliseconds 20 }

$image = New-Object byte[] $IMAGE_BYTES
for ($i = 0; $i -lt $IMAGE_BYTES; $i++) { $image[$i] = [byte]($i % 251) }

$begin = New-Object System.Collections.Generic.List[byte]
$begin.AddRange([byte[]](Get-Le32 ([uint32]$IMAGE_BYTES)))
$begin.AddRange([byte[]](Get-Le32 (Get-Crc32 $image)))
$begin.AddRange([byte[]](Get-Le32 ([uint32]2)))

$first = New-Object System.Collections.Generic.List[byte]
$first.AddRange([byte[]](Get-Le32 ([uint32]0)))
$first.AddRange([byte[]]($image[0..($CHUNK - 1)]))

$second = New-Object System.Collections.Generic.List[byte]
$second.AddRange([byte[]](Get-Le32 ([uint32]$CHUNK)))
$second.AddRange([byte[]]($image[$CHUNK..($IMAGE_BYTES - 1)]))

Send-Frame $serial (New-Frame 0x01 1 @()) "INFO_REQ" 800
Send-Frame $serial (New-Frame 0x02 2 $begin.ToArray()) "BEGIN, the board erases a bank before answering" 1500
Send-Frame $serial (New-Frame 0x03 3 $first.ToArray()) "DATA at offset 0" 1000
Send-Frame $serial (New-Frame 0x03 4 $second.ToArray() -Spoil) "DATA at 256 with a DELIBERATELY broken CRC16" 1000
Send-Frame $serial (New-Frame 0x03 4 $second.ToArray()) "the same DATA again, clean: a NAK is never remembered" 1000
Send-Frame $serial (New-Frame 0x04 5 @()) "COMMIT: image verified, bank switch is piece 7" 1500

$serial.Close()
$lines | Set-Content -Path $Out -Encoding ASCII
Write-Host ("wrote {0} lines to {1}" -f $lines.Count, $Out)
