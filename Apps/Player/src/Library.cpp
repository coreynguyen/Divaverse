#include "Library.h"
#include "MkvIoWin.h"
#include <mfapi.h>
#include <algorithm>
#include "MkvIoWin.h"
#include "MfDecoder.h"
#include <cwctype>
#include <cmath>

bool DecodePoster(const ClipDesc& clip, int targetH, int frameNo, std::vector<uint8_t>& out, int& ow, int& oh, std::wstring& err) {
    Win32Io io; MkvDemux mkv; std::string e8;
    if (!io.Open(clip.path)) { err = L"cannot open"; return false; }
    if (!mkv.Open(&io, e8)) { err = Utf8ToW(e8); return false; }
    auto& tracks = mkv.Tracks();
    std::vector<int> vids; for (int i = 0; i < (int)tracks.size(); ++i) { if (tracks[i].type == 1) vids.push_back(i); tracks[i].enabled = false; }
    if (vids.empty()) { err = L"no video"; return false; }
    int rgbV = 0, aV = -1;
    for (int k = 0; k < (int)vids.size(); ++k) { if (tracks[vids[k]].name == "divaverse:rgb") rgbV = k; if (tracks[vids[k]].name == "divaverse:alpha") aV = k; }
    bool dual = clip.layout == Layout::Dual || (clip.layout == Layout::Auto && vids.size() >= 2);
    if (dual && aV < 0) aV = rgbV == 0 ? 1 : 0;
    if (dual && aV >= (int)vids.size()) dual = false;
    int roles = dual ? 2 : 1, trk[2] = { vids[rgbV], dual ? vids[aV] : -1 };
    AvcConfig cfg[2]; MfH264Decoder dec[2]; std::deque<MfFrame> q[2];
    for (int k = 0; k < roles; ++k) {
        MkvTrack& t = tracks[trk[k]];
        if (t.codecId != "V_MPEG4/ISO/AVC" || !AvcParseConfig(t.codecPrivate.data(), t.codecPrivate.size(), cfg[k], e8)) { err = L"unsupported codec"; return false; }
        if (!dec[k].Init(cfg[k], 24, 1, nullptr, false, err)) return false;
        t.enabled = true;
    }
    MkvPacket pk; int want = std::max(0, frameNo) + 1; bool eof = false;
    for (int guard = 0; guard < 400; ++guard) {
        bool enough = true; for (int k = 0; k < roles; ++k) if ((int)q[k].size() < want) enough = false;
        if (enough || eof) break;
        if (!mkv.Read(pk, e8)) { for (int k = 0; k < roles; ++k) dec[k].Drain(q[k], err); eof = true; continue; }
        int k = pk.track == trk[0] ? 0 : pk.track == trk[1] ? 1 : -1;
        if (k < 0) continue;
        if (!dec[k].Feed(pk.data.data(), pk.data.size(), pk.ptsNs / 100, pk.key, q[k], err)) return false;
        for (int j = 0; j < roles; ++j) while ((int)q[j].size() > want) q[j].pop_front();     // keep memory flat
    }
    size_t n = q[0].size(); if (roles == 2) n = std::min(n, q[1].size());
    if (!n) { err = L"no frame decoded"; return false; }
    MfFrame& fc = q[0][n - 1]; MfFrame& fa = roles == 2 ? q[1][n - 1] : fc;
    if (!fc.Lock() || (roles == 2 && !fa.Lock())) { err = L"cannot read decoder output"; return false; }
    RECT rr, ar;
    if (clip.rectsKnown) { rr = clip.rgbRect; ar = clip.alphaRect; }
    else if (dual) { rr = { 0, 0, fc.w, fc.h }; ar = { 0, 0, fa.w, fa.h }; }
    else { rr = { 0, 0, fc.w, fc.h / 2 }; ar = { 0, fc.h / 2, fc.w, fc.h }; }
    bool opaque = clip.layout == Layout::Opaque;
    if (opaque) { rr = { 0, 0, fc.w, fc.h }; ar = rr; }
    int sw = rr.right - rr.left, sh = rr.bottom - rr.top;
    if (sw <= 0 || sh <= 0 || rr.right > fc.w || rr.bottom > fc.h || ar.right > fa.w || ar.bottom > fa.h) { fc.Unlock(); fa.Unlock(); err = L"bad rects"; return false; }
    oh = std::min(targetH, sh); ow = std::max(1, (int)std::lround((double)sw * oh / sh));
    out.assign((size_t)ow * oh * 4, 0);
    auto clampf = [](float v) { return v < 0 ? 0.f : v > 255 ? 255.f : v; };
    for (int y = 0; y < oh; ++y) {
        int sy0 = y * sh / oh, sy1 = std::max(sy0 + 1, (y + 1) * sh / oh);
        for (int x = 0; x < ow; ++x) {
            int sx0 = x * sw / ow, sx1 = std::max(sx0 + 1, (x + 1) * sw / ow);
            float R = 0, G = 0, B = 0, A = 0; int cnt = 0;
            for (int sy = sy0; sy < sy1; sy += std::max(1, (sy1 - sy0) / 2)) for (int sx = sx0; sx < sx1; sx += std::max(1, (sx1 - sx0) / 2)) {   // up to 2x2 taps
                int cx = rr.left + sx, cy = rr.top + sy;
                float Y = fc.y[(size_t)cy * fc.pitch + cx];
                const uint8_t* uv = fc.uv + (size_t)(cy / 2) * fc.pitch + (cx / 2) * 2;
                float U = uv[0] - 128.f, V = uv[1] - 128.f, yl = (Y - 16.f) * (255.f / 219.f);
                float r = yl + 1.7927f * V, g = yl - 0.2132f * U - 0.5329f * V, b = yl + 2.1124f * U;      // BT.709 limited
                float a = 1.f;
                if (!opaque) { int ax = ar.left + sx * (ar.right - ar.left) / sw, ay = ar.top + sy * (ar.bottom - ar.top) / sh;
                               a = (fa.y[(size_t)ay * fa.pitch + ax] - 16.f) / 219.f; a = a < 0 ? 0 : a > 1 ? 1 : a; }
                R += clampf(r) * a; G += clampf(g) * a; B += clampf(b) * a; A += a; ++cnt;
            }
            uint8_t* o = &out[((size_t)y * ow + x) * 4];
            o[0] = (uint8_t)(B / cnt + .5f); o[1] = (uint8_t)(G / cnt + .5f); o[2] = (uint8_t)(R / cnt + .5f); o[3] = (uint8_t)(A / cnt * 255.f + .5f);
        }
    }
    fc.Unlock(); if (roles == 2) fa.Unlock();
    return true;
}


