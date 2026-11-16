# Divaverse Player: architecture and current requirements

This file and `DIVAVERSE_MKV_INPUT_SPEC.md` are the only authoritative documents. Everything in `archive/` is
historical (earlier milestones, the packed-default format decision) and must not be used as instructions.

## Fixed rules
- One static Windows x64 exe that imports system DLLs only (static CRT). No FFmpeg, Qt, .NET, Python at runtime.
- C++20, Win32, Direct3D 11, DirectComposition (layered-window fallback), Media Foundation, Direct2D/DirectWrite, WASAPI, WIC.
- Builds with Visual Studio 2022 (`Divaverse.sln`, `/utf-8`, sources pure ASCII).
- The Player reads content; it never analyses pictures, removes backgrounds, tracks subjects or writes into media files.
- Media contract: `DIVAVERSE_MKV_INPUT_SPEC.md`. Dual-track H.264 MKV is the preferred form; packed and legacy manifests still play.
- Nothing from other products (code, formats, names, endpoints, assets).

## Modules (Apps/Player/src)
App (performer window, clock, watchdog, hotkeys, tray, settings) - Ui (launcher pages) - Wheel / Flyout (menus) -
Gfx (Direct2D canvas, palettes, icons) - Meta (probe, metadata merge, validation codes) - Library (scan, grouping,
artwork worker) - Image (WIC, thumbnail cache) - MkvDemux - MfDecoder - VideoSource (decode thread, pairing, seek,
hit mask) - Renderer + Shaders - Audio (WASAPI, per-stream gain, loudness levelling) - Music (library, tags, albums,
weighted shuffle, playlists) - Synth (stations) - Sfx (interface sounds) - UserData (ratings, tags, seen, weights) -
WetRash (mini game: simulation, pseudo-3D renderer, game audio; art baked by Tools/wetrash into res/wetrash).

## User data (never inside content)
`%APPDATA%\Divaverse`: `player.ini`, `userdata.txt`, `music.txt`, `Playlists\*.m3u8`.
`%LOCALAPPDATA%\Divaverse`: `player.log`, `player.prev.log`, `cache\` (generated thumbnails).

## Testing
`Tools/build/mingw_testbuild.sh` then `Tools/build/wine_selftest.sh` (Wine + Xvfb): CPU decode and layered
presentation only. DirectComposition, GPU decode, audio output, tag reading and album art need Windows.

## Not implemented
Content Workshop (PNG import, calibration studio, encoding, MKV metadata baking), multiple performers,
asynchronous library scan, GPU-decode hit mask, saved clip playlists, configurable hotkeys.
