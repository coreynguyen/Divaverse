# Milestone 0 — Media inspection & pipeline report

Date: 2026-11-14. All values measured with ffprobe / numpy in this session, not carried over from the previous handoff.

## 1. Source: `package.zip`

| File | Notes |
|---|---|
| `video.mp4` | the generation |
| `AI_Cutout_Tool.py` | 903 lines; background-removal GUI (rembg / transparent-background / MODNet). Inputs: **image folder or .mng only — no video input**. The "revised extractor" from the prior session is not in this upload. |
| `logo.png` 525×143, `profile.png` 616×1024, `content.png` 535×806 | RGB, no alpha |

`video.mp4`:

| Property | Measured |
|---|---|
| Video | H.264 High, yuv420p, **tv range, BT.709**, 480×992 |
| Frame rate | 24/1, CFR (every PTS step = 512 ticks @ 1/12288) |
| Frames | 243 (decoded count = container count) |
| Duration | 10.125 s |
| Audio | AAC LC, 32 kHz, stereo — **stripped** |
| Container tags | `prompt`, `workflow` (full ComfyUI graph JSON: model/LoRA filenames, free-text note nodes, seeds) — **stripped**. Never distribute generator outputs without `-map_metadata -1`. |

Handoff values (480×992, 24 fps, 10.125 s, 243 frames, AAC 32 kHz stereo) are confirmed.

## 2. Cutouts: `background removed.zip`

- 233 files `001.png … 233.png`, contiguous numbering, RGBA8, 480×992, no PNG metadata.
- **Alignment (resolved):** cutout *N* = source frame *N+10* for all 233 frames. Method: per-frame, mean-removed luma comparison over the cutout's opaque region against a ±3-frame window. Residual 0.70 mean / 0.76 max (8-bit), every frame's best match is exactly the expected index. **Source frames 1–10 (0.000–0.375 s) were dropped from the head; nothing skipped mid-sequence; nothing dropped at the tail.** Most likely cause: the frames handed to the cutout tool were extracted starting at ~0.417 s (or the first 10 were deleted). Not caused by the cutout tool itself (it processes every file in a folder).
- **Cutout RGB is not the source RGB.** After alignment it equals the source decode + ≈(+15, +17, +18) per channel, clipped at 255 (per-channel affine fit: gain ≈ 1.0, residual < 1). Highlights are lost. → The pipeline takes **colour from the source**, **alpha only** from the cutouts.
- **Alpha quality:** soft matte, 253 distinct partial levels, good hair detail. Defect: the body interior sits at **250–254, not 255** (~109 k px/frame), i.e. the performer is 1–2 % see-through. Fixed by a levels clamp (lo 4, hi 248).
- **Framing (content QA):** performer touches the top edge in 92 frames, left edge in 47, right edge in 29 → cropped head/hands, as the handoff observed. Feet baseline std 6.2 px, centroid std (6.1, 8.5) px → visible bob on the desktop. Not fixable in post; generation guideline issue.
- **Loop seam:** first vs last alpha MAD 62.5 (completely different pose) vs. 5.0 average consecutive-frame motion → hard cut on loop.

## 3. Output: `TestAssets/SHOW-000001/clips/`

Produced by `Tools/divapack/divapack.py` (reproducible; command in README):

| File | Layout | Frames | Size | Alpha MAE (decode-back) | RGB MAE (opaque) |
|---|---|---|---|---|---|
| `dance_01.mkv` | packed 480×1984, 1 track | 221 | 4.10 MB (3.56 Mb/s) | 0.147 | 1.39 |
| `dance_01_dual.mkv` | dual, 2 tracks 480×992 | 221 | 4.24 MB | 0.119 | 1.39 |

- H.264 High CRF 16, BT.709 limited, GOP 48, no audio, no container metadata, stream titles `divaverse:packed` / `divaverse:rgb` / `divaverse:alpha`.
- 233 → 221 frames: **baked 12-frame loop crossfade** (last 12 frames blended toward the first 12, first 12 removed). Seam MAD 62.5 → 10.5 (local motion at that point ≈ consecutive-frame range). Logged in the report, not silent.
- Edge decontamination: colour at soft edges / under transparency replaced by colour pulled from the opaque core (removes stage-purple fringing and makes the transparent region nearly free to encode).
- PTS: Matroska uses 1 ms ticks → 1/24 s alternates 41/42 ms. Verifier checks deviation from the ideal frame grid (max 1 tick); the Player snaps PTS to the grid.
- Per-clip `*.report.json` (full measurements) and `*.clip.json` (manifest entry) sit next to each MKV.

## 4. Player verification (this session)

Environment: Linux container, Wine 9.0 + Xvfb + Mesa software GL. **Not real Windows, no real GPU.** What that proves and doesn't:

| Run (`--selftest`) | Result |
|---|---|
| packed, software decode, layered present | pass — 69–71 frames in 3 s, 0–1 dropped, 0 unpaired, premultiplied output valid |
| dual-track, software decode | pass — 0 unpaired |
| show folder + edit overlay | pass |
| 20 s run (2 loop boundaries), packed and dual | pass — 477 frames shown (expected ≈481), 0 dropped, 0 unpaired, no re-anchor |
| DComp requested (Wine lacks it) | auto-fallback to layered → pass (this run found and fixed a fallback bug that quit the app) |
| HW decode requested (Wine has no D3D11 video) | auto-fallback to software → pass |
| missing file | fails fast, exit 3 |

**Not verified anywhere yet:** DirectComposition presentation, D3D11VA decode + NV12 SRV path, click-through over real apps, tray/hotkeys on Explorer, multi-monitor/DPI, CPU/GPU usage numbers. These need one run on your machine: `run_selftest.bat` in the test build covers the first two.
