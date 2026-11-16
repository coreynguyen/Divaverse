#!/usr/bin/env python3
"""
divapack — Divaverse clip packer / validator (offline production tool).

Source video (MP4/AVI/MKV/...) + RGBA cutout frame folder  ->  silent MKV with
colour + alpha, a manifest clip entry and a JSON processing report.

Policy:
  * Colour comes from the SOURCE decode (exact codec pixels, BT.709-correct).
    Cutout RGB is ignored: background-removal tools frequently re-grade it.
  * Alpha comes from the cutouts.
  * Cutout <-> source alignment is measured, never assumed. Any non-contiguous
    mapping or high residual is reported and (unless --force) fails the run.
  * Audio and all container metadata are stripped.

Layouts:
  packed  (default) one video stream, W x 2*Hpad. Rows [0,H) = colour,
          rows [Hpad, Hpad+H) = alpha as luma (chroma neutral). One decoder,
          sync is structural.
  dual    two video streams (track 0 colour, track 1 alpha), identical PTS.

Deps: Python 3.10+, numpy, Pillow, ffmpeg/ffprobe on PATH.
"""
from __future__ import annotations
import argparse, json, os, re, subprocess, sys, time, hashlib
import numpy as np
from PIL import Image

# --------------------------------------------------------------------------- utils
def run(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, **kw)

def ffprobe_json(path, *args):
    out = run(["ffprobe", "-v", "error", "-of", "json", *args, path]).stdout
    return json.loads(out)

def nat_key(s):
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r"(\d+)", s)]

def frac(s):
    n, d = s.split("/"); return int(n) / int(d) if int(d) else 0.0

def box_blur(img, r):
    """Separable box blur, edge-clamped. img float32 HxWxC."""
    if r <= 0: return img
    out = img
    for axis in (0, 1):
        pad = [(0, 0)] * out.ndim; pad[axis] = (r + 1, r)
        p = np.pad(out, pad, mode="edge")
        c = np.cumsum(p, axis=axis, dtype=np.float64)
        n = out.shape[axis]
        hi = np.take(c, np.arange(2 * r + 1, 2 * r + 1 + n), axis=axis)
        lo = np.take(c, np.arange(0, n), axis=axis)
        out = ((hi - lo) / (2 * r + 1)).astype(np.float32)
    return out

# --------------------------------------------------------------------------- probe
def probe_source(path):
    j = ffprobe_json(path, "-show_streams", "-show_format")
    v = [s for s in j["streams"] if s["codec_type"] == "video"]
    a = [s for s in j["streams"] if s["codec_type"] == "audio"]
    if not v: raise SystemExit(f"{path}: no video stream")
    v = v[0]
    pk = ffprobe_json(path, "-select_streams", "v:0", "-show_entries", "packet=pts", "-show_packets")
    pts = sorted(int(p["pts"]) for p in pk["packets"] if "pts" in p)
    d = np.diff(pts) if len(pts) > 1 else np.array([0])
    tb = frac(v["time_base"])
    info = {
        "file": os.path.basename(path),
        "codec": v["codec_name"], "profile": v.get("profile"),
        "width": v["width"], "height": v["height"], "pix_fmt": v.get("pix_fmt"),
        "color_range": v.get("color_range"), "color_space": v.get("color_space"),
        "r_frame_rate": v["r_frame_rate"], "fps": frac(v["r_frame_rate"]),
        "frames": len(pts), "duration_s": float(j["format"].get("duration", 0)),
        "cfr": bool(len(set(d.tolist())) == 1), "frame_ticks": int(d[0]) if len(d) else 0,
        "time_base": v["time_base"],
        "audio": [{"codec": s["codec_name"], "rate": int(s.get("sample_rate", 0)),
                   "channels": s.get("channels")} for s in a],
        "container_metadata_keys": sorted(j["format"].get("tags", {}).keys()),
    }
    if not info["cfr"]:
        gaps = [(pts[i], pts[i + 1]) for i in range(len(d)) if d[i] != np.median(d)]
        info["pts_irregularities"] = gaps[:32]
    info["_tb"] = tb
    return info

