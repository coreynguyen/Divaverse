# Divaverse MKV input specification (Player 0.5, metadata schema 1)

What the Player accepts, and what a production tool must write. The Player only reads: it never analyses pictures,
removes backgrounds, tracks subjects or writes into content files.

Reference files: `TestAssets/Divas/Luna/luna_sway.mkv` (preferred dual-track form) and `luna_sway_packed.mkv`
(packed form). Both are produced by `Tools/refmkv/make_reference.sh`.

## 1. Container

- Matroska (`.mkv`), DocType `matroska`. One clip per file; the file is self-contained.
- `Cues` should be present. Without them the clip plays, but seeking restarts from the beginning (`DV-W120`).
- Audio and subtitle tracks are ignored.
- `DefaultDuration` must be set on every video track (it is the frame rate). Constant frame rate only.

## 2. Video tracks

| Form | Tracks | How it is recognised |
|---|---|---|
| Dual (preferred) | two video tracks | track `Name` = `divaverse:rgb` and `divaverse:alpha`. Unnamed: first video track is colour, second is alpha (`DV-W104`) |
| Packed | one video track | `video.layout` = `packed` in the metadata, or a legacy manifest. Default for a single track |
| Opaque | one video track | `video.layout` = `opaque` |

- Codec: H.264 (`V_MPEG4/ISO/AVC`), 8-bit, 4:2:0, progressive. Anything else is rejected (`DV-E003`, `DV-E004`).
- Colour: BT.709. Range is read from the Matroska `Colour.Range` element, else from the SPS VUI flag; unspecified = video range.
- B-frames and open/closed GOPs are fine. The two tracks do not need the same GOP structure.
- Colour and alpha tracks may have **different resolutions** (the reference clip has 480x992 colour and 240x496
  alpha). Both are stretched over the same canvas rectangle.

## 3. Alpha

- Alpha is the luma of the alpha picture: **black = transparent, white = opaque**. Chroma is ignored (write neutral).
- Colour is **straight** (not premultiplied). The Player premultiplies.
- Range is explicit and per track. Video range: 16 = transparent, 235 = opaque. Full range: 0 and 255. For the dual form
  the alpha track's own range is used, independent of the colour track. For the packed form the single track's range applies to both halves.
- Optional `video.alpha_levels` `[lo, hi]` (0..1, after range expansion) remaps alpha; default `[0, 1]`.

## 4. Timing and sync

- Timeline origin = presentation time of the first colour frame. Frame *n* is at `n / fps`.
- Frames are paired by frame number on that shared timeline. Timestamps of the two tracks are **not** normalised
  independently: an alpha track that starts at another time is a defect. It is logged (`DV-W110`, offset in ms), its
  frames show up as *unpaired* in the diagnostics, and a colour frame is never shown with another frame's mask.
- Both tracks must have the same frame count and frame duration (`DV-W112` if `DefaultDuration` differs).
- Seeking moves both tracks together. Each track restarts at its own last keyframe at or before the target, earlier
  frames are decoded and discarded, and the first frame shown is exactly the target.
- Looping rewinds and flushes both decoders together; frame numbers restart at 0.

## 5. Packed layout

One picture holding colour above alpha. Rectangles are given in pixels of the decoded picture:

```json
"video": { "layout": "packed", "rgb_rect": [0, 0, 480, 992], "alpha_rect": [0, 992, 480, 992] }
```

`[x, y, width, height]`. Without rectangles: top half colour, bottom half alpha. Rectangles that exceed the decoded
picture are ignored (`DV-W151`). Keep a multiple-of-16 boundary between the halves so no macroblock spans both.

## 6. Embedded metadata

Attachment named `divaverse.json`, MIME `application/json`, UTF-8, at most 1 MiB. Schema version 1:

```json
{
  "schema_version": 1,
  "diva_id": "DIVA-000001", "diva_name": "Luna",
  "show_id": "SHOW-000001", "show_title": "First Session",
  "clip_id": "luna_sway", "title": "Sway", "order": 1,
  "diva": { "rating": 5, "profile": { "Age": "23", "Height": "168 cm", "Type": "Dancer", "Style": "Playful" } },
  "video": { "layout": "dual", "alpha_levels": [0, 1] },
  "canvas": { "width": 480, "height": 992 },
  "physical": { "actor_height_m": 1.68, "pixels_per_meter": 567, "ground_anchor": [0.5, 0.9597] },
  "loop": { "mode": "loop" },
  "artwork": { "cover": "cover.jpg", "thumb": "small_cover.jpg" },
  "content": { "origin": "AI_GENERATED", "rating": "ADULT" }
}
```

