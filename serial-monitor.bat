@echo off
REM Double-click to open a live serial monitor on the Nucleo (COM3 @ 115200).
REM Pass a different port as the first argument, e.g.  serial-monitor.bat COM5
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0serial-monitor.ps1" %*
