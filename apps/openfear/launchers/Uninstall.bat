@echo off
rem Removes the Atmosfear files OpenFear installed (only what --install
rem itself wrote - never a visible "assets" folder you placed yourself).
"%~dp0openfear.exe" --uninstall
