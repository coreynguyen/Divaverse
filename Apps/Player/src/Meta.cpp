#include "Meta.h"
#include "MkvIoWin.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

void JsonMergeInto(JVal& base, const JVal& over, std::vector<std::string>* blocked, const std::string& path) {
    if (over.t != JVal::Obj || base.t != JVal::Obj) { base = over; return; }
    for (auto& kv : over.o) {
        std::string p = path.empty() ? kv.first : path + "." + kv.first;
        if (blocked && path.empty() && kv.first == "media") { blocked->push_back(p); continue; }      // facts come from the container
        JVal* dst = nullptr;
        for (auto& b : base.o) if (b.first == kv.first) { dst = &b.second; break; }
        if (kv.second.t == JVal::Null) {
            for (size_t i = 0; i < base.o.size(); ++i) if (base.o[i].first == kv.first) { base.o.erase(base.o.begin() + i); break; }
        } else if (!dst) base.o.push_back(kv);
        else if (dst->t == JVal::Obj && kv.second.t == JVal::Obj) JsonMergeInto(*dst, kv.second, blocked, p);
        else *dst = kv.second;
    }
}

static bool Rect4(const JVal* v, int r[4]) {
    if (!v || v->t != JVal::Arr || v->a.size() != 4) return false;
    for (int i = 0; i < 4; ++i) { if (v->a[i].t != JVal::Num) return false; r[i] = (int)v->a[i].n; }
    return r[2] > 0 && r[3] > 0 && r[0] >= 0 && r[1] >= 0;
}

void MetaFromJson(const JVal& j, MediaMeta& m, bool fromSidecar) {
    m.schema = (int)j.num("schema_version", 1);
    m.divaId = j.str("diva_id", m.divaId); m.divaName = j.str("diva_name", m.divaName);
    m.showId = j.str("show_id", m.showId); m.showTitle = j.str("show_title", m.showTitle);
    m.clipId = j.str("clip_id", m.clipId); m.title = j.str("title", m.title);
    m.order = (int)j.num("order", m.order);
    if (auto* d = j.get("diva"); d && d->t == JVal::Obj) {
        m.rating = (int)d->num("rating", m.rating);
        if (auto* pr = d->get("profile"); pr && pr->t == JVal::Obj) {
            m.profile.clear();
            for (auto& kv : pr->o) if (kv.second.t == JVal::Str && m.profile.size() < 16) m.profile.emplace_back(kv.first, kv.second.s);
        }
    }
    if (auto* v = j.get("video"); v && v->t == JVal::Obj) {
        m.layout = v->str("layout", m.layout);
        int a[4], b[4];
        if (Rect4(v->get("rgb_rect"), a) && Rect4(v->get("alpha_rect"), b)) { m.hasRects = true; memcpy(m.rgbRect, a, sizeof(a)); memcpy(m.alphaRect, b, sizeof(b)); }
        if (auto* lv = v->get("alpha_levels"); lv && lv->t == JVal::Arr && lv->a.size() == 2 && lv->a[0].t == JVal::Num && lv->a[1].t == JVal::Num) {
            float lo = (float)lv->a[0].n, hi = (float)lv->a[1].n;
            if (lo >= 0 && hi <= 1 && hi - lo > 0.01f) { m.hasLevels = true; m.alphaLo = lo; m.alphaHi = hi; }
        }
    }
    if (auto* c = j.get("canvas"); c && c->t == JVal::Obj) { m.canvasW = (int)c->num("width", m.canvasW); m.canvasH = (int)c->num("height", m.canvasH); }
    if (auto* p = j.get("physical"); p && p->t == JVal::Obj) {
        m.actorHeightM = p->num("actor_height_m", m.actorHeightM); m.ppm = p->num("pixels_per_meter", m.ppm);
        if (auto* g = p->get("ground_anchor"); g && g->t == JVal::Arr && g->a.size() == 2 && g->a[0].t == JVal::Num && g->a[1].t == JVal::Num) {
            m.hasAnchor = true; m.anchorU = (float)g->a[0].n; m.anchorV = (float)g->a[1].n;
        }
    }
    if (auto* l = j.get("loop"); l && l->t == JVal::Obj) m.loopMode = l->str("mode", m.loopMode);
    if (auto* a = j.get("artwork"); a && a->t == JVal::Obj) {
        std::string c = a->str("cover"); if (!c.empty()) { m.cover = c; m.coverFromSidecar = fromSidecar; }
        m.thumb = a->str("thumb", m.thumb);
    }
    if (auto* c = j.get("content"); c && c->t == JVal::Obj) { m.origin = c->str("origin", m.origin); m.contentRating = c->str("rating", m.contentRating); }
}

