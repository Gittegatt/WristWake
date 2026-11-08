@echo off
rem Double-click: builds WristWake and opens the Pebble Time 2 emulator.
title WristWake - Emulator
wsl -d Ubuntu --cd "%~dp0." -- bash tools/emulator.sh
echo.
pause
