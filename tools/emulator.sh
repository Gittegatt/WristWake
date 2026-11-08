#!/bin/bash
# Builds WristWake and starts the app in the Pebble Time 2 emulator (emery).
# Used by "Start emulator.cmd" and the VS Code task.
export PATH="$HOME/.local/bin:$PATH"
export PYTHONWARNINGS=ignore   # hides harmless Python warnings of pebble-tool

cd "$(dirname "$0")/.." || exit 1

# Long presses in the emulator: keyboard auto-repeat starts after 0.4 s and splits the
# Pebble long press (about 0.5 s) into many short presses. It can only be turned off
# under X11, so force the emulator window onto X11 (newer Ubuntu versions use Wayland).
# Only affects WSL windows.
export SDL_VIDEODRIVER=x11 SDL_VIDEO_DRIVER=x11
xset r off 2>/dev/null

echo "== Building WristWake ..."
pebble build || { echo "!! Build failed"; exit 1; }

echo "== Starting emulator (Pebble Time 2) ..."
pebble install --emulator emery || { echo "!! Starting in the emulator failed"; exit 1; }

echo "== Done. The emulator window is open (maybe in the background)."
echo "   Keys: arrow up/down = Up/Down, arrow right = Select, arrow left = Back"
