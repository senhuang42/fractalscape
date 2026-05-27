#!/bin/bash
# Assemble FractalScape.app — a double-clickable macOS bundle that launches the
# interactive explorer. Run from the repo root (the Makefile `app` target does
# this for you after building ./fractal).
#
#   FractalScape.app/Contents/
#     Info.plist
#     MacOS/FractalScape   (launcher script -> exec fractal-bin explore)
#     MacOS/fractal-bin    (the real CLI binary)
#     Resources/shaders/   (GLSL, auto-discovered relative to the binary)
#     Resources/FractalScape.icns
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

APP="FractalScape.app"
CONTENTS="$APP/Contents"
ICON_SRC="${ICON_SRC:-assets/nebulabrot.png}"

[ -x ./fractal ] || { echo "error: ./fractal not built (run 'make' first)"; exit 1; }

rm -rf "$APP"
mkdir -p "$CONTENTS/MacOS" "$CONTENTS/Resources"

cp packaging/Info.plist "$CONTENTS/Info.plist"
cp ./fractal            "$CONTENTS/MacOS/fractal-bin"
cp packaging/launcher.sh "$CONTENTS/MacOS/FractalScape"
chmod +x "$CONTENTS/MacOS/FractalScape" "$CONTENTS/MacOS/fractal-bin"
cp -R shaders "$CONTENTS/Resources/shaders"

# Build a multi-resolution .icns from a source PNG, if the tools are present.
if [ -f "$ICON_SRC" ] && command -v sips >/dev/null && command -v iconutil >/dev/null; then
    ICONSET="$(mktemp -d)/FractalScape.iconset"
    mkdir -p "$ICONSET"
    for sz in 16 32 128 256 512; do
        sips -z $sz $sz       "$ICON_SRC" --out "$ICONSET/icon_${sz}x${sz}.png"     >/dev/null
        sips -z $((sz*2)) $((sz*2)) "$ICON_SRC" --out "$ICONSET/icon_${sz}x${sz}@2x.png" >/dev/null
    done
    iconutil -c icns "$ICONSET" -o "$CONTENTS/Resources/FractalScape.icns"
    rm -rf "$(dirname "$ICONSET")"
else
    echo "note: skipping icon (need $ICON_SRC + sips + iconutil)"
fi

# Unsigned local app: clear the quarantine bit so Gatekeeper doesn't block the
# first launch. (For distribution you'd codesign + notarize instead.)
xattr -dr com.apple.quarantine "$APP" 2>/dev/null || true

echo "built $APP — double-click it in Finder, or run: open $APP"
