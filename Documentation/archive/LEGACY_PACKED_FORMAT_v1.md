> ARCHIVED. Superseded by ../DIVAVERSE_MKV_INPUT_SPEC.md (dual-track MKV is the preferred form). Kept only because the Player still reads this legacy manifest layout.

# Divaverse clip format (v1) and why

## Verdict on MKV

**Matroska as a container is fine. The weak point was the planned dual-track layout, not the container.** v1 keeps `.mkv` and makes a single *packed* stream the default; dual-track stays supported for tooling/back-compat.

| Concern | Dual-track (RGB track + alpha track) | Packed (RGB top / alpha bottom, one track) |
|---|---|---|
| Decoder sessions per performer | 2 | **1** |
| Sync | PTS pairing; must handle orphans/drift | **structural** — same picture |
| HW decode (D3D11VA/NVDEC) | 2 sessions; NVDEC session count matters at 4–8 performers | 1 session |
| Ordinary players / editors | show track 0 only | show the packed picture (alpha visible as a grey matte below) — easy to eyeball |
| Size, this clip | 4.24 MB | **4.10 MB** |
| Seek/loop | two decoders to flush | one |

Both measured on the H3 test clip at identical CRF.

## v1 stream layout

```
packed (default)                     dual
+-----------+  row 0                 track 0  "divaverse:rgb"   W x H  colour
|  colour   |                        track 1  "divaverse:alpha" W x H  alpha as luma
|  W x H    |                        identical PTS on both tracks
+-----------+  row Hpad (H rounded up to 16 for H.264, 64 for HEVC; pad = replicated row / alpha 0)
|  alpha    |
|  (luma)   |
+-----------+  W x 2*Hpad
```

- Alpha = luma, BT.709 **limited** range: `a = (Y − 16) / 219`. Chroma in the alpha half is neutral (128). Straight (non-premultiplied) alpha in the file; the Player premultiplies on the GPU.
- Colour: BT.709 limited, 8-bit 4:2:0, tagged in the bitstream and container.
- No audio. No container metadata. Stream titles carry the roles; the manifest carries `layout`, `rgb_rect`, `alpha_rect`, `rgb_track`, `alpha_track`, so the Player never assumes stream order.
- Timestamps: Matroska stores 1 ms ticks → 24 fps is stored as 41/42 ms. The Player snaps `pts × fps` to the frame grid; the packer verifies ≤ 1 tick deviation.
- Colour is premultiplied-safe: under transparency and at soft edges the packer writes core-extended colour, so bilinear scaling never pulls background fringe in.

## Reader requirements (Player ≥ 0.2)

The Player reads clips with its own EBML demuxer and the OS H.264 decoder (no FFmpeg), so a v1 clip must stay inside this envelope. `divapack` output already does; the list matters for third-party muxers.

| Item | Required | Notes |
|---|---|---|
| DocType | `matroska` or `webm`, DocTypeReadVersion ≤ 4 | |
| Tracks before first Cluster | yes, or reachable through SeekHead | ffmpeg / mkvmerge default |
| CodecID | `V_MPEG4/ISO/AVC` | HEVC / AV1 rejected by 0.2 (planned via OS decoder extensions) |
| CodecPrivate | avcC (AVCDecoderConfigurationRecord), NAL length size 1/2/4 | Annex B CodecPrivate rejected |
| Bitstream | 8-bit 4:2:0, progressive preferred | SPS is parsed; anything else is refused at open |
| Blocks | SimpleBlock or BlockGroup; lacing (Xiph / EBML / fixed) accepted | timestamps = Cluster + int16, × TimestampScale |
| Unknown-size elements | Segment and Cluster only | `-live 1` style output plays |
| ContentEncodings | none, or header stripping (algo 3) | zlib / bzlib / lzo / encryption rejected |
| Cues | optional | parsed when present (front or via SeekHead); looping only needs the first Cluster |
| Track roles | track **Name** `divaverse:rgb` / `divaverse:alpha` / `divaverse:packed` | overrides manifest `rgb_track` / `alpha_track` |
| Range | Matroska `Colour.Range`, else SPS VUI `video_full_range_flag` | |

B-frames are fine (`-bf 2` is what divapack writes): Matroska blocks carry PTS in decode order, the decoder reorders, the Player maps output PTS to the frame grid.

Packed-layout size limit: the coded picture is W × 2·Hpad, and hardware H.264 decoders commonly stop at 4096 px per side (not measured here — from vendor documentation). A performer taller than ~2040 px therefore needs dual layout or HEVC.

## Manifest clip entry (written by divapack as `<clip>.clip.json`)

```json
{ "id": "dance_01", "file": "clips/dance_01.mkv", "type": "dance",
  "layout": "packed", "codec": "h264", "width": 480, "height": 992, "fps": "24/1", "frames": 221,
  "rgb_track": 0, "alpha_track": 0, "rgb_rect": [0,0,480,992], "alpha_rect": [0,992,480,992],
  "alpha_encoding": "luma_bt709_limited", "alpha_premultiplied": false,
  "ground_y": 0.9597, "loop": "cut", "audio_enabled": false, "sha256": "…" }
```
`ground_y` (median feet line, normalized) drives the procedural contact shadow. Rects are `[x, y, w, h]`.

## Codec ladder

| Codec | Use | Notes |
|---|---|---|
| **H.264 High 8-bit** | v1 default, universal fallback | D3D11VA on everything since ~2010; software decode cheap at these sizes |
| HEVC Main | smaller catalogue downloads (~30–40 % at equal quality) | HW decode on all current GPUs; divapack `--codec hevc` already supported, **Player 0.2 does not decode it yet** (needs the OS HEVC decoder MFT); check patent-licensing position for a commercial product (not legal advice) |
| HEVC Main10 | banding-free alpha gradients / soft shadows | needs a P010 surface path in the Player (not yet implemented) |
| **AV1** | recommended long-term distribution codec | royalty-free; HW decode RTX 30+/most RDNA2+/Intel Xe+; dav1d software fallback is fast but not free at 4–8 performers. Ship H.264 as fallback rendition until the install base is there |

## Alternatives considered and rejected

| Option | Why not (as the primary format) |
|---|---|
| VP9/WebM with alpha | alpha lives in Matroska BlockAdditional as a second VP9 bitstream; HW decoders ignore it → two decodes, one in software |
| HEVC "with alpha" (Apple layered extension) | no D3D11VA/MF support on Windows for the alpha layer |
| ProRes 4444 / PNG / WebP sequences | 10–50× larger, CPU decode |
| HAP Q Alpha / DXT-style GPU codecs | near-zero decode cost (texture upload only) but 5–10× file size. **Good as an optional local "performance cache"** for many simultaneous performers: transcode on install, keep MKV as the distribution format |
| Proprietary encrypted container | contradicts the open-format decision; entitlements belong outside the media |

## Future (not v1)

- Optional `shadow_rect` / extra masks (hair, clothing) as additional packed regions — the rect-based layout extends without breaking v1 readers.
- Signed manifests + per-file `sha256` (already emitted) for integrity; no DRM inside the container.
