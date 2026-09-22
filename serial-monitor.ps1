param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    # A line left without a line end for this long is printed anyway, marked.
    [int]$IdleMs = 100,
    [string]$LogDir = (Join-Path $PSScriptRoot "logs")
)

# Live serial monitor for the Nucleo, with a log you can read afterwards.
#
# Every line is shown with the time its FIRST byte reached the PC, and every
# byte that is not printable ASCII is shown as hex, like [FF 00], instead of
# being hidden. The same lines go to logs/serial-<date>-<time>.log.
#
# WHY RAW BYTES AND NOT ReadLine
# The previous version read with SerialPort.ReadLine. That did two misleading
# things at once, and both showed up the first time a reset was tested:
#
#   1. Its default ASCII decoding turns every byte above 0x7F into '?'. The
#      garbage a reset puts on the line appeared as ????? and the real values
#      were lost in the monitor, not on the wire.
#   2. It waits for '\n'. Reset garbage has no line end, so it stayed glued to
#      the front of the NEXT frame, which arrives seconds later, and was shown
#      as if it belonged to that frame.
#
# WHY THE IDLE FLUSH
# A real frame reaches the PC in a few milliseconds. Bytes that sit without a
# line end for $IdleMs are printed on their own line, marked "(no line end)",
# under the time they actually arrived. That is what separates the reset
# garbage from the first frame after it.
#
# WHAT THE TIME MEANS
# When the PC received the bytes, not when the board sent them. USB between
# the ST-Link and the PC adds a few milliseconds. Good for ordering events and
# for gaps between them, not for timing the firmware itself.
#
# Auto-retries if the port is busy or the board is unplugged, so you can flash
# and replug freely. Ctrl+C to stop. The log is flushed line by line, so
# stopping never loses what was already on screen.

# Printable ASCII, except the two brackets: those are reserved for the hex
# groups, so every [..] in the output is unambiguously raw bytes.
function Test-Printable([byte]$B) {
    return ($B -ge 0x20 -and $B -le 0x7E -and $B -ne 0x5B -and $B -ne 0x5D)
}

# Renders bytes as text, with each run of non-printable bytes as one [XX XX]
# group. Nothing is dropped and nothing is replaced by a placeholder.
function Format-Bytes([System.Collections.Generic.List[byte]]$Bytes) {
    $sb  = New-Object System.Text.StringBuilder
    $hex = New-Object System.Collections.Generic.List[string]
    foreach ($b in $Bytes) {
        if (Test-Printable $b) {
            if ($hex.Count -gt 0) {
                [void]$sb.Append('[' + ($hex -join ' ') + ']')
                $hex.Clear()
            }
            [void]$sb.Append([char]$b)
        } else {
            $hex.Add($b.ToString('X2'))
        }
    }
    if ($hex.Count -gt 0) { [void]$sb.Append('[' + ($hex -join ' ') + ']') }
    return $sb.ToString()
}

function Out-Line([datetime]$At, [string]$Text, [string]$Color) {
    $line = $At.ToString("HH:mm:ss.fff") + "  " + $Text
    if ($Color) { Write-Host $line -ForegroundColor $Color } else { Write-Host $line }
    if ($script:log) { $script:log.WriteLine($line) }
}

# Prints the bytes collected so far as one line, then starts a fresh one.
# $Marker is empty for a line that ended with '\n', and says why otherwise.
function Write-PendingLine([string]$Marker) {
    $text = Format-Bytes $script:pending
    $color = $null
    if ($Marker) {
        $text = $text + "   " + $Marker
        $color = "DarkYellow"
    }
    Out-Line $script:pendingStart $text $color
    $script:pending.Clear()
    $script:pendingStart = $null
}

# Feeds bytes that arrived at $Now into the line being built.
function Add-Bytes([byte[]]$Buffer, [int]$Count, [datetime]$Now) {
    for ($i = 0; $i -lt $Count; $i++) {
        $b = $Buffer[$i]
        if ($b -eq 0x0A) {
            if ($script:pending.Count -eq 0) {
                Out-Line $Now "(empty line)" "DarkGray"
            } else {
                Write-PendingLine ""
            }
        } else {
            if ($script:pending.Count -eq 0) { $script:pendingStart = $Now }
            $script:pending.Add($b)
        }
    }
    $script:lastByteAt = $Now
}

# Prints an unfinished line once it has been quiet for $IdleMs.
function Invoke-IdleFlush([datetime]$Now) {
    if ($script:pending.Count -gt 0 -and
        ($Now - $script:lastByteAt).TotalMilliseconds -ge $IdleMs) {
        Write-PendingLine "(no line end)"
    }
}

$script:pending      = New-Object System.Collections.Generic.List[byte]
$script:pendingStart = $null
$script:lastByteAt   = $null
$script:log          = $null

# Dot-sourcing this file (". .\serial-monitor.ps1") loads the functions above
# without opening any port, so they can be checked on their own.
if ($MyInvocation.InvocationName -eq '.') { return }

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$logPath = Join-Path $LogDir ("serial-" + (Get-Date -Format "yyyy-MM-dd-HHmmss") + ".log")
$script:log = New-Object System.IO.StreamWriter($logPath, $false, (New-Object System.Text.UTF8Encoding($false)))
$script:log.AutoFlush = $true
$script:log.WriteLine("# serial monitor, $Port @ $Baud baud, started " + (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))

Write-Host "Serial monitor on $Port @ $Baud baud. Ctrl+C to stop." -ForegroundColor Cyan
Write-Host "Log: $logPath" -ForegroundColor Cyan

$buf = New-Object byte[] 4096
$waitAnnounced = $false
try {
    while ($true) {
        $p = $null
        try {
            $p = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
            $p.Open()
            Out-Line (Get-Date) "[connected]" "Green"
            $waitAnnounced = $false
            while ($true) {
                $n = $p.BytesToRead
                if ($n -gt 0) {
                    $got = $p.Read($buf, 0, [Math]::Min($n, $buf.Length))
                    Add-Bytes $buf $got (Get-Date)
                } else {
                    Invoke-IdleFlush (Get-Date)
                    Start-Sleep -Milliseconds 5
                }
            }
        } catch {
            if ($script:pending.Count -gt 0) { Write-PendingLine "(no line end, port lost)" }
            # Said once per outage, not every 2 s, so the log stays readable.
            if (-not $waitAnnounced) {
                Out-Line (Get-Date) ("[waiting for $Port ... " + $_.Exception.Message + "]") "DarkYellow"
                $waitAnnounced = $true
            }
            Start-Sleep -Seconds 2
        } finally {
            if ($p -and $p.IsOpen) { $p.Close() }
        }
    }
} finally {
    if ($script:pending.Count -gt 0) { Write-PendingLine "(no line end, monitor stopped)" }
    $script:log.Close()
}
