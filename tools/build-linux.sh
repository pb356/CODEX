#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
root="${DESKTOPBOXES_TOOLCHAIN:-/workspace/.toolchain/root}"
if [[ -x "$root/usr/bin/x86_64-w64-mingw32-g++-posix" ]]; then
 export PATH="$root/usr/bin:$PATH"
 export NSISDIR="$root/usr/share/nsis"
fi
mkdir -p build dist
cxx="${CXX_WINDOWS:-x86_64-w64-mingw32-g++-posix}"
(cd src && x86_64-w64-mingw32-windres --preprocessor=x86_64-w64-mingw32-gcc-posix --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED --codepage=65001 app.rc -O coff -o ../build/app.res)
"$cxx" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror \
 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -municode -mwindows \
 -static -static-libgcc -static-libstdc++ -Wl,--dynamicbase,--nxcompat,--high-entropy-va \
 src/main.cpp build/app.res -o build/DesktopBoxes.exe \
 -lgdiplus -lshell32 -lole32 -luuid -lcomdlg32 -lwtsapi32 -ldwmapi -lgdi32 -luser32 -ladvapi32
"$cxx" -std=c++17 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 \
 -static -static-libgcc -static-libstdc++ tests/core_tests.cpp -o build/core-tests.exe
"$cxx" -std=c++17 -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 \
 -municode -static -static-libgcc -static-libstdc++ tests/windows_tests.cpp -o build/windows-tests.exe -lshell32 -lole32 -luuid
"$cxx" -std=c++17 -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 \
 -municode -static -static-libgcc -static-libstdc++ tests/ui_smoke.cpp -o build/ui-smoke.exe \
 -lgdiplus -lshell32 -lole32 -luuid -lcomdlg32 -lwtsapi32 -ldwmapi -lgdi32 -luser32 -ladvapi32
makensis -V2 installer/setup.nsi
cp build/DesktopBoxes.exe dist/DesktopBoxes.exe
(cd dist && sha256sum DesktopBoxes.exe DesktopBoxes-0.1.0-x64-Setup.exe > SHA256SUMS.txt)
x86_64-w64-mingw32-objdump -p build/DesktopBoxes.exe > build/pe-info.txt
python3 tools/check-imports.py build/pe-info.txt
