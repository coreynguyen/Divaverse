// Divaverse - desktop performer app. Overlay window + playback core + tray. See App.h.
#include "App.h"
#include "Sfx.h"
#include "Gfx.h"
#include "ProcIcon.h"
#include "Synth.h"
#include "resource.h"
#include <shellapi.h>
#include <windowsx.h>
#include <cmath>
#include <algorithm>

App* g_app = nullptr;

namespace {
constexpr UINT kTrayId = 1;
constexpr wchar_t kClass[] = L"Divaverse.Overlay";
constexpr wchar_t kMutex[] = L"Local\\Divaverse.SingleInstance";
constexpr ULONG_PTR kCopyOpen = 0x44564F50; // 'DVOP'
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
enum HotKey { HK_EDIT = 1, HK_PAUSE, HK_HIDE, HK_NEXT, HK_QUIT, HK_PREV, HK_MPLAY, HK_MNEXT, HK_MPREV, HK_MSTOP };
constexpr double kStagePxPerMetre = 420.0;      // at stage scale 1.0 and 96 dpi a 1.75 m performer is 735 px tall
constexpr double kAssumedCanvasMetres = 1.75;   // uncalibrated clips: the canvas is taken to span this height

std::wstring ExePath() { wchar_t b[MAX_PATH * 2]; GetModuleFileNameW(nullptr, b, MAX_PATH * 2); return b; }

HICON MakeIcon(IconState st, int size) {
    std::vector<uint8_t> px; ProcIconRGBA(size, st, px);
    BITMAPV5HEADER bh{}; bh.bV5Size = sizeof(bh); bh.bV5Width = size; bh.bV5Height = -size; bh.bV5Planes = 1;
    bh.bV5BitCount = 32; bh.bV5Compression = BI_BITFIELDS;
    bh.bV5RedMask = 0x00FF0000; bh.bV5GreenMask = 0x0000FF00; bh.bV5BlueMask = 0x000000FF; bh.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr; HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bh, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color) return nullptr;
    auto* o = (uint8_t*)bits;
    for (size_t i = 0; i < (size_t)size * size; ++i) { o[4 * i] = px[4 * i + 2]; o[4 * i + 1] = px[4 * i + 1]; o[4 * i + 2] = px[4 * i]; o[4 * i + 3] = px[4 * i + 3]; }
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{ TRUE, 0, 0, mask, color };
    HICON h = CreateIconIndirect(&ii);
    DeleteObject(color); DeleteObject(mask);
    return h;
}
void WriteFileAll(const std::wstring& p, const void* d, size_t n) {
    HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, d, (DWORD)n, &w, nullptr); CloseHandle(f); }
}
}

// ------------------------------------------------------------------------------------ settings
void Settings::Load() {
    std::wstring f = AppDataDir() + L"\\player.ini"; wchar_t b[2048];
    auto gi = [&](const wchar_t* k, int d) { return (int)GetPrivateProfileIntW(L"Player", k, d, f.c_str()); };
    auto gf = [&](const wchar_t* k, float d) { GetPrivateProfileStringW(L"Player", k, L"", b, 2048, f.c_str()); return b[0] ? (float)_wtof(b) : d; };
    auto gs = [&](const wchar_t* k) { GetPrivateProfileStringW(L"Player", k, L"", b, 2048, f.c_str()); return std::wstring(b); };
    x = gi(L"anchor_x", INT_MIN); bottom = gi(L"ground_y", INT_MIN);
    scale = std::clamp(gf(L"stage_scale", scale), 0.25f, 3.0f);
    wrLook = gi(L"wetrash_look", 1); gravity = gi(L"gravity", 1) != 0; seasonal = gi(L"seasonal", 1) != 0; editor = gi(L"editor", 0) != 0;
    clickThrough = gi(L"click_through", 0) != 0; lock = gi(L"lock", 0) != 0; loopMode = std::clamp(gi(L"loop_mode", 0), 0, 2);
    cardMode = std::clamp(gi(L"card_mode", 1), 0, 1); zoom = std::clamp(gf(L"zoom", 1.f), 0.6f, 1.8f); lastStall = gs(L"last_stall"); opacity = std::clamp(gf(L"opacity", opacity), 0.2f, 1.0f);
    mirror = gi(L"mirror", 0) != 0; topmost = gi(L"topmost", 1) != 0; shadow = gi(L"shadow", 1) != 0; autoHide = gi(L"autohide", 1) != 0;
    theme = std::clamp(gi(L"theme", 2), 0, 3); fx = gi(L"fx", 1) != 0;
    sfx = gi(L"sfx", 1) != 0; sfxVol = std::clamp(gf(L"sfx_volume", 0.5f), 0.f, 1.f);
    music = gi(L"music", 0) != 0; musicShuffle = gi(L"music_shuffle", 0) != 0; musicNormalize = gi(L"music_normalize", 1) != 0; musicVolume = std::clamp(gf(L"music_volume", musicVolume), 0.f, 1.f);
    musicMode = std::clamp(gi(L"music_mode", 0), 0, 1); station = std::max(0, gi(L"station", 0));
    decodeMode = gi(L"gpu_decode", 0) ? 1 : 0; hwBad = gi(L"hw_bad", 0) != 0; dcompBad = gi(L"dcomp_bad", 0) != 0;
    last = gs(L"last");
    folders.clear();
    auto list = [&](const wchar_t* name, std::vector<std::wstring>& out) {
        out.clear(); wchar_t k[32]; swprintf(k, 32, L"%lss", name);
        for (int i = 0, n = gi(k, 0); i < n && i < 64; ++i) { swprintf(k, 32, L"%ls%d", name, i); std::wstring v = gs(k); if (!v.empty()) out.push_back(v); }
    };
    list(L"folder", folders); list(L"fav", favs); list(L"recent", recent); list(L"queue", queue); list(L"queue", queue);
}
void Settings::Save() const {
    if (noSave) return;
    std::wstring f = AppDataDir() + L"\\player.ini"; wchar_t b[64];
    auto pi = [&](const wchar_t* k, int v) { swprintf(b, 64, L"%d", v); WritePrivateProfileStringW(L"Player", k, b, f.c_str()); };
    auto pf = [&](const wchar_t* k, float v) { swprintf(b, 64, L"%.4f", v); WritePrivateProfileStringW(L"Player", k, b, f.c_str()); };
    pi(L"anchor_x", x); pi(L"ground_y", bottom); pf(L"stage_scale", scale); pf(L"opacity", opacity);
    pi(L"wetrash_look", wrLook); pi(L"gravity", gravity); pi(L"seasonal", seasonal); pi(L"editor", editor);
    pi(L"click_through", clickThrough); pi(L"lock", lock); pi(L"loop_mode", loopMode); pi(L"card_mode", cardMode); pf(L"zoom", zoom);
    WritePrivateProfileStringW(L"Player", L"last_stall", lastStall.c_str(), f.c_str());
    pi(L"mirror", mirror); pi(L"topmost", topmost); pi(L"shadow", shadow); pi(L"autohide", autoHide); pi(L"theme", theme); pi(L"fx", fx); pi(L"sfx", sfx); pf(L"sfx_volume", sfxVol);
    pi(L"music", music); pi(L"music_shuffle", musicShuffle); pi(L"music_normalize", musicNormalize); pf(L"music_volume", musicVolume); pi(L"music_mode", musicMode); pi(L"station", station);
    pi(L"gpu_decode", decodeMode == 1); pi(L"hw_bad", hwBad); pi(L"dcomp_bad", dcompBad);
    WritePrivateProfileStringW(L"Player", L"last", last.c_str(), f.c_str());
    auto list = [&](const wchar_t* name, const std::vector<std::wstring>& v) {
        wchar_t k[32]; swprintf(k, 32, L"%lss", name); pi(k, (int)v.size());
        for (size_t i = 0; i < v.size(); ++i) { swprintf(k, 32, L"%ls%d", name, (int)i); WritePrivateProfileStringW(L"Player", k, v[i].c_str(), f.c_str()); }
    };
    list(L"folder", folders); list(L"fav", favs); list(L"recent", recent);
}

bool App::Autostart() {
    HKEY hk; if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &hk) != ERROR_SUCCESS) return false;
    bool on = RegQueryValueExW(hk, L"Divaverse", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(hk); return on;
}
void App::SetAutostart(bool on) {
    HKEY hk; if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hk) != ERROR_SUCCESS) return;
    if (on) { std::wstring v = L"\"" + ExePath() + L"\" --tray"; RegSetValueExW(hk, L"Divaverse", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t))); }
    else RegDeleteValueW(hk, L"Divaverse");
    RegCloseKey(hk);
}

// ------------------------------------------------------------------------------------- overlay
bool App::CreateOverlay() {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WndProcThunk; wc.hInstance = hi_; wc.lpszClassName = kClass;
        wc.hCursor = nullptr; wc.hIcon = bigIcon_; wc.hIconSm = icons_[0];
        if (!RegisterClassExW(&wc)) return false;
        reg = true;
    }
    DWORD ex = WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    if (mode_ == PresentMode::DComp) ex |= WS_EX_NOREDIRECTIONBITMAP;
    if (set.topmost) ex |= WS_EX_TOPMOST;
    hwnd_ = CreateWindowExW(ex, kClass, L"Divaverse performer", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, hi_, this);
    if (!hwnd_) return false;
    if (mode_ == PresentMode::DComp) SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);   // required for a layered window to be shown
    return true;
}

