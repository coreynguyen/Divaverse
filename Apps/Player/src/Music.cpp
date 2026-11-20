#include "Music.h"
#include "Synth.h"
#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <cmath>
#include <shlobj.h>
#include <shobjidl.h>
#include <propsys.h>

namespace {
std::wstring ListFile() { return AppDataDir() + L"\\music.txt"; }
std::wstring ListDir() { std::wstring d = AppDataDir() + L"\\Playlists"; CreateDirectoryW(d.c_str(), nullptr); return d; }
std::wstring Stem(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/"); std::wstring n = s == std::wstring::npos ? p : p.substr(s + 1);
    size_t dot = n.rfind(L'.'); if (dot != std::wstring::npos) n.resize(dot);
    return n;
}
std::wstring NameOf(const std::wstring& p) { std::wstring n = Stem(p); for (auto& c : n) if (c == L'_') c = L' '; return n; }
std::wstring Ext(const std::wstring& p) { size_t dot = p.rfind(L'.'); std::wstring e = dot == std::wstring::npos ? L"" : p.substr(dot + 1); for (auto& c : e) c = (wchar_t)towlower(c); return e; }
std::wstring FolderName(const std::wstring& p) { std::wstring d = DirOf(p); size_t s = d.find_last_of(L"\\/"); return s == std::wstring::npos ? d : d.substr(s + 1); }
void WriteAll(const std::wstring& file, const std::string& out) { WriteFileAtomic(file, out); }
std::vector<std::string> Lines(const std::string& txt) {
    std::vector<std::string> v; size_t i = 0;
    while (i < txt.size()) { size_t e = txt.find('\n', i); if (e == std::string::npos) e = txt.size(); std::string l = txt.substr(i, e - i); i = e + 1;
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back(); if (!l.empty()) v.push_back(l); }
    return v;
}
// Title / artist / album from the file's own tags, through the Windows property system (ID3, MP4, FLAC, WMA).
const PROPERTYKEY kTitle = { { 0xF29F85E0, 0x4FF9, 0x1068, { 0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9 } }, 2 };
const PROPERTYKEY kArtist = { { 0x56A3372E, 0xCE9C, 0x11D2, { 0x9F, 0x0E, 0x00, 0x60, 0x97, 0xC6, 0x86, 0xF6 } }, 2 };
const PROPERTYKEY kAlbum = { { 0x56A3372E, 0xCE9C, 0x11D2, { 0x9F, 0x0E, 0x00, 0x60, 0x97, 0xC6, 0x86, 0xF6 } }, 4 };
std::wstring PropStr(IPropertyStore* ps, const PROPERTYKEY& k) {
    PROPVARIANT pv; PropVariantInit(&pv); std::wstring s;
    if (SUCCEEDED(ps->GetValue(k, &pv))) {
        if (pv.vt == VT_LPWSTR && pv.pwszVal) s = pv.pwszVal;
        else if (pv.vt == (VT_VECTOR | VT_LPWSTR) && pv.calpwstr.cElems && pv.calpwstr.pElems[0]) s = pv.calpwstr.pElems[0];
        else if (pv.vt == VT_BSTR && pv.bstrVal) s = pv.bstrVal;
    }
    PropVariantClear(&pv);
    for (auto& c : s) if (c == L'\t' || c == L'\r' || c == L'\n') c = L' ';
    return s;
}
}

bool Music::IsAudioFile(const std::wstring& p) { std::wstring e = Ext(p); for (auto* x : { L"mp3", L"m4a", L"aac", L"wav", L"flac", L"wma", L"ogg", L"opus" }) if (e == x) return true; return false; }
bool Music::IsPlaylistFile(const std::wstring& p) { std::wstring e = Ext(p); return e == L"m3u" || e == L"m3u8" || e == L"pls" || e == L"wpl"; }
Music::~Music() { tagStop_ = true; if (tagTh_.joinable()) tagTh_.join(); }
int Music::FindTrack(const std::wstring& path) const { for (size_t i = 0; i < tracks_.size(); ++i) if (!_wcsicmp(tracks_[i].path.c_str(), path.c_str())) return (int)i; return -1; }