def decode_frames(path, w, h, scale=None, gray=False):
    """Generator of decoded frames in presentation order. Colour handled explicitly."""
    vf = ["scale=in_color_matrix=bt709:in_range=tv:out_range=pc:flags=accurate_rnd+full_chroma_int"]
    if scale: vf.append(f"format=rgb24,scale={scale[0]}:{scale[1]}:flags=area")
    fmt = "rgb24"
    ow, oh = scale if scale else (w, h)
    ch = 3
    p = subprocess.Popen(["ffmpeg", "-v", "error", "-i", path, "-map", "0:v:0", "-an",
                          "-fps_mode", "passthrough", "-vf", ",".join(vf),
                          "-pix_fmt", fmt, "-f", "rawvideo", "-"],
                         stdout=subprocess.PIPE)
    n = ow * oh * ch
    while True:
        b = p.stdout.read(n)
        if len(b) < n: break
        yield np.frombuffer(b, np.uint8).reshape(oh, ow, ch) if ch > 1 else \
              np.frombuffer(b, np.uint8).reshape(oh, ow)
    p.wait()

# --------------------------------------------------------------------------- cutouts
def list_cutouts(d):
    exts = (".png", ".tif", ".tiff", ".webp")
    names = sorted([f for f in os.listdir(d) if f.lower().endswith(exts)], key=nat_key)
    nums = []
    for f in names:
        m = re.findall(r"\d+", f); nums.append(int(m[-1]) if m else None)
    gaps = []
    if all(n is not None for n in nums) and nums:
        s = set(nums); gaps = sorted(set(range(min(nums), max(nums) + 1)) - s)
    return names, nums, gaps

def load_rgba(path):
    im = Image.open(path)
    if im.mode != "RGBA": im = im.convert("RGBA")
    return np.asarray(im)

# --------------------------------------------------------------------------- alignment
def align(src_small, cut_dir, names, sw, sh, sample_every=1):
    """Return (mapping list cut_idx -> src_idx, residual list) using mean-removed
    luma on the cutout's opaque region. Robust to constant colour offsets."""
    ns, nc = len(src_small), len(names)
    S = np.stack(src_small).astype(np.float32)                 # ns x sh x sw
    def cut_small(i):
        im = Image.open(os.path.join(cut_dir, names[i])).convert("RGBA").resize((sw, sh), Image.BOX)
        a = np.asarray(im, np.float32)
        lum = a[..., :3] @ np.array([0.2126, 0.7152, 0.0722], np.float32)
        return lum, a[..., 3] > 250
    # global offset from first K cutouts
    K = min(8, nc)
    heads = [cut_small(i) for i in range(K)]
    best = None
    for off in range(0, ns - nc + 1):
        e = 0.0
        for k, (lum, m) in enumerate(heads):
            if m.sum() < 16: continue
            t = S[off + k][m]; g = lum[m]
            e += np.abs((g - g.mean()) - (t - t.mean())).mean()
        if best is None or e < best[0]: best = (e / K, off)
    off = best[1]
    # per-frame verification: best local match within +-3, residual at the expected index
    mapping, resid, local = [], [], []
    for i in range(0, nc, sample_every):
        lum, m = cut_small(i)
        g = lum[m]; g = g - g.mean()
        def err(j):
            t = S[j][m]; return float(np.abs(g - (t - t.mean())).mean())
        cand = [j for j in range(max(0, off + i - 3), min(ns, off + i + 4))]
        errs = {j: err(j) for j in cand}
        jb = min(errs, key=errs.get)
        mapping.append((i, jb)); resid.append(errs[off + i] if off + i < ns else 1e9)
        local.append(jb - (off + i))
    return off, mapping, resid, local