| Field | Meaning |
|---|---|
| `schema_version` | integer. Newer than the Player knows: known fields are used, the rest ignored (`DV-W102`) |
| `diva_id` | groups clips into one diva in the library. Clips without it are listed on their own |
| `diva_name`, `show_id`, `show_title`, `clip_id`, `title` | display and identity strings |
| `order` | integer sort key of the clip inside its diva |
| `diva.rating` | 0..5 stars on the card |
| `diva.profile` | object of string pairs shown as attribute tiles, in written order (at most 16 are read, 8 shown) |
| `video.*` | section 2, 3, 5 |
| `canvas` | logical size of the stage rectangle in canvas pixels. Default: the colour picture size |
| `physical` | section 8 |
| `loop.mode` | `loop` (default) or `once` (hold the last frame, or advance when a playlist mode is on) |
| `artwork.cover`, `artwork.thumb` | attachment names (section 7) |
| `content.*` | informational |

Unknown keys are ignored. Matroska `Tags`: a file-level `TITLE` is used as the clip title when the JSON has none.
User preferences (position, stage scale, favourites, loop mode) and the user's own marks (star ratings, tags,
seen state, learned shuffle weights: `userdata.txt`) are stored by the Player in `%APPDATA%\Divaverse` and are
never written into content. `diva.rating` is the creator's rating; the user's rating overrides it for display only.

## 7. Artwork

Attachments, JPEG or PNG, at most 16 MiB and 16384 px per side:

| Name | Use | Standard aspect (w:h) | Suggested size |
|---|---|---|---|
| `cover.jpg|png` | library card | 2:3 | 800x1200 |
| `small_cover.jpg|png` | clip thumbnail | 2:3 | 240x360 |
| `banner.png|jpg` | diva page, tall | 1:3 | 480x1440 |
| `cover_land.jpg`, `small_cover_land.jpg` | landscape (recognised, not shown yet) | 16:9 | 1280x720 |

Generated images are rarely the exact ratio. The Player never stretches: covers and thumbnails are scaled to
fill their frame and cropped (the crop keeps the top 10-18 % so faces stay in view); a banner is shown whole on a
generated backdrop, so a PNG cut-out with transparency works well there. `artwork.banner` may name the attachment.
Clip thumbnails use, in order: the attachment named by `artwork.thumb`, `small_cover`, `cover`, the cached
generated thumbnail, a decoded frame.

Cover resolution order, first hit wins; none of the first three decodes video:

1. sidecar `artwork.cover` (a relative file path, see section 9)
2. embedded attachment: the name in `artwork.cover`, then `cover.jpg`, `cover.png`, `small_cover.jpg`, `small_cover.png`
3. image file beside the media: `<basename>.jpg|png`, `cover.jpg|png`, `card.png|jpg`, `profile.png`
4. a frame decoded from the clip, cached in `%LOCALAPPDATA%\Divaverse\cache` (never beside the media), keyed by path, size and modification time
5. placeholder

The landscape variants are recognised as artwork but not displayed by this version.

## 8. Physical scale

```json
"physical": { "actor_height_m": 1.68, "pixels_per_meter": 567, "ground_anchor": [0.5, 0.9597] }
```

- `pixels_per_meter`: canvas pixels per metre at the performer's plane. Valid range 20..20000 (`DV-W141` otherwise).
- `ground_anchor`: `[u, v]` normalised canvas coordinates of the point on the floor under the performer at rest.
  `v` is the floor line; `u` is the horizontal anchor.
- `actor_height_m`: informational (shown in the UI); the size comes from `pixels_per_meter`.