bool App::SwitchToLayered() {
    Log("switching presentation to layered window");
    bool hadTray = trayAdded_;
    VideoSource::Retire(src_); r_.Shutdown();
    if (hadTray) TrayRemove();
    recreating_ = true; DestroyWindow(hwnd_); recreating_ = false;     // must not post WM_QUIT
    hwnd_ = nullptr; mode_ = PresentMode::Layered;
    std::wstring err;
    if (!CreateOverlay() || !r_.Init(hwnd_, mode_, !cliSw_, err)) { Log("layered init failed: %s", WToUtf8(err).c_str()); return false; }
    if (hadTray) TrayAdd();
    interactive_ = false; mediaKeys_ = false;
    RegisterHotkeys(); UpdateMediaKeys();
    return true;
}

bool App::InitRenderer() {
    std::wstring err;
    if (r_.Init(hwnd_, mode_, !cliSw_, err)) return true;
    Log("renderer init failed: %s", WToUtf8(err).c_str());
    if (mode_ == PresentMode::DComp) { r_.Shutdown(); return SwitchToLayered(); }
    if (selftest_.empty()) MessageBoxW(nullptr, (L"Divaverse could not start its graphics device.\n\n" + err).c_str(), L"Divaverse", MB_ICONERROR);
    return false;
}

// GPU decoding is opt-in: on some drivers the D3D11 decoder stops delivering frames, and the CPU path costs little for
// clips of this size. If the opt-in stalls, the watchdog turns it off again.
bool App::WantHw() const { return !cliSw_ && r_.VideoCapable() && set.decodeMode == 1 && !set.hwBad; }

bool App::OpenPath(const std::wstring& pathIn) {
    wchar_t full[MAX_PATH * 2]; std::wstring path = pathIn;
    if (DWORD n = GetFullPathNameW(pathIn.c_str(), MAX_PATH * 2, full, nullptr); n && n < MAX_PATH * 2) path = full;
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
    Show s; std::wstring err, first = path; int idx = 0;
    auto fromLibrary = [&](const std::wstring& clipPath) {
        std::lock_guard<std::mutex> g(lib.Mutex());
        int ci = lib.FindClip(clipPath); if (ci < 0) return false;
        s = lib.MakeShow(lib.Clips()[ci].diva);
        for (size_t i = 0; i < s.clips.size(); ++i) if (!_wcsicmp(s.clips[i].path.c_str(), clipPath.c_str())) idx = (int)i;
        return !s.clips.empty();
    };
    if (!fromLibrary(path)) {
        Show raw;
        if (!LoadShowOrClip(path, raw, err)) { Notify(L"Cannot open", err); Log("open failed: %s", WToUtf8(err).c_str()); return false; }
        if (raw.clips.empty() || !fromLibrary(raw.clips[0].path)) {          // not in the library: describe it from the files themselves
            s = raw; s.clips.clear(); std::string why;
            for (auto& c : raw.clips) { MediaInfo mi; ClipDesc d = c; if (ProbeMedia(c.path, mi, &d)) { s.clips.push_back(d); if (s.divaName.empty()) s.divaName = Utf8ToW(mi.meta.divaName); } else if (why.empty() && !mi.errors.empty()) why = mi.errors[0]; }
            if (s.clips.empty()) { Notify(L"Cannot open", Utf8ToW(why.empty() ? "no playable clip" : why)); return false; }
            if (!s.divaName.empty()) s.title = s.divaName;
            if (s.title.empty()) s.title = s.clips[0].title;
        }
    }
    show = std::move(s);
    if (!OpenClip(idx)) return false;
    bool known; { std::lock_guard<std::mutex> g(lib.Mutex()); known = lib.FindClip(stagePath_) >= 0; }
    if (!known && selftest_.empty()) RescanLibrary();    // set.last now points here, so its folder joins the library
    return true;
}

bool App::OpenDiva(int diva, int clip) {
    Show s; { std::lock_guard<std::mutex> g(lib.Mutex()); s = lib.MakeShow(diva); }
    if (s.clips.empty()) return false;
    show = std::move(s);
    return OpenClip(clip);
}

bool App::OpenClip(int idx) { return StartClip(idx, WantHw()); }

bool App::StartClip(int idx, bool hw) {
    if (show.clips.empty()) return false;
    int n = (int)show.clips.size(); idx = ((idx % n) + n) % n;
    bool hadPicture = src_ != nullptr;
    VideoSource::Retire(src_);
    auto s = std::make_unique<VideoSource>(); std::wstring err;
    s->SetLowLatency(lowLatency_);
    bool ok = s->Open(show.clips[idx], r_.Device(), r_.Context(), r_.Lock(), hw, err);
    if (!ok && hw) {                                 // once more on the CPU
        Log("open (GPU decode) failed: %s -> CPU decode", WToUtf8(err).c_str());
        s = std::make_unique<VideoSource>(); err.clear(); s->SetLowLatency(lowLatency_); hw = false;
        ok = s->Open(show.clips[idx], r_.Device(), r_.Context(), r_.Lock(), false, err);
    }
    if (!ok) { Notify(L"Cannot play clip", err); UpdateOverlayVisibility(); UiInvalidate(); return false; }
    clipIdx = idx; src_ = std::move(s); clipHw_ = src_->HwActive(); src_->Start();
    stagePath_ = show.clips[idx].path; set.last = stagePath_;
    auto& rc = set.recent;
    for (size_t i = 0; i < rc.size(); ++i) if (!_wcsicmp(rc[i].c_str(), stagePath_.c_str())) { rc.erase(rc.begin() + i); break; }
    rc.insert(rc.begin(), stagePath_); if (rc.size() > 12) rc.resize(12);
    set.Save();
    ProbeMedia(stagePath_, stageInfo, nullptr);
    clockValid_ = false; shown_ = -1; if (!hadPicture) fadeIn_ = 0; dirty_ = true;
    openedAt_ = lastProgress_ = NowSeconds();
    UpdateSuspend();
    ApplyGeometry();
    UpdateOverlayVisibility();
    TrayUpdate(); UiInvalidate();
    return true;
}

void App::Unstage() {
    if (edit_) SetEdit(false);
    VideoSource::Retire(src_); stagePath_.clear(); show = Show{}; stageInfo = MediaInfo{}; set.last.clear(); set.Save();
    hidden_ = false; panic_ = false; paused_ = false; UpdateSuspend();
    UpdateOverlayVisibility(); TrayUpdate(); UiInvalidate();
}

void App::UpdateOverlayVisibility() {
    if (!hwnd_) return;
    bool vis = src_ && !hidden_ && !panic_ && !autoHidden_;
    ShowWindow(hwnd_, vis ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void App::EditPhysical(double ppm, double h, float au, float gv) {
    if (!src_) return;
    ClipDesc& c = show.clips[clipIdx];
    c.ppm = std::clamp(ppm, 20.0, 20000.0); c.actorHeightM = std::clamp(h, 0.0, 3.0); c.anchorU = std::clamp(au, 0.f, 1.f); c.groundY = std::clamp(gv, 0.f, 1.f);
    ApplyGeometry(); UiInvalidate();
}
// Field-level override beside the media: only "physical" is touched, everything else already in the sidecar is kept.
bool App::SaveSidecarPhysical(std::wstring& err) {
    if (!src_) { err = L"nothing on stage"; return false; }
    const ClipDesc& c = show.clips[clipIdx]; std::wstring side = SidecarPath(c.path); JVal root; root.t = JVal::Obj; std::string txt;
    if (ReadFileUtf8(side, txt)) { JVal j; std::string pe; if (JsonParse(txt, j, &pe) && j.t == JVal::Obj) root = j; }
    auto num = [](double v) { JVal x; x.t = JVal::Num; x.n = v; return x; };
    JVal ph; ph.t = JVal::Obj; JVal ga; ga.t = JVal::Arr; ga.a = { num(std::round(c.anchorU * 10000) / 10000), num(std::round(c.groundY * 10000) / 10000) };
    ph.o = { { "actor_height_m", num(std::round(c.actorHeightM * 1000) / 1000) }, { "pixels_per_meter", num(std::round(c.ppm * 100) / 100) }, { "ground_anchor", ga } };
    bool found = false; for (auto& kv : root.o) if (kv.first == "physical") { kv.second = ph; found = true; }
    if (!found) root.o.emplace_back("physical", ph);
    bool hasVer = false; for (auto& kv : root.o) hasVer = hasVer || kv.first == "schema_version"; if (!hasVer) root.o.insert(root.o.begin(), { "schema_version", num(1) });
    std::string out = JsonWrite(root) + "\r\n";
    HANDLE f = CreateFileW(side.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) { err = L"cannot write " + side; return false; }
    DWORD w; WriteFile(f, out.data(), (DWORD)out.size(), &w, nullptr); CloseHandle(f);
    ProbeMedia(c.path, stageInfo, nullptr); RescanLibrary();
    return true;
}
void App::Enqueue(const std::wstring& p, bool front) {
    if (set.queue.size() >= 60) return;
    if (front) set.queue.insert(set.queue.begin(), p); else set.queue.push_back(p);
    set.Save(); UiInvalidate();
}
bool App::PlayQueued() {
    while (!set.queue.empty()) {                         // skips entries whose file has gone
        std::wstring p = set.queue.front(); set.queue.erase(set.queue.begin()); set.Save();
        if (FileExists(p) && OpenPath(p)) return true;
    }
    return false;
}
void App::MoveToNextMonitor() {
    struct Ctx { std::vector<MONITORINFO> m; } cx;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR h, HDC, LPRECT, LPARAM lp) -> BOOL { MONITORINFO mi{ sizeof(mi) }; if (GetMonitorInfoW(h, &mi)) ((Ctx*)lp)->m.push_back(mi); return TRUE; }, (LPARAM)&cx);
    if (cx.m.empty()) return;
    POINT ap{ set.x == INT_MIN ? 0 : set.x, set.bottom == INT_MIN ? 0 : set.bottom - 4 }; size_t cur = 0;
    for (size_t i = 0; i < cx.m.size(); ++i) if (PtInRect(&cx.m[i].rcMonitor, ap)) cur = i;
    const RECT& w = cx.m[(cur + 1) % cx.m.size()].rcWork;
    set.x = w.left + (w.right - w.left) * 4 / 5; set.bottom = w.bottom; falling_ = false;      // same spot on the next display: right side, on its taskbar edge
    ApplyGeometry(); set.Save(); UiInvalidate();
}
bool App::Calibrated() const { const ClipDesc* c = Cur(); return c && c->ppm > 0; }
double App::StageHeightCm() const { const ClipDesc* c = Cur(); return c && c->actorHeightM > 0 ? c->actorHeightM * 100.0 * set.scale : 0; }
int64_t App::Frames() const { const ClipDesc* c = Cur(); int64_t n = c ? c->frames : 0; if (src_ && src_->ClipFrames() > 0) n = src_->ClipFrames(); return n; }
int64_t App::Frame() const { int64_t n = Frames(); return n > 0 && shown_ >= 0 ? shown_ % n : std::max<int64_t>(0, shown_); }
float App::Progress() const { int64_t n = Frames(); return n > 1 ? (float)Frame() / (float)(n - 1) : 0.f; }

