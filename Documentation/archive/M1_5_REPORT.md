# Milestone 1.5 — zero-dependency decode path

Date: 2026-11-14. Everything below was run in this session unless marked otherwise.
Environment: Linux container, mingw-w64 GCC 13 (posix threads), Wine 9.0 + Xvfb + Mesa software GL, system ffmpeg/ffprobe as the *reference* only. **No real Windows, no real GPU, no MSVC.**

## 1. Result

| Item | State |
|---|---|
| FFmpeg in the Player | removed (0 references in `Apps/`, no headers, no libs, no DLLs) |
| Deliverable | one exe, 1.89 MB, static (`-static`, libstdc++/libgcc/winpthread linked in) |
| Imports (`objdump -p`) | `ADVAPI32 comdlg32 d3d11 D3DCOMPILER_47 dcomp GDI32 KERNEL32 MFPlat MFReadWrite msvcrt ole32 SHELL32 USER32` — all in System32 on Windows 10/11. `msvcrt` is the mingw CRT; the MSVC build uses `/MT` and imports none. Runtime-loaded when present: `dwmapi`, `uxtheme` |
| Demux | own `MkvDemux` (486 lines) |
| Decode | OS H.264 decoder MFT, `IMFTransform` driven directly (`MfDecoder`, 192 lines) |
| Test build under Wine | 18 / 18 matrix rows pass |

## 2. MkvDemux — verified natively against ffprobe / ffmpeg

`Tools/mkvtest` (same source file as the Player, built with native g++).

| Check | Result |
|---|---|
| Packet table (track, PTS ms, size, keyframe) vs `ffprobe -show_packets`, `dance_01.mkv` | 221 / 221 identical |
| Same, `dance_01_dual.mkv` | 442 / 442 identical |
| AVCC → Annex B elementary stream, decoded by ffmpeg, per-frame MD5 vs ffmpeg decoding the MKV | identical for packed, dual rgb, dual alpha (221 frames each) |
| avcC + SPS parse | High, level 3.2 / 3.1, 480×1984 / 480×992, 8-bit 4:2:0, limited range, `max_num_reorder_frames` 2 |
| ffmpeg `-live 1` remux (unknown-size Segment + Clusters, no Cues) | 221 / 221 identical |
| ffmpeg `-reserve_index_space` + 20 kB clusters (Cues in front) | 221 / 221 identical, 5 cues parsed |
| `SeekNs` 0 / 1999 / 2000 / 5000 / 9000 ms | lands on cue clusters 0 / 0 / 2000 / 4000 / 8000 ms, first packet is a keyframe |
| Synthetic file: Xiph, EBML and fixed lacing, BlockGroup with / without ReferenceBlock, Void + CRC-32 in a Cluster, negative relative timestamp, header-stripping track | frame sizes, timestamps, key flags as constructed |
| Synthetic rejections | zlib ContentCompression, unknown-size Info, element overrunning parent, EncryptedBlock, lace sizes > block, truncated file, non-EBML file → each fails with a specific message |

Not exercised: ContentEncryption track flag (code path is the same branch as zlib), blocks > 256 KiB read straight from disk (largest test frame is 54 kB), files > 4 GiB.

## 3. Decoder — what Wine proves and what it does not

Wine's `CLSID_CMSH264DecoderMFT` is GStreamer-backed (libav). It is **not Microsoft's decoder**; it validates the call sequence, not the vendor behaviour.

| Run under Wine (`Tools/build/wine_selftest.sh`) | shown-upto | presents | loops | dropped | unpaired | frames decoded |
|---|---|---|---|---|---|---|
| `packed` (3 s) | 70 | 73 | 0 | 1 | 0 | 77 |
| `dual` | 67 | 72 | 0 | 0 | 0 | 148 |
| `showfolder` | 67 | 71 | 0 | 0 | 0 | 74 |
| `edit` | 67 | 200 | 0 | 0 | 0 | 74 |
| `packed_sw` (`--sw --layered`) | 68 | 73 | 0 | 0 | 0 | 75 |
| `packed_20s` | 476 | 480 | 2 | 0 | 0 | 483 |
| `dual_20s` | 476 | 480 | 2 | 1 | 0 | 966 |
| `lowlatency` | 68 | 73 | 0 | 0 | 0 | 75 |
| `missing` | exit 3 | | | | | |
| `noclip` | exit 3 in 0.00 s | | | | | |
| `forcehw_probe` (`DIVAVERSE_FORCE_HW=1`) | 67 | 79 | 0 | 0 | 0 | 74 |

- 20 s: expected ≈ 481 presents at 24 fps; got 480 with two loop boundaries and 0 unpaired. "dropped" 0–1 per run varies between runs (software GL under Xvfb).
- **Frame-accurate check** (`--scale 1 --selftest-seconds 5`): the captured frame was compared with ffmpeg's decode of frames n−3…n+3. Packed and dual both: captured frame 116 = reference frame 116, alpha MAE 0.35 / 255 at offset 0 vs 4.4 at ±1. So PTS ↔ picture association through B-frame reordering, the frame-grid snap, and dual pairing are correct on this decoder.
- Stream change: the MFT raised `MF_E_TRANSFORM_STREAM_CHANGE` once per decoder; renegotiation to NV12 worked.
- Loop: drain → flush → rewind → SPS/PPS + IDR; no gap, no duplicate, frame numbers monotonic.
- `forcehw_probe`: with the NV12-sampling gate skipped, Wine's MFT **accepted the DXGI device manager** and switched to MFT-allocated samples; the first surface copy then failed because wined3d cannot create an NV12 texture, the Player logged `decoder failed (hw=1)` and restarted the clip in software within 25 ms. That exercises `MFT_MESSAGE_SET_D3D_MANAGER`, the provides-samples branch and the mid-stream HW→SW failover — **not** an actual D3D11VA decode.
- DComp requested → auto-fallback to layered (Wine has no `CreateSwapChainForComposition`).

