# Changelog

## 0.7.0
- Wet-Rash: new mode "Spear Run" (menu item 2), a side view of the same riders. You ride at the left with two
  rivals ahead; Left / Right move, Up hop, Down duck (the sit-to-tuck frames), J / pad X throw. Rivals take three
  hits, blink red when hit, drop back when knocked out and rejoin; they throw back. 90 second round, score.
- Rider, bikini, ski and hair colours and the weather setting carry over from the race and back.
- Side sprites are drawn as the artist layered them (ski body, rider, hull over her feet, steering handle). In the
  throw frames the handle is rotated and scaled about its pivot on the ski so the grip stays in her hand. The spear
  painted in the last throw frame is removed at bake time and replaced by the separate spear sprite as a projectile.
- Generated for this mode: layered swell, far sea with the existing prop sprites as parallax, clouds, sun, rain,
  lightning, spray, exhaust jet, ponytail strands and bikini tie strings.
- QA scenes: --uishot 46 (sun, simulated), 47 (storm, simulated), 48 (frozen throw poses).

## 0.6.2
- Wet-Rash: props are baked shaded pixel-art sprites (posterised, dithered, outlined) instead of flat vector
  shapes; three courses (Lagoon, Reef Run, Open Sea) with their own length, bends and hazard density; chest
  secondary motion (sprung slice of the upper bikini region); hull chatter on chop.
- Player: playback queue ("+" on a clip tile, "Up next" panel on On Stage with reorder / play now / remove /
  clear; persists; plays before any repeat or shuffle and never interrupts the current clip).
- Player: "Next monitor" moves the performer to the next display.

## 0.6.1 (Wet-Rash pass)
- Throttle reaches top speed in about two seconds, top speed is about 65 % higher, steering is direct (no drift).
- Obstacles only bump and splash; lane ropes (pool-style floats) push you back in; only rivals knock you off.
  Remount is faster and comes with speed and a boost; a trailing player gets a 10 % top-speed lift.
- Air tricks: hold taunt off a ramp to spin; landing pays points and a full boost. Hits score too. Score on the HUD and results.
- Camera higher and further back, ski drawn about 20 % smaller; nose-up ski frame only in the air or on boost.
- Hull sits in the water (translucent sheet + foam at the waterline, bow wave), mirror image on the surface,
  stronger wet highlight, water running off the rider, finer three-tone hair, longer and thicker surface streaks.
- Divaverse music pauses while the game window is open.

## 0.6
- Wet-Rash mini game (rail button): jet-ski brawl racer on a pseudo-3D course that rides moving swells. You and two
  bots; punch, kick, tuck, taunt-for-boost; rocks, crates, logs, buoys, ramps; islands, boats, piers, lighthouses;
  sunny / rain / thunderstorm / hurricane; rider, bikini, ski and hair colours; keyboard and XInput controller;
  synthesised engine, spray, rain, wind, thunder, hits and a music loop. Rider and ski key frames are baked from the
  artist's PSD layers by `Tools/wetrash`; hair and bikini ties are simulated strings; a wet-look highlight map is
  generated per sprite.

## 0.5
- Music: volume is applied per stream (the shared session volume let interface sounds overwrite it at start);
  recursive folder import; title / artist / album from file tags; albums view with embedded or folder cover art;
  weighted shuffle that learns from skips, replays and completions; favourites; loudness levelling; playlists
  stored as M3U8 with M3U / M3U8 / PLS / WPL import; play statistics kept in memory and flushed at most once a minute.
- Library: fuzzy search over diva, show, clip and tag; sort by name / newest / rating; user star ratings; user tags;
  diva page with its own tall banner artwork and clips grouped by show; "NEW" markers.
- New-content reveal after a scan (box, burst, card), with its own sounds.
- Seasonal backdrop (winter scene and snow, hearts, petals, leaves, pumpkins); hearts in the pink theme.
- Glass panels; gentler card lean; wheel closes with an animation; quick-hide is silent.
- Dragging: the drop back to the taskbar is optional.
- Editor mode: authored density / height / floor line / anchor adjustable live, saved as a sidecar override.
- Spec: artwork aspect standards, `banner`, stage density standard SD-420.
- Documentation consolidated (ARCHITECTURE.md + spec); old roadmaps archived.

## 0.4.1
Wheel menu, free drag with gravity, interface sounds, dithered glows in all themes, embedded clip thumbnails.

## 0.4
Dual-track MKV with independent resolutions, embedded metadata + sidecar merge, physical scale, seek, hit mask,
library rework, stall recovery fixes.