// Size of the performer window. One metre of the clip's canvas is always the same number of screen pixels, so
// performers authored at different resolutions keep their relative heights. Nothing is estimated from the picture:
// a clip without physical metadata falls back to "the canvas is 1.75 m tall" and is reported as uncalibrated.
void App::StageSize(int& w, int& h) const {
    const ClipDesc* c = Cur();
    double cw = c && c->canvasW > 0 ? c->canvasW : (src_ ? src_->PerfW() : 480), ch = c && c->canvasH > 0 ? c->canvasH : (src_ ? src_->PerfH() : 992);
    double ppm = c && c->ppm > 0 ? c->ppm : ch / kAssumedCanvasMetres;
    double k = kStagePxPerMetre / ppm * set.scale * (ThemeDpi(hwnd_) / 96.0);
    w = std::clamp((int)std::lround(cw * k), 16, 8192); h = std::clamp((int)std::lround(ch * k), 16, 8192);
}

// The window is placed so that the clip's ground anchor lands on (set.x, set.bottom). Resizing therefore grows the
// performer upward and outward from her feet, and a walking clip keeps its floor line on the taskbar edge.
void App::ApplyGeometry(bool keepAnchor) {
    const ClipDesc* c = Cur();
    float au = c ? c->anchorU : 0.5f, gy = c ? c->groundY : 0.96f; if (set.mirror) au = 1.f - au;
    int w, h; StageSize(w, h);
    if (set.x == INT_MIN || set.bottom == INT_MIN || !keepAnchor) {
        RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);   // floor line = top edge of the taskbar, right side
        set.x = wa.right - 48 - (int)std::lround((1.f - au) * w); set.bottom = wa.bottom;
    }
    POINT pos{ set.x - (int)std::lround(au * w), set.bottom - (int)std::lround(gy * h) };
    SetWindowPos(hwnd_, nullptr, pos.x, pos.y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    r_.Resize(w, h); r_.SetWindowPos(pos);
    EnsureVisible();
    dirty_ = true;
}

void App::EnsureVisible() {
    RECT rc; GetWindowRect(hwnd_, &rc);
    if (MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) return;
    Log("overlay off-screen -> resetting position");
    set.x = INT_MIN; ApplyGeometry(false);
}

void App::ApplyStyles() {
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST);
    if (!edit_) ex |= WS_EX_NOACTIVATE | (interactive_ ? 0 : WS_EX_TRANSPARENT);   // clicks pass through unless the pointer is on her
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd_, set.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void App::SetEdit(bool on) {
    if (on && !src_) return;
    if (on && hidden_) SetHidden(false);
    edit_ = on; interactive_ = false; pressed_ = dragging_ = false; ApplyStyles();
    if (on) { SetForegroundWindow(hwnd_); SetFocus(hwnd_); } else set.Save();
    dirty_ = true; TrayUpdate(); UiInvalidate();
}
void App::UpdateSuspend() {
    bool s = paused_ || hidden_ || panic_ || autoHidden_ || !src_;
    if (s != suspended_) {
        double now = NowSeconds();
        if (s) suspendMedia_ = now - clockOrigin_; else { clockOrigin_ = now - suspendMedia_; lastProgress_ = now; }
        suspended_ = s; dirty_ = true;
    }
    music.SetSuspended(musicPaused_ || gameOpen_ || panic_ || autoHidden_ || (paused_ && src_));   // music keeps playing while she is merely hidden
}
void App::SetPaused(bool on) { paused_ = on; UpdateSuspend(); TrayUpdate(); UiInvalidate(); }
void App::SetHidden(bool on) {
    hidden_ = on; if (!on) panic_ = false;
    if (on && edit_) SetEdit(false);
    UpdateOverlayVisibility(); UpdateSuspend(); TrayUpdate(); UiInvalidate();
}
// The "someone walked in" switch (Ctrl+Alt+H, always registered): performer, launcher and menus disappear at once and
// the music stops. The same key brings the performer back; the launcher stays closed until it is opened again.
void App::Panic(bool on) {
    panic_ = on;
    if (on) { FlyoutClose(); WheelClose(); if (edit_) SetEdit(false); if (UiVisible()) ShowWindow(UiWindow(), SW_HIDE); }
    else hidden_ = false;
    UpdateOverlayVisibility(); UpdateSuspend(); TrayUpdate(); UiInvalidate();
}
void App::SetMusicPaused(bool on) { musicPaused_ = on; UpdateSuspend(); UiInvalidate(); }
void App::SetLoopMode(int m) { set.loopMode = std::clamp(m, 0, 2); set.Save(); UiInvalidate(); }
void App::SetClickThrough(bool on) { set.clickThrough = on; set.Save(); UiInvalidate(); }
void App::SetLock(bool on) { set.lock = on; set.Save(); UiInvalidate(); }
bool App::IsFav(const std::wstring& k) const { for (auto& f : set.favs) if (!_wcsicmp(f.c_str(), k.c_str())) return true; return false; }
void App::ToggleFav(const std::wstring& k) {
    for (size_t i = 0; i < set.favs.size(); ++i) if (!_wcsicmp(set.favs[i].c_str(), k.c_str())) { set.favs.erase(set.favs.begin() + i); set.Save(); UiInvalidate(); return; }
    if (set.favs.size() < 64) set.favs.push_back(k);
    set.Save(); UiInvalidate();
}
void App::SeekFrame(int64_t f) {
    int64_t n = Frames(); if (!src_ || n <= 0) return;
    src_->Seek(std::clamp<int64_t>(f, 0, n - 1));
    clockValid_ = false; shown_ = -1; lastProgress_ = NowSeconds(); dirty_ = true;
}
void App::StepFrame(int dir) {
    if (!src_) return;
    if (!paused_) SetPaused(true);
    if (dir > 0 && clockValid_) { suspendMedia_ += 1.0 / src_->Fps(); dirty_ = true; }
    else if (dir < 0) SeekFrame(Frame() - 1);
}
void App::SetScale(float s, bool save) {
    set.scale = std::clamp(s, 0.25f, 3.0f);
    ApplyGeometry(); if (save) set.Save();
}
void App::SetOpacity(float o, bool save) { set.opacity = std::clamp(o, 0.2f, 1.f); dirty_ = true; if (save) set.Save(); }
void App::SetMirror(bool on) { set.mirror = on; ApplyGeometry(); set.Save(); UiInvalidate(); }
void App::SetShadow(bool on) { set.shadow = on; dirty_ = true; set.Save(); UiInvalidate(); }
void App::SetTopmost(bool on) { set.topmost = on; ApplyStyles(); set.Save(); UiInvalidate(); }
void App::SetAutoHide(bool on) { set.autoHide = on; if (!on && autoHidden_) { autoHidden_ = false; UpdateOverlayVisibility(); UpdateSuspend(); } set.Save(); UiInvalidate(); }
void App::ResetPosition() { set.x = INT_MIN; ApplyGeometry(false); set.Save(); }
void App::SetTheme(int id) { set.theme = std::clamp(id, 0, 3); ThemeSet((ThemeId)set.theme); set.Save(); UiThemeChanged(); }
void App::SetDecodeMode(int m) {
    set.decodeMode = m == 1 ? 1 : 0; if (m == 1) set.hwBad = false;           // choosing GPU explicitly clears the learned fallback
    set.Save(); if (src_) OpenClip(clipIdx); UiInvalidate();
}

std::vector<std::wstring> App::LibraryFolders() const {
    std::vector<std::wstring> v; std::wstring exeDir = DirOf(ExePath());
    auto add = [&](const std::wstring& p) { for (auto& q : v) if (!_wcsicmp(q.c_str(), p.c_str())) return; v.push_back(p); };
    add(JoinPath(exeDir, L"Divas")); add(AppDataDir() + L"\\Divas");
    add(JoinPath(exeDir, L"Shows")); add(AppDataDir() + L"\\Shows");         // pre-0.4 name, still read
    for (auto& f : set.folders) add(f);
    if (!set.last.empty()) {                             // the staged clip's own folder (or its legacy show folder)
        std::wstring d = DirOf(set.last), up = DirOf(d);
        add(FileExists(JoinPath(up, L"manifest.json")) ? up : d);
    }
    return v;
}
void App::RescanLibrary() { lib.Rescan(LibraryFolders(), UiWindow(), WM_APP_POSTER); UiInvalidate(); }
void App::AddLibraryFolder(const std::wstring& dir) {
    for (auto& f : set.folders) if (!_wcsicmp(f.c_str(), dir.c_str())) return;
    set.folders.push_back(dir); set.Save(); RescanLibrary();
}
void App::RemoveLibraryFolder(int i) { if (i >= 0 && i < (int)set.folders.size()) { set.folders.erase(set.folders.begin() + i); set.Save(); RescanLibrary(); } }

