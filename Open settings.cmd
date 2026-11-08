@echo off
rem Double-click: opens the WristWake settings page (emulator) in the browser.
rem Start the emulator first and keep WristWake open on it.
title WristWake - Settings
for /f "delims=" %%u in ('wsl -d Ubuntu --cd "%~dp0." -- bash tools/settings-url.sh') do set URL=%%u
if "%URL%"=="ERROR" (
  echo The emulator is not running. Please double-click "Start emulator.cmd" first.
  pause
  exit /b 1
)
echo Opening %URL%
start "" "%URL%"
