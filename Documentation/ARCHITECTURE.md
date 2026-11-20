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

## Robustness
- Settings, user data and the music library are written whole to `<file>.tmp`, flushed and renamed over the old file
  (`WriteFileAtomic`). `player.ini` is UTF-16 with a BOM so paths in any script survive.
- Unhandled exceptions, `abort`, pure calls and invalid CRT parameters write `crash\crash-<time>.dmp` + `.txt`
  (faulting module + offset, log tail) under `%LOCALAPPDATA%\Divaverse`; the next start says so once.
- Audio follows the Windows default output device and reopens in place when it changes or disappears.
- Decoding stops while the system sleeps, the display is off, the session is locked, a full-screen app runs, or the
  schedule keeps the performer off stage.
- The library scan runs on a worker thread and swaps its result in when complete; library folders are watched
  (`ReadDirectoryChangesW`, 2 s debounce). Measured under Wine: 300 clips in 715 ms cold, 234 ms warm, so there is
  no on-disk index; revisit if libraries reach thousands of clips or live on network drives.

## Diagnostics
Settings > Diagnostics: copy report, export diagnostics (report, logs, settings, newest crash into one folder),
performance overlay (also a shortcut, default Ctrl+Alt+D), and a 20 s GPU decode trace that logs every decoder call
and then exports the folder. CI (`.github/workflows/windows.yml`) builds with MSVC and runs smoke tests on Windows.

## Content plugins
`DvPlugin.h` is a self-contained C interface: `DvPluginInit` returns `scan / open / read / close`. The host side is
`Plugin.cpp` (loader, `plugin://<plugin id>/<item id>` addresses) plus three guarded call sites: `VideoSource`
(plugin frames are converted to the same colour + alpha plane pairs the decoder path produces, so the renderer, hit
mask, clock and seek are shared), `Library::AddPlugins` (items become clips grouped by the plugin's group id) and
`DecodePoster` (thumbnails). Divaverse contains no knowledge of any plugin's content or format; plugins live outside
this repository. Removing support: delete the DLL (run time) or build with `DV_PLUGINS=0` (compile time).

## Not implemented
Content Workshop (PNG import, calibration studio, encoding, MKV metadata baking), multiple performers,
GPU-decode hit mask and zero-copy pipeline, desktop behaviours (standing on windows, pose transitions),
launcher keyboard navigation and localisation.
