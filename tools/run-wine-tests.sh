#!/usr/bin/env bash
# Optional compatibility check in an already prepared Wine + Xvfb development snapshot.
# This is not Windows desktop, installer, physical input, GPU or performance acceptance.
set -euo pipefail
cd "$(dirname "$0")/.."
: "${DISPLAY:?Start an isolated Xvfb display first; do not target an active user desktop}"
root="${DESKTOPBOXES_TOOLCHAIN:-/workspace/.toolchain/root}"
export WINEPREFIX="${DESKTOPBOXES_WINEPREFIX:-/workspace/.wine-desktopboxes}"
export WINEDLLPATH="$root/usr/lib/x86_64-linux-gnu/wine/x86_64-windows:$root/usr/lib/x86_64-linux-gnu/wine/x86_64-unix"
export LD_LIBRARY_PATH="$root/usr/lib/x86_64-linux-gnu:$root/usr/lib/x86_64-linux-gnu/wine/x86_64-unix${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export XDG_CACHE_HOME=/workspace/.cache
export WINEDEBUG=-all
for name in core-tests windows-tests ui-smoke; do
 "$root/usr/lib/wine/wine64" "$PWD/build/$name.exe" | tee "build/$name-wine.log"
done