### Not verified anywhere

| Item | Why | How to close |
|---|---|---|
| Microsoft's H.264 MFT (software and DXVA) | not in Wine | `run_selftest.bat` rows `sw_*`, `gpu_*` |
| D3D11VA decode, `IMFDXGIBuffer` → `CopySubresourceRegion` from a decoder texture-array slice | no GPU | `gpu_*` rows expect `"decode": "d3d11va"` (set only when every frame arrived as a D3D11 surface) |
| DirectComposition presentation | no DWM | `gpu_*` rows expect `"renderer": "dcomp"` |
| MSVC build of the `.sln` | no MSVC | open, Release x64, build |
| WASAPI output (device init, event loop, session volume) | no audio endpoint in the container | play a track from the tray |
| Themed menu on a real popup (measure/draw callbacks from `TrackPopupMenuEx`, system-arrow suppression, dark native dialogs via uxtheme #135) | paint code only ran through `--menutest` | open the tray menu in each theme |
| Click-through, tray, hotkeys, DPI, multi-monitor, CPU/GPU load | as in M0/M1 | manual |

Known risk on real hardware, in order of likelihood: (1) the MS decoder's output frame size is macroblock-aligned with a display aperture — handled (visible size comes from the SPS crop, plane offset from the negotiated frame height), but the test clips are already multiples of 16 so that branch never ran with padding; (2) `--lowlatency` with B-frames; (3) decoder surface pool exhaustion in dual layout if one track runs far ahead (pairing queues hold `IMFSample`s; the test clips interleave tightly).

## 4. Ported from the PORT list

| Module | Decision | Verified |
|---|---|---|
| `minijson` | **Not adopted.** It is not equivalent to `Json.cpp`: flat dotted-key table, 4096 entries, 128-char keys, 512-char values, `\uXXXX` → `?`. `Json.cpp` (92 lines) is a full tree with UTF-8 escapes, which manifests with non-ASCII titles need. `Json.cpp` stays. | — |
| `theme` | Ported to `Theme.*` (C++). Kept: registry dark-mode detection, runtime-linked DWM title bar (attr 20 → 19), DPI helpers. Added: System / Light / Dark / Pink palettes (ModernNotepad colours), uxtheme app mode for native menus/dialogs, owner-drawn popup menus. Tray menu → Theme. | `--menutest` in three themes under Wine (rendered and inspected) |
| `mf_audio` | Ported to `Audio.*`: file URL source reader instead of an in-memory byte stream, decode at the device mix format, WASAPI shared + event callback, leftover buffer, session volume. All COM/MF objects on the feeder thread. | decode path headless (`--audiotest`): WAV, MP3, M4A, FLAC, 3.00–3.03 s of a 3 s tone. WASAPI: not verified |
| `music` | **Rewritten, not ported.** The original parses another product's playlist file and data-directory layout, which the IP rules exclude. `Music.*` keeps only the generic shape (track list, enable flags, shuffle, add/remove) with its own list file (`music.txt`). | builds; playback not verified (needs an audio device) |

## 5. Regressions / behaviour changes vs 0.1

- HEVC clips no longer open (0.1 decoded them through FFmpeg). Rejected with a message; see FORMAT.md.
- `--selftest` JSON gained `decoder`, `demux`, `d3d_manager`, `gpu_frames`, `cpu_frames`, `stream_changes`, `loops`, `expect_failed`. `"decode": "d3d11va"` now means *observed* GPU surfaces, not "requested".

## 6. Recommended Milestone 2

1. **Close the hardware gap first** — one `run_selftest.bat` run on the RTX 4090; fix whatever the Microsoft decoder does differently before building on it.
2. **Multi-performer compositor**: one window + one swap chain per monitor, N `VideoSource`s drawn in one pass (instanced quads, per-performer constants, z-order by feet line). Replaces one-window-per-performer.
3. **Measure** CPU %, GPU %, VRAM, decode ms/frame at 1 / 4 / 8 performers, HW and SW, packed and dual, on the 4090 and on an iGPU. Decide from numbers: zero-copy (`MF_SA_D3D11_BINDFLAGS` + SRV on the decoder array) vs the current copy, slot-ring depth, whether a HAP-style local cache is needed.
4. **Playlists and transitions**: clip graph per show (intro / loop / outro), cross-fade between clips on the frame grid, two decoders alive only during the overlap.
5. **Music**: verify WASAPI, device-change handling (`IMMNotificationClient`), per-show music from the manifest, fade on pause.
6. **HEVC through the OS MFT** (same `MfDecoder`, hvcC → Annex B, VPS/SPS/PPS), gated on the extension being installed.
7. Remaining PORT-list items when a consumer exists: `png` + `mz_deflate` (thumbnails), `minizip` (show packages), `overlays`.

Not started, per instruction: Hub, store, Arcade, Lounge, Studio.
