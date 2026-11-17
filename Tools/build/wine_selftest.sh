#!/usr/bin/env bash
# Playback regression matrix for the mingw test build under Wine + Xvfb (developer tool, not part of the product).
#   Tools/build/mingw_testbuild.sh && Tools/build/wine_selftest.sh
# Needs: wine (64-bit prefix in $WINEPREFIX), Xvfb on $DISPLAY, the reference clips in TestAssets/Divas.
# Wine has no DirectComposition and no D3D11 video decoder, so this covers CPU decode + layered presentation only;
# DirectComposition and GPU decode can only be exercised on Windows.
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd); EXE=${EXE:-/tmp/dv_mgw/Divaverse.exe}
R=$(mktemp -d); cp "$EXE" "$R/"; cp -r "$ROOT/TestAssets/Divas" "$R/"; cd "$R"
export WINEDEBUG=-all; FAIL=0
t() { local name=$1; shift
  timeout 120 wine Divaverse.exe --selftest "Z:$R/$name.bmp" --selftest-seconds "${SECS:-12}" --layered "$@" >/dev/null 2>&1; local rc=$?
  local why; why=$(tr -d '\n' < "$R/$name.bmp.json" 2>/dev/null | grep -o '"expect_failed": "[^"]*"')
  if [ $rc = 0 ]; then echo "PASS $name"; else echo "FAIL $name (rc=$rc) $why"; FAIL=1; fi; }
D="Z:$R/Divas/Luna/luna_sway.mkv"; P="Z:$R/Divas/Luna/luna_sway_packed.mkv"
t dual_loop      --sw --expect layout=dual --expect loops=1 --expect unpaired=0 --expect anomalies=0 "$D"
t packed_loop    --sw --expect layout=packed --expect loops=1 --expect anomalies=0 "$P"
t legacy_show    --sw --expect loops=1 "Z:$ROOT/TestAssets/SHOW-000001"
t hit_and_scale  --sw --expect hit=10 --expect width=356 "$D"
for f in 0 7 150 219; do DIVAVERSE_QA_SEEK=$f SECS=6 t "seek_dual_$f" --sw --expect seek=$f --expect unpaired=0 "$D"; done
DIVAVERSE_QA_SEEK=133 SECS=6 t seek_packed_133 --sw --expect seek=133 "$P"
DIVAVERSE_QA_STALL=1 t stall_fallback --hw --expect decode=software "$D"
DIVAVERSE_QA_DROP=1 SECS=6 t gravity_drop --sw --expect floor=1 "$D"
timeout 30 wine Divaverse.exe --sfxtest "Z:$R/sfx.wav" >/dev/null 2>&1 && echo "PASS sfx_synthesis" || { echo "FAIL sfx_synthesis"; FAIL=1; }
timeout 60 wine Divaverse.exe --selftest "Z:$R/wh.bmp" --selftest-seconds 20 --layered --sw --qa-wheel "Z:$R/wheel.bmp" "$D" >/dev/null 2>&1 && echo "PASS wheel_render" || { echo "FAIL wheel_render"; FAIL=1; }
printf '{ "physical": { "pixels_per_meter": 1134 }, "media": { "fps": 60 } }' > Divas/Luna/luna_sway.divaverse.json
t sidecar_scale  --sw --expect width=178 "$D"; rm -f Divas/Luna/luna_sway.divaverse.json
mkdir -p mus/A/Disc1 mus/B; for i in 1 2 3; do ffmpeg -v error -y -f lavfi -i "sine=frequency=$((300+i*110)):duration=2" mus/A/Disc1/t$i.mp3; done; ffmpeg -v error -y -f lavfi -i "sine=frequency=880:duration=2" mus/B/loose.wav
printf '#EXTM3U\r\nA\\Disc1\\t1.mp3\r\nB/loose.wav\r\n' > mus/mix.m3u
DIVAVERSE_QA_MUSIC="Z:$R/mus" timeout 60 wine Divaverse.exe --uishot 11 "Z:$R/albums.bmp" >/dev/null 2>&1
grep -q 'qa music: added 4.*with 2 entries' "$WINEPREFIX"/drive_c/users/*/AppData/Local/Divaverse/player.log && echo "PASS music_recursive_import_and_m3u" || { echo "FAIL music_recursive_import_and_m3u"; FAIL=1; }
for pg in 0 1 2 3 10 11 20 21 22 30 31; do timeout 60 wine Divaverse.exe --theme dark --uishot $pg "Z:$R/ui$pg.bmp" "$D" >/dev/null 2>&1 && echo "PASS ui_page_$pg" || { echo "FAIL ui_page_$pg"; FAIL=1; }; done
# Wet-Rash: 41 = 19 s of real simulation in sun, 42 = the same in a hurricane, 45 = a whole race to the results screen
for sc in 41 42 45 40 43 44 46 47 48; do timeout 240 wine Divaverse.exe --uishot $sc "Z:$R/wetrash$sc.bmp" >/dev/null 2>&1 && echo "PASS wetrash_scene_$sc" || { echo "FAIL wetrash_scene_$sc"; FAIL=1; }; done
echo "artifacts in $R"; exit $FAIL
