param([string]$Port = "COM3", [int]$Baud = 115200)

# Live serial monitor for the Nucleo. Streams whatever the board prints on
# the ST-Link virtual COM port until you press Ctrl+C. Auto-retries if the
# port is busy or the board is unplugged, so you can flash + replug freely.

Write-Host "Serial monitor on $Port @ $Baud baud. Ctrl+C to stop." -ForegroundColor Cyan

while ($true) {
    $p = $null
    try {
        $p = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
        $p.ReadTimeout = 2000
        $p.Open()
        Write-Host "[connected]" -ForegroundColor Green
        while ($true) {
            try {
                $line = $p.ReadLine()
                Write-Host $line
            } catch [System.TimeoutException] {
                # no data in the window; keep waiting
            }
        }
    } catch {
        Write-Host ("[waiting for $Port ... " + $_.Exception.Message + "]") -ForegroundColor DarkYellow
        Start-Sleep -Seconds 2
    } finally {
        if ($p -and $p.IsOpen) { $p.Close() }
    }
}