namespace {
uint64_t FileTime64(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa)) return 0;
    return ((uint64_t)fa.ftCreationTime.dwHighDateTime << 32) | fa.ftCreationTime.dwLowDateTime;
}
std::wstring Stem(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/"); std::wstring n = s == std::wstring::npos ? p : p.substr(s + 1);
    size_t dot = n.rfind(L'.'); if (dot != std::wstring::npos) n.resize(dot);
    for (auto& c : n) if (c == L'_') c = L' ';
    return n;
}
bool EndsWith(const std::wstring& s, const wchar_t* e) {
    size_t n = wcslen(e); if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) if (towlower(s[s.size() - n + i]) != e[i]) return false;
    return true;
}
// A sidecar may point at an image file. Only a plain relative path below the media's own folder is accepted.
bool SafeRelative(const std::string& rel) {
    if (rel.empty() || rel.size() > 200 || rel[0] == '/' || rel[0] == '\\' || rel.find(':') != std::string::npos || rel.find("..") != std::string::npos) return false;
    return true;
}
}

int Library::FindClip(const std::wstring& path) const {
    for (size_t i = 0; i < clips_.size(); ++i) if (!_wcsicmp(clips_[i].desc.path.c_str(), path.c_str())) return (int)i;
    return -1;
}

Show Library::MakeShow(int d) const {
    Show s;
    if (d < 0 || d >= (int)divas_.size()) return s;
    s.title = divas_[d].name; s.divaId = divas_[d].key; s.divaName = divas_[d].name; s.profile = divas_[d].profile; s.rating = divas_[d].rating;
    for (int c : divas_[d].clips) s.clips.push_back(clips_[c].desc);
    return s;
}