std::wstring SidecarPath(const std::wstring& p) {
    size_t dot = p.find_last_of(L'.'), sl = p.find_last_of(L"\\/");
    std::wstring stem = (dot != std::wstring::npos && (sl == std::wstring::npos || dot > sl)) ? p.substr(0, dot) : p;
    return stem + L".divaverse.json";
}

bool ProbeMedia(const std::wstring& path, MediaInfo& info, ClipDesc* desc) {
    info = MediaInfo{}; info.path = path;
    auto E = [&](const char* s) { info.errors.emplace_back(s); return false; };
    auto W = [&](const std::string& s) { info.warnings.push_back(s); };
    Win32Io io; MkvDemux mkv; std::string e8;
    if (!io.Open(path)) return E("DV-E000: file cannot be opened");
    info.fileSize = io.Size();
    if (!mkv.Open(&io, e8)) { info.errors.push_back("DV-E001: not a readable Matroska file (" + e8 + ")"); return false; }
    info.docType = mkv.DocType(); info.cues = mkv.Cues().size(); info.durationS = mkv.DurationNs() / 1e9;

    // ---- facts: tracks
    auto& tr = mkv.Tracks(); std::vector<int> vids;
    for (int i = 0; i < (int)tr.size(); ++i) if (tr[i].type == 1) vids.push_back(i);
    info.videoTracks = (int)vids.size();
    if (vids.empty()) return E("DV-E002: no video track");
    int rgbV = -1, aV = -1;
    for (int k = 0; k < (int)vids.size(); ++k) { if (tr[vids[k]].name == "divaverse:rgb") rgbV = k; else if (tr[vids[k]].name == "divaverse:alpha") aV = k; }
    if (rgbV < 0 && aV < 0 && vids.size() >= 2) { rgbV = 0; aV = 1; W("DV-W104: video tracks are not named divaverse:rgb / divaverse:alpha; assuming track order"); }
    if (rgbV < 0) rgbV = aV == 0 ? 1 : 0;
    if (rgbV >= (int)vids.size()) return E("DV-E010: alpha track present but no colour track");
    auto facts = [&](int k, TrackFacts& f) -> bool {
        const MkvTrack& t = tr[vids[k]]; f.present = true; f.codec = t.codecId; f.name = t.name; f.w = t.width; f.h = t.height; f.range = t.colourRange;
        if (!t.unsupported.empty()) { info.errors.push_back("DV-E005: " + t.unsupported); return false; }
        if (t.codecId != "V_MPEG4/ISO/AVC") { info.errors.push_back("DV-E003: unsupported codec " + t.codecId + " (this Player decodes H.264)"); return false; }
        AvcConfig c; std::string ce;
        if (!AvcParseConfig(t.codecPrivate.data(), t.codecPrivate.size(), c, ce)) { info.errors.push_back("DV-E004: H.264 configuration unreadable (" + ce + ")"); return false; }
        f.w = c.cropW; f.h = c.cropH; f.profile = c.profile; f.level = c.level; f.bitDepth = c.bitDepth;
        if (c.bitDepth != 8 || c.chromaFormat != 1) { info.errors.push_back("DV-E004: only 8-bit 4:2:0 is supported"); return false; }
        if (f.range == 0) f.range = c.fullRange ? 2 : 1;
        return true;
    };
    bool ok = facts(rgbV, info.rgb);
    info.dual = aV >= 0;
    if (info.dual) ok = facts(aV, info.alpha) && ok;
    const MkvTrack& t0 = tr[vids[rgbV]];
    if (t0.defaultDurationNs) info.fps = 1e9 / (double)t0.defaultDurationNs;
    if (info.dual && tr[vids[aV]].defaultDurationNs && t0.defaultDurationNs &&
        std::llabs((long long)tr[vids[aV]].defaultDurationNs - (long long)t0.defaultDurationNs) > 1000)
        W("DV-W112: colour and alpha tracks declare different frame durations");
    if (info.fps > 0 && info.durationS > 0) info.frames = (int)std::llround(info.durationS * info.fps);
    if (!info.cues) W("DV-W120: no Cues; seeking restarts from the beginning");

    // ---- attachments
    for (auto& a : mkv.Attachments()) {
        char b[160]; snprintf(b, sizeof(b), "%s (%s, %llu bytes)", a.name.c_str(), a.mime.c_str(), (unsigned long long)a.size); info.attachments.emplace_back(b);
    }
    for (auto* n : { "cover.jpg", "cover.png", "small_cover.jpg", "small_cover.png", "cover_land.jpg", "small_cover_land.jpg" }) if (mkv.FindAttachment(n)) info.hasCover = true;

    // ---- metadata: defaults < embedded < sidecar
    JVal merged; merged.t = JVal::Obj; bool any = false;
    if (const MkvAttachment* a = mkv.FindAttachment("divaverse.json")) {
        info.hasMetaAttachment = true; std::vector<uint8_t> raw; JVal j; std::string pe;
        if (!mkv.ReadAttachment(*a, raw, 1u << 20)) W("DV-W130: embedded divaverse.json larger than 1 MiB, ignored");
        else if (!JsonParse(std::string(raw.begin(), raw.end()), j, &pe) || j.t != JVal::Obj) W("DV-W103: embedded divaverse.json is not valid JSON (" + pe + ")");
        else { JsonMergeInto(merged, j, nullptr); info.meta.embedded = true; any = true; }
    } else W("DV-W101: no embedded divaverse.json; using defaults");
    std::wstring side = SidecarPath(path); std::string txt;
    if (FileExists(side)) {
        info.hasSidecar = true; JVal j; std::string pe; std::vector<std::string> blocked;
        if (!ReadFileUtf8(side, txt) || txt.size() > (1u << 20)) W("DV-W130: sidecar unreadable or larger than 1 MiB, ignored");
        else if (!JsonParse(txt, j, &pe) || j.t != JVal::Obj) W("DV-W103: sidecar is not valid JSON (" + pe + ")");
        else {
            JsonMergeInto(merged, j, &blocked); any = true; info.meta.sidecar = true;
            for (auto& b : blocked) W("DV-W150: sidecar key '" + b + "' ignored (media facts come from the container)");
            MediaMeta sideOnly; MetaFromJson(j, sideOnly, true); info.meta.coverFromSidecar = !sideOnly.cover.empty();
        }
    }
    if (any) {
        bool fromSide = info.meta.coverFromSidecar, emb = info.meta.embedded, sc = info.meta.sidecar;
        MetaFromJson(merged, info.meta, false);
        info.meta.coverFromSidecar = fromSide; info.meta.embedded = emb; info.meta.sidecar = sc;
        if (info.meta.schema > 1) W("DV-W102: metadata schema_version " + std::to_string(info.meta.schema) + " is newer than this Player (1); unknown fields ignored");
    }
    if (info.meta.title.empty()) info.meta.title = mkv.Tag("TITLE");
    MediaMeta& m = info.meta;
    if (m.ppm <= 0) W("DV-W140: no physical scale (physical.pixels_per_meter); displayed with the uncalibrated fallback");
    else if (m.ppm < 20 || m.ppm > 20000) { W("DV-W141: pixels_per_meter out of range, ignored"); m.ppm = 0; }

    // ---- playable description
    if (desc && ok) {
        ClipDesc& c = *desc; c.path = path;
        int W0 = info.rgb.w, H0 = info.rgb.h;
        if (info.dual) {
            c.layout = Layout::Dual; c.rgbTrack = rgbV; c.alphaTrack = aV;
            c.rgbRect = { 0, 0, W0, H0 }; c.alphaRect = { 0, 0, info.alpha.w, info.alpha.h }; c.rectsKnown = true;
        } else {
            if (m.layout == "opaque") c.layout = Layout::Opaque; else if (m.layout == "packed" || c.layout == Layout::Auto || c.layout == Layout::Dual) c.layout = Layout::Packed;
            c.rgbTrack = rgbV;
            if (m.hasRects) {
                auto inside = [&](const int r[4]) { return r[0] + r[2] <= W0 && r[1] + r[3] <= H0; };
                if (inside(m.rgbRect) && inside(m.alphaRect)) {
                    c.rgbRect = { m.rgbRect[0], m.rgbRect[1], m.rgbRect[0] + m.rgbRect[2], m.rgbRect[1] + m.rgbRect[3] };
                    c.alphaRect = { m.alphaRect[0], m.alphaRect[1], m.alphaRect[0] + m.alphaRect[2], m.alphaRect[1] + m.alphaRect[3] }; c.rectsKnown = true;
                } else W("DV-W151: video.rgb_rect / alpha_rect exceed the decoded picture, ignored");
            }
            if (c.rectsKnown && (c.rgbRect.right > W0 || c.rgbRect.bottom > H0 || c.alphaRect.right > W0 || c.alphaRect.bottom > H0)) c.rectsKnown = false;   // stale legacy manifest
            if (!c.rectsKnown) {
                if (c.layout == Layout::Opaque) { c.rgbRect = { 0, 0, W0, H0 }; c.alphaRect = c.rgbRect; }
                else { c.rgbRect = { 0, 0, W0, H0 / 2 }; c.alphaRect = { 0, H0 / 2, W0, H0 }; }
                c.rectsKnown = true;
            }
        }
        c.width = c.rgbRect.right - c.rgbRect.left; c.height = c.rgbRect.bottom - c.rgbRect.top;
        if (info.fps > 0) {                                         // facts beat any manifest value
            double f = info.fps; c.fpsNum = (int)std::llround(f * 1000); c.fpsDen = 1000;
            for (int n : { 24, 25, 30, 48, 50, 60, 120 }) if (std::fabs(f - n) < 0.005) { c.fpsNum = n; c.fpsDen = 1; }
            for (int n : { 24000, 30000, 60000 }) if (std::fabs(f - n / 1001.0) < 0.005) { c.fpsNum = n; c.fpsDen = 1001; }
        }
        if (info.frames > 0) c.frames = info.frames;
        if (m.hasLevels) { c.alphaLo = m.alphaLo; c.alphaHi = m.alphaHi; }
        if (m.hasAnchor) { c.anchorU = std::fmin(1.f, std::fmax(0.f, m.anchorU)); c.groundY = std::fmin(1.f, std::fmax(0.f, m.anchorV)); }
        c.canvasW = m.canvasW > 0 ? m.canvasW : c.width; c.canvasH = m.canvasH > 0 ? m.canvasH : c.height;
        c.ppm = m.ppm; c.actorHeightM = m.actorHeightM; c.loopOnce = m.loopMode == "once";
        if (!m.clipId.empty()) c.id = Utf8ToW(m.clipId);
        if (!m.title.empty()) c.title = Utf8ToW(m.title);
        if (c.title.empty()) c.title = c.id;
    }
    return ok;
}
