#!/usr/bin/env bash
# Builds the known-good reference clips described in Documentation/DIVAVERSE_MKV_INPUT_SPEC.md from an existing
# dual-track clip and a cover image. Offline tool: needs ffmpeg + ffprobe. Nothing here ships inside the Player.
#   make_reference.sh <dual.mkv> <packed.mkv> <cover image> <out dir> [banner image]
set -euo pipefail
DUAL=$1; PACKED=$2; COVER=$3; OUT=$4; BANNER=${5:-}; T=$(mktemp -d); mkdir -p "$OUT"
ffmpeg -v error -y -i "$COVER" -vf "scale=616:-2" -q:v 3 "$T/cover.jpg"
ffmpeg -v error -y -i "$COVER" -vf "scale=240:-2" -q:v 4 "$T/small_cover.jpg"
meta() { # clip_id title order layout extra-video-json
cat <<J
{
  "schema_version": 1,
  "diva_id": "DIVA-000001", "diva_name": "Luna",
  "show_id": "SHOW-000001", "show_title": "First Session",
  "clip_id": "$1", "title": "$2", "order": $3,
  "diva": { "rating": 5, "profile": { "Age": "23", "Height": "168 cm", "Type": "Dancer", "Style": "Playful" } },
  "video": { "layout": "$4"$5 },
  "canvas": { "width": 480, "height": 992 },
  "physical": { "actor_height_m": 1.68, "pixels_per_meter": 567, "ground_anchor": [0.5, 0.9597] },
  "loop": { "mode": "loop" },
  "artwork": { "cover": "cover.jpg", "thumb": "small_cover.jpg", "banner": "banner.png" },
  "content": { "origin": "AI_GENERATED", "rating": "ADULT" }
}
J
}
ATT=(-attach "$T/divaverse.json" -metadata:s:t:0 mimetype=application/json -metadata:s:t:0 filename=divaverse.json
     -attach "$T/cover.jpg" -metadata:s:t:1 mimetype=image/jpeg -metadata:s:t:1 filename=cover.jpg
     -attach "$T/small_cover.jpg" -metadata:s:t:2 mimetype=image/jpeg -metadata:s:t:2 filename=small_cover.jpg)
# optional tall banner for the diva page (1:3 standard; PNG keeps a cut-out's transparency)
if [ -n "$BANNER" ]; then cp "$BANNER" "$T/banner.png"; ATT+=(-attach "$T/banner.png" -metadata:s:t:3 mimetype=image/png -metadata:s:t:3 filename=banner.png); fi
# 1) preferred form: two H.264 tracks. Colour is copied; alpha is re-encoded at HALF resolution and FULL range
#    (0 = transparent, 255 = opaque) to exercise independent track sizes and the explicit alpha range.
meta luna_sway "Sway" 1 dual "" > "$T/divaverse.json"
ffmpeg -v error -y -i "$DUAL" -map 0:v:0 -c:v:0 copy \
  -map 0:v:1 -c:v:1 libx264 -preset:v:1 slow -crf:v:1 16 -bf:v:1 0 -g:v:1 24 -pix_fmt:v:1 yuv420p -filter:v:1 "scale=240:496:in_range=tv:out_range=pc:flags=bicubic" -color_range:v:1 pc \
  -metadata:s:v:0 title=divaverse:rgb -metadata:s:v:1 title=divaverse:alpha -metadata title="Sway" -metadata DIVAVERSE_SCHEMA=1 \
  "${ATT[@]}" -an -sn "$OUT/luna_sway.mkv"
# 2) packed form (colour above, alpha below, one track), same metadata contract.
meta luna_sway_packed "Sway (packed layout)" 2 packed ', "rgb_rect": [0, 0, 480, 992], "alpha_rect": [0, 992, 480, 992]' > "$T/divaverse.json"
ffmpeg -v error -y -i "$PACKED" -map 0:v:0 -c copy -metadata title="Sway (packed layout)" -metadata DIVAVERSE_SCHEMA=1 "${ATT[@]}" -an -sn "$OUT/luna_sway_packed.mkv"
for f in "$OUT"/luna_sway.mkv "$OUT"/luna_sway_packed.mkv; do
  echo "== $f"; ffprobe -v error -show_entries stream=index,codec_name,codec_type,width,height,color_range,nb_read_frames:stream_tags=title,filename,mimetype -count_frames -of compact "$f"
done
rm -rf "$T"