int Music::AddFiles(const std::vector<std::wstring>& paths) {
    int added = 0;
    for (auto& p : paths) {
        if (!IsAudioFile(p) || FindTrack(p) >= 0) continue;
        MusicTrack t; t.path = p; t.name = NameOf(p); t.album = FolderName(p);
        tracks_.push_back(std::move(t)); ++added;
        std::lock_guard<std::mutex> g(tagMx_); tagTodo_.push_back(p);
    }
    if (added) { ++gen_; Touch(); if (!tagBusy_.exchange(true)) { if (tagTh_.joinable()) tagTh_.join(); tagTh_ = std::thread([this] { TagWorker(); }); } }
    return added;
}
int Music::AddFolder(const std::wstring& root) {
    std::vector<std::wstring> files, stack{ root };
    while (!stack.empty() && files.size() < 50000) {            // iterative walk: any depth, no recursion
        std::wstring dir = stack.back(); stack.pop_back();
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) stack.push_back(JoinPath(dir, n)); }
            else if (IsAudioFile(n)) files.push_back(JoinPath(dir, n));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(files.begin(), files.end());
    return AddFiles(files);
}
void Music::TagWorker() {
    bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        std::wstring p;
        { std::lock_guard<std::mutex> g(tagMx_); if (tagTodo_.empty() || tagStop_) { tagBusy_ = false; break; } p = tagTodo_.back(); tagTodo_.pop_back(); }
        Tag t; t.path = p; ComPtr<IPropertyStore> ps;
        if (SUCCEEDED(SHGetPropertyStoreFromParsingName(p.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&ps)))) { t.title = PropStr(ps.Get(), kTitle); t.artist = PropStr(ps.Get(), kArtist); t.album = PropStr(ps.Get(), kAlbum); }
        std::lock_guard<std::mutex> g(tagMx_); tagDone_.push_back(std::move(t));
    }
    if (com) CoUninitialize();
}
bool Music::CoverOf(const std::wstring& path, int size, ImageBGRA& out) {
    out = ImageBGRA{};
    for (auto* n : { L"cover.jpg", L"folder.jpg", L"cover.png", L"front.jpg", L"AlbumArtSmall.jpg" }) { std::wstring f = JoinPath(DirOf(path), n); if (FileExists(f) && DecodeImageFile(f, size, out)) return true; }
    ComPtr<IShellItemImageFactory> fac; HBITMAP bm = nullptr;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&fac))) || FAILED(fac->GetImage(SIZE{ size, size }, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &bm)) || !bm) return false;
    BITMAP b{}; GetObjectW(bm, sizeof(b), &b);
    if (b.bmWidth > 0 && b.bmHeight > 0 && b.bmWidth <= 4096 && b.bmHeight <= 4096) {
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = b.bmWidth; bi.bmiHeader.biHeight = -b.bmHeight; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        out.w = b.bmWidth; out.h = b.bmHeight; out.px.resize((size_t)out.w * out.h * 4);
        HDC dc = GetDC(nullptr); int got = GetDIBits(dc, bm, 0, (UINT)out.h, out.px.data(), &bi, DIB_RGB_COLORS); ReleaseDC(nullptr, dc);
        if (got) for (size_t i = 3; i < out.px.size(); i += 4) out.px[i] = 255; else out = ImageBGRA{};
    }
    DeleteObject(bm);
    return out.ok();
}

