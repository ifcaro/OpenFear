@echo off
rem Double-click: auto-detects a mounted CD/ISO. Drag a folder or .iso onto
rem this file to install from that source instead.
if "%~1"=="" (
    "%~dp0openfear.exe" --install
) else (
    "%~dp0openfear.exe" --assets "%~1" --install
)