void Library::AddMkv(const std::wstring& file, const ClipDesc* legacy, const Show* show, const std::wstring& artDir) {
    if (FindClip(file) >= 0) return;
    LibClip c;
    if (legacy) c.desc = *legacy;
    else { Show tmp; std::wstring e; if (LoadShowOrClip(file, tmp, e) && !tmp.clips.empty()) c.desc = tmp.clips[0]; }     // picks up a legacy .clip.json
    if (!ProbeMedia(file, c.info, &c.desc)) {
        problems_.push_back({ file, c.info.errors.empty() ? "unreadable" : c.info.errors[0] });
        Log("library: %s: %s", WToUtf8(file).c_str(), problems_.back().what.c_str());
        return;
    }
    c.title = !c.desc.title.empty() ? c.desc.title : Stem(file);
    if (c.desc.id.empty() || EndsWith(c.desc.id, L".mkv")) c.desc.id = Stem(file);
    c.seconds = c.info.durationS; c.added = FileTime64(file); c.folderArt = artDir.empty() ? DirOf(file) : artDir;
    clips_.push_back(std::move(c));
    clipShow_.push_back(show ? (int)(show - shows_.data()) : -1);
}

void Library::AddManifest(const std::wstring& dir) {
    Show s; std::wstring err;
    if (!LoadShowOrClip(dir, s, err)) { problems_.push_back({ dir, "DV-E020: " + WToUtf8(err) }); return; }
    shows_.push_back(s);
    const Show* sp = &shows_.back();
    for (auto& c : s.clips) AddMkv(c.path, &c, sp, dir);
}

