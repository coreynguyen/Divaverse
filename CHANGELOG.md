# Changelog

## 0.15.0 (albums, content plugins)
- Music: the options (Crossfade, Even loudness, Shuffle) move to their own row when the window is too narrow to hold
  them beside the tabs; they no longer overlap.
- Albums open like folders: click an album to see its songs in file order, play one, play the album, favourite or
  disable songs, or take the album out of the library. "Add album" imports a folder (sub-folders included) as one
  album named after the folder, whatever the files' tags say.
- Content plugins: Divaverse loads `Plugins\*.dll` beside the exe (interface: `Apps/Player/src/DvPlugin.h`, plain C).
  A plugin lists items and hands over BGRA frames; its items appear in the library with thumbnails and play on the
  stage like any clip (looping, seeking, click hit-testing, queue, statistics). Video only. Remove the DLL and the
  items are gone; `DV_PLUGINS 0` compiles the support out. `Tools/plugin_sample` is a working example and skeleton.

## 0.14.0 (library and music)
- Play statistics per clip (times staged, seconds actually shown, last played), kept in userdata.txt.
- Library search conditions: fav, unseen, season, tag:x, rating>=n / rating:n, plays>=n / plays:n, stale:days;
  plain words still match names, titles and tags. Sort by Watched.
- Collections: preset chips (Unseen, Top rated, Not lately, Never played, In season) and "Save search" for your own.
  "In season" matches the tag season:winter / spring / summer / autumn for the current month.
- Settings > Library tools: Check the library (duplicate clips by size + content sample, unreadable files, metadata
  warnings; writes a report, changes nothing); export / import of ratings, tags and favourites as a
  .dvcollection file keyed by diva id (no paths, no media, no history; imports never overwrite your own ratings).
- Music: 16-band spectrum of the real output beside the title; crossfade between your own songs (Off, 2-8 s);
  internet radio streams added from a copied http(s) address.

## 0.13.0 (Player: robustness, diagnostics, library, shortcuts)
- Windows CI: GitHub Actions builds `Divaverse.sln` with MSVC, checks the exe imports only system DLLs, and runs
  playback smoke tests on a Windows runner. Results appear as annotations on the run.
- Crash capture: minidump + text report (module + offset, log tail) in `%LOCALAPPDATA%\Divaverse\crash`; the next
  start shows a notice once. `--crashtest` exercises it.
- Settings > Diagnostics: Export diagnostics (report, logs, settings, newest crash in one folder), performance
  overlay (fps, decode ms per shown frame, present ms, dropped / unpaired, decode path, frame), Record GPU trace
  (turns GPU decode on, logs every decoder call for 20 s, then exports).
- Settings are written atomically as UTF-16 (paths in any script survive). The play queue is now actually saved;
  it was read but never written.
- User data and the music library are written atomically.
- Shortcuts are rebindable (Settings > Shortcuts: click, press the combination; Backspace clears; restore
  defaults). New action: performance overlay, default Ctrl+Alt+D.
- Placement is remembered per monitor arrangement: docking / undocking puts her back where she was on that setup.
- Audio follows the default output device and reopens in place (music keeps its position).
- Decoding pauses during sleep, display off and a locked session.
- Schedule: on stage only between set hours; optionally come out after N idle minutes and go back on input.
- Saved clip playlists on On Stage: name, save (clip on stage + queue), play, delete.
- Library scan runs in the background and swaps in when done; library folders are watched and rescanned after
  changes settle for two seconds.
- Version string shown in Settings and the report comes from one constant.

## 0.12.0 (hair: weight)
Measured first: --uishot 54 now logs the tip of a guide strand every frame through a rest / hop / landing / throw
script, and DIVAVERSE_QA_HAIR switches parts of the solver off. Findings and fixes:
- A resting ponytail was whipping through +-30 px with nothing happening (tip height sd 32 px). Switching the
  rest-shape term off dropped that to 3 px: steering each node toward the continuation of the previous segment
  feeds back along the chain. Now only the first seven nodes are steered, toward a fixed direction that fades out,
  and the rest of the strand is free except for a limit on how sharply it may fold. Resting sd is now about 1 px.
- A hop made the tip whip over the top of her head. Cause: the length constraint moved the lower node 88 % and
  the upper 12 %, which is a chain whose mass tapers 7:1 per link, i.e. a bullwhip. Equal masses, six iterations.
  The tip stays below the root through the landing and the throw (except the extra-long high ponytail, which reaches level).
- Hair drifted like something light. Drag per unit of weight cut to a sixth and gravity raised (2800 px/s^2), with
  a faster apparent wind so the lean is kept (22 to 41 degrees at rest depending on style); internal friction
  0.12 -> 0.03 per substep. It now swings and settles in 0.85 to 1.3 s after a landing and throw instead of creeping.
- Collisions eased out over a few steps instead of teleporting a node up to 22 px (which kicked its neighbours).
- After a throw the hair stayed knotted behind her head for good. Two causes, both visible in a dump of the guide
  nodes: the throw frames put her head 73 px further forward, so the roots teleported there and back within ten
  frames; and every contact damped the node's whole velocity, six iterations a substep, which froze the knot
  where it lay. Now the hair is carried with a large pose jump, and a contact cancels only motion into the
  surface, so hair slides down her back under its weight. The spear and grips she holds no longer count as body.
- Her motion reaches the roots through a short low-pass, and only 60 % of the vertical part, so the hair does not
  go weightless for the length of a hop.
- Curl is no longer simulated; wavy and curly styles are drawn as waves around the simulated strand.
- Drawing: strands are spread across the bundle by a profile (nothing at the tie, widest a quarter of the way
  down, tapering to a point) and gather into locks toward the ends; the mass under them is opaque.