**Stage density standard (SD-420).** At stage scale 100 % one metre of authored space is 420 device-independent
pixels on screen (x DPI/96). This plays the role DPI plays for print: content of any resolution that states its own
density (`pixels_per_meter`) comes out the same physical size, and a user's "25 %" means the same thing for every
clip. A pipeline that changes output resolution only has to write the matching density: for a 1.68 m performer who
is 952 px tall in a 992 px canvas, `pixels_per_meter` = 952 / 1.68 = 567; the same render at 2x is 1134.

Window height on screen = `canvas.height / pixels_per_meter` metres x 420 px per metre x the user's stage scale x DPI/96.

Editor mode (Settings) shows these values for the clip on stage, lets them be adjusted live, and saves them as a
sidecar `physical` override (section 9). The MKV is never modified by the Player.
Every clip with physical metadata therefore has the same metre-to-pixel ratio, regardless of its resolution.
The window is placed so the ground anchor stays on a fixed screen point: resizing grows the performer from her
feet, and the floor line stays on the taskbar edge while she moves inside the canvas.

Without `pixels_per_meter` the clip is **uncalibrated** (`DV-W140`): the canvas is treated as 1.75 m tall and the
UI says so. Nothing is ever estimated from the picture.

## 9. Sidecar overrides

Optional `<basename>.divaverse.json` beside the media, same schema, at most 1 MiB.

- Precedence: built-in defaults, then embedded `divaverse.json`, then sidecar.
- Merge is per field: objects merge key by key, scalars and arrays replace, `null` removes the embedded value.
- Facts always come from the container and cannot be overridden: codec, track sizes, frame rate, timestamps,
  duration, which tracks exist. A top-level `media` object in a sidecar is ignored (`DV-W150`).
- `artwork.cover` in a sidecar is a file path relative to the media's folder. Absolute paths, drive letters and `..` are refused.

## 10. Legacy inputs

Still read: show folders with `manifest.json` and `<clip>.clip.json` (format in `FORMAT.md`). Their values act as
defaults below the embedded metadata; rectangles that do not fit the decoded picture are discarded.

## 11. Limits of this Player version

H.264 only (no HEVC/AV1). One performer on stage. No audio from clips. No interactivity data. Playback speed is fixed at 1x.

## 12. Validation codes

Errors make the file unplayable; it is listed under the library with the first error. Warnings are shown in
On Stage, Media info.

| Code | Meaning |
|---|---|
| `DV-E000` | file cannot be opened |
| `DV-E001` | not a readable Matroska file |
| `DV-E002` | no video track |
| `DV-E003` | unsupported codec |
| `DV-E004` | H.264 configuration unreadable, or not 8-bit 4:2:0 |
| `DV-E005` | track uses an unsupported container feature (encryption, unsupported compression) |
| `DV-E010` | alpha track present but no colour track |
| `DV-E020` | legacy manifest unreadable |
| `DV-W101` | no embedded `divaverse.json`; defaults used |
| `DV-W102` | `schema_version` newer than the Player |
| `DV-W103` | embedded or sidecar JSON is not valid JSON; ignored |
| `DV-W104` | video tracks not named; track order assumed |
| `DV-W110` | alpha track starts at a different time than the colour track (log) |
| `DV-W112` | colour and alpha declare different frame durations |
| `DV-W120` | no Cues |
| `DV-W130` | metadata attachment or sidecar larger than 1 MiB or unreadable; ignored |
| `DV-W140` | no physical scale; uncalibrated fallback |
| `DV-W141` | `pixels_per_meter` out of range; ignored |
| `DV-W150` | sidecar tried to override a media fact |
| `DV-W151` | packed rectangles exceed the decoded picture; ignored |

## 13. Writing a file with ffmpeg

```
ffmpeg -i colour.mov -i alpha_gray.mov \
  -map 0:v:0 -map 1:v:0 -c:v libx264 -pix_fmt yuv420p -crf 16 \
  -color_range:v:1 pc -metadata:s:v:0 title=divaverse:rgb -metadata:s:v:1 title=divaverse:alpha \
  -attach divaverse.json   -metadata:s:t:0 mimetype=application/json -metadata:s:t:0 filename=divaverse.json \
  -attach cover.jpg        -metadata:s:t:1 mimetype=image/jpeg       -metadata:s:t:1 filename=cover.jpg \
  -an -sn out.mkv
```

The alpha input must already be full range if `-color_range pc` is set; otherwise leave it video range and omit the flag.