void Library::ScanDir(const std::wstring& dir, int depth) {
    if (FileExists(JoinPath(dir, L"manifest.json"))) { AddManifest(dir); return; }
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> dirs, files;
    do {
        std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) dirs.push_back(n); else if (EndsWith(n, L".mkv")) files.push_back(n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(dirs.begin(), dirs.end()); std::sort(files.begin(), files.end());
    for (auto& f : files) AddMkv(JoinPath(dir, f), nullptr, nullptr, L"");
    if (depth < 3) for (auto& d : dirs) ScanDir(JoinPath(dir, d), depth + 1);
}

void Library::Group() {
    divas_.clear();
    for (size_t i = 0; i < clips_.size(); ++i) {
        LibClip& c = clips_[i]; const MediaMeta& m = c.info.meta;
        const Show* sh = clipShow_[i] >= 0 ? &shows_[clipShow_[i]] : nullptr;
        std::wstring key = !m.divaId.empty() ? Utf8ToW(m.divaId) : sh && !sh->divaId.empty() ? sh->divaId : sh ? L"show:" + sh->root : L"file:" + c.desc.path;
        int d = -1;
        for (size_t k = 0; k < divas_.size(); ++k) if (!_wcsicmp(divas_[k].key.c_str(), key.c_str())) { d = (int)k; break; }
        if (d < 0) { divas_.emplace_back(); d = (int)divas_.size() - 1; divas_[d].key = key; }
        LibDiva& dv = divas_[d];
        if (dv.name.empty()) dv.name = !m.divaName.empty() ? Utf8ToW(m.divaName) : sh && !sh->divaName.empty() ? sh->divaName : sh ? sh->title : L"";
        if (dv.profile.empty()) { for (auto& kv : m.profile) dv.profile.emplace_back(Utf8ToW(kv.first), Utf8ToW(kv.second)); if (dv.profile.empty() && sh) dv.profile = sh->profile; }
        if (!dv.rating) dv.rating = m.rating ? m.rating : sh ? sh->rating : 0;
        dv.clips.push_back((int)i); dv.seconds += c.seconds; dv.added = std::max(dv.added, c.added); c.diva = d;
    }
    for (auto& dv : divas_) {
        if (dv.name.empty() && !dv.clips.empty()) dv.name = clips_[dv.clips[0]].title;
        std::stable_sort(dv.clips.begin(), dv.clips.end(), [&](int a, int b) { return clips_[a].info.meta.order < clips_[b].info.meta.order; });
    }
}

void Library::Stop() { stop_ = true; if (th_.joinable()) th_.join(); stop_ = false; }

void Library::Rescan(const std::vector<std::wstring>& folders, HWND notify, UINT msg) {
    Stop();
    {
        std::lock_guard<std::mutex> g(mx_);
        clips_.clear(); divas_.clear(); problems_.clear(); shows_.clear(); clipShow_.clear(); ++gen_;
        shows_.reserve(256);                              // AddMkv keeps pointers into shows_ during the scan
        for (auto& root : folders) if (DirExists(root)) ScanDir(root, 0);
        Group();
        Log("library: %zu divas, %zu clips, %zu problems", divas_.size(), clips_.size(), problems_.size());
    }
    int gen = gen_;
    th_ = std::thread([this, notify, msg, gen] { Worker(notify, msg, gen); });
}

void Library::Worker(HWND notify, UINT msg, int gen) {
    bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    bool mf = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    auto ping = [&] { if (notify) PostMessageW(notify, msg, 0, (LPARAM)gen); };
    // ---- covers: sidecar override > embedded artwork > image beside the media
    size_t nd; { std::lock_guard<std::mutex> g(mx_); nd = divas_.size(); }
    for (size_t d = 0; d < nd && !stop_; ++d) {
        struct Cand { std::wstring path, folder; std::string cover; bool side; };
        std::vector<Cand> cands;
        { std::lock_guard<std::mutex> g(mx_); for (int c : divas_[d].clips) cands.push_back({ clips_[c].desc.path, clips_[c].folderArt, clips_[c].info.meta.cover, clips_[c].info.meta.coverFromSidecar }); }
        ImageBGRA img; std::wstring src;
        for (auto& c : cands) {                             // 1. explicit local override
            if (!c.side || !SafeRelative(c.cover)) continue;
            std::wstring p = JoinPath(DirOf(c.path), Utf8ToW(c.cover));
            if (DecodeImageFile(p, 900, img)) { src = L"sidecar: " + p; break; }
        }
        for (auto& c : cands) {                             // 2. embedded attachment (never decodes video)
            if (img.ok()) break;
            Win32Io io; MkvDemux mkv; std::string e;
            if (!io.Open(c.path) || !mkv.Open(&io, e)) continue;
            std::vector<std::string> names; if (!c.cover.empty() && !c.side) names.push_back(c.cover);
            for (auto* n : { "cover.jpg", "cover.png", "small_cover.jpg", "small_cover.png" }) names.emplace_back(n);
            for (auto& n : names) {
                const MkvAttachment* a = mkv.FindAttachment(n.c_str()); std::vector<uint8_t> raw;
                if (a && mkv.ReadAttachment(*a, raw, 16u << 20) && DecodeImageMem(raw.data(), raw.size(), 900, img)) { src = L"embedded: " + Utf8ToW(a->name); break; }
            }
        }
        for (auto& c : cands) {                             // 3. loose artwork next to the media / in the show folder
            if (img.ok()) break;
            std::wstring stem = c.path.substr(0, c.path.find_last_of(L'.'));
            for (auto& p : { stem + L".jpg", stem + L".png", JoinPath(c.folder, L"cover.jpg"), JoinPath(c.folder, L"cover.png"), JoinPath(c.folder, L"card.png"), JoinPath(c.folder, L"card.jpg"), JoinPath(c.folder, L"profile.png") })
                if (FileExists(p) && DecodeImageFile(p, 900, img)) { src = L"file: " + p; break; }
        }
        ImageBGRA ban;                                      // banner: embedded first, then beside the media
        for (auto& c : cands) {
            if (ban.ok()) break;
            Win32Io io; MkvDemux mkv; std::string e;
            if (io.Open(c.path) && mkv.Open(&io, e)) for (auto* n : { "banner.png", "banner.jpg" }) {
                const MkvAttachment* a = mkv.FindAttachment(n); std::vector<uint8_t> raw;
                if (a && mkv.ReadAttachment(*a, raw, 16u << 20) && DecodeImageMem(raw.data(), raw.size(), 1400, ban)) break;
            }
            for (auto* n : { L"banner.png", L"banner.jpg" }) { if (ban.ok()) break; std::wstring p = JoinPath(c.folder, n); if (FileExists(p)) DecodeImageFile(p, 1400, ban); }
        }
        { std::lock_guard<std::mutex> g(mx_); if (gen == gen_ && d < divas_.size()) { divas_[d].banner = std::move(ban); divas_[d].bannerReady = divas_[d].banner.ok(); } }
        { std::lock_guard<std::mutex> g(mx_); if (gen == gen_ && d < divas_.size()) { divas_[d].cover = std::move(img); divas_[d].coverReady = divas_[d].cover.ok(); divas_[d].coverTried = true; divas_[d].coverSource = src; } }
        ping();
    }
    // ---- clip thumbnails: cache, else one decoded frame
    for (size_t i = 0; mf && !stop_; ++i) {
        ClipDesc c;
        { std::lock_guard<std::mutex> g(mx_); if (i >= clips_.size()) break; c = clips_[i].desc; }
        uint64_t key = CacheKey(c.path); ImageBGRA img; bool art = false;
        {   // authored thumbnail first: the attachment named by artwork.thumb, then small_cover, then any cover. No video decode.
            std::string named; { std::lock_guard<std::mutex> g(mx_); if (i < clips_.size()) named = clips_[i].info.meta.thumb; }
            Win32Io io; MkvDemux mkv; std::string e;
            if (io.Open(c.path) && mkv.Open(&io, e) && !mkv.Attachments().empty()) {
                std::vector<std::string> names; if (!named.empty()) names.push_back(named);
                for (auto* n : { "small_cover.jpg", "small_cover.png", "cover.jpg", "cover.png" }) names.emplace_back(n);
                for (auto& n : names) {
                    const MkvAttachment* a = mkv.FindAttachment(n.c_str()); std::vector<uint8_t> raw;
                    if (a && mkv.ReadAttachment(*a, raw, 16u << 20) && DecodeImageMem(raw.data(), raw.size(), 420, img)) { art = true; break; }
                }
            }
        }
        if (!art && !CacheLoad(key, img)) {
            std::wstring err; int w = 0, h = 0;
            if (DecodePoster(c, 420, 12, img.px, w, h, err)) { img.w = w; img.h = h; CacheSave(key, img); }
            else { img = ImageBGRA{}; Log("thumbnail %s: %s", WToUtf8(c.path).c_str(), WToUtf8(err).c_str()); }
        }
        { std::lock_guard<std::mutex> g(mx_); if (gen == gen_ && i < clips_.size()) { clips_[i].thumbReady = img.ok(); clips_[i].thumbFailed = !img.ok(); clips_[i].thumbIsArt = art; clips_[i].thumb = std::move(img); } }
        ping();
    }
    if (mf) MFShutdown();
    if (com) CoUninitialize();
}