// Clicks reach the performer only where she is solid. A DirectComposition window is hit-tested by its rectangle, so
// the pointer is sampled against the alpha of the frame on screen and WS_EX_TRANSPARENT is switched accordingly:
// she can walk across the work area and everything around her stays clickable.
void App::PollPointer() {
    if (!hwnd_ || edit_) return;
    bool solid = false;
    if (src_ && !hidden_ && !panic_ && !autoHidden_ && (!set.clickThrough || pressed_)) {
        POINT p; RECT rc;
        if (pressed_) solid = true;
        else if (GetCursorPos(&p) && GetWindowRect(hwnd_, &rc) && PtInRect(&rc, p)) {
            float u = (p.x - rc.left + 0.5f) / (float)std::max<LONG>(1, rc.right - rc.left), v = (p.y - rc.top + 0.5f) / (float)std::max<LONG>(1, rc.bottom - rc.top);
            solid = src_->HitTest(set.mirror ? 1.f - u : u, v);
        }
    }
    if (solid == interactive_) return;
    interactive_ = solid;
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, solid ? ex & ~WS_EX_TRANSPARENT : ex | WS_EX_TRANSPARENT);
}

void App::RegisterHotkeys() {
    struct { int id; UINT vk; } hk[] = { { HK_EDIT, 'E' }, { HK_PAUSE, 'P' }, { HK_HIDE, 'H' }, { HK_NEXT, 'N' }, { HK_QUIT, 'Q' }, { HK_PREV, 'B' } };
    for (auto& k : hk) if (!RegisterHotKey(hwnd_, k.id, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, k.vk)) Log("hotkey Ctrl+Alt+%c unavailable", (char)k.vk);
}
// Keyboard media keys drive the music while it is switched on; while it is off they are left to other players.
void App::UpdateMediaKeys() {
    bool want = music.Enabled() && selftest_.empty() && hwnd_;
    if (want == mediaKeys_) return;
    mediaKeys_ = want;
    struct { int id; UINT vk; } mk[] = { { HK_MPLAY, VK_MEDIA_PLAY_PAUSE }, { HK_MNEXT, VK_MEDIA_NEXT_TRACK }, { HK_MPREV, VK_MEDIA_PREV_TRACK }, { HK_MSTOP, VK_MEDIA_STOP } };
    for (auto& k : mk) { if (want) { if (!RegisterHotKey(hwnd_, k.id, 0, k.vk)) Log("media key 0x%02X unavailable (held by another app)", k.vk); } else UnregisterHotKey(hwnd_, k.id); }
}

void App::PollFullscreen(double now) {
    QUERY_USER_NOTIFICATION_STATE q{}; bool busy = false;
    if (set.autoHide && SUCCEEDED(SHQueryUserNotificationState(&q)))
        busy = q == QUNS_BUSY || q == QUNS_RUNNING_D3D_FULL_SCREEN || q == QUNS_PRESENTATION_MODE;
    // Debounce: screenshot overlays and other momentary full-screen surfaces must not make the performer vanish.
    if (busy && !autoHidden_) { if (busySince_ == 0) busySince_ = now; if (now - busySince_ < 3.0) return; }
    if (!busy) busySince_ = 0;
    if (busy != autoHidden_) {
        autoHidden_ = busy; Log("fullscreen app %s -> %s", busy ? "detected" : "gone", busy ? "auto-hide" : "restore");
        UpdateOverlayVisibility(); UpdateSuspend();
    }
}

// Playback watchdog. If frames stop arriving while the clock is running, record where the decoder is and walk the
// fallback chain: GPU decode -> CPU decode -> layered presentation. What it learns is remembered for this machine.
void App::Watchdog(double now) {
    if (!src_ || suspended_) return;
    double since = now - lastProgress_, limit = shown_ < 0 ? 4.0 : 2.0;
    if (since < limit) return;
    VideoSource::DecodeInfo di = src_->Info();
    char b[400];
    snprintf(b, sizeof(b), "playback stalled %.1fs at frame %lld: decode=%s stage=%s fed=%llu emitted=%llu gpu_frames=%d cpu_frames=%d ts_anomalies=%llu present=%s",
             since, (long long)shown_, clipHw_ ? "gpu" : "cpu", di.stage, (unsigned long long)di.fed, (unsigned long long)di.emitted, di.gpuFrames, di.cpuFrames,
             (unsigned long long)di.tsAnomalies, mode_ == PresentMode::DComp ? "dcomp" : "layered");
    Log("%s", b); events_.push_back(b);
    lastProgress_ = now;
    set.lastStall = Utf8ToW(b); set.Save();
    if (clipHw_) {
        set.hwBad = true; set.decodeMode = 0; set.Save(); events_.push_back("-> switched to CPU decoding");
        Log("watchdog: GPU decode produced no frames -> CPU decode (remembered)");
        StartClip(clipIdx, false);
    } else if (mode_ == PresentMode::DComp && selftest_.empty()) {
        set.dcompBad = true; set.Save(); events_.push_back("-> switched to layered presentation");
        if (SwitchToLayered()) StartClip(clipIdx, false);
    } else if (stallLevel_++ == 0) {
        Notify(L"Playback problem", L"The performer stopped animating. Open Divaverse, go to Settings, Diagnostics and use Copy report.");
        lastProgress_ = now + 60;
    }
    UiInvalidate();
}

