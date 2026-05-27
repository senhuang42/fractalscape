#!/bin/bash
# Bundle entry point. A double-clicked .app launches its CFBundleExecutable with
# no arguments and CWD=/, so this thin launcher just finds the real binary
# (next to it in Contents/MacOS/) and starts the interactive explorer. Shaders
# are auto-discovered from Contents/Resources/shaders relative to the binary.
DIR="$(cd "$(dirname "$0")" && pwd)"
exec "$DIR/fractal-bin" explore "$@"
