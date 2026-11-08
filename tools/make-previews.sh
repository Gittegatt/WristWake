#!/bin/bash
# Recreates the theme preview images of the settings page (emulator must be running).
# Details: tools/make_previews.py "$@"
export PATH="$HOME/.local/bin:$PATH"
export PYTHONWARNINGS=ignore
cd "$(dirname "$0")/.." || exit 1
"$HOME/.local/share/uv/tools/pebble-tool/bin/python" tools/make_previews.py "$@"