void App::Tick() {
    double now = NowSeconds(), dt = std::min(0.1, now - lastTick_); lastTick_ = now;
    if (!selftest_.empty() && (!src_ || now - selftestStart_ > selftestSecs_ + 20.0)) {   // nothing to test / stuck
        Log(src_ ? "selftest timeout" : "selftest: no playable clip"); SelftestFinish(); return;
    }
    if (now - lastFsPoll_ > 1.0) { lastFsPoll_ = now; PollFullscreen(now); }
    if (now - lastMusicPoll_ > 1.0) { lastMusicPoll_ = now; music.Poll(); UpdateMediaKeys(); user.Flush(); }
    if (now - lastPtrPoll_ > 0.03) { lastPtrPoll_ = now; PollPointer(); }
    if (qaDrop_ == 1 && shown_ > 30) {                   // QA: lift her 300 px and let go
        qaDrop_ = 2; RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        floorY_ = wa.bottom; fallY_ = set.bottom - 300; fallV_ = 0; fallT_ = now; falling_ = true;
    }
    if (falling_) {                                      // released above (or below) the floor: accelerate back to it, one small bounce
        double dt = std::min(0.05, now - fallT_); fallT_ = now;
        double dir = fallY_ < floorY_ ? 1.0 : -1.0, g = 5200.0 * (ThemeDpi(hwnd_) / 96.0);
        fallV_ += dir * g * dt; fallY_ += fallV_ * dt;
        if ((dir > 0 && fallY_ >= floorY_) || (dir < 0 && fallY_ <= floorY_)) {
            if (dir > 0 && fallV_ > 900) { fallY_ = floorY_ - 1; fallV_ = -fallV_ * 0.22; SfxPlay(SFX_DROP); }
            else { fallY_ = floorY_; falling_ = false; }
        }
        set.bottom = (int)std::lround(fallY_); ApplyGeometry();
        if (!falling_) { set.Save(); UiInvalidate(); }
    }

    float goal = edit_ ? 1.f : 0.f;
    if (editAnim_ != goal) { editAnim_ += (goal - editAnim_) * (float)std::min(1.0, dt * 12.0); if (std::fabs(goal - editAnim_) < 0.01f) editAnim_ = goal; dirty_ = true; }
    if (fadeIn_ < 1.f) { fadeIn_ = std::min(1.f, fadeIn_ + (float)dt * 4.0f); dirty_ = true; }

    if (src_ && src_->Failed()) {
        Log("decoder failed (%s)", clipHw_ ? "gpu" : "cpu"); events_.push_back(clipHw_ ? "GPU decoder error -> CPU" : "CPU decoder error");
        if (clipHw_) { set.hwBad = true; set.Save(); StartClip(clipIdx, false); }
        else { VideoSource::Retire(src_); UpdateOverlayVisibility(); Notify(L"Playback stopped", L"The clip could not be decoded."); UiInvalidate(); }
        return;
    }
    if (!src_ || hidden_ || panic_ || autoHidden_) return;
    Watchdog(now);
    if (!src_) return;

    const FrameSlot* slot = nullptr; bool newFrame = false;
    double fps = src_->Fps();
    int64_t target = clockValid_ ? (int64_t)std::floor(MediaTime(now) * fps + 1e-6) : -1, earliest = -1;
    if (clockValid_ && !suspended_ && target >= show.clips[clipIdx].frames && show.clips[clipIdx].frames > 0) {     // end of the clip
        int n = (int)show.clips.size(); const ClipDesc& cd = show.clips[clipIdx];
        if (n > 1 && set.loopMode == 1) { OpenClip(clipIdx + 1); return; }
        user.Nudge(cd.path, 1.05f);                                    // played to the end
        if (PlayQueued()) return;                                       // queued clips come before any loop mode
        if (n > 1 && set.loopMode == 2) {                               // weighted draw among the other clips
            double total = 0; for (int i = 0; i < n; ++i) if (i != clipIdx) total += user.Weight(show.clips[i].path);
            double x = (rand() / (RAND_MAX + 1.0)) * total; int pick = (clipIdx + 1) % n;
            for (int i = 0; i < n; ++i) if (i != clipIdx) { x -= user.Weight(show.clips[i].path); if (x < 0) { pick = i; break; } }
            OpenClip(pick); return;
        }
        if (cd.loopOnce && target < cd.frames + 2) { SetPaused(true); return; }                                      // authored as a one-shot: hold the last frame
    }
    slot = src_->Acquire(clockValid_ ? target : -1, &earliest);
    if (!clockValid_ && earliest >= 0) {             // anchor the clock on the first decoded frame
        clockOrigin_ = now - earliest / fps; clockValid_ = true;
        if (suspended_) suspendMedia_ = earliest / fps;
        slot = src_->Acquire(earliest, nullptr);
    } else if (clockValid_ && !suspended_ && earliest >= 0 && target - earliest > fps * 0.5) {
        Log("decoder behind by %lld frames -> re-anchor", (long long)(target - earliest));
        clockOrigin_ = now - earliest / fps;          // stall recovery: don't fast-forward
        slot = src_->Acquire(earliest, nullptr);
    }
    if (slot && slot->frame != shown_) { shown_ = slot->frame; newFrame = true; lastProgress_ = now; }
    if (qaSeek_ >= 0 && shown_ > 40 && clockValid_) { Log("qa: seek to frame %d", qaSeek_); qaSeekWait_ = qaSeek_; SeekFrame(qaSeek_); qaSeek_ = -1; return; }
    if (qaSeekWait_ >= 0 && newFrame && clockValid_) { qaSeekLanded_ = (int)shown_; Log("qa: seek landed on frame %lld (wanted %d)", (long long)shown_, qaSeekWait_); qaSeekWait_ = -1; }
    if (!newFrame && !dirty_ && !edit_) return;
    if (!slot && !edit_) return;                     // switching clips: keep the last picture until the new one has a frame

    ShaderCB cb{};
    if (slot) {
        src_->Rects(cb.rgbRect, cb.alphaRect);
        const Planes& pc = slot->p[0]; const Planes& pa = src_->Dual() ? slot->p[1] : slot->p[0];
        cb.texel[0] = 1.f / std::max(1, pc.w); cb.texel[1] = 1.f / std::max(1, pc.h);
        cb.texel[2] = 1.f / std::max(1, pa.w); cb.texel[3] = 1.f / std::max(1, pa.h);
        cb.hasAlpha = src_->HasAlpha() ? 1.f : 0.f; cb.fullRange = src_->FullRange() ? 1.f : 0.f;
        const ClipDesc& c = show.clips[clipIdx];
        cb.alphaLo = c.alphaLo; cb.alphaHi = c.alphaHi; cb.groundY = c.groundY; cb.anchorU = c.anchorU;
        cb.alphaFull = src_->AlphaFullRange() ? 1.f : 0.f;
        int64_t n = std::max<int64_t>(1, src_->ClipFrames());
        cb.progress = src_->ClipFrames() > 0 ? (float)((slot->frame % n) + 1) / (float)n : 0.f;
    } else { cb.alphaHi = 1.f; cb.groundY = 0.96f; cb.hasAlpha = 1.f; cb.anchorU = 0.5f; }
    cb.viewPx[0] = (float)r_.W(); cb.viewPx[1] = (float)r_.H();
    cb.time = (float)(now - t0_); cb.mirror = set.mirror ? 1.f : 0.f;
    cb.shadow = set.shadow ? 1.f : 0.f; cb.editMode = editAnim_;
    cb.opacity = set.opacity * fadeIn_; cb.paused = paused_ ? 1.f : 0.f;

    if (!r_.Render(slot, src_->Dual(), cb)) {
        Log("device lost -> rebuilding renderer"); events_.push_back("graphics device lost -> rebuilt");
        VideoSource::Retire(src_); r_.Shutdown();
        std::wstring err;
        if (!r_.Init(hwnd_, mode_, !cliSw_, err)) { Notify(L"Graphics device lost", err); Quit(); return; }
        OpenClip(clipIdx);
        return;
    }
    dirty_ = false; ++presented_;
    if (!selftest_.empty()) {
        if (!selftestRequested_ && now - selftestStart_ > selftestSecs_ && shown_ > 24) { r_.RequestCapture(selftest_); selftestRequested_ = true; dirty_ = true; }
        else if (r_.CaptureDone()) { SelftestFinish(); return; }
    }
    if (now - lastStats_ > 10.0) {
        lastStats_ = now; VideoSource::DecodeInfo di = src_->Info();
        Log("stats: frame %lld presents %llu dropped %llu unpaired %llu decode=%s stage=%s fed=%llu emitted=%llu gpu=%d cpu=%d anomalies=%llu loops=%llu",
            (long long)shown_, (unsigned long long)presented_, (unsigned long long)src_->Dropped(), (unsigned long long)src_->Unpaired(), clipHw_ ? "gpu" : "cpu",
            di.stage, (unsigned long long)di.fed, (unsigned long long)di.emitted, di.gpuFrames, di.cpuFrames, (unsigned long long)di.tsAnomalies, (unsigned long long)di.loops);
    }
}