void Music::Load() {
    tracks_.clear(); lists_.clear(); std::string txt;
    if (ReadFileUtf8(ListFile(), txt)) for (auto& line : Lines(txt)) {
        if (line[0] == '#') continue;
        std::vector<std::string> f; size_t i = 0;
        for (;;) { size_t e = line.find('\t', i); f.push_back(line.substr(i, e == std::string::npos ? e : e - i)); if (e == std::string::npos) break; i = e + 1; }
        MusicTrack t;
        if (f.size() >= 5) {                                    // current format
            t.enabled = f[0].find('-') == std::string::npos; t.fav = f[0].find('f') != std::string::npos; t.tagged = f[0].find('t') != std::string::npos; t.albumFixed = f[0].find('a') != std::string::npos;
            t.weight = std::clamp((float)atof(f[1].c_str()), 0.15f, 2.5f); t.plays = atoi(f[2].c_str()); t.skips = atoi(f[3].c_str()); t.path = Utf8ToW(f[4]);
            if (f.size() > 5) t.name = Utf8ToW(f[5]); if (f.size() > 6) t.artist = Utf8ToW(f[6]); if (f.size() > 7) t.album = Utf8ToW(f[7]);
        } else { std::string p = f[0]; if (p[0] == '-') { t.enabled = false; p.erase(0, 1); } t.path = Utf8ToW(p); }      // 0.3 / 0.4 list: one path per line
        if (IsStream(t.path)) { if (FindTrack(t.path) < 0) { t.tagged = true; tracks_.push_back(t); } continue; }
        if (!FileExists(t.path) || !IsAudioFile(t.path) || FindTrack(t.path) >= 0) continue;
        if (t.name.empty()) t.name = NameOf(t.path); if (t.album.empty()) t.album = FolderName(t.path);
        if (!t.tagged) { std::lock_guard<std::mutex> g(tagMx_); tagTodo_.push_back(t.path); }
        tracks_.push_back(std::move(t));
    }
    { std::lock_guard<std::mutex> g(tagMx_); if (!tagTodo_.empty() && !tagBusy_.exchange(true)) tagTh_ = std::thread([this] { TagWorker(); }); }
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((ListDir() + L"\\*.m3u8").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) { do { ImportPlaylist(JoinPath(ListDir(), fd.cFileName)); } while (FindNextFileW(h, &fd)); FindClose(h); }
    ++gen_; dirty_ = false;
}
void Music::Save() {
    std::string out = "# Divaverse music library: flags(-=off f=favourite t=tags read)\tweight\tplays\tskips\tpath\ttitle\tartist\talbum\r\n"; char b[64];
    for (auto& t : tracks_) {
        std::string fl; if (!t.enabled) fl += '-'; if (t.fav) fl += 'f'; if (t.tagged) fl += 't'; if (t.albumFixed) fl += 'a'; if (fl.empty()) fl = "+";
        snprintf(b, sizeof(b), "\t%.3f\t%d\t%d\t", t.weight, t.plays, t.skips);
        out += fl + b + WToUtf8(t.path) + "\t" + WToUtf8(t.name) + "\t" + WToUtf8(t.artist) + "\t" + WToUtf8(t.album) + "\r\n";
    }
    WriteAll(ListFile(), out);
    for (auto& l : lists_) { std::string m = "#EXTM3U\r\n"; for (auto& p : l.paths) m += WToUtf8(p) + "\r\n"; WriteAll(JoinPath(ListDir(), l.name + L".m3u8"), m); }
    dirty_ = false; lastFlush_ = NowSeconds();
}
void Music::Flush(bool force) { if (dirty_ && (force || NowSeconds() - lastFlush_ > 60)) Save(); }