# --------------------------------------------------------------------------- alpha
def process_alpha(a8, lo, hi):
    a = a8.astype(np.float32)
    a = np.clip((a - lo) / float(hi - lo), 0.0, 1.0)
    return a

def decontaminate(rgb, a, radius=6):
    """Edge colour extension: replace background-contaminated colour at soft
    edges and under transparency with colour pulled from the opaque core.
    Also flattens the transparent region -> much cheaper to encode."""
    core = (a > 0.96).astype(np.float32)[..., None]
    f = rgb.astype(np.float32)
    num = f * core; den = core
    acc_n, acc_d = np.zeros_like(f), np.zeros_like(core)
    for r in (radius, radius * 4, radius * 16):           # coarse-to-fine fill
        bn, bd = box_blur(num, r), box_blur(den, r)
        need = acc_d[..., 0] < 1e-3
        acc_n[need] = bn[need]; acc_d[need] = bd[need]
    fill = np.where(acc_d > 1e-4, acc_n / np.maximum(acc_d, 1e-4), f)
    w = np.clip((a - 0.55) / 0.40, 0.0, 1.0)[..., None]    # smoothstep-ish weight
    w = w * w * (3 - 2 * w)
    return np.clip(f * w + fill * (1 - w) + 0.5, 0, 255).astype(np.uint8)

# --------------------------------------------------------------------------- encode
def enc_args(codec, crf, fps, preset):
    common = ["-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709",
              "-color_range", "tv", "-pix_fmt", "yuv420p"]
    g = str(max(1, int(round(fps * 2))))
    if codec == "h264":
        return ["-c:v", "libx264", "-preset", preset, "-crf", str(crf), "-profile:v", "high",
                "-g", g, "-bf", "2", "-x264-params", "scenecut=0"] + common
    if codec == "hevc":
        return ["-c:v", "libx265", "-preset", preset, "-crf", str(crf), "-tag:v", "hvc1",
                "-x265-params", f"keyint={g}:scenecut=0:log-level=error"] + common
    raise SystemExit("codec must be h264|hevc")

def ff_writer(out, w, h, fps_str, codec, crf, preset, title):
    cmd = ["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
           "-s", f"{w}x{h}", "-framerate", fps_str, "-i", "-",
           "-vf", "scale=out_color_matrix=bt709:out_range=tv:flags=accurate_rnd+full_chroma_int",
           *enc_args(codec, crf, frac(fps_str), preset),
           "-an", "-map_metadata", "-1", "-metadata:s:v:0", f"title={title}", out]
    return subprocess.Popen(cmd, stdin=subprocess.PIPE)