std::wstring App::DiagText() {
    std::wstring s; wchar_t b[640];                  // built wide: no dependence on the compiler's narrow execution charset
    OSVERSIONINFOW vi{ sizeof(vi) }; using PFN = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (auto rtl = (PFN)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")) rtl(&vi);
    swprintf(b, 640, L"Divaverse 0.6 | Windows %lu.%lu.%lu\r\n", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber); s += b;
    swprintf(b, 640, L"Graphics: %ls | %ls\r\n", Utf8ToW(r_.AdapterName()).c_str(), mode_ == PresentMode::DComp ? L"DirectComposition" : L"layered window"); s += b;
    if (r_.PresentFailures()) { swprintf(b, 640, L"Layered presentation errors: %u\r\n", r_.PresentFailures()); s += b; }
    swprintf(b, 640, L"Video decoding: %ls%ls%ls\r\n", set.decodeMode == 1 ? L"GPU (opt-in)" : L"CPU", set.hwBad ? L" (GPU decoding stalled on this PC)" : L"", set.dcompBad ? L" (DirectComposition failed here)" : L""); s += b;
    if (src_) {
        VideoSource::DecodeInfo di = src_->Info(); const ClipDesc& c = show.clips[clipIdx];
        swprintf(b, 640, L"On stage: %ls | %ls | %dx%d @ %.3f fps | %ls\r\n", c.id.c_str(), src_->Dual() ? L"dual track" : src_->HasAlpha() ? L"packed" : L"opaque", src_->PerfW(), src_->PerfH(), src_->Fps(),
                 c.ppm > 0 ? L"calibrated" : L"uncalibrated scale"); s += b;
        swprintf(b, 640, L"Decoder: %ls | stage %ls | fed %llu | emitted %llu | GPU frames %d | CPU frames %d | loops %llu\r\n",
                 clipHw_ ? L"GPU (D3D11)" : L"CPU", Utf8ToW(di.stage).c_str(), (unsigned long long)di.fed, (unsigned long long)di.emitted, di.gpuFrames, di.cpuFrames, (unsigned long long)di.loops); s += b;
        swprintf(b, 640, L"Frames: shown %lld | presents %llu | dropped %llu | unpaired %llu | timestamp fixes %llu | track offset %.1f ms\r\n", (long long)shown_,
                 (unsigned long long)presented_, (unsigned long long)src_->Dropped(), (unsigned long long)src_->Unpaired(), (unsigned long long)di.tsAnomalies, src_->TrackOffsetMs()); s += b;
    } else s += L"On stage: nobody\r\n";
    s += L"Music: " + (music.Enabled() ? music.NowPlaying() : std::wstring(L"off")) + (music.LastError().empty() ? L"" : L" | " + music.LastError()) + L"\r\n";
    if (!set.lastStall.empty()) s += L"Last stall on this PC: " + set.lastStall + L"\r\n";
    if (!events_.empty()) { s += L"Events:\r\n"; for (auto& e : events_) s += L"  " + Utf8ToW(e) + L"\r\n"; }
    return s;
}

void App::SelftestFinish() {
    char buf[2048];
    double cov = r_.CaptureCoverage();
    bool pass = r_.CaptureDone() && src_ && shown_ > 24 && cov > 0.02 && (edit_ || cov < 0.98);
    VideoSource::DecodeInfo di = src_ ? src_->Info() : VideoSource::DecodeInfo{};
    const char* rendererS = mode_ == PresentMode::DComp ? "dcomp" : "layered";
    const char* decodeS = src_ ? (di.gpuFrames > 0 && di.cpuFrames == 0 ? "d3d11va" : "software") : "none";
    uint64_t droppedN = src_ ? src_->Dropped() : 0, unpairedN = src_ ? src_->Unpaired() : 0;
    std::string expectFail;
    for (auto& [k, v] : expect_) {
        std::string k8 = WToUtf8(k), v8 = WToUtf8(v), got; bool ok = true;
        if (k8 == "renderer") { got = rendererS; ok = got == v8; }
        else if (k8 == "decode") { got = decodeS; ok = got == v8; }
        else if (k8 == "layout") { got = src_ ? (src_->Dual() ? "dual" : "packed") : "none"; ok = got == v8; }
        else if (k8 == "loops") { got = std::to_string(di.loops); ok = di.loops >= strtoull(v8.c_str(), nullptr, 10); }
        else if (k8 == "dropped") { got = std::to_string(droppedN); ok = droppedN <= strtoull(v8.c_str(), nullptr, 10); }
        else if (k8 == "unpaired") { got = std::to_string(unpairedN); ok = unpairedN <= strtoull(v8.c_str(), nullptr, 10); }
        else if (k8 == "floor") { RECT rc; GetWindowRect(hwnd_, &rc); const ClipDesc* c = Cur();
            got = std::to_string((int)(qaDrop_ == 2 && !falling_ && set.bottom == floorY_ && c && std::abs(rc.top + (int)std::lround(c->groundY * (rc.bottom - rc.top)) - floorY_) <= 1)); ok = got == v8; }
        else if (k8 == "seek") { got = std::to_string(qaSeekLanded_); ok = got == v8; }
        else if (k8 == "width") { got = std::to_string(r_.W()); ok = got == v8; }
        else if (k8 == "hit") { got = std::string(src_ && src_->HitTest(0.5f, 0.5f) ? "1" : "0") + (src_ && src_->HitTest(0.03f, 0.03f) ? "1" : "0"); ok = got == v8; }   // centre / corner
        else if (k8 == "anomalies") { got = std::to_string(di.tsAnomalies); ok = di.tsAnomalies <= strtoull(v8.c_str(), nullptr, 10); }
        else { got = "unknown key"; ok = false; }
        if (!ok) { if (!expectFail.empty()) expectFail += "; "; expectFail += k8 + "=" + v8 + " (got " + got + ")"; }
    }
    if (!expectFail.empty()) pass = false;
    snprintf(buf, sizeof(buf),
        "{\n  \"pass\": %s,\n  \"renderer\": \"%s\",\n  \"adapter\": \"%s\",\n  \"decode\": \"%s\",\n  \"gpu_frames\": %d,\n  \"cpu_frames\": %d,\n"
        "  \"stream_changes\": %d,\n  \"loops\": %llu,\n  \"ts_anomalies\": %llu,\n  \"layout\": \"%s\",\n  \"window\": [%d, %d],\n  \"frames_shown_upto\": %lld,\n"
        "  \"presents\": %llu,\n  \"dropped\": %llu,\n  \"unpaired\": %llu,\n  \"alpha_coverage\": %.4f,\n  \"seconds\": %.2f,\n  \"watchdog_events\": %d,\n  \"expect_failed\": \"%s\"\n}\n",
        pass ? "true" : "false", rendererS, r_.AdapterName().c_str(), decodeS, di.gpuFrames, di.cpuFrames, di.streamChanges, (unsigned long long)di.loops,
        (unsigned long long)di.tsAnomalies, src_ ? (src_->Dual() ? "dual" : "packed") : "none", r_.W(), r_.H(), (long long)shown_, (unsigned long long)presented_,
        (unsigned long long)droppedN, (unsigned long long)unpairedN, cov, NowSeconds() - selftestStart_, (int)events_.size(), expectFail.c_str());
    WriteFileAll(selftest_ + L".json", buf, strlen(buf));
    Log("selftest: %s", buf);
    selftest_.clear();
    PostQuitMessage(pass ? 0 : 3);
}

// ---------------------------------------------------------------------------------------- tray
void App::TrayAdd() {
    NOTIFYICONDATAW n{ sizeof(n) }; n.hWnd = hwnd_; n.uID = kTrayId;
    n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP; n.uCallbackMessage = WM_APP_TRAY;
    n.hIcon = icons_[0]; wcscpy_s(n.szTip, L"Divaverse");
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
    n.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &n);
    TrayUpdate();
}
void App::TrayUpdate() {
    if (!trayAdded_) return;
    NOTIFYICONDATAW n{ sizeof(n) }; n.hWnd = hwnd_; n.uID = kTrayId; n.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    n.hIcon = icons_[edit_ ? 2 : paused_ ? 1 : 0];
    std::wstring tip = L"Divaverse";
    if (src_) tip += L"\n" + show.title + (paused_ ? L" (paused)" : Hidden() ? L" (hidden)" : L"");
    wcsncpy_s(n.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}
void App::TrayRemove() {
    if (!trayAdded_) return;
    NOTIFYICONDATAW n{ sizeof(n) }; n.hWnd = hwnd_; n.uID = kTrayId; Shell_NotifyIconW(NIM_DELETE, &n); trayAdded_ = false;
}
void App::Notify(const std::wstring& title, const std::wstring& text) {
    Log("notify: %s: %s", WToUtf8(title).c_str(), WToUtf8(text).c_str());
    if (!selftest_.empty()) return;
    if (UiVisible()) { UiToast(title + L": " + text); return; }
    if (!trayAdded_) return;
    NOTIFYICONDATAW n{ sizeof(n) }; n.hWnd = hwnd_; n.uID = kTrayId; n.uFlags = NIF_INFO;
    wcsncpy_s(n.szInfoTitle, title.c_str(), _TRUNCATE); wcsncpy_s(n.szInfo, text.c_str(), _TRUNCATE);
    n.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON; n.hBalloonIcon = bigIcon_;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void App::ShowTrayMenu(POINT pt) {
    std::vector<FlyItem> m; bool hid = Hidden();
    m.push_back({ ID_OPEN_UI, L"Open Divaverse", I_WINDOW });
    m.push_back({ 0, L"", 0, false, true, true });
    m.push_back({ ID_PLAYPAUSE, paused_ ? L"Play" : L"Pause", paused_ ? I_PLAY : I_PAUSE, false, src_ != nullptr, false, L"Ctrl+Alt+P" });
    m.push_back({ ID_NEXT, L"Next clip", I_NEXT, false, src_ && show.clips.size() > 1, false, L"Ctrl+Alt+N" });
    m.push_back({ ID_PANIC, hid ? L"Show performer" : L"Hide everything now", hid ? I_EYE : I_EYEOFF, false, true, false, L"Ctrl+Alt+H" });
    m.push_back({ ID_EDIT, L"Move and resize", I_MOVE, edit_, src_ != nullptr, false, L"Ctrl+Alt+E" });
    m.push_back({ ID_LOCK, L"Lock position", I_LOCK, set.lock, src_ != nullptr });
    m.push_back({ ID_CLICKTHROUGH, L"Ignore mouse (click-through)", I_CURSOR, set.clickThrough, src_ != nullptr });
    m.push_back({ ID_UNSTAGE, L"Remove from desktop", I_CLOSE, false, src_ != nullptr });
    m.push_back({ 0, L"", 0, false, true, true });
    m.push_back({ ID_MUSIC_TOGGLE, L"Music", I_MUSIC, music.Enabled() });
    m.push_back({ ID_MUSIC_NEXT, music.Mode() == 0 ? L"Next station" : L"Next track", I_SHUFFLE, false, music.Enabled() });
    m.push_back({ 0, L"", 0, false, true, true });
    m.push_back({ ID_EXIT, L"Exit", I_POWER, false, true, false, L"Ctrl+Alt+Q" });
    FlyoutShow(hwnd_, pt, std::move(m));
}

// Clicking the performer opens a compact wheel with the few things one wants right there. Everything else lives in
// the tray menu and the On Stage page.
void App::PerformerMenu(POINT pt) {
    if (!src_) return;
    bool multi = show.clips.size() > 1;
    std::vector<WheelItem> m;
    m.push_back({ ID_PLAYPAUSE, paused_ ? L"Play" : L"Pause", paused_ ? I_PLAY : I_PAUSE });
    m.push_back({ ID_NEXT, L"Next clip", I_NEXT, multi });
    m.push_back({ ID_PANIC, L"Hide", I_EYEOFF });
    m.push_back({ ID_OPEN_UI, L"Divaverse", I_WINDOW });
    m.push_back({ ID_EDIT, edit_ ? L"Done" : L"Move / size", I_MOVE });
    m.push_back({ ID_PREV, L"Previous", I_PREV, multi });
    WheelShow(hwnd_, pt, show.title, std::move(m));
}

void App::OnCommand(UINT id) {
    switch (id) {
    case ID_OPEN_UI: if (panic_) Panic(false); UiShow(); break;
    case ID_PLAYPAUSE: if (src_) SetPaused(!paused_); break;
    case ID_NEXT: if (src_ && PlayQueued()) { SfxPlay(SFX_PAGE); break; }
        if (src_) { if (NowSeconds() - openedAt_ < 20) user.Nudge(show.clips[clipIdx].path, 0.85f); OpenClip(clipIdx + 1); SfxPlay(SFX_PAGE); } break;   // skipped early: less likely on shuffle
    case ID_PREV: if (src_) { OpenClip(clipIdx - 1); SfxPlay(SFX_BACK); } break;
    case ID_SHOWHIDE: if (src_) SetHidden(!Hidden()); break;
    case ID_PANIC: if (Hidden()) SetHidden(false); else Panic(true); break;
    case ID_EDIT: SetEdit(!edit_); break;
    case ID_LOCK: SetLock(!set.lock); break;
    case ID_CLICKTHROUGH: SetClickThrough(!set.clickThrough); break;
    case ID_UNSTAGE: Unstage(); break;
    case ID_MUSIC_TOGGLE: set.music = !music.Enabled(); musicPaused_ = false; music.SetEnabled(set.music); UpdateSuspend(); UpdateMediaKeys(); set.Save(); UiInvalidate(); break;
    case ID_MUSIC_NEXT: music.Next(); set.station = music.Station(); set.Save(); UiInvalidate(); break;
    case ID_MUSIC_PREV: music.Prev(); set.station = music.Station(); set.Save(); UiInvalidate(); break;
    case ID_MUSIC_PAUSE: if (music.Enabled()) SetMusicPaused(!musicPaused_); break;
    case ID_EXIT: Quit(); break;
    }
}
void App::Quit() { quitting_ = true; if (hwnd_) DestroyWindow(hwnd_); else PostQuitMessage(0); }

LRESULT CALLBACK App::WndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) { auto* a = (App*)((CREATESTRUCTW*)l)->lpCreateParams; a->hwnd_ = h; SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)a); }
    auto* a = (App*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return a ? a->WndProc(m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT App::WndProc(UINT m, WPARAM w, LPARAM l) {
    if (m == wmTaskbarCreated_ && m) { trayAdded_ = false; TrayAdd(); return 0; }
    switch (m) {
    case WM_COMMAND: OnCommand(LOWORD(w)); return 0;
    case WM_SETTINGCHANGE: if (set.theme == 0) { ThemeSet(ThemeId::System); UiThemeChanged(); } break;
    case WM_NCHITTEST: return edit_ ? HTCAPTION : interactive_ ? HTCLIENT : HTTRANSPARENT;
    case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr, edit_ || dragging_ ? IDC_SIZEALL : IDC_HAND)); return TRUE;
    case WM_MOUSEACTIVATE: return edit_ ? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_LBUTTONDOWN: if (!edit_) { pressed_ = true; dragging_ = false; GetCursorPos(&pressPt_); pressAnchor_ = { set.x, set.bottom }; SetCapture(hwnd_); } return 0;
    case WM_MOUSEMOVE: if (pressed_ && !set.lock) {
        POINT p; GetCursorPos(&p); int dx = p.x - pressPt_.x, dy = p.y - pressPt_.y;
        if (!dragging_ && (std::abs(dx) > 4 || std::abs(dy) > 4)) { dragging_ = true; falling_ = false; }
        if (dragging_) { set.x = pressAnchor_.x + dx; set.bottom = pressAnchor_.y + dy; ApplyGeometry(); }      // she can be lifted anywhere
        }
        return 0;
    case WM_LBUTTONUP: if (pressed_) {
        bool wasDrag = dragging_; pressed_ = dragging_ = false; if (GetCapture() == hwnd_) ReleaseCapture();
        if (wasDrag) {
            // Let go: gravity returns her to the floor (the top edge of the taskbar on the monitor she is over).
            // Free placement off the floor is what "Move and resize" is for.
            POINT ap{ set.x, std::min<LONG>(set.bottom, pressAnchor_.y) }; MONITORINFO mi{ sizeof(mi) };
            GetMonitorInfoW(MonitorFromPoint(ap, MONITOR_DEFAULTTONEAREST), &mi);
            set.x = std::clamp<int>(set.x, mi.rcWork.left + 8, mi.rcWork.right - 8);
            floorY_ = mi.rcWork.bottom; fallY_ = set.bottom; fallV_ = 0; fallT_ = NowSeconds(); falling_ = set.gravity && set.bottom != floorY_;   // gravity off: she stays where she is put
            if (!falling_) { set.Save(); UiInvalidate(); }
        } else { POINT p; GetCursorPos(&p); PerformerMenu(p); } }
        return 0;
    case WM_CAPTURECHANGED: pressed_ = dragging_ = false; return 0;
    case WM_MOVE: {
        RECT rc; GetWindowRect(hwnd_, &rc);
        r_.SetWindowPos({ rc.left, rc.top });
        if (edit_) {
            const ClipDesc* c = Cur(); float au = c ? c->anchorU : 0.5f, gy = c ? c->groundY : 0.96f; if (set.mirror) au = 1.f - au;
            set.x = rc.left + (int)std::lround(au * (rc.right - rc.left)); set.bottom = rc.top + (int)std::lround(gy * (rc.bottom - rc.top));
        }
        return 0; }
    case WM_EXITSIZEMOVE: set.Save(); return 0;
    case WM_NCLBUTTONDBLCLK: SetEdit(false); return 0;
    case WM_SYSCOMMAND: if ((w & 0xFFF0) == SC_MAXIMIZE || (w & 0xFFF0) == SC_SIZE) return 0; break;
    case WM_NCRBUTTONUP: case WM_RBUTTONUP: { POINT p; GetCursorPos(&p); PerformerMenu(p); return 0; }
    case WM_MOUSEWHEEL: if (edit_ || ((GET_KEYSTATE_WPARAM(w) & MK_CONTROL) && !set.lock)) { SetScale(set.scale * std::pow(1.08f, GET_WHEEL_DELTA_WPARAM(w) / (float)WHEEL_DELTA), false); UiInvalidate(); return 0; } break;
    case WM_KEYDOWN: if (edit_) {
        int step = (GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 1;
        switch (w) {
        case VK_ESCAPE: case VK_RETURN: SetEdit(false); break;
        case VK_SPACE: SetPaused(!paused_); break;
        case 'M': SetMirror(!set.mirror); break;
        case VK_LEFT: set.x -= step; ApplyGeometry(); break;
        case VK_RIGHT: set.x += step; ApplyGeometry(); break;
        case VK_UP: set.bottom -= step; ApplyGeometry(); break;
        case VK_DOWN: set.bottom += step; ApplyGeometry(); break;
        case VK_OEM_PLUS: case VK_ADD: SetScale(set.scale * 1.08f, false); break;
        case VK_OEM_MINUS: case VK_SUBTRACT: SetScale(set.scale / 1.08f, false); break;
        }
        return 0; } break;
    case WM_HOTKEY:
        switch (w) {
        case HK_EDIT: SetEdit(!edit_); break;
        case HK_PAUSE: if (src_) SetPaused(!paused_); break;
        case HK_HIDE: if (Hidden()) SetHidden(false); else Panic(true); break;     // works with or without a performer on stage
        case HK_PREV: OnCommand(ID_PREV); break;
        case HK_MPLAY: OnCommand(ID_MUSIC_PAUSE); break;
        case HK_MNEXT: OnCommand(ID_MUSIC_NEXT); break;
        case HK_MPREV: OnCommand(ID_MUSIC_PREV); break;
        case HK_MSTOP: if (music.Enabled()) SetMusicPaused(true); break;
        case HK_NEXT: OnCommand(ID_NEXT); break;
        case HK_QUIT: Quit(); break;
        }
        return 0;
    case WM_APP_TRAY:
        // NOTIFYICON_VERSION_4 delivers both WM_RBUTTONUP and WM_CONTEXTMENU for one right click: act on exactly one.
        switch (LOWORD(l)) {
        case WM_CONTEXTMENU: { POINT p{ GET_X_LPARAM(w), GET_Y_LPARAM(w) }; ShowTrayMenu(p); break; }
        case NIN_SELECT: case NIN_KEYSELECT: if (UiVisible() && GetForegroundWindow() == UiWindow()) ShowWindow(UiWindow(), SW_HIDE); else UiShow(); break;
        }
        return 0;
    case WM_APP_SHOWUI: UiShow(); return 0;
    case WM_COPYDATA: {
        auto* cd = (COPYDATASTRUCT*)l;
        if (cd->dwData == kCopyOpen && cd->cbData >= sizeof(wchar_t)) {
            std::wstring p((const wchar_t*)cd->lpData, cd->cbData / sizeof(wchar_t));
            while (!p.empty() && p.back() == 0) p.pop_back();
            if (!p.empty()) OpenPath(p);
            return TRUE;
        }
        break; }
    case WM_DPICHANGED: ApplyGeometry(); return 0;
    case WM_DISPLAYCHANGE: EnsureVisible(); dirty_ = true; return 0;
    case WM_ENDSESSION: set.Save(); return 0;
    case WM_DESTROY:
        if (recreating_) return 0;
        set.Save(); TrayRemove(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd_, m, w, l);
}

int App::Run(HINSTANCE hi, int argc, wchar_t** argv) {
    hi_ = hi; t0_ = lastTick_ = NowSeconds();
    bool qaFlyout = false;
    std::wstring path, themeArg, audiotest, audiotestOut, uishot, synthOut; bool startEdit = false; float cliScale = 0; int uishotPage = 0, synthStation = 0; double synthSecs = 0;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--tray") startHidden_ = true;
        else if (a == L"--qa-flyout") qaFlyout = true;
        else if (a == L"--qa-wheel" && i + 1 < argc) qaWheel_ = argv[++i];
        else if (a == L"--sfxtest" && i + 1 < argc) { LogInit(); bool ok = SfxRenderWav(argv[++i]); return ok ? 0 : 3; }
        // ---- internal QA hooks below: not documented, not part of the product surface
        else if (a == L"--layered") cliLayered_ = true;
        else if (a == L"--sw") cliSw_ = true;
        else if (a == L"--hw") qaHw_ = true;
        else if (a == L"--lowlatency") lowLatency_ = true;
        else if (a == L"--edit") startEdit = true;
        else if (a == L"--expect" && i + 1 < argc) { std::wstring kv = argv[++i]; size_t eq = kv.find(L'='); if (eq != std::wstring::npos) expect_.emplace_back(kv.substr(0, eq), kv.substr(eq + 1)); }
        else if (a == L"--theme" && i + 1 < argc) themeArg = argv[++i];
        else if (a == L"--selftest" && i + 1 < argc) selftest_ = argv[++i];
        else if (a == L"--selftest-seconds" && i + 1 < argc) selftestSecs_ = std::clamp(_wtof(argv[++i]), 1.0, 600.0);
        else if (a == L"--scale" && i + 1 < argc) cliScale = (float)_wtof(argv[++i]);
        else if (a == L"--uishot" && i + 2 < argc) { uishotPage = _wtoi(argv[++i]); uishot = argv[++i]; qaQuiet = true; }
        else if (a == L"--audiotest" && i + 2 < argc) { audiotest = argv[++i]; audiotestOut = argv[++i]; }
        else if (a == L"--synthtest" && i + 3 < argc) { synthStation = _wtoi(argv[++i]); synthSecs = _wtof(argv[++i]); synthOut = argv[++i]; }
        else if (a[0] != L'-') path = a;
    }
    if (!audiotest.empty() || !synthOut.empty()) {      // headless audio path check (no device)
        struct Tap : AudioSource {                      // wraps a source, keeps what it rendered
            std::unique_ptr<AudioSource> in; std::vector<float> pcm; uint64_t limit = 0, n = 0;
            bool Prepare(int r, int c, std::wstring& e) override { return in->Prepare(r, c, e); }
            uint32_t Render(float* d, uint32_t k) override { uint32_t g = in->Render(d, k); if (limit) { pcm.insert(pcm.end(), d, d + (size_t)g * 2); n += g; if (n >= limit) return 0; } return g; }
            void Shutdown() override { in->Shutdown(); }
        };
        auto tap = std::make_unique<Tap>(); Tap* t = tap.get();
        if (!synthOut.empty()) { t->in = MakeSynthSource(synthStation); t->limit = (uint64_t)(synthSecs * 48000); } else t->in = MakeFileSource(audiotest);
        AudioOut ao; std::wstring err; bool ok = ao.Open(std::move(tap), 1.f, true, err);
        double t0 = NowSeconds(); while (ok && !ao.Done() && NowSeconds() - t0 < 120) Sleep(10);
        uint64_t frames = ao.Frames(); bool pass = ok && ao.Done() && frames > 0;
        if (!synthOut.empty() && pass) {                // 16-bit PCM WAV
            std::vector<int16_t> pcm(t->pcm.size()); for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = (int16_t)std::lround(std::clamp(t->pcm[i], -1.f, 1.f) * 32767.f);
            uint32_t bytes = (uint32_t)(pcm.size() * 2); std::vector<uint8_t> w(44 + bytes);
            auto u32 = [&](size_t o, uint32_t v) { memcpy(&w[o], &v, 4); }; auto u16 = [&](size_t o, uint16_t v) { memcpy(&w[o], &v, 2); };
            memcpy(&w[0], "RIFF", 4); u32(4, 36 + bytes); memcpy(&w[8], "WAVEfmt ", 8); u32(16, 16); u16(20, 1); u16(22, 2); u32(24, 48000); u32(28, 48000 * 4); u16(32, 4); u16(34, 16);
            memcpy(&w[36], "data", 4); u32(40, bytes); memcpy(&w[44], pcm.data(), bytes);
            WriteFileAll(synthOut, w.data(), w.size());
        } else if (!audiotestOut.empty()) {
            char buf[256]; snprintf(buf, sizeof(buf), "{ \"pass\": %s, \"frames\": %llu, \"seconds\": %.3f }\n", pass ? "true" : "false", (unsigned long long)frames, frames / 48000.0);
            WriteFileAll(audiotestOut, buf, strlen(buf));
        }
        return pass ? 0 : 3;
    }
    bool test = !selftest_.empty() || !uishot.empty();

    // single instance: hand the request to the running app
    HANDLE mx = !test ? CreateMutexW(nullptr, TRUE, kMutex) : nullptr;
    if (mx && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kClass, nullptr)) {
            if (!path.empty()) {
                wchar_t full[MAX_PATH]; GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
                COPYDATASTRUCT cd{ kCopyOpen, (DWORD)((wcslen(full) + 1) * sizeof(wchar_t)), full };
                SendMessageW(other, WM_COPYDATA, 0, (LPARAM)&cd);
            } else PostMessageW(other, WM_APP_SHOWUI, 0, 0);
        }
        return 0;
    }

    set.Load(); set.noSave = test; if (!test) user.Load();
    if (test) { set.loopMode = 0; set.clickThrough = false; set.decodeMode = qaHw_ ? 1 : 0; set.hwBad = false; set.x = set.bottom = INT_MIN; set.scale = 1.f; set.mirror = false; }
    { wchar_t qs[32]; if (GetEnvironmentVariableW(L"DIVAVERSE_QA_SEEK", qs, 32)) qaSeek_ = _wtoi(qs); if (GetEnvironmentVariableW(L"DIVAVERSE_QA_DROP", qs, 32)) qaDrop_ = 1; }
    if (cliScale > 0) set.scale = std::clamp(cliScale, 0.25f, 3.0f);
    if (themeArg == L"light") set.theme = 1; else if (themeArg == L"dark") set.theme = 2; else if (themeArg == L"pink") set.theme = 3; else if (themeArg == L"system") set.theme = 0;
    ThemeSet((ThemeId)set.theme);
    bool gfx = GfxInit();
    if (!gfx) Log("Direct2D / DirectWrite unavailable: launcher window disabled");
    if (cliLayered_ || (set.dcompBad && selftest_.empty())) mode_ = PresentMode::Layered;

    for (int s = 0; s < 3; ++s) icons_[s] = MakeIcon((IconState)s, GetSystemMetrics(SM_CXSMICON));
    bigIcon_ = MakeIcon(IconState::Normal, 64);
    wmTaskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");

    if (!CreateOverlay() || !InitRenderer()) return 2;
    { wchar_t qm[MAX_PATH]; if (GetEnvironmentVariableW(L"DIVAVERSE_QA_MUSIC", qm, MAX_PATH)) { music.Load();      // QA: recursive import + playlist import
            int n = music.AddFolder(qm); Sleep(1500); music.Poll(); int pl = music.ImportPlaylist(JoinPath(qm, L"mix.m3u"));
            Log("qa music: added %d, tracks %zu, albums %zu, playlist %d with %zu entries, first '%s' / '%s' / '%s'", n, music.Tracks().size(), music.Albums().size(), pl,
                pl >= 0 ? music.Playlists()[pl].paths.size() : (size_t)0, music.Tracks().empty() ? "" : WToUtf8(music.Tracks()[0].name).c_str(), music.Tracks().empty() ? "" : WToUtf8(music.Tracks()[0].artist).c_str(), music.Tracks().empty() ? "" : WToUtf8(music.Tracks()[0].album).c_str()); } } music.Select(set.musicMode, set.musicMode == 0 ? set.station : -1);
    if (!test) {
        TrayAdd();
        RegisterHotkeys();
        music.Load(); music.SetVolume(set.musicVolume); music.SetShuffle(set.musicShuffle); music.SetNormalize(set.musicNormalize);

        if (set.music) music.SetEnabled(true);
        SfxConfigure(set.sfx, set.sfxVol);
        UpdateMediaKeys();
    }
    selftestStart_ = NowSeconds();
    if (selftest_.empty()) {
        if (gfx && (!startHidden_ || !uishot.empty())) UiShow(uishot.empty());
        RescanLibrary();
        if (path.empty() && !test && !set.last.empty() && (FileExists(set.last) || DirExists(set.last))) path = set.last;   // bring back whoever was on stage
    }
    if (!path.empty()) OpenPath(path);
    if (startEdit) SetEdit(true);
    UpdateSuspend();

    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);   // pre-1803 fallback

    if (qaFlyout) ShowTrayMenu({ 700, 500 });
    MSG msg{}; double uishotAt = NowSeconds() + 4.0;
    for (;;) {
        // Sleep until: input, the next frame's due time, or an animation step.
        double now = NowSeconds(), wait = 0.25;
        bool active = src_ && !suspended_ && !hidden_ && !panic_ && !autoHidden_;
        if (edit_ || dirty_ || editAnim_ > 0.f || fadeIn_ < 1.f) wait = 1.0 / 60.0;
        else if (active && clockValid_) wait = std::clamp(clockOrigin_ + (shown_ + 1) / src_->Fps() - now, 0.001, 0.25);
        else if (active) wait = 0.004;                // waiting for the first decoded frame
        if (falling_) wait = std::min(wait, 0.006);
        if (src_ && !hidden_ && !panic_ && !autoHidden_) wait = std::min(wait, 0.033);   // pointer sampling for click-on-performer
        LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(wait * 1e7);
        SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE);
        MsgWaitForMultipleObjectsEx(1, &timer_, 250, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        // The pump is bounded: a window that repaints slower than its animation timer would otherwise keep the queue
        // non-empty forever (WM_TIMER and WM_PAINT are regenerated on demand) and playback would starve.
        for (double pumpStart = NowSeconds(); PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE);) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (NowSeconds() - pumpStart > 0.006) break;
        }
        Tick();
        if (!qaWheel_.empty() && src_ && shown_ > 24) {
            if (!WheelOpen()) { RECT rc; GetWindowRect(hwnd_, &rc); PerformerMenu({ (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 }); }
            else { bool ok = WheelScreenshot(qaWheel_); msg.wParam = ok ? 0 : 3; goto done; }
        }
        if (!uishot.empty() && NowSeconds() > uishotAt) { bool ok = uishotPage >= 40 ? WetRashShot(uishot, uishotPage - 40) : UiScreenshot(uishot, uishotPage); msg.wParam = ok ? 0 : 3; goto done; }
    }
done:
    FlyoutClose(); WheelClose(); WetRashClose(); SfxShutdown();
    lib.Stop(); music.SetEnabled(false); music.Flush(true); user.Flush(true); set.Save();
    VideoSource::Retire(src_); r_.Shutdown();
    for (auto h : icons_) if (h) DestroyIcon(h);
    if (bigIcon_) DestroyIcon(bigIcon_);
    if (timer_) CloseHandle(timer_);
    if (mx) { ReleaseMutex(mx); CloseHandle(mx); }
    return (int)msg.wParam;
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);   // also in manifest
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LogInit();
    Log("Divaverse 0.6.2 starting");
    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    App app; g_app = &app;
    int rc = app.Run(hi, argc, argv);
    LocalFree(argv);
    CoUninitialize();
    return rc;
}