const std::vector<MusicAlbum>& Music::Albums() {
    if (albumsGen_ == gen_) return albums_;
    albums_.clear(); albumsGen_ = gen_;
    for (int i = 0; i < (int)tracks_.size(); ++i) {
        const MusicTrack& t = tracks_[i]; MusicAlbum* a = nullptr;
        for (auto& x : albums_) if (!_wcsicmp(x.name.c_str(), t.album.c_str())) { a = &x; break; }
        if (!a) { albums_.push_back({ t.album, t.artist, {} }); a = &albums_.back(); }
        if (a->artist.empty()) a->artist = t.artist; else if (!t.artist.empty() && _wcsicmp(a->artist.c_str(), t.artist.c_str())) a->artist = L"Various artists";
        a->tracks.push_back(i);
    }
    for (auto& x : albums_) std::sort(x.tracks.begin(), x.tracks.end(), [&](int p, int q) { return _wcsicmp(tracks_[p].path.c_str(), tracks_[q].path.c_str()) < 0; });     // file order = track order
    std::sort(albums_.begin(), albums_.end(), [](const MusicAlbum& x, const MusicAlbum& y) { return _wcsicmp(x.name.c_str(), y.name.c_str()) < 0; });
    return albums_;
}
int Music::AddAlbumFolder(const std::wstring& dirIn, std::wstring& name) {
    std::wstring dir = dirIn; while (dir.size() > 3 && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    size_t sl = dir.find_last_of(L"\\/"); name = sl == std::wstring::npos ? dir : dir.substr(sl + 1); if (name.empty()) name = dir;
    AddFolder(dir);
    std::wstring pre = dir + L"\\"; int n = 0;
    for (auto& t : tracks_) { if (_wcsnicmp(t.path.c_str(), pre.c_str(), pre.size())) continue;
        t.album = name; t.albumFixed = true; ++n; }                       // discs / sub-folders stay inside the one album, in path order
    if (n) { ++gen_; Touch(); }
    return n;
}
int Music::RemoveAlbum(const std::wstring& name) {
    int n = 0; std::wstring cur = current_ >= 0 && current_ < (int)tracks_.size() ? tracks_[current_].path : L"";
    for (size_t i = tracks_.size(); i-- > 0;) if (!_wcsicmp(tracks_[i].album.c_str(), name.c_str())) { tracks_.erase(tracks_.begin() + i); ++n; }
    if (n) { current_ = FindTrack(cur); recent_.clear(); scopeKind_ = 0; ++gen_; Touch(); }
    return n;
}
void Music::Toggle(int i) { if (i >= 0 && i < (int)tracks_.size()) { tracks_[i].enabled = !tracks_[i].enabled; Touch(); } }
void Music::ToggleFav(int i) { if (i >= 0 && i < (int)tracks_.size()) { tracks_[i].fav = !tracks_[i].fav; Touch(); } }
void Music::Remove(int i) {
    if (i < 0 || i >= (int)tracks_.size()) return;
    bool wasCurrent = mode_ == 1 && current_ == i;
    tracks_.erase(tracks_.begin() + i); recent_.clear(); ++gen_; Touch();
    if (current_ > i) --current_;
    if (wasCurrent) { current_ = -1; if (enabled_) Next(false); }
}

void Music::SetScope(int kind, int index) { scopeKind_ = kind; scopeIndex_ = index; recent_.clear(); }
std::wstring Music::ScopeName() {
    if (scopeKind_ == 1) { auto& a = Albums(); if (scopeIndex_ >= 0 && scopeIndex_ < (int)a.size()) return a[scopeIndex_].name; }
    if (scopeKind_ == 2 && scopeIndex_ >= 0 && scopeIndex_ < (int)lists_.size()) return lists_[scopeIndex_].name;
    return L"All songs";
}
std::vector<int> Music::Pool() const {
    std::vector<int> ok;
    if (scopeKind_ == 1 && albumsGen_ == gen_ && scopeIndex_ >= 0 && scopeIndex_ < (int)albums_.size()) { for (int i : albums_[scopeIndex_].tracks) if (tracks_[i].enabled) ok.push_back(i); }
    else if (scopeKind_ == 2 && scopeIndex_ >= 0 && scopeIndex_ < (int)lists_.size()) { for (auto& p : lists_[scopeIndex_].paths) { int i = FindTrack(p); if (i >= 0 && tracks_[i].enabled) ok.push_back(i); } }
    if (ok.empty()) for (int i = 0; i < (int)tracks_.size(); ++i) if (tracks_[i].enabled) ok.push_back(i);
    return ok;
}
// Shuffle is a weighted draw: every track can come up, liked ones more often, skipped ones less, and nothing that
// played in the last few picks. In-order play ignores weights.
int Music::Pick(int from) {
    if (scopeKind_ == 1) Albums();
    std::vector<int> ok = Pool();
    if (ok.empty()) return -1;
    if (shuffle_ && ok.size() > 1) {
        size_t keep = std::min<size_t>(ok.size() / 2, 24); while (recent_.size() > keep) recent_.erase(recent_.begin());
        double total = 0; std::vector<double> w(ok.size());
        for (size_t k = 0; k < ok.size(); ++k) { int i = ok[k]; bool rec = i == from || std::find(recent_.begin(), recent_.end(), i) != recent_.end(); w[k] = rec ? 0 : (tracks_[i].fav ? 3.0 : tracks_[i].weight); total += w[k]; }
        if (total <= 0) { int r; do r = ok[(size_t)rand() % ok.size()]; while (r == from); return r; }
        double x = (rand() / (RAND_MAX + 1.0)) * total;
        for (size_t k = 0; k < ok.size(); ++k) { x -= w[k]; if (x < 0) return ok[k]; }
        return ok.back();
    }
    for (size_t k = 0; k < ok.size(); ++k) if (ok[k] == from) return ok[(k + 1) % ok.size()];
    return ok.front();
}

int Music::NewPlaylist() {
    for (int n = 1; n < 1000; ++n) { std::wstring nm = L"Playlist " + std::to_wstring(n); bool used = false; for (auto& l : lists_) used = used || !_wcsicmp(l.name.c_str(), nm.c_str()); if (!used) { lists_.push_back({ nm, {} }); Touch(); return (int)lists_.size() - 1; } }
    return -1;
}
void Music::DeletePlaylist(int i) {
    if (i < 0 || i >= (int)lists_.size()) return;
    DeleteFileW(JoinPath(ListDir(), lists_[i].name + L".m3u8").c_str()); lists_.erase(lists_.begin() + i);
    if (scopeKind_ == 2) SetScope(0, 0);
}
void Music::AddToPlaylist(int l, int t) {
    if (l < 0 || l >= (int)lists_.size() || t < 0 || t >= (int)tracks_.size()) return;
    for (auto& p : lists_[l].paths) if (!_wcsicmp(p.c_str(), tracks_[t].path.c_str())) return;
    lists_[l].paths.push_back(tracks_[t].path); Touch();
}
void Music::RemoveFromPlaylist(int l, int k) { if (l >= 0 && l < (int)lists_.size() && k >= 0 && k < (int)lists_[l].paths.size()) { lists_[l].paths.erase(lists_[l].paths.begin() + k); Touch(); } }
int Music::ImportPlaylist(const std::wstring& file) {
    std::string txt; if (!ReadFileUtf8(file, txt)) return -1;
    if (txt.size() >= 3 && (unsigned char)txt[0] == 0xEF) txt.erase(0, 3);
    std::wstring e = Ext(file), base = DirOf(file); std::vector<std::wstring> paths;
    auto add = [&](std::string s) {
        if (s.rfind("file:///", 0) == 0) s.erase(0, 8);
        std::wstring p = Utf8ToW(s); for (auto& c : p) if (c == L'/') c = L'\\';
        if (p.size() < 2) return;
        if (!(p[1] == L':' || p[0] == L'\\')) p = JoinPath(base, p);                // relative entries are relative to the playlist
        if (IsAudioFile(p) && FileExists(p)) paths.push_back(p);
    };
    if (e == L"wpl") { size_t i = 0; while ((i = txt.find("src=\"", i)) != std::string::npos) { size_t q = txt.find('"', i + 5); if (q == std::string::npos) break; std::string s = txt.substr(i + 5, q - i - 5); size_t a; while ((a = s.find("&amp;")) != std::string::npos) s.replace(a, 5, "&"); add(s); i = q; } }
    else for (auto& l : Lines(txt)) { if (l[0] == '#' || l[0] == '[') continue; if (e == L"pls") { if (_strnicmp(l.c_str(), "File", 4)) continue; size_t eq = l.find('='); if (eq != std::string::npos) add(l.substr(eq + 1)); } else add(l); }
    std::wstring nm = Stem(file);
    for (auto& l : lists_) if (!_wcsicmp(l.name.c_str(), nm.c_str())) { if (_wcsicmp(DirOf(file).c_str(), ListDir().c_str())) nm += L" (imported)"; break; }
    bool own = !_wcsicmp(DirOf(file).c_str(), ListDir().c_str());
    AddFiles(paths);                                            // songs in a playlist join the library
    lists_.push_back({ nm, paths }); if (!own) Touch();
    return (int)lists_.size() - 1;
}

bool Music::AddStream(const std::wstring& urlIn) {
    std::wstring url = urlIn; while (!url.empty() && iswspace(url.back())) url.pop_back(); while (!url.empty() && iswspace(url.front())) url.erase(url.begin());
    if (!IsStream(url) || url.size() > 1024 || url.find_first_of(L" \t\r\n\"<>") != std::wstring::npos || FindTrack(url) >= 0) return false;
    MusicTrack t; t.path = url; t.tagged = true; t.artist = L"Internet radio";
    size_t h = url.find(L"//") + 2, e = url.find_first_of(L"/:?", h); t.name = url.substr(h, e == std::wstring::npos ? e : e - h); t.album = L"Streams";
    tracks_.push_back(t); ++gen_; Touch(); return true;
}
bool Music::Start() {
    bool fade = xfade_ > 0.05f && mode_ == 1 && out_ && !out_->Done() && !out_->Failed() && !suspended_;
    if (fade) { old_ = std::move(out_); old_->Fade(0, xfade_); oldUntil_ = NowSeconds() + xfade_ + 0.3; } else old_.reset();
    out_.reset(); lastErr_.clear();
    if (!enabled_) return false;
    std::unique_ptr<AudioSource> src;
    if (mode_ == 1) {
        if (current_ < 0 || current_ >= (int)tracks_.size()) current_ = Pick(-1);
        if (current_ < 0) { mode_ = 0; } else src = MakeFileSource(tracks_[current_].path);
    }
    if (mode_ == 0) src = MakeSynthSource(station_);
    auto o = std::make_unique<AudioOut>(); std::wstring err;
    o->SetNormalize(normalize_ && mode_ == 1);                  // the generated stations are already level
    if (fade) { o->PresetFade(0); o->Fade(1, xfade_); }
    if (!o->Open(std::move(src), volume_, false, err)) { lastErr_ = err; Log("music: %s", WToUtf8(err).c_str()); return false; }
    Log("music: %s, %d Hz %d ch", WToUtf8(NowPlaying()).c_str(), o->SampleRate(), o->Channels());
    out_ = std::move(o);
    if (mode_ == 1 && current_ >= 0) recent_.push_back(current_);
    if (suspended_) out_->Pause(true);
    return true;
}
std::wstring Music::NowPlaying() const {
    if (mode_ == 1) { if (current_ < 0 || current_ >= (int)tracks_.size()) return L""; const MusicTrack& t = tracks_[current_]; return t.artist.empty() ? t.name : t.name + L"  -  " + t.artist; }
    int n; const SynthStation* s = SynthStations(&n); return s[((station_ % n) + n) % n].name;
}
void Music::Select(int mode, int index) {
    if (mode == 1 && mode_ == 1 && index == current_ && index >= 0 && index < (int)tracks_.size()) { tracks_[index].weight = std::min(2.5f, tracks_[index].weight * 1.25f); Touch(); }   // asked for again
    mode_ = mode;
    if (mode == 0) station_ = index; else current_ = index;
    if (enabled_) Start();
}
void Music::Next(bool byUser) {
    if (mode_ == 0) { int n; SynthStations(&n); station_ = (station_ + 1) % n; if (enabled_) Start(); return; }
    int from = current_;
    if (from >= 0 && from < (int)tracks_.size()) {              // learn from how the track ended
        MusicTrack& t = tracks_[from]; double heard = out_ ? out_->Seconds() : 0;
        if (byUser && heard < 45) { ++t.skips; t.weight = std::max(0.15f, t.weight * 0.80f); }          // skipped early
        else if (!byUser) { ++t.plays; t.weight = std::min(2.5f, t.weight * 1.06f); }                    // played to the end
        Touch();
    }
    for (size_t tries = 0; tries <= tracks_.size(); ++tries) {            // skip unplayable files, never spin
        int i = Pick(from); if (i < 0) break;
        current_ = i;
        if (!enabled_ || Start()) return;
        from = i;
    }
    out_.reset();
}
void Music::Prev() {
    if (mode_ == 0) { int n; SynthStations(&n); station_ = (station_ + n - 1) % n; if (enabled_) Start(); return; }
    if (out_ && out_->Seconds() > 4 && current_ >= 0 && current_ < (int)tracks_.size()) {                // restart = replay: she likes it
        tracks_[current_].weight = std::min(2.5f, tracks_[current_].weight * 1.25f); Touch(); if (enabled_) Start(); return;
    }
    std::vector<int> ok = Pool(); if (ok.empty()) return;
    if (recent_.size() >= 2) { recent_.pop_back(); current_ = recent_.back(); recent_.pop_back(); }
    else { size_t k = 0; for (size_t j = 0; j < ok.size(); ++j) if (ok[j] == current_) k = j; current_ = ok[(k + ok.size() - 1) % ok.size()]; }
    if (enabled_) Start();
}
void Music::SetEnabled(bool on) { enabled_ = on; if (!on) { out_.reset(); old_.reset(); } else if (!out_) Start(); }
void Music::SetSuspended(bool on) { suspended_ = on; if (out_) out_->Pause(on); if (on) old_.reset(); }
void Music::SetVolume(float v) { volume_ = std::clamp(v, 0.f, 1.f); if (out_) out_->SetVolume(volume_); if (old_) old_->SetVolume(volume_); }
void Music::SetNormalize(bool on) { normalize_ = on; if (out_) out_->SetNormalize(on && mode_ == 1); }
void Music::Poll() {
    {   // tags read by the worker are applied here, on the UI thread
        std::vector<Tag> done; { std::lock_guard<std::mutex> g(tagMx_); done.swap(tagDone_); }
        for (auto& t : done) { int i = FindTrack(t.path); if (i < 0) continue; MusicTrack& m = tracks_[i]; m.tagged = true;
            if (!t.title.empty()) m.name = t.title; if (!t.artist.empty()) m.artist = t.artist; if (!t.album.empty() && !m.albumFixed) m.album = t.album; }
        if (!done.empty()) { ++gen_; Touch(); }
    }
    Flush();
    if (old_ && NowSeconds() > oldUntil_) old_.reset();
    if (!enabled_ || suspended_ || !out_) return;
    if (mode_ == 1 && xfade_ > 0.05f && !old_ && out_->Duration() > xfade_ * 3 && out_->Seconds() > out_->Duration() - xfade_ - 0.5) { Next(false); return; }      // start the next song under the tail of this one
    if (out_->Failed()) { Start(); return; }                               // device changed / removed: reopen on the new default
    if (out_->Done() && mode_ == 1) Next(false);
}