# --------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", required=True)
    ap.add_argument("--alpha-dir", required=True, help="RGBA cutout frames")
    ap.add_argument("--out", required=True, help="output .mkv")
    ap.add_argument("--layout", choices=["packed", "dual"], default="packed")
    ap.add_argument("--codec", choices=["h264", "hevc"], default="h264")
    ap.add_argument("--crf", type=int, default=16)
    ap.add_argument("--preset", default="slow")
    ap.add_argument("--offset", default="auto", help="source frame index of cutout #1 (0-based) or 'auto'")
    ap.add_argument("--alpha-lo", type=float, default=4.0, help="alpha <= lo -> 0")
    ap.add_argument("--alpha-hi", type=float, default=248.0, help="alpha >= hi -> 255")
    ap.add_argument("--no-decontam", action="store_true")
    ap.add_argument("--clip-id", default=None)
    ap.add_argument("--max-resid", type=float, default=3.0, help="fail if alignment residual exceeds")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--loop-crossfade", type=int, default=0, metavar="N",
                    help="bake a seamless loop: blend last N frames into first N, drop first N")
    args = ap.parse_args()

    t0 = time.time()
    rep = {"tool": "divapack 0.1", "args": vars(args), "warnings": [], "errors": []}
    src = probe_source(args.source); tb = src.pop("_tb")
    rep["source"] = src
    W, H, fps_str = src["width"], src["height"], src["r_frame_rate"]
    if src["audio"]: rep["warnings"].append(f"source has {len(src['audio'])} audio stream(s): stripped")
    if src["container_metadata_keys"]:
        rep["warnings"].append(f"source container metadata {src['container_metadata_keys']} stripped "
                               "(may contain generator workflow / prompts)")
    if not src["cfr"]: rep["warnings"].append("source is not CFR; output re-timed to r_frame_rate")

    names, nums, gaps = list_cutouts(args.alpha_dir)
    rep["cutouts"] = {"count": len(names), "first": names[0] if names else None,
                      "last": names[-1] if names else None, "numbering_gaps": gaps}
    if not names: raise SystemExit("no cutout frames")
    if gaps: rep["errors"].append(f"cutout numbering gaps: {gaps[:20]}")
    a0 = load_rgba(os.path.join(args.alpha_dir, names[0]))
    if a0.shape[:2] != (H, W):
        rep["errors"].append(f"cutout size {a0.shape[1]}x{a0.shape[0]} != source {W}x{H}")
        raise SystemExit(json.dumps(rep, indent=2))

    # ---- alignment (pass 1, downscaled luma)
    sw, sh = max(32, W // 4) & ~1, max(32, H // 4) & ~1
    lw = np.array([0.2126, 0.7152, 0.0722], np.float32)
    small = [f.astype(np.float32) @ lw for f in decode_frames(args.source, W, H, scale=(sw, sh))]
    if args.offset == "auto":
        off, mapping, resid, local = align(small, args.alpha_dir, names, sw, sh)
    else:
        off = int(args.offset)
        _, mapping, resid, local = align(small, args.alpha_dir, names, sw, sh)
    bad = [(i + 1, off + i + 1, round(r, 2), l) for i, (r, l) in enumerate(zip(resid, local))
           if r > args.max_resid or l != 0]
    rep["alignment"] = {
        "source_frames": len(small), "cutout_frames": len(names),
        "offset_frames": off, "offset_seconds": round(off / src["fps"], 6),
        "source_range_1based": [off + 1, off + len(names)],
        "dropped_head": off, "dropped_tail": len(small) - off - len(names),
        "residual_mean": round(float(np.mean(resid)), 3), "residual_max": round(float(np.max(resid)), 3),
        "nonmonotonic_or_high_residual": bad[:50],
        "verdict": "contiguous" if not bad else "MISMATCH",
    }
    if bad and not args.force:
        rep["errors"].append("cutout/source alignment not contiguous; refusing (use --force)")
        print(json.dumps(rep, indent=2)); sys.exit(2)

    # ---- pass 2: full-res process + encode
    if args.layout == "packed":
        hp = (H + 15) // 16 * 16 if args.codec == "h264" else (H + 63) // 64 * 64
        ow, oh = W, hp * 2
        wr = [ff_writer(args.out, ow, oh, fps_str, args.codec, args.crf, args.preset, "divaverse:packed")]
    else:
        hp = H
        tmp_rgb, tmp_a = args.out + ".rgb.mkv", args.out + ".alpha.mkv"
        wr = [ff_writer(tmp_rgb, W, H, fps_str, args.codec, args.crf, args.preset, "divaverse:rgb"),
              ff_writer(tmp_a, W, H, fps_str, args.codec, max(0, args.crf - 2), args.preset, "divaverse:alpha")]

    stats = {"interior_250_254_px": 0, "partial_px": 0, "opaque_px": 0, "touch": [0, 0, 0, 0],
             "bbox": [], "centroid": []}
    first_a = last_a = None
    ref_rgb, ref_a = [], []        # premultiplied refs for verification (every 8th frame)
    frames_out = []
    gen = decode_frames(args.source, W, H)
    for fi, rgb in enumerate(gen):
        ci = fi - off
        if ci < 0: continue
        if ci >= len(names): break
        a8 = load_rgba(os.path.join(args.alpha_dir, names[ci]))[..., 3]
        stats["interior_250_254_px"] += int(((a8 >= 250) & (a8 < 255)).sum())
        a = process_alpha(a8, args.alpha_lo, args.alpha_hi)
        col = rgb if args.no_decontam else decontaminate(rgb, a)
        aq = np.round(a * 255).astype(np.uint8)
        stats["partial_px"] += int(((aq > 0) & (aq < 255)).sum()); stats["opaque_px"] += int((aq == 255).sum())
        vis = aq > 8
        stats["touch"] = [stats["touch"][0] + int(vis[0].any()), stats["touch"][1] + int(vis[-1].any()),
                          stats["touch"][2] + int(vis[:, 0].any()), stats["touch"][3] + int(vis[:, -1].any())]
        ys, xs = np.nonzero(vis)
        if len(xs):
            stats["bbox"].append((int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())))
            wgt = aq[vis].astype(np.float64)
            stats["centroid"].append((float((xs * wgt).sum() / wgt.sum()), float((ys * wgt).sum() / wgt.sum())))
        if first_a is None: first_a = (aq, col)
        last_a = (aq, col)
        frames_out.append((col, aq))
    N = max(0, min(args.loop_crossfade, len(frames_out) // 3))
    if N:
        head = frames_out[:N]; body = frames_out[N:]
        for k in range(N):                     # tail frame L-N+k blends toward head frame k
            t = (k + 1) / N          # last tail frame == head[N-1] exactly
            cb, ab = body[len(body) - N + k]; ch, ah = head[k]
            fa, fb = ab.astype(np.float32) / 255, ah.astype(np.float32) / 255
            ao = fa * (1 - t) + fb * t
            pc = cb.astype(np.float32) * (fa * (1 - t))[..., None] + ch.astype(np.float32) * (fb * t)[..., None]
            co = np.where(ao[..., None] > 1e-4, pc / np.maximum(ao, 1e-4)[..., None], cb)
            body[len(body) - N + k] = (np.clip(co + .5, 0, 255).astype(np.uint8), np.round(ao * 255).astype(np.uint8))
        frames_out = body
        first_a = (frames_out[0][1], frames_out[0][0]); last_a = (frames_out[-1][1], frames_out[-1][0])
        rep["loop_crossfade"] = {"frames": N, "dropped_head_after_blend": N,
            "note": "seam now = natural consecutive-frame motion"}
    for ci, (col, aq) in enumerate(frames_out):
        if ci % 8 == 0: ref_rgb.append((ci, col)); ref_a.append((ci, aq))
        if args.layout == "packed":
            fr = np.zeros((oh, ow, 3), np.uint8)
            fr[:H] = col; fr[H:hp] = col[-1:]                       # replicate row into pad
            fr[hp:hp + H] = aq[..., None]                           # alpha as gray
            wr[0].stdin.write(fr.tobytes())
        else:
            wr[0].stdin.write(col.tobytes()); wr[1].stdin.write(np.repeat(aq[..., None], 3, 2).tobytes())
    for w in wr: w.stdin.close(); w.wait()
    if any(w.returncode for w in wr): raise SystemExit("encoder failed")
    if args.layout == "dual":
        run(["ffmpeg", "-v", "error", "-y", "-i", tmp_rgb, "-i", tmp_a, "-map", "0:v:0", "-map", "1:v:0",
             "-c", "copy", "-an", "-map_metadata", "-1",
             "-metadata:s:v:0", "title=divaverse:rgb", "-metadata:s:v:1", "title=divaverse:alpha", args.out])
        os.remove(tmp_rgb); os.remove(tmp_a)

    # ---- content metrics
    n = len(frames_out)
    bb = np.array(stats["bbox"]); ce = np.array(stats["centroid"])
    seam_a = float(np.abs(first_a[0].astype(np.int16) - last_a[0].astype(np.int16)).mean())
    seam_rgb = float(np.abs(first_a[1].astype(np.int16) - last_a[1].astype(np.int16)).mean())
    rep["content"] = {
        "frames": n, "alpha_levels": [args.alpha_lo, args.alpha_hi],
        "raw_interior_alpha_250_254_px_per_frame": round(stats["interior_250_254_px"] / n, 1),
        "partial_alpha_px_per_frame": round(stats["partial_px"] / n, 1),
        "opaque_px_per_frame": round(stats["opaque_px"] / n, 1),
        "frames_touching_edge": dict(zip(["top", "bottom", "left", "right"], stats["touch"])),
        "union_bbox": [int(bb[:, 0].min()), int(bb[:, 1].min()), int(bb[:, 2].max()), int(bb[:, 3].max())],
        "feet_y_max": int(bb[:, 3].max()), "feet_y_median": float(np.median(bb[:, 3])),
        "centroid_std_px": [round(float(ce[:, 0].std()), 2), round(float(ce[:, 1].std()), 2)],
        "feet_y_std_px": round(float(bb[:, 3].std()), 2),
        "consecutive_alpha_mad_mean": round(float(np.mean([np.abs(frames_out[i][1].astype(np.int16) - frames_out[i+1][1].astype(np.int16)).mean() for i in range(0, n - 1, 4)])), 2),
        "loop_seam_alpha_mad": round(seam_a, 2), "loop_seam_rgb_mad": round(seam_rgb, 2),
    }
    c = rep["content"]
    if c["frames_touching_edge"]["top"] or c["frames_touching_edge"]["left"] or c["frames_touching_edge"]["right"]:
        rep["warnings"].append(f"performer touches frame edge {c['frames_touching_edge']} -> likely cropped limbs/head")
    if c["feet_y_std_px"] > 4:
        rep["warnings"].append(f"feet baseline std {c['feet_y_std_px']}px -> camera bob / jumps")
    if N: seam_a = float(np.abs(frames_out[0][1].astype(np.int16) - frames_out[-1][1].astype(np.int16)).mean())
    if seam_a > 6 and not N:
        rep["warnings"].append(f"loop seam alpha MAD {seam_a:.1f}: hard cut on loop (needs crossfade or ping-pong)")

    # ---- verify output
    rep["output"] = verify(args.out, args.layout, W, H, hp, n, fps_str, ref_rgb, ref_a)
    if rep["output"]["errors"]: rep["errors"] += rep["output"]["errors"]
    rep["elapsed_s"] = round(time.time() - t0, 1)

    clip = {
        "id": args.clip_id or os.path.splitext(os.path.basename(args.out))[0],
        "file": os.path.basename(args.out), "type": "dance",
        "layout": args.layout, "codec": args.codec,
        "width": W, "height": H, "fps": fps_str, "frames": n,
        "rgb_track": 0, "alpha_track": 0 if args.layout == "packed" else 1,
        "rgb_rect": [0, 0, W, H], "alpha_rect": [0, hp if args.layout == "packed" else 0, W, H],
        "alpha_encoding": "luma_bt709_limited", "alpha_premultiplied": False,
        "ground_y": round(c["feet_y_median"] / H, 4),
        "loop": f"seamless_baked_crossfade_{N}" if N else ("cut" if seam_a <= 6 else "needs_crossfade"),
        "audio_enabled": False,
        "sha256": hashlib.sha256(open(args.out, "rb").read()).hexdigest(),
    }
    rep["manifest_clip"] = clip
    base = os.path.splitext(args.out)[0]
    with open(base + ".report.json", "w") as f: json.dump(rep, f, indent=2)
    with open(base + ".clip.json", "w") as f: json.dump(clip, f, indent=2)
    print(json.dumps({k: rep[k] for k in ("alignment", "content", "output", "warnings", "errors")}, indent=2))
    sys.exit(1 if rep["errors"] else 0)

def verify(path, layout, W, H, hp, n, fps_str, ref_rgb, ref_a):
    r = {"file": os.path.basename(path), "bytes": os.path.getsize(path), "errors": []}
    j = ffprobe_json(path, "-show_streams", "-show_format", "-count_frames")
    vs = [s for s in j["streams"] if s["codec_type"] == "video"]
    au = [s for s in j["streams"] if s["codec_type"] != "video"]
    r["streams"] = [{"index": s["index"], "codec": s["codec_name"], "w": s["width"], "h": s["height"],
                     "frames": int(s.get("nb_read_frames", 0)), "title": s.get("tags", {}).get("title"),
                     "pix_fmt": s.get("pix_fmt"), "color_range": s.get("color_range"),
                     "color_space": s.get("color_space")} for s in vs]
    r["non_video_streams"] = len(au)
    r["format_tags"] = j["format"].get("tags", {})
    r["bitrate_kbps"] = round(int(j["format"].get("bit_rate", 0)) / 1000, 1)
    if au: r["errors"].append("output contains non-video streams")
    exp = 1 if layout == "packed" else 2
    if len(vs) != exp: r["errors"].append(f"expected {exp} video streams, got {len(vs)}")
    for s in r["streams"]:
        if s["frames"] != n: r["errors"].append(f"stream {s['index']} has {s['frames']} frames, expected {n}")
    # PTS equality across tracks
    ptss = []
    for s in vs:
        pk = ffprobe_json(path, "-select_streams", str(s["index"]), "-show_entries", "packet=pts", "-show_packets")
        ptss.append(sorted(int(p["pts"]) for p in pk["packets"]))
    if len(ptss) == 2 and ptss[0] != ptss[1]: r["errors"].append("RGB/alpha PTS differ")
    tb = frac(vs[0]["time_base"]); fps = frac(fps_str); p0 = ptss[0]
    dev = [abs(p0[k] - p0[0] - round(k / fps / tb)) for k in range(len(p0))]
    r["pts_grid_max_dev_ticks"] = int(max(dev)); r["time_base"] = vs[0]["time_base"]
    r["pts_on_frame_grid"] = bool(max(dev) <= 1)
    if not r["pts_on_frame_grid"]: r["errors"].append("PTS deviate from frame grid by >1 tick")
    # decode-back fidelity (alpha via luma, like the Player shader)
    want = {ci for ci, _ in ref_a}
    if layout == "packed":
        dec = decode_frames(path, W, hp * 2)
        pairs = ((f[:H], f[hp:hp + H, :, 0]) for f in dec)
    else:
        def tr(i):
            p = subprocess.Popen(["ffmpeg", "-v", "error", "-i", path, "-map", f"0:v:{i}",
                "-vf", "scale=in_color_matrix=bt709:in_range=tv:out_range=pc:flags=accurate_rnd+full_chroma_int",
                "-pix_fmt", "rgb24", "-f", "rawvideo", "-"], stdout=subprocess.PIPE)
            while True:
                b = p.stdout.read(W * H * 3)
                if len(b) < W * H * 3: break
                yield np.frombuffer(b, np.uint8).reshape(H, W, 3)
        pairs = ((c, a[..., 0]) for c, a in zip(tr(0), tr(1)))
    ra, rc = dict(ref_a), dict(ref_rgb); ea, ec, emax = [], [], 0
    for i, (c, a) in enumerate(pairs):
        if i in want:
            da = np.abs(a.astype(np.int16) - ra[i].astype(np.int16))
            ea.append(float(da.mean())); emax = max(emax, int(da.max()))
            m = ra[i] > 128
            ec.append(float(np.abs(c[m].astype(np.int16) - rc[i][m].astype(np.int16)).mean()))
    psnr = lambda e: round(20 * np.log10(255 / max(e, 1e-6)), 1)
    r["alpha_mae"] = round(float(np.mean(ea)), 3); r["alpha_max_err"] = emax
    r["rgb_mae_opaque"] = round(float(np.mean(ec)), 3)
    if r["alpha_mae"] > 2.0: r["errors"].append(f"alpha MAE {r['alpha_mae']} too high")
    return r

if __name__ == "__main__":
    main()