## 0.11.0 (Spear Run: speed, hair physics, artist fixes)
- Do-not-tint mask (donttint.psd): the spear, steering column and grips, including the ones painted into the throw
  frames, keep their painted colours whatever the skin tone. Baked into the blue channel of the side mask atlas.
- Riding / tuck frames: steering column and grips are drawn behind her. In the throw frames the column stays in
  front and follows her hand as before.
- Speed. The mode was issuing, per frame, about 760 gradient brushes (one per 5 px water column), about 360 path
  geometries (hair fill) and about 2000 filled ellipses (spray, foam, beads). Now: each water layer is one gradient
  polygon, the hair mass is one polygon per group, and every droplet, foam fleck, stain and bead is a pre-baked
  disc drawn as a bitmap. Particle cap 1800 -> 1000, fewer render strands (drawn thicker), coarser reflection
  bands. Measured under Wine's software Direct2D, sunny scene: 346 ms -> about 165 ms per frame of draw calls;
  not measured on a GPU. F3 in Spear Run shows the frame cost.
- Hair, ties, beads and chest motion now advance by real elapsed time, not by a fixed 1/60 s per rendered frame,
  so a low frame rate no longer plays them in slow motion.
- Hair physics: air drag grows with the square of the relative wind plus a small linear term (hair that is only
  falling or swinging keeps its weight; headwind and gusts take hold), slow gusts instead of constant strong
  turbulence, flutter only toward the tips, internal friction along the strand, gravity matched to the hop.
  Loose styles and the bob use 12 guide strands, so they read as a mass, not a rope.
- QA: --uishot 54 (one large rider who hops and throws; DIVAVERSE_QA_FRAMES / DIVAVERSE_QA_STYLE pick the frame and
  the hair style, for filmstrips), per-section frame timings in the log.

## 0.10.0 (hair and tie systems rebuilt)
- Hair is now a position-based strand simulation instead of animated curves. Up to 8 guide strands of up to 18
  nodes per rider, two substeps per frame:
  - forces: gravity and air drag against the apparent wind, with a travelling turbulence field shared by all
    strands (so ripples pass through the mass coherently);
  - constraints: segment length; a rest shape (the direction it leaves the head or its tie is held for the first
    few segments, then only the style's own springiness and curl); spacing between neighbouring guides, which is
    what gives the mass volume; collision with the head, her back (the sprite's own silhouette) and the seat, with
    friction, strands stacking in layers;
  - simulated in an upright frame that travels with the rider, so hops, waves, lunges and pose changes reach the
    hair as real motion of its roots.
- Rendering: about 36 strands interpolated between the guides, each with its own length and offset; lit from the
  strand tangent with a tight white highlight and a broad coloured one, shifted per strand; filled mass under
  them; flyaways. Wet hair darkens, gathers into locks, gets heavier and drips from the ends.
- Bikini ties use the same solver: two strings per tie point (all tie points in the frame, not just the first),
  drawn as ribbons with a knot and end beads, colliding with the body.
- All ten styles retuned and lengthened for the new system.
- QA scenes: --uishot 52 (hair sheet a few frames later), 53 (hair sheet in a hurricane).

## 0.9.0 (hair, reflections, refraction, beads)
- Hair: ten styles (long ponytail, extra-long high ponytail, twin tails, loose straight, loose wavy, curls, long
  braid, twin braids, side ponytail, short bob), up to hip length and beyond. The mass is filled between
  neighbouring strands and thins toward the tips; strands curl in locks; two sheen bands; upper strands lit.
- Reflections: each rider is mirrored below her waterline in thin bands that slide with the ripples and break up
  with distance; clouds and the far boats / islands are mirrored in the far water.
- Refraction: the part of the hull under the waterline is drawn row by row, shifted sideways and foreshortened;
  bright wandering lines of bent light on the near water.
- Water beads: drops land on skin and hull, cling, then run down in fits and starts leaving a wet track, and drip
  off the lowest edge as real droplets. More of them in rain and after a landing.
- QA scene: --uishot 51 (all hair styles).

## 0.8.0 (Spear Run quality pass)
- Fixed: on the game menu Left / Right on "Weather" changed the course and the other way round.
- Camera pulled back in Spear Run (riders are about 27 % smaller on screen).
- Steering handle grips are drawn over her hands in the riding / tuck frames (the throw frames have them painted in).
- Sprites are lit: a height field from the silhouette and the painted shading gives a normal per pixel; key light,
  sky rim light and specular highlights are baked per material (skin, bikini, hair, ski), plus a wet-glint layer
  whose strength follows how wet she is (rain, landings).
- Hair styles (new row on the rider screen, saved): long ponytail, twin tails, loose and long, braid, short bob.
  Much longer, drawn as spring-driven streaming curves with a sheen band; the race view scales its strands to match.
- Chest secondary motion in the side view: the bikini-top region is cut out per frame at bake time and driven by a
  spring from the body's vertical acceleration and the throw.
- Water: droplets with gravity and drag drawn as streaks, mist, foam that stays on the surface and drifts back with
  the flow, bow wave, rooster tail, landing and spear-entry splashes, crest foam, sun glitter, rain rings,
  reflections of the riders, depth gradient.
- Blood: directional burst on a hit, drips while wounded, stains that spread on the water and drift away, wounds on
  the sprite that fade, red edge flash when you are hit.
- Clouds are generated (fbm density in a flat-bottomed dome, lit from the sun side), one set per weather, used by
  both the race and Spear Run.
- Feel: hit-stop, combo multiplier, floating score numbers.
- QA scenes: --uishot 49 (moment of a hit), 50 (rider screen on the hair style row).

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
