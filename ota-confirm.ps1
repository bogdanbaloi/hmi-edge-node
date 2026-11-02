param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    # The host's own timeout is 2 s (spec section 6), so an answer later than
    # this is a failure anyway.
    [int]$WaitMs = 2000
)

# Tells the board to KEEP the image it is running.
#
# Why this exists. An image written with ST-Link, which is every image
# STM32CubeIDE's Run button produces, starts in the TRIAL state: nobody has
# ever said it works. The board is built to protect itself from a trial image
# that hangs, so the first watchdog reset sends it back to the other bank, and
# it comes up running something older with no warning at all. That cost an
# evening on 2026-11-02: the board answered buttons but had silently gone back
# to version 5 in bank 2, and every measurement after that was of the wrong
# firmware.
#
# Nothing in the firmware needs changing for this. The protocol already has
# CONFIRM (uart-flash-v1.md, message 0x05) and the board already accepts it
# while idle. Only the host side was missing.
#
# Run it once after every flash from the IDE. Close serial-monitor.ps1 first:
# a COM port has one owner at a time.

$FRAME_START     = 0xA5
$MSG_INFO_REQ    = 0x01
$MSG_CONFIRM     = 0x05
$MSG_INFO        = 0x81
$MSG_ACK         = 0x82
$MSG_NAK         = 0x83
$STATE_CONFIRMED = 0x00
$NAK_FLASH_ERROR = 0x05
$INFO_PAYLOAD_LEN = 6
$HEADER_LEN       = 6   # start, type, seq lo, seq hi, len lo, len hi

# Where the record lives, from Inc/flash.h: FLASH_RUNNING_BASE plus
# FLASH_IMAGE_BYTES, which is the bank less its last 2 KB page. Quoted here
# only to print a recovery command, never used to address anything.
$CONFIRM_ADDRESS = "0x0807F800"
$CONFIRM_PAGE    = 255

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
    # Little-endian, low byte first, like LEN and every other field.
    $frame.Add([byte]($crc -band 0xFF)); $frame.Add([byte](($crc -shr 8) -band 0xFF))
    return $frame.ToArray()
}

function ConvertTo-HexText([byte[]]$Bytes) {
    ($Bytes | ForEach-Object { $_.ToString("X2") }) -join " "
}

# Index of the first frame of this type, or -1. The answer arrives mixed in
# with telemetry text, so the frame is SEARCHED for rather than assumed to
# start at byte zero.
function Find-Frame([byte[]]$Haystack, [int]$Type) {
    for ($i = 0; $i -le $Haystack.Length - 2; $i++) {
        if ($Haystack[$i] -eq $FRAME_START -and $Haystack[$i + 1] -eq $Type) {
            return $i
        }
    }
    return -1
}

$serial = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
$serial.ReadTimeout = 50

# Send one frame and collect everything that comes back within the window.
function Invoke-Frame([byte[]]$Frame, [string]$What) {
    $serial.DiscardInBuffer()
    Write-Host ("sent     " + $What + "  " + (ConvertTo-HexText $Frame))
    $serial.Write($Frame, 0, $Frame.Length)

    $received = New-Object System.Collections.Generic.List[byte]
    $deadline = (Get-Date).AddMilliseconds($WaitMs)
    while ((Get-Date) -lt $deadline) {
        while ($serial.BytesToRead -gt 0) { $received.Add([byte]$serial.ReadByte()) }
        Start-Sleep -Milliseconds 10
    }
    return $received.ToArray()
}

try {
    $serial.Open()
} catch {
    Write-Host "Cannot open $Port. Is serial-monitor.ps1 still running?" -ForegroundColor Red
    exit 1
}

try {
    # 1. Ask the board to keep what it runs.
    $answer = Invoke-Frame (New-Frame $MSG_CONFIRM 1 @()) "CONFIRM"
    if ($answer.Length -eq 0) {
        Write-Host "received nothing in $WaitMs ms" -ForegroundColor Red
        exit 1
    }
    Write-Host ("received " + (ConvertTo-HexText $answer))

    $nak = Find-Frame $answer $MSG_NAK
    if ($nak -ge 0) {
        $code = $answer[$nak + $HEADER_LEN]
        Write-Host ("NAK, reason code 0x" + $code.ToString("X2")) -ForegroundColor Red
        if ($code -eq $NAK_FLASH_ERROR) {
            # Seen on 2026-11-02 and worth spelling out, because the code
            # alone sends you looking at the flash driver when the driver is
            # working exactly as written.
            Write-Host ""
            Write-Host "That page already holds a record for a DIFFERENT image." -ForegroundColor Yellow
            Write-Host "The board refuses rather than answer OK to a confirmation that would not hold."
            Write-Host "A record cannot be overwritten, only erased, and an ST-Link flash does not"
            Write-Host "touch that page because it sits past the end of the image."
            Write-Host ""
            Write-Host "Erase the one page it lives in, then reset and run this again:"
            Write-Host ("  STM32_Programmer_CLI -c port=SWD -e " + $CONFIRM_PAGE)
            Write-Host "  STM32_Programmer_CLI -c port=SWD mode=hotplug -rst"
            Write-Host ("(page " + $CONFIRM_PAGE + " is " + $CONFIRM_ADDRESS + ", the last page of bank 1, and holds nothing else)")
        }
        exit 1
    }
    if ((Find-Frame $answer $MSG_ACK) -lt 0) {
        Write-Host "No ACK in the answer." -ForegroundColor Red
        exit 1
    }
    Write-Host "ACK" -ForegroundColor Green

    # 2. Read the state back. An ACK says the board accepted the request, not
    #    that the record in flash actually changed, and those are different
    #    claims. This is the one that matters.
    $info = Invoke-Frame (New-Frame $MSG_INFO_REQ 2 @()) "INFO_REQ"
    Write-Host ("received " + (ConvertTo-HexText $info))

    $at = Find-Frame $info $MSG_INFO
    if ($at -lt 0 -or ($info.Length - $at) -lt ($HEADER_LEN + $INFO_PAYLOAD_LEN)) {
        Write-Host "No readable INFO came back, so the state is unverified." -ForegroundColor Yellow
        exit 1
    }

    $version = [BitConverter]::ToUInt32($info, $at + $HEADER_LEN)
    $bank    = $info[$at + $HEADER_LEN + 4]
    $state   = $info[$at + $HEADER_LEN + 5]
    $stateText = if ($state -eq $STATE_CONFIRMED) { "CONFIRMED" } else { "TRIAL" }
    Write-Host ("version $version, bank $bank, $stateText")

    if ($state -eq $STATE_CONFIRMED) {
        Write-Host "OK: the running image is kept. A watchdog reset will not roll it back." -ForegroundColor Green
        exit 0
    }
    Write-Host "Still on trial. The board answered but the record did not change." -ForegroundColor Red
    exit 1
} finally {
    $serial.Close()
}
