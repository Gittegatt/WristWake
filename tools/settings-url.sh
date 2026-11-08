#!/bin/bash
# Prints the address of the settings page in the emulator (the emulator must be running
# with WristWake open). Used by "Open settings.cmd".
cd "$(dirname "$0")/.." || exit 1
UUID=$(grep -o '"uuid": *"[^"]*"' package.json | sed 's/.*"\([^"]*\)"$/\1/')
PORT=$(ps -eo args | grep "[p]ypkjs --qemu" | grep -o -- "--port [0-9]*" | awk '{print $2}' | head -1)
if [ -z "$PORT" ]; then
  echo "ERROR"
  exit 1
fi
echo "http://localhost:$PORT/config/$UUID/"
