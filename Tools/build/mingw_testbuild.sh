#!/usr/bin/env bash
# Cross-build of the Player with mingw-w64 (GCC 13, posix threads), fully static.
# The result must import Windows system DLLs only; the script fails otherwise.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
O=${O:-/tmp/dv_mgw}; rm -rf "$O"; mkdir -p "$O"
CXX="x86_64-w64-mingw32-g++-posix -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-cast-function-type -Wno-shadow -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00"
for f in "$ROOT"/Apps/Player/src/*.cpp; do $CXX -c "$f" -o "$O/$(basename "${f%.cpp}").o" & if (( $(jobs -r | wc -l) >= 6 )); then wait -n; fi; done; wait
x86_64-w64-mingw32-windres -I "$ROOT/Apps/Player/src" "$ROOT/Apps/Player/res/Player.rc" -O coff -o "$O/res.o"
x86_64-w64-mingw32-g++-posix -static -mwindows -municode -s "$O"/*.o -o "$O/Divaverse.exe" \
  -lmfplat -lmfreadwrite -lmfuuid -ld2d1 -ldwrite -ld3d11 -ldxgi -ldcomp -ld3dcompiler_47 -lwindowscodecs -lshell32 -lcomdlg32 -lgdi32 -luser32 -lole32 -luuid -ladvapi32
echo "built $O/Divaverse.exe ($(stat -c %s "$O/Divaverse.exe") bytes)"
ALLOW='^(KERNEL32|USER32|GDI32|SHELL32|ole32|OLEAUT32|ADVAPI32|comdlg32|COMCTL32|msvcrt|d3d11|dxgi|dcomp|D3DCOMPILER_47|MFPlat|MFReadWrite|WINMM|dwmapi|UxTheme|SHLWAPI|d2d1|DWrite|WindowsCodecs|api-ms-win-[a-z0-9-]+)\.(dll|DLL)$'
BAD=0
while read -r dll; do
  if echo "$dll" | grep -qiE "$ALLOW"; then echo "  import: $dll"; else echo "  NON-SYSTEM IMPORT: $dll"; BAD=1; fi
done < <(x86_64-w64-mingw32-objdump -p "$O/Divaverse.exe" | sed -n 's/^[[:space:]]*DLL Name: //p')
[ "$BAD" = 0 ] || { echo "FAIL: non-system DLL imports"; exit 1; }
echo "OK: system DLLs only"
