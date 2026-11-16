#include "Clip.h"
#include "Json.h"

static bool ParseRect(const JVal* v, RECT& r) {
    if (!v || v->t != JVal::Arr || v->a.size() != 4) return false;
    int x = (int)v->a[0].n, y = (int)v->a[1].n, w = (int)v->a[2].n, h = (int)v->a[3].n;
    r = { x, y, x + w, y + h }; return w > 0 && h > 0;
}

static void ParseFps(const std::string& s, ClipDesc& c) {
    int n = 0, d = 1;
    if (sscanf_s(s.c_str(), "%d/%d", &n, &d) == 2 && n > 0 && d > 0) { c.fpsNum = n; c.fpsDen = d; }
    else if (sscanf_s(s.c_str(), "%d", &n) == 1 && n > 0) { c.fpsNum = n; c.fpsDen = 1; }
}

static void ApplyClipJson(const JVal& j, ClipDesc& c) {
    std::string lay = j.str("layout");
    if (lay == "packed") c.layout = Layout::Packed;
    else if (lay == "dual") c.layout = Layout::Dual;
    else if (lay == "opaque") c.layout = Layout::Opaque;
    c.width  = (int)j.num("width", c.width);
    c.height = (int)j.num("height", c.height);
    c.frames = (int)j.num("frames", c.frames);
    c.rgbTrack = (int)j.num("rgb_track", c.rgbTrack);
    c.alphaTrack = (int)j.num("alpha_track", c.alphaTrack);
    c.groundY = (float)j.num("ground_y", c.groundY);
    if (auto* f = j.get("fps")) { if (f->t == JVal::Str) ParseFps(f->s, c); else if (f->t == JVal::Num) { c.fpsNum = (int)(f->n * 1000); c.fpsDen = 1000; } }
    if (ParseRect(j.get("rgb_rect"), c.rgbRect) && ParseRect(j.get("alpha_rect"), c.alphaRect)) c.rectsKnown = true;
    if (auto* lv = j.get("alpha_levels"); lv && lv->t == JVal::Arr && lv->a.size() == 2) {
        c.alphaLo = (float)lv->a[0].n; c.alphaHi = (float)lv->a[1].n;
    }
    std::string id = j.str("id"); if (!id.empty()) c.id = Utf8ToW(id);
}

static bool LoadManifest(const std::wstring& file, Show& out, std::wstring& err) {
    std::string txt, perr; JVal j;
    if (!ReadFileUtf8(file, txt)) { err = L"cannot read " + file; return false; }
    if (!JsonParse(txt, j, &perr)) { err = L"manifest parse error: " + Utf8ToW(perr); return false; }
    int schema = (int)j.num("schema_version", 1);
    if (schema > 1) Log("manifest schema_version %d > 1, attempting best-effort load", schema);
    out.root = DirOf(file);
    out.title = Utf8ToW(j.str("title", "Untitled"));
    out.showId = Utf8ToW(j.str("show_id"));
    out.divaId = Utf8ToW(j.str("diva_id")); out.divaName = Utf8ToW(j.str("diva_name"));
    if (auto* dv = j.get("diva"); dv && dv->t == JVal::Obj) {
        out.rating = (int)dv->num("rating", 0);
        if (auto* pr = dv->get("profile"); pr && pr->t == JVal::Obj)
            for (auto& kv : pr->o) if (kv.second.t == JVal::Str) out.profile.emplace_back(Utf8ToW(kv.first), Utf8ToW(kv.second.s));
    }
    auto* clips = j.get("clips");
    if (!clips || clips->t != JVal::Arr) { err = L"manifest has no clips[]"; return false; }
    for (auto& cj : clips->a) {
        ClipDesc c; std::string f = cj.str("file");
        if (f.empty()) continue;
        c.path = JoinPath(out.root, Utf8ToW(f));
        c.id = Utf8ToW(cj.str("id", f));
        ApplyClipJson(cj, c);
        if (!FileExists(c.path)) { Log("manifest clip missing: %s", WToUtf8(c.path).c_str()); continue; }
        out.clips.push_back(std::move(c));
    }
    if (out.clips.empty()) { err = L"manifest lists no playable clips"; return false; }
    return true;
}

bool LoadShowOrClip(const std::wstring& path, Show& out, std::wstring& err) {
    out = Show{};
    if (DirExists(path)) {
        std::wstring m = JoinPath(path, L"manifest.json");
        if (!FileExists(m)) { err = L"no manifest.json in " + path; return false; }
        return LoadManifest(m, out, err);
    }
    if (!FileExists(path)) { err = L"not found: " + path; return false; }
    std::wstring low = path; for (auto& ch : low) ch = (wchar_t)towlower(ch);
    if (low.size() > 5 && low.compare(low.size() - 5, 5, L".json") == 0) return LoadManifest(path, out, err);

    ClipDesc c; c.path = path;
    size_t s = path.find_last_of(L"\\/"), d = path.find_last_of(L'.');
    std::wstring stem = path.substr(0, d == std::wstring::npos ? path.size() : d);
    c.id = path.substr(s == std::wstring::npos ? 0 : s + 1);
    std::wstring side = stem + L".clip.json"; std::string txt; JVal j;
    if (FileExists(side) && ReadFileUtf8(side, txt) && JsonParse(txt, j)) { ApplyClipJson(j, c); Log("using sidecar %s", WToUtf8(side).c_str()); }
    out.root = DirOf(path); out.title = c.id; out.clips.push_back(std::move(c));
    return true;
}
