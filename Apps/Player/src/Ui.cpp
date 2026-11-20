// Divaverse launcher window: Divas (library + diva page), On Stage, Music, Settings.
// Immediate-mode widgets over Canvas (Direct2D). Cover art comes from the media (embedded or beside it); everything
// else on screen (backdrop lights, borders, icons, sparkles, equaliser) is drawn from code.
#include "App.h"
#include "Gfx.h"
#include "Synth.h"
#include "Image.h"
#include "Sfx.h"
#include "resource.h"
#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <functional>
#include <unordered_map>
#include <cstdarg>
#include <cmath>
#include <cwctype>
#include <algorithm>

namespace {
constexpr wchar_t kUiClass[] = L"Divaverse.Main";
enum { PG_LIBRARY, PG_STAGE, PG_MUSIC, PG_SETTINGS, PG_COUNT, PG_DETAIL = PG_COUNT };
constexpr float RAIL = 212;
constexpr uint32_t kPink = 0xFF4FA8, kViolet = 0x8E5BFF, kCyan = 0x4FD6FF;

struct Hit { RectF r; int id; std::function<void()> click; std::function<void(float)> drag; std::function<void()> release; };

struct UiState {
    HWND h = nullptr; Canvas cv; int page = PG_LIBRARY;
    std::vector<Hit> hits; int hover = -1, down = -1; float mx = -1, my = -1;
    std::unordered_map<int, float> anim; bool animating = false; double lastPaint = 0, t = 0;
    float scroll[PG_COUNT + 1] = {}, maxScroll = 0;
    // device bitmaps, valid for one (library generation, canvas generation) pair
    std::vector<ComPtr<ID2D1Bitmap>> covers, thumbs; ComPtr<ID2D1Bitmap> logo; int bmpLib = -1; unsigned bmpCv = 0;
    std::wstring toast; double toastUntil = 0;
    int musicTab = 0; bool timer = false, closedHintShown = false;
    // library view
    std::wstring search; bool searchFocus = false; int filter = 0; std::wstring detailKey; bool infoOpen = false;
    std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap>> songArt; int plSel = -1, sort = 0;
    std::vector<ComPtr<ID2D1Bitmap>> banners; std::wstring tagText; bool tagFocus = false;
    int hkCapture = -1;                                  // Settings -> Shortcuts: action waiting for a key combination
    std::wstring plName; bool plFocus = false;           // On Stage -> name for "save the queue as a playlist"
    struct Loot { std::wstring key, name; int fresh; bool wholeNew; }; std::vector<Loot> loot; double lootT = 0; int lootGen = -1; bool lootBurst = false;
    std::function<void()> pendingLoot, deferred;
    double pageT = 0, ripT = -10; float ripX = 0, ripY = 0; float scrub = -1;
} U;

Canvas& C() { return U.cv; }
const Palette& P() { return g_pal; }
float Fx() { return g_app->set.fx ? 1.f : 0.f; }
int Month() { wchar_t b[8]; if (GetEnvironmentVariableW(L"DIVAVERSE_QA_MONTH", b, 8)) return _wtoi(b); SYSTEMTIME st; GetLocalTime(&st); return st.wMonth; }

// ---- widget plumbing ---------------------------------------------------------------------------
float Anim(int id, float target, float speed = 14.f) {
    float& v = U.anim[id]; float dt = (float)std::min(0.05, U.t - U.lastPaint);
    if (std::fabs(target - v) < 0.004f) v = target; else { v += (target - v) * std::min(1.f, dt * speed); U.animating = true; }
    return v;
}
bool Hot(int id) { return U.hover == id; }
void AddHit(const RectF& r, int id, std::function<void()> click, std::function<void(float)> drag = nullptr, std::function<void()> release = nullptr) {
    U.hits.push_back({ r, id, std::move(click), std::move(drag), std::move(release) });
}

enum BtnKind { B_PRIMARY, B_SOFT, B_GHOST, B_DANGER };
void Button(int id, const RectF& r, const std::wstring& label, int icon, BtnKind kind, std::function<void()> fn, bool enabled = true) {
    float h = Anim(id, enabled && Hot(id) ? 1.f : 0.f), d = enabled && U.down == id ? 1.f : 0.f;
    Color bg, fg;
    if (kind == B_PRIMARY) { bg = Mix(P().accent, P().dark ? Hex(0xFFFFFF) : Hex(0x000000), 0.10f * h + 0.08f * d); fg = P().accentText; }
    else if (kind == B_DANGER) { bg = Mix(Alpha(P().danger, 0.12f), Alpha(P().danger, 0.26f), h); fg = P().danger; }
    else if (kind == B_SOFT) { bg = Mix(Mix(P().card, P().text, 0.06f), Mix(P().card, P().text, 0.13f), h); fg = P().text; }
    else { bg = Alpha(P().text, 0.09f * h); fg = P().text; }
    if (!enabled) { bg = Alpha(P().text, 0.05f); fg = P().faint; }
    float rad = RH(r) > 40 ? 13.f : 10.f;
    if (kind == B_PRIMARY && enabled) {
        C().Glow((r.left + r.right) / 2, r.bottom - 2, RW(r) * 0.6f, RH(r) * (0.9f + 0.5f * h), Hex(kPink, 0.16f + 0.14f * h));
        C().Grad3RR(r, rad, Mix(Hex(kPink), Hex(0xFFFFFF), 0.10f * h), Mix(P().accent, Hex(0xFFFFFF), 0.08f * h), Hex(kViolet), 12.f);
        if (h > 0.01f) C().Sheen(r, rad, h, 0.28f * (1 - h) + 0.06f);
    } else C().FillRR(r, rad, bg);
    if (kind == B_SOFT && enabled) C().StrokeRR(r, rad, Alpha(P().text, 0.08f + 0.08f * h), 1);
    float tw = label.empty() ? 0 : C().TextW(label, 13.5f, kind == B_PRIMARY), iw = icon >= 0 ? 16.f : 0, gap = (iw && tw) ? 8.f : 0;
    float x = (r.left + r.right) / 2 - (tw + iw + gap) / 2, cy = (r.top + r.bottom) / 2;
    if (icon >= 0) C().IconAt((Icon)icon, x + 8, cy, 15, fg, 1.5f);
    if (tw) C().Text(label, RF(x + iw + gap, r.top, tw + 4, RH(r)), 13.5f, fg, 0, kind == B_PRIMARY);
    if (enabled) AddHit(r, id, std::move(fn));
}
void RoundBtn(int id, float cx, float cy, float rad, int icon, bool primary, std::function<void()> fn, bool enabled = true) {
    float h = Anim(id, enabled && Hot(id) ? 1.f : 0.f);
    Color bg = primary ? Mix(P().accent, Hex(0xFFFFFF), 0.12f * h) : Alpha(P().text, 0.06f + 0.08f * h);
    if (primary && enabled) C().Glow(cx, cy, rad * 2.1f, rad * 2.1f, Hex(kPink, 0.20f + 0.15f * h));
    if (primary && enabled) { RectF q = RF(cx - rad - 1.5f * h, cy - rad - 1.5f * h, (rad + 1.5f * h) * 2, (rad + 1.5f * h) * 2); C().Grad3RR(q, RW(q) / 2, Hex(kPink), P().accent, Hex(kViolet), 40.f); }
    else C().Circle(cx, cy, rad + 1.5f * h, enabled ? bg : Alpha(P().text, 0.04f));
    C().IconAt((Icon)icon, cx + (icon == I_PLAY ? rad * 0.06f : 0), cy, rad * 0.9f, !enabled ? P().faint : primary ? P().accentText : P().text, 1.5f);
    if (enabled) AddHit(RF(cx - rad, cy - rad, rad * 2, rad * 2), id, std::move(fn));
}
void Toggle(int id, float right, float cy, bool v, std::function<void()> fn) {
    RectF r = RF(right - 44, cy - 12, 44, 24);
    float a = Anim(id, v ? 1.f : 0.f, 18.f), h = Anim(id + 500000, Hot(id) ? 1.f : 0.f);
    C().FillRR(r, 12, Mix(P().card, P().text, 0.22f + 0.06f * h));
    if (a > 0.01f) C().Grad3RR(r, 12, Hex(kPink, a), Alpha(P().accent, a), Hex(kViolet, a), 0.f);
    C().Circle(r.left + 12 + 20 * a, cy, 8.5f + 0.8f * h, Hex(0xFFFFFF));
    AddHit(Inset(r, -6), id, std::move(fn));
}
void Slider(int id, const RectF& r, float v, std::function<void(float)> onChange, std::function<void()> onRelease) {
    float cy = (r.top + r.bottom) / 2, h = Anim(id, Hot(id) || U.down == id ? 1.f : 0.f);
    v = std::clamp(v, 0.f, 1.f);
    C().FillRR(RF(r.left, cy - 2.5f, RW(r), 5), 2.5f, Mix(P().card, P().text, 0.16f));
    if (v > 0.005f) C().Grad3RR(RF(r.left, cy - 2.5f, RW(r) * v, 5), 2.5f, Hex(kPink), P().accent, Hex(kViolet), 0.f);
    float kx = r.left + RW(r) * v;
    C().Circle(kx, cy, 9 + 1.5f * h, Hex(0xFFFFFF)); C().Circle(kx, cy, 5 + 1.2f * h, P().accent);
    float l = r.left, w = RW(r);
    AddHit(Inset(r, -8), id, nullptr, [l, w, onChange](float x) { onChange(std::clamp((x - l) / w, 0.f, 1.f)); }, std::move(onRelease));
}
void Segmented(int idBase, const RectF& r, const std::vector<std::wstring>& labels, int cur, std::function<void(int)> fn) {
    C().FillRR(r, 10, Mix(P().card, P().text, 0.07f));
    float w = RW(r) / labels.size();
    float pos = Anim(idBase + 90, (float)cur, 16.f);
    C().Grad3RR(RF(r.left + 3 + pos * w, r.top + 3, w - 6, RH(r) - 6), 8, Hex(kPink), P().accent, Hex(kViolet), 10.f);
    for (size_t i = 0; i < labels.size(); ++i) {
        RectF q = RF(r.left + i * w, r.top, w, RH(r));
        C().Text(labels[i], q, 13, (int)i == cur ? P().accentText : (Hot(idBase + (int)i) ? P().text : P().dim), 1, (int)i == cur);
        int k = (int)i; AddHit(q, idBase + k, [fn, k] { fn(k); });
    }
}
void Card(const RectF& r, float rad = 14) {      // glass panel: translucent body, light catching the top edge, thin bright rim
    C().FillRR(r, rad, Alpha(P().card, P().dark ? 0.62f : 0.78f));
    C().GradRR(RF(r.left, r.top, RW(r), std::min(RH(r), 90.f)), rad, Hex(0xFFFFFF, P().dark ? 0.075f : 0.55f), Hex(0xFFFFFF, 0.f), true);
    C().GradStrokeRR(r, rad, Hex(0xFFFFFF, P().dark ? 0.05f : 0.5f), Hex(0xFFFFFF, P().dark ? 0.26f : 0.95f), 1.f, 35.f);
    C().StrokeRR(Inset(r, -0.5f), rad, Alpha(P().border, 0.6f), 1);
}
void SectionTitle(float x, float& y, const std::wstring& s) { C().Text(s, RF(x, y, 400, 22), 12, P().dim, 0, true); y += 30; }
// A settings row inside a card: title + description on the left, control drawn by the caller on the right.
void Row(const RectF& card, float& y, int icon, const std::wstring& title, const std::wstring& desc, bool divider = true) {
    float h = desc.empty() ? 52.f : 62.f;
    C().IconAt((Icon)icon, card.left + 28, y + h / 2, 17, P().dim, 1.4f);
    if (desc.empty()) C().Text(title, RF(card.left + 54, y, RW(card) - 260, h), 14, P().text);
    else { C().Text(title, RF(card.left + 54, y + 10, RW(card) - 260, 22), 14, P().text); C().Text(desc, RF(card.left + 54, y + 31, RW(card) - 260, 20), 12, P().dim); }
    if (divider) C().Line(card.left + 54, y + h, card.right - 16, y + h, P().border, 1);
    y += h;
}
std::wstring Fmt(const wchar_t* f, ...) { wchar_t b[256]; va_list ap; va_start(ap, f); vswprintf(b, 256, f, ap); va_end(ap); return b; }

std::wstring PickFolder() {
    std::wstring out; ComPtr<IFileOpenDialog> d;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return out;
    DWORD o = 0; d->GetOptions(&o); d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    d->SetTitle(L"Add a folder to the Divaverse library");
    if (SUCCEEDED(d->Show(U.h))) { ComPtr<IShellItem> it; PWSTR p = nullptr; if (SUCCEEDED(d->GetResult(&it)) && SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { out = p; CoTaskMemFree(p); } }
    return out;
}
std::vector<std::wstring> PickFiles(const wchar_t* filter) {
    std::vector<wchar_t> file(32768); std::vector<std::wstring> paths;
    OPENFILENAMEW of{ sizeof(of) }; of.hwndOwner = U.h; of.lpstrFile = file.data(); of.nMaxFile = (DWORD)file.size(); of.lpstrFilter = filter;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameW(&of)) return paths;
    std::vector<std::wstring> parts; for (const wchar_t* q = file.data(); *q; q += wcslen(q) + 1) parts.emplace_back(q);
    if (parts.size() == 1) paths = parts; else for (size_t i = 1; i < parts.size(); ++i) paths.push_back(JoinPath(parts[0], parts[i]));
    return paths;
}
void CopyText(const std::wstring& s) {
    if (!OpenClipboard(U.h)) return;
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes)) { memcpy(GlobalLock(g), s.c_str(), bytes); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT, g); }
    CloseClipboard();
}
// ---- helpers ------------------------------------------------------------------------------------
std::wstring Lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
std::wstring Clock(double sec) { int s = (int)std::lround(std::max(0.0, sec)); return Fmt(L"%d:%02d", s / 60, s % 60); }

void Equalizer(const RectF& r, int bars, float level, bool live) {     // bars driven by the real output peak
    float bw = RW(r) / (bars * 1.6f); if (live) U.animating = true;
    for (int i = 0; i < bars; ++i) {
        float ph = 0.5f + 0.5f * std::sin((float)U.t * (4.6f + i * 1.37f) + i * 1.9f), k = live ? std::min(1.f, 0.12f + level * 1.7f) * (0.30f + 0.70f * ph) : 0.10f;
        float bh = std::max(3.f, RH(r) * k), x = r.left + i * bw * 1.6f;
        C().GradRR(RF(x, r.bottom - bh, bw, bh), bw / 2, live ? Hex(kPink) : P().faint, live ? P().accent : P().faint, true);
    }
}

// Device bitmaps. All of them die with the render target, so the cache is keyed by the canvas generation as well as
// the library generation (a stale cache was why the On Stage picture could vanish).
void SyncBitmaps() {
    Library& lib = g_app->lib;
    if (U.bmpLib != lib.Generation() || U.bmpCv != C().Generation()) {
        U.covers.clear(); U.thumbs.clear(); U.banners.clear(); U.logo.Reset(); U.bmpLib = lib.Generation(); U.bmpCv = C().Generation();
    }
    if (!U.logo) { ImageBGRA img; if (DecodeImageResource(IDR_LOGO, 0, img)) U.logo = C().MakeBitmap(img.px.data(), img.w, img.h); }
}
// Callers hold the library mutex (PaintAll takes it for the whole frame).
ID2D1Bitmap* Cover(int d) {
    auto& dv = g_app->lib.Divas(); if (d < 0 || d >= (int)dv.size()) return nullptr;
    if ((int)U.covers.size() <= d) U.covers.resize(dv.size());
    if (!U.covers[d] && dv[d].coverReady) U.covers[d] = C().MakeBitmap(dv[d].cover.px.data(), dv[d].cover.w, dv[d].cover.h);
    return U.covers[d].Get();
}
ID2D1Bitmap* Banner(int d) {
    auto& dv = g_app->lib.Divas(); if (d < 0 || d >= (int)dv.size()) return nullptr;
    if ((int)U.banners.size() <= d) U.banners.resize(dv.size());
    if (!U.banners[d] && dv[d].bannerReady) U.banners[d] = C().MakeBitmap(dv[d].banner.px.data(), dv[d].banner.w, dv[d].banner.h);
    return U.banners[d].Get();
}
ID2D1Bitmap* Thumb(int c) {
    auto& cl = g_app->lib.Clips(); if (c < 0 || c >= (int)cl.size()) return nullptr;
    if ((int)U.thumbs.size() <= c) U.thumbs.resize(cl.size());
    if (!U.thumbs[c] && cl[c].thumbReady) U.thumbs[c] = C().MakeBitmap(cl[c].thumb.px.data(), cl[c].thumb.w, cl[c].thumb.h);
    return U.thumbs[c].Get();
}
void Contain(ID2D1Bitmap* b, const RectF& area, float zoom = 1.f) {       // whole picture, feet on the bottom edge
    if (!b) return;
    D2D1_SIZE_F sz = b->GetSize(); float k = std::min(RW(area) / sz.width, RH(area) / sz.height) * zoom;
    float w = sz.width * k, h = sz.height * k;
    C().Bitmap(b, RF((area.left + area.right) / 2 - w / 2, area.bottom - h, w, h));
}
void Backdrop(const RectF& r, float rad, int seed); void Loading(const RectF& r);
// A clip's picture inside r: authored artwork fills the tile, a decoded cut-out frame stands on a generated backdrop.
void ClipPic(int ci, const RectF& r, float rad, float zoom) {
    auto& cl = g_app->lib.Clips(); if (ci < 0 || ci >= (int)cl.size()) { Backdrop(r, rad, 0); return; }
    ID2D1Bitmap* t = Thumb(ci);
    if (t && cl[ci].thumbIsArt) { C().BitmapCover(t, r, rad, 1.f, 0.15f, zoom); return; }
    Backdrop(r, rad, ci + 2);
    C().PushClip(r, rad);
    if (t) Contain(t, RF(r.left, r.top + RH(r) * 0.06f, RW(r), RH(r) * 0.94f), zoom); else if (!cl[ci].thumbFailed) Loading(r);
    C().PopClip();
}
void Backdrop(const RectF& r, float rad, int seed) {                      // generated stage backdrop behind a cut-out thumbnail
    static const uint32_t a[] = { 0xE0409B, 0x6A4BFF, 0x1AA7EC, 0xF2994A, 0x30A46C, 0xD06BFF }, b[] = { 0x6A4BFF, 0x35D6FF, 0x8A3FFC, 0xE0409B, 0x1AA7EC, 0xFF6FA8 };
    int k = ((seed % 6) + 6) % 6; float al = P().dark ? 0.42f : 0.26f;
    C().FillRR(r, rad, Mix(P().card, P().text, 0.04f));
    C().GradRR(r, rad, Hex(a[k], al), Hex(b[k], al * 0.35f), false);
    C().PushClip(r, rad);
    float cx = (r.left + r.right) / 2;
    C().Glow(cx, r.bottom, RW(r) * 0.55f, RH(r) * 0.22f, Hex(0xFFFFFF, 0.16f));
    for (int i = 0; i < 3; ++i) C().Ring(cx, r.bottom + 6, RW(r) * (0.20f + 0.12f * i), Hex(0xFFFFFF, 0.12f - 0.03f * i), 1.2f);     // floor ripples
    C().PopClip();
}
void Loading(const RectF& r) {
    float sp = (float)U.t * 4.f; U.animating = true;
    for (int k = 0; k < 3; ++k) C().Circle((r.left + r.right) / 2 - 16 + 16 * k, (r.top + r.bottom) / 2 - 5 * std::fabs(std::sin(sp + k * 0.6f)), 4, Alpha(P().accent, 0.8f));
}
// The picture that represents a diva: her cover if the media has one, else a thumbnail of her first clip.
void DivaArt(int d, const RectF& r, float rad, float hover, bool square) {
    auto& dv = g_app->lib.Divas()[d];
    if (ID2D1Bitmap* b = Cover(d)) { C().BitmapCover(b, r, rad, 1.f, square ? 0.12f : 0.18f, 1.f + 0.05f * hover); return; }
    ClipPic(dv.clips.empty() ? -1 : dv.clips[0], r, rad, 1.f + 0.04f * hover);
}
void Stars(float x, float cy, int rating, float size = 7.f) {
    for (int i = 0; i < 5; ++i) C().Star(x + size + i * (size * 2 + 3), cy, size, i < rating ? Hex(0xFFC53D) : Alpha(P().text, 0.18f));
}
void Pill(const RectF& r, const std::wstring& s, bool hot) {
    if (hot) C().Grad3RR(r, RH(r) / 2, Hex(kPink), P().accent, Hex(kViolet), 0.f); else C().FillRR(r, RH(r) / 2, Hex(0x000000, 0.55f));
    C().Text(s, r, 10.5f, Hex(0xFFFFFF), 1, true);
}
int StageDiva() {       // library index of the diva on the desktop, or -1
    if (!g_app->OnStage()) return -1;
    int c = g_app->lib.FindClip(g_app->StagePath());
    return c < 0 ? -1 : g_app->lib.Clips()[c].diva;
}
int DivaByKey(const std::wstring& key) {
    auto& dv = g_app->lib.Divas();
    for (size_t i = 0; i < dv.size(); ++i) if (dv[i].key == key) return (int)i;
    return -1;
}
void GoPage(int p) { if (U.page != p || !U.detailKey.empty()) { SfxPlay(SFX_PAGE); U.search.clear(); U.page = p; U.detailKey.clear(); U.pageT = NowSeconds(); U.searchFocus = false; } UiInvalidate(); }
// Runs after the frame (the paint holds the library lock; these calls take it themselves).
void Stage(int diva, int clip) {
    if (g_app->OpenDiva(diva, clip)) { if (g_app->Hidden()) g_app->SetHidden(false); SfxPlay(SFX_STAGE); UiToast(L"On the desktop now"); }
    UiInvalidate();
}

bool Fuzzy(const std::wstring& hayIn, const std::wstring& q);
// ---- pages --------------------------------------------------------------------------------------
float PageHeader(const RectF& a, const std::wstring& title, const std::wstring& sub, float reserve = 300) {
    C().Text(title, RF(a.left, a.top, RW(a) - reserve, 40), 26, P().text, 0, true);
    if (!sub.empty()) C().Text(sub, RF(a.left, a.top + 40, RW(a) - reserve, 22), 13, P().dim);
    return a.top + 82;
}
void EmptyState(const RectF& a, float y, int icon, const std::wstring& title, const std::wstring& body) {
    float cx = (a.left + a.right) / 2, pulse = 0.5f + 0.5f * std::sin((float)U.t * 2.f); U.animating = true;
    C().Glow(cx, y + 70, 120, 120, Hex(kPink, 0.14f + 0.06f * pulse));
    C().Circle(cx, y + 70, 54 + 4 * pulse, Alpha(P().accent, 0.10f)); C().Circle(cx, y + 70, 40, Alpha(P().accent, 0.16f));
    C().IconAt((Icon)icon, cx, y + 70, 34, P().accent, 1.5f);
    C().Text(title, RF(a.left, y + 140, RW(a), 28), 18, P().text, 1, true);
    C().Text(body, RF(cx - 240, y + 172, 480, 44), 13, P().dim, 1, false, false, true);
}

void SearchBox(const RectF& r) {
    float f = Anim(120, U.searchFocus ? 1.f : 0.f), h = Anim(121, Hot(120) ? 1.f : 0.f);
    C().FillRR(r, RH(r) / 2, Alpha(Mix(P().card, P().text, 0.05f + 0.04f * h), 0.92f));
    if (f > 0.01f) C().GradStrokeRR(r, RH(r) / 2, Hex(kPink, f), Alpha(P().accent2, f), 1.5f, (float)U.t * 40.f); else C().StrokeRR(r, RH(r) / 2, P().border, 1);
    if (U.searchFocus) U.animating = true;
    C().IconAt(I_SEARCH, r.left + 18, (r.top + r.bottom) / 2, 14, U.searchFocus ? P().accent : P().dim, 1.5f);
    RectF tr = RF(r.left + 34, r.top, RW(r) - 62, RH(r));
    if (U.search.empty() && !U.searchFocus) C().Text(L"Search divas and clips", tr, 13, P().faint);
    else {
        C().Text(U.search, tr, 13, P().text);
        if (U.searchFocus && std::fmod(U.t, 1.0) < 0.55) { float cx = tr.left + std::min(RW(tr) - 2, C().TextW(U.search, 13)) + 1; C().Line(cx, r.top + 9, cx, r.bottom - 9, P().text, 1.2f); }
    }
    AddHit(r, 120, [] { U.searchFocus = true; UiInvalidate(); });
    if (!U.search.empty()) Button(122, RF(r.right - 30, r.top + (RH(r) - 24) / 2, 24, 24), L"", I_CLOSE, B_GHOST, [] { U.search.clear(); UiInvalidate(); });
}

// One diva card. Two forms: small square tile and large portrait card. All text sits inside the picture.
void DivaCard(int d, const RectF& slot, bool large, bool onStage) {
    auto& dv = g_app->lib.Divas()[d];
    int id = 2000 + d; float h = Anim(id, Hot(id) || Hot(3000 + d) ? 1.f : 0.f, 12.f), rad = large ? 18.f : 14.f;
    RectF r = RF(slot.left, slot.top - (large ? 6 : 3) * h, RW(slot), RH(slot));
    // Hover: the card leans toward the pointer like a held trading card (affine lean + sliding highlight + shifting
    // shadow; Direct2D render targets have no perspective transform, so this is a lean, not a true 3D rotation).
    float ccx = (r.left + r.right) / 2, ccy = (r.top + r.bottom) / 2;
    float nx = std::clamp((U.mx - ccx) / (RW(r) / 2), -1.f, 1.f) * h, ny = std::clamp((U.my - ccy) / (RH(r) / 2), -1.f, 1.f) * h;
    float tx3 = Anim(id + 20000, nx, 7.f), ty3 = Anim(id + 30000, ny, 7.f); bool lean = h > 0.01f;
    C().Shadow(RF(r.left - tx3 * 4, r.top - ty3 * 3 + 3 * h, RW(r), RH(r)), rad, 6 + 12 * h, P().dark ? 0.6f : 0.24f);
    if (lean) {
        float sc = 1 + 0.022f * h;
        C().SetTransform(D2D1::Matrix3x2F::Translation(-ccx, -ccy) *
                         D2D1::Matrix3x2F(sc * (1 - 0.018f * std::fabs(tx3)), 0.016f * tx3, -0.012f * ty3, sc * (1 - 0.018f * std::fabs(ty3)), 0, 0) *
                         D2D1::Matrix3x2F::Translation(ccx, ccy));
    }
    if (h > 0.01f || onStage) C().Glow((r.left + r.right) / 2, r.bottom - RH(r) * 0.1f, RW(r) * 0.75f, RH(r) * 0.5f, Hex(kPink, 0.20f * std::max(h, onStage ? 0.7f : 0.f)));
    C().FillRR(r, rad, P().card);
    DivaArt(d, r, rad, h, !large);
    // scrim + text inside the card
    float th = large ? 86.f : 46.f;
    C().GradRR(RF(r.left, r.bottom - th - 40, RW(r), th + 40), rad, Hex(0x0A0614, 0.f), Hex(0x0A0614, 0.55f), true);
    { RectF pl = RF(r.left + 6, r.bottom - th + 2, RW(r) - 12, th - 8); C().FillRR(pl, rad - 5, Hex(0x140A24, 0.46f)); C().GradRR(RF(pl.left, pl.top, RW(pl), RH(pl) * 0.6f), rad - 5, Hex(0xFFFFFF, 0.16f), Hex(0xFFFFFF, 0.f), true);
      C().GradStrokeRR(pl, rad - 5, Hex(0xFFFFFF, 0.10f), Hex(0xFFFFFF, 0.45f), 1.f, 30.f); }
    float tx = r.left + (large ? 14 : 10), tw = RW(r) - (large ? 28 : 20);
    if (large) {
        C().Text(dv.name, RF(tx, r.bottom - 76, tw, 26), 16.5f, Hex(0xFFFFFF), 0, true);
        int rate = g_app->user.Rating(dv.key, dv.rating); if (rate > 0) Stars(tx, r.bottom - 42, rate, 5.5f);
        std::wstring meta = Fmt(L"%d %ls", (int)dv.clips.size(), dv.clips.size() == 1 ? L"clip" : L"clips"); if (dv.seconds > 0) meta += L"   " + Clock(dv.seconds);
        C().Text(meta, RF(tx, r.bottom - 30, tw, 20), 11.5f, Hex(0xFFFFFF, 0.72f), rate > 0 ? 2 : 0);
    } else {
        C().Text(dv.name, RF(tx, r.bottom - 40, tw, 20), 13, Hex(0xFFFFFF), 0, true);
        C().Text(Fmt(L"%d %ls", (int)dv.clips.size(), dv.clips.size() == 1 ? L"clip" : L"clips"), RF(tx, r.bottom - 22, tw, 16), 10.5f, Hex(0xFFFFFF, 0.7f));
    }
    // hover: play badge, moving highlight, animated neon rim
    if (h > 0.01f) {
        float cx = (r.left + r.right) / 2, cy = r.top + (RH(r) - th) / 2, pr = (large ? 26.f : 19.f) * (0.8f + 0.2f * h);
        C().Grad3RR(RF(cx - pr, cy - pr, pr * 2, pr * 2), pr, Hex(kPink, h), Alpha(P().accent, h), Hex(kViolet, h), 40.f);
        C().IconAt(I_PLAY, cx + pr * 0.08f, cy, pr * 0.85f, Hex(0xFFFFFF, h), 1.6f);
        C().Sheen(r, rad, h, 0.20f * (1 - h));
    }
    if (onStage) { C().GradStrokeRR(r, rad, Hex(kPink), Hex(kCyan), 2.2f, (float)U.t * 50.f); U.animating = true; Pill(RF(r.left + 10, r.top + 10, 78, 22), L"ON STAGE", true); }
    else if (h > 0.01f) { C().GradStrokeRR(r, rad, Hex(kPink, h), Hex(kCyan, h), 1.6f, (float)U.t * 70.f); U.animating = true; }
    else C().StrokeRR(r, rad, Alpha(P().dark ? Hex(0xFFFFFF) : P().border, P().dark ? 0.08f : 1.f), 1);
    if (lean) C().Glow(ccx + tx3 * RW(r) * 0.45f, ccy + ty3 * RH(r) * 0.45f, RW(r) * 0.55f, RW(r) * 0.55f, Hex(0xFFFFFF, 0.16f * h));      // light follows the pointer
    std::wstring key = dv.key;
    AddHit(r, id, [key] { U.detailKey = key; U.pageT = NowSeconds(); U.scroll[PG_DETAIL] = 0; U.searchFocus = false; UiInvalidate(); });
    // favourite heart (top right, inside the card)
    bool fav = g_app->IsFav(key); float fh = Anim(3000 + d, Hot(3000 + d) ? 1.f : 0.f), hs = large ? 15.f : 13.f;
    if (fav || h > 0.01f) {
        float hx = r.right - 20, hy = r.top + 20;
        C().Circle(hx, hy, hs * 0.95f + 1.5f * fh, Hex(0x000000, 0.45f * std::max(h, fav ? 1.f : 0.f)));
        C().IconAt(fav ? I_HEART : I_HEARTO, hx, hy + 0.5f, hs, fav ? Hex(kPink) : Hex(0xFFFFFF, h), 1.5f);
        AddHit(RF(hx - 15, hy - 15, 30, 30), 3000 + d, [key] { g_app->ToggleFav(key); });
    }
    if (lean) C().ResetTransform();
}

float PageLibrary(const RectF& a) {
    App& A = *g_app; auto& divas = A.lib.Divas(); auto& clips = A.lib.Clips();
    float y = PageHeader(a, L"Divas", divas.empty() ? L"Your performers live here" : Fmt(L"%d %ls, %d %ls", (int)divas.size(), divas.size() == 1 ? L"diva" : L"divas", (int)clips.size(), clips.size() == 1 ? L"clip" : L"clips"), 420);
    Button(110, RF(a.right - 136, a.top + 4, 136, 36), L"Add folder", I_FOLDER, B_SOFT, [] { std::wstring d = PickFolder(); if (!d.empty()) g_app->AddLibraryFolder(d); });
    Button(111, RF(a.right - 178, a.top + 4, 36, 36), L"", I_REFRESH, B_GHOST, [] { g_app->RescanLibrary(); UiToast(L"Library refreshed"); });
    SearchBox(RF(a.right - 420, a.top + 4, 232, 36));
    // toolbar: filter, card form, zoom
    y -= 18;
    Segmented(130, RF(a.left, y, 300, 34), { L"All", L"Favourites", L"Recent" }, U.filter, [](int k) { U.filter = k; U.scroll[PG_LIBRARY] = 0; UiInvalidate(); });
    C().Text(L"Sort", RF(a.left + 316, y, 40, 34), 12, P().faint);
    Segmented(131, RF(a.left + 356, y, 250, 34), { L"Name", L"Newest", L"Rating" }, U.sort, [](int k) { U.sort = k; UiInvalidate(); });
    float rx = a.right;
    Slider(140, RF(rx - 130, y, 130, 34), (A.set.zoom - 0.6f) / 1.2f, [](float v) { g_app->set.zoom = 0.6f + v * 1.2f; UiInvalidate(); }, [] { g_app->set.Save(); });
    C().IconAt(I_SEARCH, rx - 150, y + 17, 14, P().dim, 1.5f);
    for (int m = 0; m < 2; ++m) {
        RectF b = RF(rx - 250 + m * 38, y, 34, 34); int id = 135 + m; bool cur = A.set.cardMode == m; float h = Anim(id, Hot(id) ? 1.f : 0.f);
        C().FillRR(b, 9, cur ? Alpha(P().accent, 0.22f) : Alpha(P().text, 0.05f + 0.06f * h)); if (cur) C().StrokeRR(b, 9, P().accent, 1.2f);
        C().IconAt(m == 0 ? I_TILES : I_CARDS, b.left + 17, b.top + 17, 15, cur ? P().accent : P().dim, 1.4f);
        AddHit(b, id, [m] { g_app->set.cardMode = m; g_app->set.Save(); UiInvalidate(); });
    }
    y += 54;
    if (divas.empty()) {
        EmptyState(a, y + 10, I_SPARKLE, L"No divas yet", L"Drop clips or a folder onto this window, or put them in the Divas folder next to Divaverse.");
        y += 280;
    } else {
        // filter + order
        std::vector<int> order; std::wstring q = Lower(U.search);
        for (int d = 0; d < (int)divas.size(); ++d) {
            if (U.filter == 1 && !A.IsFav(divas[d].key)) continue;
            if (U.filter == 2) { bool played = false; for (int c : divas[d].clips) for (auto& rp : A.set.recent) played = played || !_wcsicmp(clips[c].desc.path.c_str(), rp.c_str()); if (!played) continue; }
            if (!q.empty()) { std::wstring hay = divas[d].name; for (int c : divas[d].clips) hay += L" " + clips[c].title + L" " + Utf8ToW(clips[c].info.meta.showTitle);
                auto tg = A.user.tags.find(divas[d].key); if (tg != A.user.tags.end()) for (auto& t : tg->second) hay += L" " + t;
                if (!Fuzzy(hay, q)) continue; }
            order.push_back(d);
        }
        if (U.filter == 2) {      // most recently played first, then most recently added
            auto rank = [&](int d) { for (size_t i = 0; i < A.set.recent.size(); ++i) for (int c : divas[d].clips) if (!_wcsicmp(clips[c].desc.path.c_str(), A.set.recent[i].c_str())) return (int)i; return 1000; };
            std::stable_sort(order.begin(), order.end(), [&](int x, int z) { int rxk = rank(x), rz = rank(z); return rxk != rz ? rxk < rz : divas[x].added > divas[z].added; });
        } else if (U.sort == 1) std::stable_sort(order.begin(), order.end(), [&](int x, int z) { return divas[x].added > divas[z].added; });
        else if (U.sort == 2) std::stable_sort(order.begin(), order.end(), [&](int x, int z) { return A.user.Rating(divas[x].key, divas[x].rating) > A.user.Rating(divas[z].key, divas[z].rating); });
        else std::stable_sort(order.begin(), order.end(), [&](int x, int z) { return _wcsicmp(divas[x].name.c_str(), divas[z].name.c_str()) < 0; });
        if (order.empty()) {
            C().Text(U.filter == 1 ? L"No favourites yet. Hover a card and press the heart." : U.filter == 2 && q.empty() ? L"Nothing played yet." : L"Nothing matches.", RF(a.left, y + 30, RW(a), 30), 14, P().dim, 1);
            y += 100;
        } else {
            bool large = A.set.cardMode == 1; float z = A.set.zoom, gap = large ? 22.f : 16.f;
            float cw = (large ? 212.f : 132.f) * z, ch = large ? cw * 1.46f : cw;
            int cols = std::max(1, (int)((RW(a) + gap) / (cw + gap))); int stage = StageDiva();
            float intro = (float)std::min(1.0, (U.t - U.pageT) * 2.2);
            for (size_t i = 0; i < order.size(); ++i) {
                float st = std::clamp(intro * 1.6f - (float)i * 0.05f, 0.f, 1.f), ease = 1 - (1 - st) * (1 - st); if (st < 1) U.animating = true;     // staggered entrance
                RectF slot = RF(a.left + (float)(i % cols) * (cw + gap), y + (float)(i / cols) * (ch + gap) + 18 * (1 - ease), cw, ch);
                if (slot.bottom < -40 || slot.top > C().H() + 40) continue;
                DivaCard(order[i], slot, large, order[i] == stage);
                if (ease < 1) C().FillRR(Inset(slot, -8), 0, Alpha(P().surface, 1 - ease));
            }
            y += (float)((order.size() + cols - 1) / cols) * (ch + gap);
        }
    }
    // files that could not be used are listed, never silently skipped
    auto& pr = A.lib.Problems();
    if (!pr.empty()) {
        y += 10; SectionTitle(a.left, y, Fmt(L"%d %ls COULD NOT BE READ", (int)pr.size(), pr.size() == 1 ? L"FILE" : L"FILES"));
        size_t n = std::min<size_t>(pr.size(), 6); RectF card = RF(a.left, y, RW(a), n * 44.f); Card(card);
        for (size_t i = 0; i < n; ++i) {
            C().IconAt(I_INFO, card.left + 24, y + 22, 15, P().danger, 1.5f);
            C().Text(pr[i].path, RF(card.left + 46, y + 4, RW(card) - 62, 20), 12.5f, P().text);
            C().Text(Utf8ToW(pr[i].what), RF(card.left + 46, y + 22, RW(card) - 62, 18), 11.5f, P().dim);
            y += 44;
        }
        y += 16;
    }
    return y;
}

// Diva page: tall banner (its own artwork, so the page does not repeat the library card), rating, tags, and her
// clips grouped by show.
float PageDetail(const RectF& a, int d) {
    App& A = *g_app; auto& dv = A.lib.Divas()[d]; auto& clips = A.lib.Clips(); std::wstring key = dv.key;
    Button(200, RF(a.left, a.top, 96, 34), L"Divas", I_BACK, B_GHOST, [] { SfxPlay(SFX_BACK); U.detailKey.clear(); U.tagFocus = false; U.pageT = NowSeconds(); UiInvalidate(); });
    float y = a.top + 52, cw = std::min(250.f, RW(a) * 0.30f), chh = std::min(cw * 2.6f, std::max(420.f, C().H() - 110));      // banner standard is 1:3, shown cropped to at most 1:2.6
    RectF art = RF(a.left, y, cw, chh);
    C().Glow((art.left + art.right) / 2, art.bottom - 40, cw * 0.9f, chh * 0.35f, Hex(kPink, 0.22f));
    C().Shadow(art, 20, 16, P().dark ? 0.6f : 0.22f);
    if (ID2D1Bitmap* bn = Banner(d)) { Backdrop(art, 20, d + 3); C().PushClip(art, 20); Contain(bn, RF(art.left + 6, art.top + 10, cw - 12, chh - 14)); C().PopClip(); }
    else { C().FillRR(art, 20, P().card); if (ID2D1Bitmap* cv = Cover(d)) C().BitmapCover(cv, art, 20, 1.f, 0.1f, 1.f); else DivaArt(d, art, 20, 0, false); }
    C().GradStrokeRR(art, 20, Hex(kPink), Hex(kCyan), 2.f, (float)U.t * 30.f); if (Fx() > 0) U.animating = true;
    int stage = StageDiva(); bool on = stage == d;
    if (on) Pill(RF(art.left + 12, art.top + 12, 82, 24), L"ON STAGE", true);

    float x = art.right + 34, w = a.right - x, yy = y + 4;
    C().Text(dv.name, RF(x, yy, w - 50, 44), 32, P().text, 0, true);
    bool fav = A.IsFav(key);
    RoundBtn(201, a.right - 20, yy + 22, 18, fav ? I_HEART : I_HEARTO, fav, [key] { g_app->ToggleFav(key); });
    yy += 50;
    {   // your rating: click a star (click the same star again to go back to the creator's rating)
        int rate = A.user.Rating(key, dv.rating); bool mine = A.user.rating.count(key) != 0; int hv = 0;
        for (int i = 0; i < 5; ++i) if (Hot(210 + i)) hv = i + 1;
        for (int i = 0; i < 5; ++i) {
            float sx = x + 12 + i * 28.f; bool lit = hv ? i < hv : i < rate;
            C().Star(sx, yy + 11, 10.f + (Hot(210 + i) ? 1.5f : 0), lit ? (hv ? Hex(kPink) : Hex(0xFFC53D)) : Alpha(P().text, 0.18f));
            int v = i + 1; AddHit(RF(sx - 14, yy - 3, 28, 28), 210 + i, [key, v, rate, mine] { auto& u = g_app->user; if (mine && rate == v) u.rating.erase(key); else u.rating[key] = v; u.dirty = true; SfxPlay(SFX_TOGGLE); UiInvalidate(); });
        }
        C().Text(mine ? L"your rating" : dv.rating > 0 ? L"creator's rating - click to set yours" : L"click to rate", RF(x + 152, yy, w - 152, 22), 11.5f, P().faint);
        yy += 34;
    }
    C().Text(Fmt(L"%d %ls   %ls total", (int)dv.clips.size(), dv.clips.size() == 1 ? L"clip" : L"clips", Clock(dv.seconds).c_str()), RF(x, yy, w, 22), 13, P().dim); yy += 30;
    {   // tags (yours; searchable from the library)
        auto& tags = A.user.tags[key]; float cx = x;
        for (size_t i = 0; i < tags.size(); ++i) {
            float tw = C().TextW(tags[i], 12) + 34; if (cx + tw > a.right) { cx = x; yy += 32; }
            RectF chip = RF(cx, yy, tw, 26); C().FillRR(chip, 13, Alpha(P().accent, 0.18f)); C().StrokeRR(chip, 13, Alpha(P().accent, 0.5f), 1);
            C().Text(tags[i], RF(chip.left + 10, chip.top, tw - 30, 26), 12, P().text);
            C().IconAt(I_CLOSE, chip.right - 13, chip.top + 13, 9, Hot(230 + (int)i) ? P().text : P().dim, 1.4f);
            int k = (int)i; AddHit(RF(chip.right - 24, chip.top, 24, 26), 230 + (int)i, [key, k] { auto& t = g_app->user.tags[key]; if (k < (int)t.size()) t.erase(t.begin() + k); g_app->user.dirty = true; UiInvalidate(); });
            cx += tw + 6;
        }
        float iw = 130; if (cx + iw > a.right) { cx = x; yy += 32; }
        RectF in = RF(cx, yy, iw, 26); C().FillRR(in, 13, Alpha(P().text, 0.06f)); C().StrokeRR(in, 13, U.tagFocus ? P().accent : P().border, 1);
        if (U.tagText.empty() && !U.tagFocus) C().Text(L"+ add tag", RF(in.left + 12, in.top, iw - 20, 26), 12, P().faint);
        else { C().Text(U.tagText, RF(in.left + 12, in.top, iw - 20, 26), 12, P().text); if (U.tagFocus) { U.animating = true; if (std::fmod(U.t, 1.0) < 0.55) { float tx = in.left + 13 + std::min(iw - 24, C().TextW(U.tagText, 12)); C().Line(tx, in.top + 6, tx, in.bottom - 6, P().text, 1.2f); } } }
        AddHit(in, 250, [] { U.tagFocus = true; UiInvalidate(); });
        yy += 40;
    }
    if (!dv.profile.empty()) {
        int n = (int)std::min<size_t>(dv.profile.size(), 8), cols = w > 520 ? 4 : 2; float gw = 10, tw = (w - gw * (cols - 1)) / cols;
        for (int i = 0; i < n; ++i) {
            RectF t = RF(x + (i % cols) * (tw + gw), yy + (float)(i / cols) * 66, tw, 56); Card(t, 12);
            C().Text(dv.profile[i].first, RF(t.left + 12, t.top + 8, tw - 20, 16), 10.5f, P().faint, 0, true);
            C().Text(dv.profile[i].second, RF(t.left + 12, t.top + 25, tw - 20, 24), 14.5f, P().text, 0, true);
        }
        yy += (float)((n + cols - 1) / cols) * 66 + 8;
    }
    Button(202, RF(x, yy, 196, 44), on ? L"On the desktop" : L"Put on desktop", on ? I_CHECK : I_PLAY, B_PRIMARY, [d, on] { if (on) GoPage(PG_STAGE); else Stage(d, 0); });
    if (on) Button(203, RF(x + 208, yy, 150, 44), L"Controls", I_STAGE, B_SOFT, [] { GoPage(PG_STAGE); });
    yy += 66;

    // clips, one band per show (clips without show metadata go under "Singles")
    struct Grp { std::wstring id, title; std::vector<int> pos; }; std::vector<Grp> groups;
    for (size_t i = 0; i < dv.clips.size(); ++i) {
        const MediaMeta& m = clips[dv.clips[i]].info.meta; std::wstring id = Utf8ToW(m.showId), title = Utf8ToW(m.showTitle);
        if (id.empty() && title.empty()) { id = L"\x01"; title = L"Singles"; } else if (title.empty()) title = id;
        Grp* g = nullptr; for (auto& q : groups) if (q.id == id) g = &q;
        if (!g) { groups.push_back({ id, title, {} }); g = &groups.back(); }
        g->pos.push_back((int)i);
    }
    float cy = yy, tw = 148, thh = 210, gap = 14; int cols = std::max(1, (int)((w + gap) / (tw + gap)));
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        Grp& g = groups[gi]; RectF band = RF(x, cy, w, 40);
        C().Grad3RR(band, 12, Hex(kPink, 0.30f), Alpha(P().accent, 0.16f), Hex(kViolet, 0.04f), 0.f); C().Grad3RR(RF(x, cy + 8, 4, 24), 2, Hex(kPink), P().accent, Hex(kViolet), 90.f);
        C().Text(g.title, RF(x + 16, cy, w - 220, 40), 14.5f, P().text, 0, true);
        C().Text(Fmt(L"%d %ls", (int)g.pos.size(), g.pos.size() == 1 ? L"clip" : L"clips"), RF(band.right - 200, cy, 90, 40), 12, P().dim, 2);
        int firstPos = g.pos[0]; Button(260 + (int)gi, RF(band.right - 100, cy + 5, 92, 30), L"Play", I_PLAY, B_SOFT, [d, firstPos] { Stage(d, firstPos); });
        cy += 52;
        for (size_t n = 0; n < g.pos.size(); ++n) {
            int pos = g.pos[n], ci = dv.clips[pos]; const LibClip& c = clips[ci];
            int id = 4000 + pos; float h = Anim(id, Hot(id) ? 1.f : 0.f, 12.f);
            RectF r = RF(x + (float)(n % cols) * (tw + gap), cy + (float)(n / cols) * (thh + gap) - 3 * h, tw, thh);
            if (r.top > C().H() || r.bottom < 0) continue;
            bool playing = on && !_wcsicmp(c.desc.path.c_str(), A.StagePath().c_str());
            C().Shadow(r, 14, 4 + 8 * h, P().dark ? 0.5f : 0.2f);
            ClipPic(ci, r, 14, 1.f + 0.04f * h);
            C().GradRR(RF(r.left, r.bottom - 70, tw, 70), 14, Hex(0x0A0614, 0.f), Hex(0x0A0614, 0.88f), true);
            C().Text(c.title, RF(r.left + 10, r.bottom - 42, tw - 20, 20), 12.5f, Hex(0xFFFFFF), 0, true);
            C().Text(Fmt(L"%ls  %dx%d  %ls", Clock(c.seconds).c_str(), c.info.rgb.w, c.info.dual ? c.info.rgb.h : c.desc.height, c.info.dual ? L"dual" : L"packed"), RF(r.left + 10, r.bottom - 23, tw - 20, 16), 10, Hex(0xFFFFFF, 0.7f));
            if (c.desc.ppm <= 0) { C().Circle(r.right - 19, r.top + 19, 11, Hex(0x000000, 0.5f)); C().IconAt(I_RULER, r.right - 19, r.top + 19, 12, Hex(0xFFC53D), 1.3f); }
            bool unseen = !A.user.seen.count(key + L"|" + c.desc.id) && !A.user.seen.empty();
            if (playing) { C().GradStrokeRR(r, 14, Hex(kPink), Hex(kCyan), 2.f, (float)U.t * 50.f); U.animating = true; Pill(RF(r.left + 8, r.top + 8, 64, 20), L"PLAYING", true); }
            else if (unseen) Pill(RF(r.left + 8, r.top + 8, 44, 20), L"NEW", true);
            if (!playing && h > 0.01f) { C().GradStrokeRR(r, 14, Hex(kPink, h), Hex(kCyan, h), 1.5f, (float)U.t * 70.f); U.animating = true;
                float pr = 20 * (0.8f + 0.2f * h); C().Grad3RR(RF(r.left + tw / 2 - pr, r.top + thh / 2 - 20 - pr, pr * 2, pr * 2), pr, Hex(kPink, h), Alpha(P().accent, h), Hex(kViolet, h), 40.f);
                C().IconAt(I_PLAY, r.left + tw / 2 + 2, r.top + thh / 2 - 20, 17, Hex(0xFFFFFF, h), 1.6f); }
            AddHit(r, id, [d, pos] { Stage(d, pos); });
            if (h > 0.01f || Hot(5000 + pos)) {                       // queue it without interrupting what is playing
                RectF qb = RF(r.right - 34, r.bottom - 76, 26, 26); C().FillRR(qb, 13, Hex(0x000000, 0.6f)); C().IconAt(I_ADD, qb.left + 13, qb.top + 13, 12, Hex(0xFFFFFF), 1.6f);
                std::wstring cp = c.desc.path; AddHit(qb, 5000 + pos, [cp] { g_app->Enqueue(cp); UiToast(L"Added to the queue"); });
            }
        }
        cy += (float)((g.pos.size() + cols - 1) / cols) * (thh + gap) + 10;
    }
    return std::max(cy, art.bottom) + 30;
}

void InfoRow(float x, float& y, float w, const std::wstring& k, const std::wstring& v, Color vc) {
    C().Text(k, RF(x, y, 150, 22), 12, P().faint); C().Text(v, RF(x + 150, y, w - 150, 22), 12.5f, vc); y += 22;
}
// Media information for the clip on stage: facts read from the container, then the creator's metadata.
float MediaInfoCard(float x, float y, float w) {
    App& A = *g_app; const MediaInfo& m = A.stageInfo; const MediaMeta& mm = m.meta;
    std::vector<std::pair<std::wstring, std::wstring>> rows;
    auto add = [&](const wchar_t* k, const std::wstring& v) { rows.emplace_back(k, v); };
    add(L"File", m.path.substr(m.path.find_last_of(L"\\/") + 1) + Fmt(L"   (%.1f MB)", m.fileSize / 1048576.0));
    add(L"Container", Utf8ToW(m.docType) + Fmt(L", %d video %ls, %d attachments, %ls", m.videoTracks, m.videoTracks == 1 ? L"track" : L"tracks", (int)m.attachments.size(), m.cues ? L"seekable" : L"no cues"));
    auto trk = [&](const TrackFacts& t) { return Fmt(L"%ls  %dx%d  profile %d level %.1f  %ls range", Utf8ToW(t.codec).c_str(), t.w, t.h, t.profile, t.level / 10.0, t.range == 2 ? L"full" : L"video"); };
    add(L"Colour track", trk(m.rgb));
    add(L"Alpha", m.dual ? trk(m.alpha) : A.Cur() && A.Cur()->layout == Layout::Opaque ? std::wstring(L"none (opaque clip)") : std::wstring(L"packed below the colour picture"));
    add(L"Timing", Fmt(L"%.3f fps, %d frames, %ls", m.fps, m.frames, Clock(m.durationS).c_str()));
    add(L"Metadata", mm.schema ? Fmt(L"schema %d: %ls%ls%ls", mm.schema, mm.embedded ? L"embedded divaverse.json" : L"", mm.embedded && mm.sidecar ? L" + " : L"", mm.sidecar ? L"sidecar overrides" : L"") : std::wstring(L"none (defaults)"));
    if (!mm.divaName.empty() || !mm.divaId.empty()) add(L"Diva", Utf8ToW(mm.divaName) + (mm.divaId.empty() ? L"" : L"   [" + Utf8ToW(mm.divaId) + L"]"));
    if (!mm.showTitle.empty() || !mm.showId.empty()) add(L"Show", Utf8ToW(mm.showTitle) + (mm.showId.empty() ? L"" : L"   [" + Utf8ToW(mm.showId) + L"]"));
    const ClipDesc* c = A.Cur();
    if (c) add(L"Stage", c->ppm > 0 ? Fmt(L"canvas %dx%d, %.0f px per metre, performer %.2f m, ground anchor %.3f / %.3f", c->canvasW, c->canvasH, c->ppm, c->actorHeightM, c->anchorU, c->groundY)
                                     : Fmt(L"uncalibrated: canvas %dx%d treated as 1.75 m tall, ground anchor %.3f / %.3f", c->canvasW, c->canvasH, c->anchorU, c->groundY));
    add(L"Artwork", m.hasCover ? L"embedded cover" : mm.coverFromSidecar ? L"sidecar override" : L"none embedded (thumbnail generated from the video)");
    float h = 20 + rows.size() * 22.f + (m.warnings.size() + m.errors.size()) * 20.f + 16;
    RectF card = RF(x, y, w, h); Card(card); float yy = y + 14;
    for (auto& r : rows) InfoRow(x + 20, yy, w - 40, r.first, r.second, P().text);
    for (auto& e : m.errors) { C().Text(Utf8ToW(e), RF(x + 20, yy, w - 40, 20), 12, P().danger); yy += 20; }
    for (auto& e : m.warnings) { C().Text(Utf8ToW(e), RF(x + 20, yy, w - 40, 20), 12, Hex(0xE0A030)); yy += 20; }
    return y + h;
}

float PageStage(const RectF& a) {
    App& A = *g_app;
    float y = PageHeader(a, L"On Stage", A.OnStage() ? L"Control the performer on your desktop" : L"Nobody is on the desktop right now");
    if (!A.OnStage()) {
        EmptyState(a, y + 20, I_STAGE, L"The stage is empty", L"Pick a diva and she will appear above your taskbar.");
        Button(300, RF((a.left + a.right) / 2 - 90, y + 250, 180, 40), L"Browse divas", I_GRID, B_PRIMARY, [] { GoPage(PG_LIBRARY); });
        return y + 320;
    }
    float artW = std::min(280.f, RW(a) * 0.34f), artH = artW * 1.5f;
    RectF art = RF(a.left, y, artW, artH);
    int sd = StageDiva(), sc = A.lib.FindClip(A.StagePath());
    C().Glow((art.left + art.right) / 2, art.bottom - 30, artW * 0.8f, artH * 0.45f, Hex(kPink, 0.20f));
    C().Shadow(art, 20, 14, P().dark ? 0.55f : 0.2f); C().FillRR(art, 20, P().card);
    if (sd >= 0) DivaArt(sd, art, 20, 0, false);
    else { Backdrop(art, 20, 0); C().IconAt(I_STAGE, (art.left + art.right) / 2, (art.top + art.bottom) / 2, 60, Alpha(P().text, 0.35f), 1.2f); }   // staged from outside the library
    C().GradStrokeRR(art, 20, Hex(kPink), Hex(kCyan), 2.f, (float)U.t * 30.f); if (!A.Paused() && !A.Hidden()) U.animating = true;
    if (A.Paused() || A.Hidden()) Pill(RF(art.left + 12, art.top + 12, 78, 24), A.Hidden() ? L"HIDDEN" : L"PAUSED", false);
    // live thumbnail of the clip in the corner of the cover
    if (sc >= 0 && sd >= 0 && Cover(sd)) if (Thumb(sc) && !A.lib.Clips()[sc].thumbIsArt) { RectF q = RF(art.right - 76, art.bottom - 108, 64, 96); ClipPic(sc, q, 10, 1.f); C().StrokeRR(q, 10, Hex(0xFFFFFF, 0.5f), 1.2f); }

    float x = art.right + 32, w = a.right - x, yy = y;
    C().Text(A.show.title, RF(x, yy, w, 34), 24, P().text, 0, true); yy += 36;
    const ClipDesc& cur = A.show.clips[A.clipIdx];
    C().Text(cur.title.empty() ? cur.id : cur.title, RF(x, yy, w, 22), 13.5f, P().dim); yy += 32;
    // clip chips
    float cx = x;
    for (size_t i = 0; i < A.show.clips.size() && A.show.clips.size() > 1; ++i) {
        std::wstring nm = A.show.clips[i].title.empty() ? A.show.clips[i].id : A.show.clips[i].title; float tw = std::min(w, C().TextW(nm, 12.5f) + 26);
        if (cx + tw > a.right) { cx = x; yy += 36; }
        bool isCur = (int)i == A.clipIdx; int id = 320 + (int)i; float h = Anim(id, Hot(id) ? 1.f : 0.f);
        RectF chip = RF(cx, yy, tw, 28);
        if (isCur) C().Grad3RR(chip, 14, Hex(kPink), P().accent, Hex(kViolet), 0.f); else C().FillRR(chip, 14, Mix(Mix(P().card, P().text, 0.07f), Mix(P().card, P().text, 0.15f), h));
        C().Text(nm, chip, 12.5f, isCur ? Hex(0xFFFFFF) : P().text, 1, isCur);
        int k = (int)i; AddHit(chip, id, [k] { g_app->OpenClip(k); });
        cx += tw + 8;
    }
    if (A.show.clips.size() > 1) yy += 44;
    // scrub bar
    { int64_t n = A.Frames(); double fps = std::max(1.0, A.stageInfo.fps); float p = U.scrub >= 0 ? U.scrub : A.Progress();
      C().Text(Clock(p * (n - 1) / fps), RF(x, yy, 44, 30), 11.5f, P().dim);
      Slider(330, RF(x + 46, yy, w - 100, 30), p, [](float v) { U.scrub = v; UiInvalidate(); }, [] { if (U.scrub >= 0) g_app->SeekFrame((int64_t)std::lround(U.scrub * (double)std::max<int64_t>(0, g_app->Frames() - 1))); U.scrub = -1; });
      C().Text(Clock(n / fps), RF(a.right - 48, yy, 48, 30), 11.5f, P().dim, 2);
      if (!A.Paused() && !A.Hidden()) U.animating = true;
      yy += 40; }
    // transport
    float tx = x + 20; bool multi = A.show.clips.size() > 1;
    RoundBtn(301, tx, yy + 28, 18, I_PREV, false, [] { g_app->OnCommand(ID_PREV); }, multi);
    RoundBtn(306, tx + 46, yy + 28, 15, I_BACK, false, [] { g_app->StepFrame(-1); });
    RoundBtn(302, tx + 100, yy + 28, 28, A.Paused() ? I_PLAY : I_PAUSE, true, [] { g_app->SetPaused(!g_app->Paused()); });
    { float sx = tx + 154; float h = Anim(307, Hot(307) ? 1.f : 0.f); C().Circle(sx, yy + 28, 15 + 1.5f * h, Alpha(P().text, 0.06f + 0.08f * h)); C().Line(sx - 3, yy + 22, sx + 3, yy + 28, P().text, 1.5f); C().Line(sx + 3, yy + 28, sx - 3, yy + 34, P().text, 1.5f);
      AddHit(RF(sx - 15, yy + 13, 30, 30), 307, [] { g_app->StepFrame(1); }); }
    RoundBtn(303, tx + 200, yy + 28, 18, I_NEXT, false, [] { g_app->OnCommand(ID_NEXT); }, multi);
    Segmented(340, RF(a.right - std::min(300.f, w - 250), yy + 10, std::min(300.f, w - 250), 36), { L"Repeat", L"In order", L"Shuffle" }, A.set.loopMode, [](int k) { g_app->SetLoopMode(k); });
    yy += 70;
    Button(304, RF(x, yy, 150, 38), A.Hidden() ? L"Show" : L"Hide", A.Hidden() ? I_EYE : I_EYEOFF, B_SOFT, [] { g_app->SetHidden(!g_app->Hidden()); });
    Button(305, RF(x + 160, yy, 176, 38), A.Editing() ? L"Done moving" : L"Move and resize", I_MOVE, A.Editing() ? B_PRIMARY : B_SOFT, [] { g_app->SetEdit(!g_app->Editing()); });
    C().Text(L"Ctrl+Alt+H hides everything instantly", RF(x + 348, yy, w - 348, 38), 11.5f, P().faint);
    yy += 56;
    RectF card = RF(x, yy, w, 62 * 2 + 52 * 6); Card(card);
    double cm = A.StageHeightCm();
    (void)cm; const ClipDesc* cd = A.Cur();
    Row(card, yy, I_RULER, Fmt(L"Stage scale %d%%", (int)std::lround(A.set.scale * 100)),
        cd && cd->ppm > 0 ? (cd->actorHeightM > 0 ? Fmt(L"Calibrated. Creator height %.0f cm (from the clip, not a screen measurement)", cd->actorHeightM * 100) : std::wstring(L"Calibrated by the creator"))
                          : std::wstring(L"Uncalibrated: this clip carries no physical scale"));
    Slider(310, RF(card.right - 216, yy - 62, 196, 62), (A.set.scale - 0.25f) / 1.75f, [](float v) { g_app->SetScale(0.25f + v * 1.75f, false); UiInvalidate(); }, [] { g_app->set.Save(); });
    Row(card, yy, I_EYE, L"Opacity", Fmt(L"%d%%", (int)std::lround(A.set.opacity * 100)));
    Slider(311, RF(card.right - 216, yy - 62, 196, 62), (A.set.opacity - 0.2f) / 0.8f, [](float v) { g_app->SetOpacity(0.2f + v * 0.8f, false); UiInvalidate(); }, [] { g_app->set.Save(); });
    Row(card, yy, I_MIRROR, L"Mirror", L""); Toggle(312, card.right - 20, yy - 26, A.set.mirror, [] { g_app->SetMirror(!g_app->set.mirror); });
    Row(card, yy, I_SHADOW, L"Contact shadow", L""); Toggle(313, card.right - 20, yy - 26, A.set.shadow, [] { g_app->SetShadow(!g_app->set.shadow); });
    Row(card, yy, I_PIN, L"Always on top", L""); Toggle(314, card.right - 20, yy - 26, A.set.topmost, [] { g_app->SetTopmost(!g_app->set.topmost); });
    Row(card, yy, I_LOCK, L"Lock position", L""); Toggle(317, card.right - 20, yy - 26, A.set.lock, [] { g_app->SetLock(!g_app->set.lock); });
    Row(card, yy, I_NEXT, L"Drop back to the taskbar after dragging", L""); Toggle(321, card.right - 20, yy - 26, A.set.gravity, [] { g_app->set.gravity = !g_app->set.gravity; g_app->set.Save(); UiInvalidate(); });
    Row(card, yy, I_CURSOR, L"Ignore the mouse (clicks pass through her)", L"", false); Toggle(318, card.right - 20, yy - 26, A.set.clickThrough, [] { g_app->SetClickThrough(!g_app->set.clickThrough); });
    yy += 16;
    C().Text(L"Click her for the wheel menu. Drag her anywhere; with the drop option on she falls back to the taskbar, with it off she stays put. Ctrl + wheel resizes.", RF(x, yy, w, 36), 11.5f, P().faint, 0, false, false, true);
    yy += 44;
    Button(315, RF(x, yy, 150, 36), L"Reset position", I_REFRESH, B_GHOST, [] { g_app->ResetPosition(); });
    if (GetSystemMetrics(SM_CMONITORS) > 1) Button(322, RF(x + 360, yy, 150, 36), L"Next monitor", I_WINDOW, B_GHOST, [] { g_app->MoveToNextMonitor(); });
    Button(316, RF(x + 160, yy, 190, 36), L"Remove from desktop", I_CLOSE, B_DANGER, [] { g_app->Unstage(); });
    Button(319, RF(a.right - 150, yy, 150, 36), U.infoOpen ? L"Hide media info" : L"Media info", I_FILM, B_GHOST, [] { U.infoOpen = !U.infoOpen; UiInvalidate(); });
    yy = std::max(yy + 56, art.bottom + 24);
    {   // playback queue: plays before any repeat / shuffle; clips are added with the + on a clip tile
        auto& q = A.set.queue; SectionTitle(a.left, yy, Fmt(L"UP NEXT (%d)", (int)q.size()));
        if (q.empty()) { C().Text(L"The queue is empty. Open a diva and press + on a clip to line it up; it plays when the current clip ends, without interrupting it.", RF(a.left, yy, RW(a), 22), 12, P().faint); yy += 40; }
        else {
            Button(360, RF(a.right - 110, yy - 38, 110, 30), L"Clear", I_TRASH, B_GHOST, [] { g_app->set.queue.clear(); g_app->set.Save(); UiInvalidate(); });
            size_t n = std::min<size_t>(q.size(), 12); RectF qc = RF(a.left, yy, RW(a), n * 40.f); Card(qc);
            for (size_t i = 0; i < n; ++i, yy += 40) {
                int ci = A.lib.FindClip(q[i]); std::wstring nm = ci >= 0 ? A.lib.Divas()[A.lib.Clips()[ci].diva].name + L"  -  " + A.lib.Clips()[ci].title : q[i].substr(q[i].find_last_of(L"\\/") + 1);
                C().Text(Fmt(L"%d", (int)i + 1), RF(qc.left + 14, yy, 26, 40), 12, P().faint); C().Text(nm, RF(qc.left + 44, yy, RW(qc) - 200, 40), 13, P().text);
                int k = (int)i;
                if (i > 0) Button(6500 + (int)i * 3, RF(qc.right - 128, yy + 5, 30, 30), L"", I_BACK, B_GHOST, [k] { auto& v = g_app->set.queue; if (k > 0 && k < (int)v.size()) { std::swap(v[k], v[k - 1]); g_app->set.Save(); UiInvalidate(); } });      // move earlier
                Button(6501 + (int)i * 3, RF(qc.right - 90, yy + 5, 30, 30), L"", I_PLAY, B_GHOST, [k] { auto& v = g_app->set.queue; if (k < (int)v.size()) { std::wstring p = v[k]; v.erase(v.begin() + k); g_app->set.Save(); if (g_app->OpenPath(p)) UiToast(L"Playing now"); } });
                Button(6502 + (int)i * 3, RF(qc.right - 52, yy + 5, 30, 30), L"", I_CLOSE, B_GHOST, [k] { auto& v = g_app->set.queue; if (k < (int)v.size()) { v.erase(v.begin() + k); g_app->set.Save(); UiInvalidate(); } });
            }
            yy += 20;
        }
        // saved clip playlists: the clip on stage plus the queue, stored as .m3u8 in %APPDATA%\Divaverse\ClipPlaylists
        auto lists = A.ClipPlaylists(); float px = a.left;
        RectF in = RF(px, yy, 220, 32); C().FillRR(in, 16, Alpha(P().text, 0.06f)); C().StrokeRR(in, 16, U.plFocus ? P().accent : P().border, 1);
        if (U.plName.empty() && !U.plFocus) C().Text(L"Playlist name", RF(in.left + 14, in.top, 200, 32), 12, P().faint);
        else { C().Text(U.plName, RF(in.left + 14, in.top, 196, 32), 12.5f, P().text); if (U.plFocus) { U.animating = true; if (std::fmod(U.t, 1.0) < 0.55) { float tx = in.left + 15 + std::min(190.f, C().TextW(U.plName, 12.5f)); C().Line(tx, in.top + 8, tx, in.bottom - 8, P().text, 1.2f); } } }
        AddHit(in, 370, [] { U.plFocus = true; UiInvalidate(); });
        Button(371, RF(px + 230, yy, 150, 32), L"Save as playlist", I_ADD, B_SOFT, [] { if (g_app->SaveClipPlaylist(U.plName)) { UiToast(L"Saved playlist " + U.plName); U.plName.clear(); } else UiToast(U.plName.empty() ? L"Type a name first" : L"Nothing on stage or in the queue to save"); UiInvalidate(); },
               A.OnStage() || !q.empty());
        yy += 44;
        if (!lists.empty()) {
            float cx = a.left;
            for (size_t i = 0; i < lists.size() && i < 24; ++i) {
                float tw = C().TextW(lists[i], 12.5f, true) + 84; if (cx + tw > a.right) { cx = a.left; yy += 40; }
                RectF chip = RF(cx, yy, tw, 32); C().FillRR(chip, 16, Mix(P().card, P().text, Hot(7400 + (int)i * 2) ? 0.14f : 0.07f));
                C().Text(lists[i], RF(chip.left + 14, chip.top, tw - 80, 32), 12.5f, P().text, 0, true);
                std::wstring nm = lists[i];
                AddHit(RF(chip.left, chip.top, tw - 64, 32), 7400 + (int)i * 2, [nm] { if (g_app->LoadClipPlaylist(nm, true)) UiToast(L"Playing " + nm); else UiToast(L"None of the clips in " + nm + L" were found"); });
                Button(7401 + (int)i * 2, RF(chip.right - 34, chip.top + 2, 28, 28), L"", I_CLOSE, B_GHOST, [nm] { g_app->DeleteClipPlaylist(nm); UiToast(L"Deleted " + nm); });
                C().IconAt(I_PLAY, chip.right - 50, chip.top + 16, 11, P().accent, 1.4f);
                cx += tw + 8;
            }
            yy += 48;
        }
    }
    if (A.set.editor && A.Cur()) {                    // creator values of this clip, adjustable live, stored as a sidecar override
        const ClipDesc cd2 = *A.Cur(); SectionTitle(a.left, yy, L"EDITOR: AUTHORED SCALE (SAVED BESIDE THE CLIP, NEVER INTO IT)");
        RectF ec = RF(a.left, yy, RW(a), 62 * 4 + 60); Card(ec); float ey = yy; double ppm = cd2.ppm > 0 ? cd2.ppm : cd2.canvasH / 1.75, hm = cd2.actorHeightM > 0 ? cd2.actorHeightM : 1.68;
        Row(ec, ey, I_RULER, Fmt(L"Density: %.1f canvas pixels per metre", ppm), Fmt(L"Canvas %dx%d = %.2f m x %.2f m. Standard stage: 420 screen px per metre at 100%%.", cd2.canvasW, cd2.canvasH, cd2.canvasW / ppm, cd2.canvasH / ppm));
        Slider(350, RF(ec.right - 276, ey - 62, 256, 62), (float)((ppm - 100) / 1900), [cd2, hm](float v) { g_app->EditPhysical(100 + v * 1900, hm, cd2.anchorU, cd2.groundY); }, [] {});
        Row(ec, ey, I_STAGE, Fmt(L"Creator height: %.0f cm", hm * 100), L"Informational; size on screen comes from the density");
        Slider(351, RF(ec.right - 276, ey - 62, 256, 62), (float)((hm - 1.2) / 1.0), [cd2, ppm](float v) { g_app->EditPhysical(ppm, 1.2 + v, cd2.anchorU, cd2.groundY); }, [] {});
        Row(ec, ey, I_SHADOW, Fmt(L"Floor line: %.3f of canvas height", cd2.groundY), L"Where her feet meet the ground");
        Slider(352, RF(ec.right - 276, ey - 62, 256, 62), (cd2.groundY - 0.5f) / 0.5f, [cd2, ppm, hm](float v) { g_app->EditPhysical(ppm, hm, cd2.anchorU, 0.5f + v * 0.5f); }, [] {});
        Row(ec, ey, I_MIRROR, Fmt(L"Anchor: %.3f of canvas width", cd2.anchorU), L"The point that stays fixed when she is resized", false);
        Slider(353, RF(ec.right - 276, ey - 62, 256, 62), cd2.anchorU, [cd2, ppm, hm](float v) { g_app->EditPhysical(ppm, hm, v, cd2.groundY); }, [] {});
        Button(354, RF(ec.left + 20, ey + 10, 250, 38), L"Save as sidecar override", I_CHECK, B_PRIMARY, [] { std::wstring e; if (g_app->SaveSidecarPhysical(e)) UiToast(L"Saved to the .divaverse.json beside the clip"); else UiToast(e); });
        C().Text(L"Clips saved with a density keep the same real-world size at any resolution.", RF(ec.left + 284, ey + 10, RW(ec) - 300, 38), 11.5f, P().faint);
        yy = ey + 60 + 24;
    }
    if (U.infoOpen) { SectionTitle(a.left, yy, L"MEDIA INFORMATION"); yy = MediaInfoCard(a.left, yy, RW(a)) + 20; }
    return yy;
}

// Query matching for the library and the song list: every word must appear, either as a substring or (3+ letters) as
// letters in order, so "lna swy" finds "Luna Sway" and a typo with a dropped letter still hits.
bool Fuzzy(const std::wstring& hayIn, const std::wstring& q) {
    if (q.empty()) return true;
    std::wstring hay = Lower(hayIn); size_t i = 0;
    while (i < q.size()) {
        while (i < q.size() && q[i] == L' ') ++i;
        size_t e = q.find(L' ', i); if (e == std::wstring::npos) e = q.size();
        if (e > i) {
            std::wstring tok = q.substr(i, e - i);
            if (hay.find(tok) == std::wstring::npos) {
                if (tok.size() < 3) return false;
                size_t k = 0, gaps = 0, last = 0; bool any = false;
                for (size_t h = 0; h < hay.size() && k < tok.size(); ++h) if (hay[h] == tok[k]) { if (any && h - last > 1) ++gaps; last = h; any = true; ++k; }
                if (k < tok.size() || gaps > tok.size() / 2 + 1) return false;
            }
        }
        i = e;
    }
    return true;
}
ID2D1Bitmap* SongCover(const std::wstring& path, int& budget) {        // album art, loaded lazily (a few per frame at most)
    static unsigned gen = 0; if (gen != C().Generation()) { U.songArt.clear(); gen = C().Generation(); }
    auto it = U.songArt.find(path); if (it != U.songArt.end()) return it->second.Get();
    if (budget <= 0) { U.animating = true; return nullptr; }
    --budget; ImageBGRA img; ComPtr<ID2D1Bitmap> b;
    if (Music::CoverOf(path, 256, img)) b = C().MakeBitmap(img.px.data(), img.w, img.h);
    U.songArt[path] = b; return b.Get();
}
void NoteTile(const RectF& r, float rad, int seed) { Backdrop(r, rad, seed); C().IconAt(I_MUSIC, (r.left + r.right) / 2, (r.top + r.bottom) / 2, RW(r) * 0.34f, Hex(0xFFFFFF, 0.75f), 1.4f); }
void PlayFile(int k) { App& A2 = *g_app; A2.set.musicMode = 1; A2.set.music = true; A2.music.Select(1, k); A2.music.SetEnabled(true); A2.UpdateMusicState(); A2.set.Save(); UiInvalidate(); }

float PageMusic(const RectF& a) {
    App& A = *g_app; Music& M = A.music; int budget = 2;
    float y = PageHeader(a, L"Music", L"Generated live by the built-in synthesiser, or your own collection");
    Toggle(400, a.right, a.top + 22, M.Enabled(), [] { g_app->OnCommand(ID_MUSIC_TOGGLE); });
    C().Text(M.Enabled() ? L"On" : L"Off", RF(a.right - 110, a.top + 8, 56, 28), 13, P().dim, 2);
    // now playing strip
    RectF np = RF(a.left, y, RW(a), 76); Card(np);
    float lvl = M.Enabled() ? M.Level() : 0.f; if (M.Enabled()) U.animating = true;
    auto& tr = M.Tracks(); int cur = M.CurrentFile(); ID2D1Bitmap* art = cur >= 0 && cur < (int)tr.size() ? SongCover(tr[cur].path, budget) : nullptr;
    if (art) C().BitmapCover(art, RF(np.left + 12, np.top + 10, 56, 56), 10); else Equalizer(RF(np.left + 20, np.top + 16, 52, 44), 7, lvl, M.Enabled() && !A.MusicPaused());
    if (art) Equalizer(RF(np.left + 18, np.top + 44, 44, 18), 6, lvl, M.Enabled() && !A.MusicPaused());
    C().Text(!M.Enabled() ? L"Music is off" : A.MusicPaused() ? M.NowPlaying() + L"  (paused)" : M.NowPlaying(), RF(np.left + 84, np.top + 14, RW(np) - 480, 24), 15, P().text, 0, true);
    C().Text(!M.LastError().empty() ? M.LastError() : M.Mode() == 0 ? L"Procedural station, never repeats. Media keys work here." : L"From: " + M.ScopeName() + (M.Shuffle() ? L"  (weighted shuffle)" : L""), RF(np.left + 84, np.top + 40, RW(np) - 480, 20), 12, M.LastError().empty() ? P().dim : P().danger);
    C().IconAt(I_MUSIC, np.right - 358, np.top + 38, 15, P().dim);
    Slider(401, RF(np.right - 338, np.top + 8, 190, 60), M.Volume(), [](float v) { g_app->music.SetVolume(v); g_app->set.musicVolume = v; UiInvalidate(); }, [] { g_app->set.Save(); });
    Button(404, RF(np.right - 118, np.top + 20, 34, 36), L"", I_PREV, B_GHOST, [] { g_app->OnCommand(ID_MUSIC_PREV); }, M.Enabled());
    Button(403, RF(np.right - 82, np.top + 20, 34, 36), L"", A.MusicPaused() ? I_PLAY : I_PAUSE, B_GHOST, [] { g_app->OnCommand(ID_MUSIC_PAUSE); }, M.Enabled());
    Button(402, RF(np.right - 46, np.top + 20, 34, 36), L"", I_NEXT, B_GHOST, [] { g_app->OnCommand(ID_MUSIC_NEXT); }, M.Enabled());
    y += 96;
    Segmented(410, RF(a.left, y, 440, 38), { L"Generated", L"Albums", L"Songs", L"Playlists" }, U.musicTab, [](int k) { U.musicTab = k; U.scroll[PG_MUSIC] = 0; UiInvalidate(); });
    if (U.musicTab > 0) {
        C().Text(L"Shuffle", RF(a.right - 130, y, 70, 38), 13, P().dim, 2);
        Toggle(451, a.right, y + 19, M.Shuffle(), [] { g_app->set.musicShuffle = !g_app->music.Shuffle(); g_app->music.SetShuffle(g_app->set.musicShuffle); g_app->set.Save(); UiInvalidate(); });
        C().Text(L"Even loudness", RF(a.right - 310, y, 116, 38), 13, P().dim, 2);
        Toggle(452, a.right - 140, y + 19, M.Normalize(), [] { g_app->set.musicNormalize = !g_app->music.Normalize(); g_app->music.SetNormalize(g_app->set.musicNormalize); g_app->set.Save(); UiInvalidate(); });
    }
    y += 56;
    if (U.musicTab == 0) {
        int n; const SynthStation* st = SynthStations(&n);
        const float cw = (RW(a) - 16) / 2, ch = 72;
        for (int i = 0; i < n; ++i) {
            RectF r = RF(a.left + (i % 2) * (cw + 16), y + (float)(i / 2) * (ch + 14), cw, ch);
            bool on = M.Mode() == 0 && M.Station() == i && M.Enabled(); int id = 420 + i; float h = Anim(id, Hot(id) ? 1.f : 0.f);
            C().FillRR(r, 14, Mix(P().card, P().accent, on ? 0.14f : 0.05f * h)); C().StrokeRR(r, 14, on ? P().accent : P().border, on ? 1.6f : 1.f);
            C().Circle(r.left + 36, r.top + 36, 20, on ? P().accent : Alpha(P().accent, 0.14f + 0.1f * h));
            C().IconAt(on ? I_MUSIC : I_PLAY, r.left + 36 + (on ? 0 : 1.5f), r.top + 36, 16, on ? P().accentText : P().accent, 1.5f);
            C().Text(st[i].name, RF(r.left + 70, r.top + 13, cw - 150, 24), 14.5f, P().text, 0, true);
            C().Text(st[i].mood, RF(r.left + 70, r.top + 38, cw - 150, 20), 12, P().dim);
            C().Text(Fmt(L"%d BPM", st[i].bpm), RF(r.right - 90, r.top, 74, ch), 12, P().faint, 2);
            AddHit(r, id, [i] { App& A2 = *g_app; A2.set.musicMode = 0; A2.set.station = i; A2.set.music = true; A2.music.Select(0, i); A2.music.SetEnabled(true); A2.UpdateMusicState(); A2.set.Save(); UiInvalidate(); });
        }
        return y + (float)((n + 1) / 2) * (ch + 14) + 20;
    }
    // ---- own collection: import row shared by the three tabs
    Button(450, RF(a.left, y, 150, 36), L"Add folder", I_FOLDER, B_SOFT, [] { std::wstring d = PickFolder(); if (d.empty()) return; int n = g_app->music.AddFolder(d); UiToast(n ? Fmt(L"%d songs added (sub-folders included)", n) : std::wstring(L"No new songs in that folder")); });
    Button(453, RF(a.left + 160, y, 124, 36), L"Add files", I_ADD, B_GHOST, [] {
        auto p = PickFiles(L"Audio (*.mp3;*.m4a;*.aac;*.wav;*.flac;*.wma;*.ogg;*.opus)\0*.mp3;*.m4a;*.aac;*.wav;*.flac;*.wma;*.ogg;*.opus\0All files\0*.*\0");
        if (g_app->music.AddFiles(p)) UiInvalidate(); });
    C().Text(Fmt(L"%d songs, %d albums", (int)tr.size(), (int)M.Albums().size()), RF(a.left + 296, y, 240, 36), 12.5f, P().faint);
    y += 52;
    if (tr.empty()) { C().Text(L"Nothing here yet. Add a folder: every sub-folder is searched, and albums are rebuilt from the files' own tags.", RF(a.left, y, RW(a), 40), 13, P().dim, 0, false, false, true); return y + 60; }
    if (U.musicTab == 1) {                              // albums
        auto& al = M.Albums(); float cw = 150, gap = 16; int cols = std::max(1, (int)((RW(a) + gap) / (cw + gap)));
        for (size_t i = 0; i < al.size(); ++i) {
            RectF r = RF(a.left + (float)(i % cols) * (cw + gap), y + (float)(i / cols) * (cw + 54 + gap), cw, cw);
            if (r.top > C().H() || r.bottom + 54 < 0) continue;
            int id = 6000 + (int)i; float h = Anim(id, Hot(id) ? 1.f : 0.f); bool on = M.Mode() == 1 && M.ScopeKind() == 1 && M.ScopeIndex() == (int)i && M.Enabled();
            RectF rr = RF(r.left, r.top - 3 * h, cw, cw); C().Shadow(rr, 12, 4 + 8 * h, P().dark ? 0.5f : 0.2f);
            if (ID2D1Bitmap* b = SongCover(tr[al[i].tracks[0]].path, budget)) C().BitmapCover(b, rr, 12, 1.f, 0.5f, 1.f + 0.04f * h); else NoteTile(rr, 12, (int)i);
            if (on) { C().GradStrokeRR(rr, 12, Hex(kPink), Hex(kCyan), 2.f, (float)U.t * 50.f); U.animating = true; } else if (h > 0.01f) C().StrokeRR(rr, 12, Alpha(P().accent, h), 1.5f);
            C().Text(al[i].name, RF(r.left, r.bottom + 6, cw, 20), 13, P().text, 0, true);
            C().Text((al[i].artist.empty() ? std::wstring() : al[i].artist + L"   ") + Fmt(L"%d", (int)al[i].tracks.size()), RF(r.left, r.bottom + 26, cw, 18), 11.5f, P().dim);
            int k = (int)i, first = al[i].tracks[0]; AddHit(rr, id, [k, first] { g_app->music.SetScope(1, k); PlayFile(first); });
        }
        return y + (float)((al.size() + cols - 1) / cols) * (cw + 54 + gap) + 20;
    }
    if (U.musicTab == 3) {                              // playlists
        auto& pl = M.Playlists();
        Button(470, RF(a.right - 290, y - 52, 140, 36), L"New playlist", I_ADD, B_SOFT, [] { U.plSel = g_app->music.NewPlaylist(); UiInvalidate(); });
        Button(471, RF(a.right - 140, y - 52, 140, 36), L"Import", I_FOLDER, B_GHOST, [] {
            auto p = PickFiles(L"Playlists (*.m3u;*.m3u8;*.pls;*.wpl)\0*.m3u;*.m3u8;*.pls;*.wpl\0All files\0*.*\0");
            for (auto& f : p) { int i = g_app->music.ImportPlaylist(f); if (i >= 0) { U.plSel = i; UiToast(Fmt(L"Imported %d songs", (int)g_app->music.Playlists()[i].paths.size())); } } UiInvalidate(); });
        if (pl.empty()) { C().Text(L"No playlists. Create one, then use the + on a song in Songs to add it. M3U, M3U8, PLS (VLC, Winamp, foobar2000, Apple Music export) and WPL (Windows Media Player) files can be imported.", RF(a.left, y, RW(a), 44), 13, P().dim, 0, false, false, true); return y + 70; }
        if (U.plSel >= (int)pl.size()) U.plSel = (int)pl.size() - 1;
        float cx = a.left;
        for (size_t i = 0; i < pl.size(); ++i) {
            std::wstring nm = pl[i].name + Fmt(L"  (%d)", (int)pl[i].paths.size()); float tw = C().TextW(nm, 12.5f) + 28;
            if (cx + tw > a.right) { cx = a.left; y += 36; }
            RectF chip = RF(cx, y, tw, 28); bool sel = U.plSel == (int)i;
            if (sel) C().Grad3RR(chip, 14, Hex(kPink), P().accent, Hex(kViolet), 0.f); else C().FillRR(chip, 14, Mix(P().card, P().text, Hot(480 + (int)i) ? 0.15f : 0.07f));
            C().Text(nm, chip, 12.5f, sel ? Hex(0xFFFFFF) : P().text, 1, sel);
            int k = (int)i; AddHit(chip, 480 + (int)i, [k] { U.plSel = k; UiInvalidate(); });
            cx += tw + 8;
        }
        y += 44;
        if (U.plSel >= 0) {
            int sel = U.plSel; auto& L = pl[sel];
            Button(472, RF(a.left, y, 120, 34), L"Play", I_PLAY, B_PRIMARY, [sel] { auto& M2 = g_app->music; auto& l = M2.Playlists()[sel]; for (auto& p : l.paths) { int t = M2.FindTrack(p); if (t >= 0) { M2.SetScope(2, sel); PlayFile(t); return; } } UiToast(L"This playlist is empty"); });
            Button(473, RF(a.left + 130, y, 120, 34), L"Delete", I_TRASH, B_DANGER, [sel] { g_app->music.DeletePlaylist(sel); U.plSel = -1; UiInvalidate(); });
            C().Text(L"Saved as " + L.name + L".m3u8 in the Divaverse Playlists folder", RF(a.left + 264, y, RW(a) - 264, 34), 11.5f, P().faint);
            y += 46;
            if (!L.paths.empty()) { RectF card = RF(a.left, y, RW(a), L.paths.size() * 42.f); Card(card);
                for (size_t i = 0; i < L.paths.size(); ++i, y += 42) {
                    if (y > C().H() || y + 42 < 0) continue;
                    int t = M.FindTrack(L.paths[i]); bool on = t >= 0 && t == cur && M.Enabled();
                    C().Text(t >= 0 ? tr[t].name + (tr[t].artist.empty() ? L"" : L"  -  " + tr[t].artist) : L.paths[i], RF(card.left + 20, y, RW(card) - 80, 42), 13, t >= 0 ? (on ? P().accent : P().text) : P().faint, 0, on);
                    int k = (int)i; if (t >= 0) AddHit(RF(card.left, y, RW(card) - 60, 42), 7000 + (int)i * 2, [sel, t] { g_app->music.SetScope(2, sel); PlayFile(t); });
                    Button(7001 + (int)i * 2, RF(card.right - 46, y + 5, 32, 32), L"", I_CLOSE, B_GHOST, [sel, k] { g_app->music.RemoveFromPlaylist(sel, k); UiInvalidate(); });
                } }
        }
        return y + 30;
    }
    // ---- songs
    SearchBox(RF(a.right - 250, y - 52, 250, 36));
    auto& pl = M.Playlists(); bool canAdd = U.plSel >= 0 && U.plSel < (int)pl.size();
    C().Text(canAdd ? L"+ adds to " + pl[U.plSel].name : L"Pick a playlist in Playlists to enable +", RF(a.left + 296 + 250, y - 52, RW(a) - 820, 36), 11.5f, P().faint);
    std::wstring q = Lower(U.search); std::vector<int> rows;
    for (int i = 0; i < (int)tr.size(); ++i) if (Fuzzy(tr[i].name + L" " + tr[i].artist + L" " + tr[i].album, q)) rows.push_back(i);
    if (rows.empty()) { C().Text(L"Nothing matches.", RF(a.left, y + 10, RW(a), 30), 13, P().dim, 1); return y + 60; }
    RectF card = RF(a.left, y, RW(a), rows.size() * 50.f); Card(card);
    for (size_t n = 0; n < rows.size(); ++n, y += 50) {
        if (y > C().H() || y + 50 < 0) continue;          // only the visible rows are drawn: thousands of songs stay cheap
        int i = rows[n]; const MusicTrack& t = tr[i]; RectF r = RF(card.left, y, RW(card), 50); int id = 10000 + i * 5; bool on = cur == i && M.Enabled();
        if (Hot(id)) C().FillRR(Inset(r, 3), 10, P().hover);
        C().IconAt(on ? I_MUSIC : I_PLAY, r.left + 26, y + 25, 14, on ? P().accent : P().dim, 1.5f);
        C().Text(t.name, RF(r.left + 50, y + 6, RW(r) - 330, 22), 13.5f, t.enabled ? (on ? P().accent : P().text) : P().faint, 0, on);
        C().Text((t.artist.empty() ? std::wstring() : t.artist + L"   ") + t.album, RF(r.left + 50, y + 26, RW(r) - 330, 18), 11.5f, P().faint);
        // how often shuffle will pick it (learned from skips and replays)
        { float w = t.fav ? 1.f : std::clamp((t.weight - 0.15f) / 2.35f, 0.f, 1.f), bx = r.right - 262; C().FillRR(RF(bx, y + 23, 60, 4), 2, Alpha(P().text, 0.12f)); C().Grad3RR(RF(bx, y + 23, std::max(4.f, 60 * w), 4), 2, Hex(kPink), P().accent, Hex(kViolet), 0.f); }
        AddHit(RF(r.left, y, RW(r) - 280, 50), id, [i] { g_app->music.SetScope(0, 0); PlayFile(i); });
        Button(id + 1, RF(r.right - 190, y + 9, 32, 32), L"", t.fav ? I_HEART : I_HEARTO, B_GHOST, [i] { g_app->music.ToggleFav(i); UiInvalidate(); });
        Button(id + 2, RF(r.right - 154, y + 9, 32, 32), L"", I_ADD, B_GHOST, [i] { int l = U.plSel; g_app->music.AddToPlaylist(l, i); UiToast(L"Added to " + g_app->music.Playlists()[l].name); }, canAdd);
        Toggle(id + 3, r.right - 62, y + 25, t.enabled, [i] { g_app->music.Toggle(i); UiInvalidate(); });
        Button(id + 4, RF(r.right - 48, y + 9, 32, 32), L"", I_TRASH, B_GHOST, [i] { g_app->music.Remove(i); UiInvalidate(); });
        if (n + 1 < rows.size()) C().Line(r.left + 50, y + 50, r.right - 16, y + 50, P().border, 1);
    }
    return y + 30;
}

float PageSettings(const RectF& a) {
    App& A = *g_app;
    float y = PageHeader(a, L"Settings", L"");
    y -= 24;
    SectionTitle(a.left, y, L"APPEARANCE");
    { RectF card = RF(a.left, y, RW(a), 62 * 5); Card(card); float yy = y;
      Row(card, yy, A.set.theme == 2 ? I_MOON : A.set.theme == 3 ? I_HEART : I_SUN, L"Theme", L"Applies to this window, the menus and dialogs");
      Segmented(500, RF(card.right - 356, y + 12, 340, 38), { L"Windows", L"Light", L"Neon", L"Pink" }, A.set.theme, [](int k) { g_app->SetTheme(k); });
      Row(card, yy, I_SPARKLE, L"Animated backdrop", L"Drifting lights and sparkles behind the pages");
      Toggle(501, card.right - 20, yy - 31, A.set.fx, [] { g_app->set.fx = !g_app->set.fx; g_app->set.Save(); UiInvalidate(); });
      Row(card, yy, I_STAR, L"Seasonal decorations", L"Snow and ornaments in winter, petals in spring, leaves in autumn");
      Toggle(504, card.right - 20, yy - 31, A.set.seasonal, [] { g_app->set.seasonal = !g_app->set.seasonal; g_app->set.Save(); UiInvalidate(); });
      Row(card, yy, I_RULER, L"Editor mode", L"Shows the clip's authored scale on On Stage and lets you correct it with a sidecar");
      Toggle(505, card.right - 20, yy - 31, A.set.editor, [] { g_app->set.editor = !g_app->set.editor; g_app->set.Save(); UiInvalidate(); });
      Row(card, yy, I_MUSIC, L"Interface sounds", L"Synthesised clicks and chimes for buttons, pages and the performer wheel", false);
      Slider(503, RF(card.right - 290, yy - 62, 190, 62), A.set.sfxVol, [](float v) { g_app->set.sfxVol = v; SfxConfigure(g_app->set.sfx, v); UiInvalidate(); }, [] { g_app->set.Save(); SfxPlay(SFX_CLICK); });
      Toggle(502, card.right - 20, yy - 31, A.set.sfx, [] { g_app->set.sfx = !g_app->set.sfx; SfxConfigure(g_app->set.sfx, g_app->set.sfxVol); g_app->set.Save(); UiInvalidate(); });
      y += 62 * 5 + 26; }
    SectionTitle(a.left, y, L"GENERAL");
    { RectF card = RF(a.left, y, RW(a), 62 * 2); Card(card); float yy = y; bool as = App::Autostart();
      Row(card, yy, I_POWER, L"Start with Windows", L"Opens quietly in the tray and brings back your last performer");
      Toggle(510, card.right - 20, yy - 31, as, [as] { App::SetAutostart(!as); UiInvalidate(); });
      Row(card, yy, I_WINDOW, L"Hide during full-screen apps", L"Games, videos and presentations get the screen to themselves", false);
      Toggle(511, card.right - 20, yy - 31, A.set.autoHide, [] { g_app->SetAutoHide(!g_app->set.autoHide); });
      y += 62 * 2 + 26; }
    SectionTitle(a.left, y, L"VIDEO");
    { RectF card = RF(a.left, y, RW(a), 62); Card(card); float yy = y;
      Row(card, yy, I_CHIP, L"Decode on the graphics card", A.set.hwBad ? L"Off: GPU decoding stalled on this PC. Turning it on tries again." : L"Experimental. The CPU decoder is the default and is what gets tested.", false);
      Toggle(520, card.right - 20, yy - 31, A.set.decodeMode == 1, [] { g_app->SetDecodeMode(g_app->set.decodeMode == 1 ? 0 : 1); });
      y += 62 + 26; }
    SectionTitle(a.left, y, L"LIBRARY FOLDERS");
    { auto& f = A.set.folders; RectF card = RF(a.left, y, RW(a), 52.f * (f.size() + 2)); Card(card); float yy = y;
      Row(card, yy, I_FOLDER, L"Divas folder next to Divaverse", L"");
      Button(528, RF(card.right - 126, yy - 44, 110, 36), L"Open", I_FOLDER, B_GHOST, [] { wchar_t b[MAX_PATH * 2]; GetModuleFileNameW(nullptr, b, MAX_PATH * 2); std::wstring d = JoinPath(DirOf(b), L"Divas"); CreateDirectoryW(d.c_str(), nullptr); ShellExecuteW(nullptr, L"open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL); });
      for (size_t i = 0; i < f.size(); ++i) { Row(card, yy, I_FOLDER, f[i], L""); int k = (int)i;
          Button(530 + (int)i, RF(card.right - 48, yy - 42, 32, 32), L"", I_TRASH, B_GHOST, [k] { g_app->RemoveLibraryFolder(k); }); }
      Row(card, yy, I_ADD, L"Add a folder", L"", false);
      Button(529, RF(card.right - 126, yy - 44, 110, 36), L"Browse", I_FOLDER, B_SOFT, [] { std::wstring d = PickFolder(); if (!d.empty()) g_app->AddLibraryFolder(d); });
      y = yy + 26; }
    SectionTitle(a.left, y, L"SHORTCUTS");
    { const int n = HA_COUNT + 1; RectF card = RF(a.left, y, RW(a), 44.f * n + 56); Card(card);
      for (int i = 0; i < n; ++i) {
          bool media = i == HA_COUNT; bool cap = !media && U.hkCapture == i;
          std::wstring keys = media ? L"Media keys" : cap ? L"Press the new keys...  Esc cancels, Backspace clears" : HotkeyText(A.set.hotkeys[i]);
          float tw = C().TextW(keys, 12, true) + 22; RectF kc = RF(card.right - 18 - tw, y + 44 * i + 10, tw, 24);
          C().Text(media ? L"Music: play / pause, next, previous, stop" : HotActionName(i), RF(card.left + 22, y + 44 * i, RW(card) - tw - 60, 44), 13.5f, P().text);
          C().FillRR(kc, 7, cap ? Alpha(P().accent, 0.22f) : Mix(P().card, P().text, Hot(560 + i) ? 0.14f : 0.08f)); C().Text(keys, kc, 12, cap || i == HA_HIDE ? P().accent : P().dim, 1, true);
          if (cap) { C().StrokeRR(kc, 7, P().accent, 1.2f); U.animating = true; }
          if (!media) AddHit(RF(card.left, y + 44 * i, RW(card), 44), 560 + i, [i] { U.hkCapture = i; UiInvalidate(); });
          C().Line(card.left + 22, y + 44 * (i + 1), card.right - 16, y + 44 * (i + 1), P().border, 1);
      }
      Button(569, RF(card.left + 20, y + 44 * n + 10, 170, 34), L"Restore defaults", I_REFRESH, B_GHOST, [] { int bad = 0;
          for (int k = 0; k < HA_COUNT; ++k) g_app->SetHotkey(k, 0);
          for (int k = 0; k < HA_COUNT; ++k) if (!g_app->SetHotkey(k, HotkeyDefault(k))) ++bad;
          UiToast(bad ? Fmt(L"%d default shortcuts are taken by other programs", bad) : std::wstring(L"Shortcuts restored")); UiInvalidate(); });
      C().Text(L"Click a shortcut, then press the new combination.", RF(card.left + 204, y + 44 * n + 10, RW(card) - 224, 34), 11.5f, P().faint);
      y += 44.f * n + 56 + 26; }
    SectionTitle(a.left, y, L"SCHEDULE");
    { auto& S = A.set; bool on = S.schedFrom >= 0 && S.schedTo >= 0; RectF card = RF(a.left, y, RW(a), 62.f * 2 + (on ? 52 : 0)); Card(card); float yy = y;
      auto stepper = [&](int id, float right, float cy, const std::wstring& text, std::function<void(int)> fn) {     // [-] value [+], `right` = its right edge
          Button(id, RF(right - 150, cy - 16, 32, 32), L"-", -1, B_SOFT, [fn] { fn(-1); });
          C().Text(text, RF(right - 116, cy - 16, 82, 32), 13, P().text, 1, true);
          Button(id + 1, RF(right - 32, cy - 16, 32, 32), L"+", -1, B_SOFT, [fn] { fn(1); }); };
      Row(card, yy, I_SUN, L"On stage only between set hours", on ? Fmt(L"%02d:00 to %02d:00; off stage the rest of the day", S.schedFrom, S.schedTo) : std::wstring(L"Off: she stays whenever you put her on stage"));
      Toggle(570, card.right - 20, yy - 31, on, [on] { auto& T = g_app->set; if (on) { T.schedFrom = T.schedTo = -1; } else { T.schedFrom = 18; T.schedTo = 23; } T.Save(); UiInvalidate(); });
      if (on) {
          Row(card, yy, I_REFRESH, L"Hours", L"");
          C().Text(L"from", RF(card.right - 420, yy - 52, 50, 52), 12, P().dim, 2);
          stepper(571, card.right - 210, yy - 26, Fmt(L"%02d:00", S.schedFrom), [](int d) { auto& T = g_app->set; T.schedFrom = (T.schedFrom + d + 24) % 24; T.Save(); UiInvalidate(); });
          C().Text(L"to", RF(card.right - 200, yy - 52, 24, 52), 12, P().dim, 2);
          stepper(573, card.right - 20, yy - 26, Fmt(L"%02d:00", S.schedTo), [](int d) { auto& T = g_app->set; T.schedTo = (T.schedTo + d + 24) % 24; T.Save(); UiInvalidate(); });
      }
      Row(card, yy, I_MOON, L"Come out while the PC is idle", S.idleShow ? Fmt(L"After %d min without mouse or keyboard; back off stage when you return", S.idleShow) : std::wstring(L"Off"), false);
      stepper(575, card.right - 20, yy - 31, S.idleShow ? Fmt(L"%d min", S.idleShow) : L"Off", [](int d) { auto& T = g_app->set; static const int st[] = { 0, 1, 2, 5, 10, 15, 30, 60 }; int k = 0; for (int q = 0; q < 8; ++q) if (st[q] <= T.idleShow) k = q; k = std::clamp(k + d, 0, 7); T.idleShow = st[k]; T.Save(); UiInvalidate(); });
      y = yy + 26; }
    SectionTitle(a.left, y, L"DIAGNOSTICS");
    { std::wstring d = A.DiagText(); int lines = 1; for (wchar_t ch : d) if (ch == L'\n') ++lines;
      float th = lines * 19.f + 8; RectF card = RF(a.left, y, RW(a), th + 78); Card(card);
      C().Text(d, RF(card.left + 20, y + 14, RW(card) - 40, th), 12.5f, P().dim, 0, false, false, true);
      float by = y + th + 26;
      Button(540, RF(card.left + 20, by, 140, 36), L"Copy report", I_COPY, B_SOFT, [] {
          std::wstring s = g_app->DiagText() + L"\r\n--- log ---\r\n"; for (auto& l : LogTail(120)) s += Utf8ToW(l) + L"\r"; CopyText(s); UiToast(L"Report copied to the clipboard"); });
      Button(541, RF(card.left + 170, by, 150, 36), L"Open log folder", I_FOLDER, B_GHOST, [] { ShellExecuteW(nullptr, L"open", LogDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); });
      Button(542, RF(card.left + 330, by, 180, 36), L"Export diagnostics", I_FOLDER, B_SOFT, [] { std::wstring d = g_app->ExportDiagnostics();
          if (d.empty()) UiToast(L"Could not write the diagnostics folder"); else { ShellExecuteW(nullptr, L"open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL); UiToast(L"Diagnostics collected"); } });
      Button(543, RF(card.left + 520, by, 190, 36), A.set.perfHud ? L"Hide performance" : L"Show performance", I_CHIP, B_GHOST, [] { g_app->SetPerfHud(!g_app->set.perfHud); });
      Button(544, RF(card.left + 720, by, 190, 36), L"Record GPU trace", I_CHIP, B_GHOST, [] { g_app->StartGpuTrace(); }, A.OnStage());
      y += th + 78 + 26; }
    C().Text(L"Divaverse " DV_VERSION_W, RF(a.left, y, RW(a), 24), 12, P().faint);
    return y + 40;
}

// Slow lights and sparkles behind everything. Positions are pure functions of time: no state, no allocations.
void Ambience(const RectF& view) {
    float fx = Fx(); if (fx <= 0) return;
    float t = (float)U.t, W = RW(view), H = RH(view), k = P().dark ? 1.f : 1.5f;      // light surfaces need more pigment for the same visible tint
    C().Glow(view.left + W * (0.22f + 0.10f * std::sin(t * 0.11f)), H * (0.18f + 0.08f * std::cos(t * 0.09f)), W * 0.55f, H * 0.60f, Hex(kPink, 0.085f * k));
    C().Glow(view.left + W * (0.80f + 0.08f * std::cos(t * 0.07f)), H * (0.72f + 0.10f * std::sin(t * 0.10f)), W * 0.60f, H * 0.65f, Hex(kViolet, 0.11f * k));
    C().Glow(view.left + W * (0.62f + 0.12f * std::sin(t * 0.05f + 2.f)), H * (0.10f + 0.06f * std::sin(t * 0.13f)), W * 0.40f, H * 0.40f, Hex(kCyan, 0.055f * k));
    // drifting particles: sparkles by default, hearts in the pink theme, and the season's own shapes
    int month = Month(), kind = 0;                       // 0 sparkle, 1 heart, 2 snow, 3 petal, 4 leaf
    bool season = g_app->set.seasonal;
    if (g_app->set.theme == 3) kind = 1;
    if (season) { if (month == 12 || month == 1) kind = 2; else if (month == 2) kind = 1; else if (month >= 3 && month <= 5) kind = 3; else if (month >= 9 && month <= 11) kind = 4; }
    int count = kind == 2 ? 46 : kind == 0 ? 22 : 30;
    for (int i = 0; i < count; ++i) {
        float s = i * 12.9898f, px = view.left + W * (0.5f + 0.5f * std::sin(s * 1.7f)), sp = (kind == 0 ? 0.012f : 0.022f) + 0.014f * (0.5f + 0.5f * std::sin(s * 3.1f));
        float ph = std::fmod(t * sp + (0.5f + 0.5f * std::sin(s * 5.3f)), 1.f), tw = 0.5f + 0.5f * std::sin(t * (1.2f + (i % 5) * 0.4f) + s);
        bool up = kind <= 1; float py = up ? H * (1.05f - 1.1f * ph) : H * (-0.05f + 1.1f * ph), x = px + (kind >= 2 ? 30.f : 14.f) * std::sin(t * 0.3f + s);
        float a = (P().dark ? 0.55f : 0.42f) * (0.5f + 0.5f * tw) * std::sin(ph * 3.14159f), size = 5 + 7 * (0.5f + 0.5f * std::sin(s * 7.7f));
        switch (kind) {
        case 1: C().IconAt(I_HEART, x, py, size * 1.9f, Alpha(i % 3 ? Hex(kPink) : Hex(0xFF8FC8), a), 1.f); break;
        case 2: { Color c = P().dark ? Hex(0xFFFFFF, a) : Hex(0x7FB4E8, a); if (i % 3) C().Circle(x, py, size * 0.28f, c); else for (int k = 0; k < 3; ++k) { float an = k * 1.0472f + t * 0.2f + s; C().Line(x - size * std::cos(an), py - size * std::sin(an), x + size * std::cos(an), py + size * std::sin(an), c, 1.1f); } break; }
        case 3: C().SetTransform(D2D1::Matrix3x2F::Rotation(t * 20 + s * 40, { x, py })); C().FillRR(RF(x - size * 0.9f, py - size * 0.45f, size * 1.8f, size * 0.9f), size * 0.45f, Alpha(i % 2 ? Hex(0xFFB7D5) : Hex(0xFFFFFF), a)); C().ResetTransform(); break;
        case 4: C().SetTransform(D2D1::Matrix3x2F::Rotation(t * 25 + s * 40, { x, py })); C().FillRR(RF(x - size, py - size * 0.5f, size * 2, size), size * 0.5f, Alpha(i % 3 == 0 ? Hex(0xD9532B) : i % 3 == 1 ? Hex(0xE8A13A) : Hex(0x9C5A2B), a)); C().Line(x - size, py, x + size, py, Hex(0x5A3418, a), 1); C().ResetTransform(); break;
        default:
            if (i % 4 == 0) C().IconAt(I_SPARKLE, x, py, 9 + 5 * tw, Alpha(i % 8 ? Hex(kPink) : Hex(kCyan), a), 1.f);
            else C().Circle(x, py, 1.1f + 1.2f * tw, Alpha(i % 3 ? (P().dark ? Hex(0xFFFFFF) : Hex(kViolet)) : Hex(kPink), a));
        }
    }
    if (season && (month == 12 || month == 1)) {        // a small winter scene in the corner and ornaments along the top
        float bx = view.right - 170, by = H - 14, al = 0.9f;
        for (int k = 0; k < 3; ++k) { float top = by - 150 + k * 34.f, hw = 26 + k * 13.f, hh = 56 + k * 4.f; C().Tri(bx, top, bx - hw, top + hh, bx + hw, top + hh, Hex(k % 2 ? 0x239A5E : 0x1F8F56, al)); }   // tree: three tiers
        C().FillR(RF(bx - 6, by - 14, 12, 14), Hex(0x6B4226, al)); C().Star(bx, by - 152, 9, Hex(0xFFC53D, al));
        static const uint32_t oc[] = { 0xFF4FA8, 0x4FD6FF, 0xFFC53D, 0xFF5A5A };
        for (int k = 0; k < 7; ++k) C().Circle(bx - 26 + (k * 37 % 52), by - 34 - (k * 29 % 84), 4, Hex(oc[k % 4], al * (0.6f + 0.4f * std::sin(t * 2 + k))));
        float sx = bx + 78; C().Circle(sx, by - 20, 22, Hex(0xFFFFFF, al)); C().Circle(sx, by - 52, 16, Hex(0xFFFFFF, al)); C().Circle(sx, by - 76, 11, Hex(0xFFFFFF, al));  // snowman
        C().Circle(sx - 4, by - 79, 1.4f, Hex(0x222222, al)); C().Circle(sx + 4, by - 79, 1.4f, Hex(0x222222, al)); C().Line(sx, by - 75, sx + 9, by - 73, Hex(0xFF8A2A, al), 2.4f);
        C().FillR(RF(sx - 12, by - 89, 24, 4), Hex(0x2A2233, al)); C().FillR(RF(sx - 8, by - 103, 16, 15), Hex(0x2A2233, al)); C().FillR(RF(sx - 12, by - 66, 24, 5), Hex(0xFF4FA8, al));
        for (int k = 0; k < 3; ++k) C().Circle(sx, by - 56 + k * 9.f, 1.6f, Hex(0x222222, al));
        float gx = bx - 74, gb = by - 6; Color gc = Hex(0xB06A32, al), ic = Hex(0xFFFFFF, al);                                                     // gingerbread man
        C().Circle(gx, gb - 46, 11, gc); C().FillRR(RF(gx - 9, gb - 37, 18, 24), 6, gc); C().FillRR(RF(gx - 22, gb - 34, 44, 8), 4, gc); C().FillRR(RF(gx - 10, gb - 16, 8, 16), 4, gc); C().FillRR(RF(gx + 2, gb - 16, 8, 16), 4, gc);
        C().Circle(gx - 4, gb - 48, 1.3f, ic); C().Circle(gx + 4, gb - 48, 1.3f, ic); C().Line(gx - 4, gb - 42, gx + 4, gb - 42, ic, 1.2f); C().Circle(gx, gb - 30, 1.6f, Hex(0xFF4FA8, al)); C().Circle(gx, gb - 23, 1.6f, Hex(0x4FD6FF, al));
        for (int k = 0; k < 9; ++k) {                    // hanging ornaments
            float ox = view.left + W * (0.06f + 0.11f * k), len = 22 + 16 * (0.5f + 0.5f * std::sin(k * 2.3f)), sw = 4 * std::sin(t * 0.9f + k);
            C().Line(ox, 0, ox + sw, len, Alpha(P().text, 0.25f), 1); C().Circle(ox + sw, len + 9, 9, Hex(oc[k % 4], al)); C().Circle(ox + sw - 3, len + 6, 2.4f, Hex(0xFFFFFF, 0.55f)); C().FillR(RF(ox + sw - 2.5f, len - 2, 5, 4), Hex(0xFFC53D, al));
        }
    } else if (season && month == 10) {                  // pumpkins
        for (int k = 0; k < 3; ++k) { float bx = view.right - 150 + k * 46.f, by = H - 12 - (k == 1 ? 6 : 0), r = k == 1 ? 24.f : 18.f;
            C().Circle(bx - r * 0.45f, by - r, r * 0.8f, Hex(0xE8742A, 0.9f)); C().Circle(bx + r * 0.45f, by - r, r * 0.8f, Hex(0xE8742A, 0.9f)); C().Circle(bx, by - r, r * 0.85f, Hex(0xF08A30, 0.95f));
            C().FillR(RF(bx - 2, by - r * 2 - 4, 4, 8), Hex(0x4E7A2A, 0.95f)); C().Circle(bx - r * 0.3f, by - r * 1.1f, 2.6f, Hex(0x2A1408, 0.9f)); C().Circle(bx + r * 0.3f, by - r * 1.1f, 2.6f, Hex(0x2A1408, 0.9f)); C().Arc(bx, by - r * 0.95f, r * 0.38f, 20, 160, Hex(0x2A1408, 0.9f), 2.4f); }
    }
}

// "You got something new": shown once per new diva / new clips after a scan. A gift box shakes, bursts, and the card
// rises out of it. Click to skip ahead; nothing is staged unless the button is pressed.
void LootCheck() {
    App& A = *g_app; if (U.lootGen == A.lib.Generation()) return; U.lootGen = A.lib.Generation();
    if (!U.loot.empty() || A.SelfTesting()) return;
    for (auto& dv : A.lib.Divas()) { int fresh = 0; for (int c : dv.clips) fresh += !A.user.seen.count(dv.key + L"|" + A.lib.Clips()[c].desc.id);
        if (fresh) U.loot.push_back({ dv.key, dv.name, fresh, fresh == (int)dv.clips.size() }); }
    if (U.loot.size() > 6) {                             // a big first import is not an hour of unboxing: mark the rest seen
        for (size_t i = 6; i < U.loot.size(); ++i) { int d = DivaByKey(U.loot[i].key); if (d >= 0) for (int c : A.lib.Divas()[d].clips) A.user.seen.insert(U.loot[i].key + L"|" + A.lib.Clips()[c].desc.id); }
        U.loot.resize(6); A.user.dirty = true;
    }
    if (!U.loot.empty()) { U.lootT = NowSeconds(); U.lootBurst = false; SfxPlay(SFX_LOOT_SHAKE); }
}
void LootNext(bool stage) {
    if (U.loot.empty()) return;
    std::wstring key = U.loot.front().key; U.loot.erase(U.loot.begin());
    U.pendingLoot = [key, stage] {                       // runs after the frame: these take the library lock themselves
        App& A2 = *g_app; int d;
        { std::lock_guard<std::mutex> g(A2.lib.Mutex()); d = DivaByKey(key); if (d >= 0) for (int c : A2.lib.Divas()[d].clips) A2.user.seen.insert(key + L"|" + A2.lib.Clips()[c].desc.id); }
        A2.user.dirty = true; if (stage && d >= 0) Stage(d, 0);
    };
    U.lootT = NowSeconds(); U.lootBurst = false; if (!U.loot.empty()) SfxPlay(SFX_LOOT_SHAKE);
}
void LootPaint() {
    if (U.loot.empty()) return;
    Canvas& c = C(); auto& L = U.loot.front(); float t = (float)(U.t - U.lootT), W = c.W(), H = c.H(), cx = W / 2, cy = H / 2 + 10; U.animating = true;
    const float tb = 0.95f; float in = std::min(1.f, t / 0.25f);
    c.FillR(RF(0, 0, W, H), Hex(0x06030E, 0.78f * in));
    AddHit(RF(0, 0, W, H), 9000, [] { if (U.t - U.lootT < 0.95) U.lootT = U.t - 0.95; });       // click: skip the wind-up
    if (t < tb) {                                       // the box: drops in, shakes harder and harder, light leaks out
        float k = t / tb, shake = std::sin(t * 60) * 7 * k * k, drop = (1 - std::min(1.f, t / 0.22f)); drop = drop * drop * -260;
        c.Glow(cx, cy, 150 + 120 * k, 150 + 120 * k, Hex(kPink, 0.30f + 0.4f * k));
        c.SetTransform(D2D1::Matrix3x2F::Rotation(shake * 0.9f, { cx, cy + 60 }) * D2D1::Matrix3x2F::Translation(shake, drop));
        RectF box = RF(cx - 74, cy - 40, 148, 110), lid = RF(cx - 84, cy - 66 - 10 * k * k, 168, 34);
        c.Shadow(box, 14, 18, 0.6f); c.Grad3RR(box, 14, Hex(kViolet), Hex(0x6A3BE0), Hex(0x3A1E8A), 80.f); c.Grad3RR(lid, 10, Hex(0xA574FF), Hex(kViolet), Hex(0x5A2FD0), 80.f);
        c.FillR(RF(cx - 11, cy - 40, 22, 110), Hex(kPink)); c.FillR(RF(cx - 11, lid.top, 22, 34), Hex(0xFF7CC0));
        c.Ring(cx - 16, lid.top - 9, 13, Hex(kPink), 5); c.Ring(cx + 16, lid.top - 9, 13, Hex(kPink), 5);
        c.FillR(RF(box.left + 6, box.top - 3, RW(box) - 12, 5), Hex(0xFFF2A8, 0.5f + 0.5f * k));
        c.ResetTransform();
        return;
    }
    if (!U.lootBurst) { U.lootBurst = true; SfxPlay(SFX_LOOT); }
    float r = t - tb, pop = std::min(1.f, r / 0.45f), e = 1 - (1 - pop) * (1 - pop) * (1 - pop), over = 1 + 0.12f * std::sin(std::min(1.f, r / 0.6f) * 3.14159f);
    for (int i = 0; i < 12; ++i) { float a0 = (i * 30.f + r * 22) * 0.0174533f, a1 = a0 + 0.23f, R = 700 * e;                                                 // rotating rays
        c.Tri(cx, cy, cx + R * std::cos(a0), cy + R * std::sin(a0), cx + R * std::cos(a1), cy + R * std::sin(a1), Hex(i % 2 ? kPink : 0xFFE38A, 0.16f * e)); }
    c.Glow(cx, cy, 380, 380, Hex(0xFFFFFF, 0.5f * std::max(0.f, 1 - r / 0.5f) + 0.10f));
    c.Ring(cx, cy, 40 + 600 * std::min(1.f, r / 0.6f), Hex(0xFFFFFF, 0.6f * std::max(0.f, 1 - r / 0.6f)), 6);
    for (int i = 0; i < 40; ++i) {                      // confetti
        float s = i * 7.31f, an = s * 2.4f, v = 220 + 260 * (0.5f + 0.5f * std::sin(s * 3.3f)), x = cx + std::cos(an) * v * r, y = cy - 40 + std::sin(an) * v * r + 260 * r * r, al = std::max(0.f, 1 - r / 2.4f);
        static const uint32_t cc[] = { kPink, kCyan, 0xFFE38A, kViolet, 0xFFFFFF };
        c.SetTransform(D2D1::Matrix3x2F::Rotation(r * 300 + s * 50, { x, y })); c.FillR(RF(x - 4, y - 2, 8, 4), Hex(cc[i % 5], al)); c.ResetTransform();
    }
    int d = DivaByKey(L.key); float cw = 230 * e * over, ch = cw * 1.46f; RectF card = RF(cx - cw / 2, cy - ch / 2 - 30 * e, cw, ch);
    if (d >= 0 && cw > 20) { c.Shadow(card, 18, 24, 0.7f); c.FillRR(card, 18, P().card); DivaArt(d, card, 18, 0, false); c.GradStrokeRR(card, 18, Hex(0xFFE38A), Hex(kPink), 3.f, (float)U.t * 90.f); c.Sheen(card, 18, std::fmod(r * 0.7f, 1.6f) / 1.6f, 0.35f); }
    if (pop >= 1) {
        float ty = card.bottom + 18;
        c.Text(L.wholeNew ? L"NEW DIVA" : Fmt(L"%d NEW %ls", L.fresh, L.fresh == 1 ? L"CLIP" : L"CLIPS"), RF(0, card.top - 46, W, 24), 13, Hex(0xFFE38A), 1, true);
        c.Text(L.name, RF(0, ty, W, 40), 28, Hex(0xFFFFFF), 1, true);
        Button(9001, RF(cx - 190, ty + 52, 190, 42), L"Put on desktop", I_PLAY, B_PRIMARY, [] { LootNext(true); });
        Button(9002, RF(cx + 10, ty + 52, 180, 42), U.loot.size() > 1 ? Fmt(L"Next (%d more)", (int)U.loot.size() - 1) : std::wstring(L"Nice"), I_CHECK, B_SOFT, [] { LootNext(false); });
    }
}

void PaintAll() {
    if (!U.cv.Begin(U.h)) return;
    std::lock_guard<std::mutex> libLock(g_app->lib.Mutex());      // covers and thumbnails are filled in by the library worker
    U.t = NowSeconds(); U.hits.clear(); U.animating = false;
    Canvas& c = U.cv; const Palette& p = P();
    SyncBitmaps(); LootCheck();
    c.Clear(p.surface);
    RectF view = RF(RAIL + 1, 0, c.W() - RAIL - 1, c.H());
    c.PushClip(view); Ambience(view); c.PopClip();
    
    c.FillR(RF(0, 0, RAIL, c.H()), p.chrome);
    if (Fx() > 0) { c.PushClip(RF(0, 0, RAIL, c.H())); c.Glow(RAIL * 0.3f, c.H() * (0.85f + 0.05f * std::sin((float)U.t * 0.2f)), RAIL * 1.2f, c.H() * 0.5f, Hex(kViolet, 0.16f)); c.PopClip(); }
    c.Line(RAIL, 0, RAIL, c.H(), p.border, 1);
    { RectF lr = RF(12, 14, RAIL - 24, (RAIL - 24) * 143.f / 525.f);            // the Divaverse wordmark (embedded artwork) on its own dark plate
      if (U.logo) { c.BitmapCover(U.logo.Get(), lr, 12); c.StrokeRR(lr, 12, Hex(0xFFFFFF, p.dark ? 0.08f : 0.f), 1); }
      else { c.Logo(38, 40, 34); c.Text(L"Divaverse", RF(64, 22, 140, 36), 18, p.text, 0, true); } }
    static const struct { const wchar_t* name; Icon icon; } nav[PG_COUNT] = { { L"Divas", I_GRID }, { L"On Stage", I_STAGE }, { L"Music", I_MUSIC }, { L"Settings", I_GEAR } };
    const float navY = 92;
    float sel = Anim(90, (float)U.page, 18.f);
    { RectF sr = RF(12, navY + sel * 46, RAIL - 24, 40); c.Grad3RR(sr, 10, Hex(kPink, p.dark ? 0.26f : 0.18f), Alpha(p.accent, p.dark ? 0.20f : 0.14f), Hex(kViolet, 0.06f), 0.f);
      c.Grad3RR(RF(12, navY + 10 + sel * 46, 3.5f, 20), 2, Hex(kPink), p.accent, Hex(kViolet), 90.f); }
    for (int i = 0; i < PG_COUNT; ++i) {
        RectF r = RF(12, navY + i * 46.f, RAIL - 24, 40); int id = 10 + i; float h = Anim(id, Hot(id) && U.page != i ? 1.f : 0.f);
        if (h > 0.01f) c.FillRR(r, 10, Alpha(p.text, 0.06f * h));
        bool cur = U.page == i;
        c.IconAt(nav[i].icon, r.left + 26 + 2 * h, r.top + 20, 17, cur ? p.accent : p.dim, 1.5f);
        c.Text(nav[i].name, RF(r.left + 50 + 2 * h, r.top, RW(r) - 56, 40), 14, cur ? p.text : p.dim, 0, cur);
        if (i == PG_STAGE && g_app->OnStage()) c.Circle(r.right - 16, r.top + 20, 4, g_app->Paused() || g_app->Hidden() ? p.faint : p.good);
        if (i == PG_MUSIC && g_app->music.Enabled()) Equalizer(RF(r.right - 30, r.top + 12, 20, 16), 4, g_app->music.Level(), !g_app->MusicPaused());
        AddHit(r, id, [i] { GoPage(i); });
    }
    
    {   // arcade: the Wet-Rash mini game
        RectF r = RF(12, navY + (float)PG_COUNT * 46.f + 14, RAIL - 24, 54); int id = 98; float h = Anim(id, Hot(id) ? 1.f : 0.f);
        c.Grad3RR(r, 12, Hex(0x1E88E5, 0.55f + 0.25f * h), Hex(0x4FD6FF, 0.40f + 0.2f * h), Hex(kPink, 0.45f + 0.25f * h), 20.f); c.StrokeRR(r, 12, Hex(0xFFFFFF, 0.25f + 0.4f * h), 1);
        for (int k = 0; k < 3; ++k) c.Arc(r.left + 30 + k * 2.f, r.bottom + 6 - k * 7.f, 16, 205 + (float)std::sin(U.t * 2 + k) * 6, 335, Hex(0xFFFFFF, 0.55f), 2);
        c.Text(L"WET-RASH", RF(r.left + 56, r.top + 6, RW(r) - 60, 24), 14.5f, Hex(0xFFFFFF), 0, true); c.Text(L"jet-ski brawl racer", RF(r.left + 56, r.top + 28, RW(r) - 60, 18), 10.5f, Hex(0xFFFFFF, 0.85f));
        AddHit(r, id, [] { WetRashOpen(); });
    }
    if (g_app->OnStage()) {
        RectF r = RF(12, c.H() - 78, RAIL - 24, 64); c.FillRR(r, 14, Alpha(Mix(p.chrome, p.text, 0.06f), 0.95f)); c.StrokeRR(r, 14, p.border, 1);
        int sd = StageDiva(); RectF th = RF(r.left + 8, r.top + 8, 36, 48);
        if (sd >= 0) DivaArt(sd, th, 8, 0, false); else c.FillRR(th, 8, Alpha(p.accent, 0.2f));
        c.Text(g_app->Hidden() ? L"HIDDEN" : g_app->Paused() ? L"PAUSED" : L"ON STAGE", RF(th.right + 10, r.top + 10, 90, 16), 9.5f, p.faint, 0, true);
        c.Text(g_app->show.title, RF(th.right + 10, r.top + 27, RW(r) - (g_app->show.clips.size() > 1 ? 124 : 96), 24), 13, p.text, 0, true);
        RoundBtn(95, r.right - 22, r.top + 32, 13, g_app->Paused() ? I_PLAY : I_PAUSE, true, [] { g_app->SetPaused(!g_app->Paused()); });
        if (g_app->show.clips.size() > 1) RoundBtn(96, r.right - 52, r.top + 32, 11, I_NEXT, false, [] { g_app->OnCommand(ID_NEXT); });
        float pr = g_app->Progress(); c.FillRR(RF(r.left + 10, r.bottom - 5, (RW(r) - 20) * pr, 2), 1, Hex(kPink));
    }
    
    c.PushClip(view);
    int d = U.page == PG_LIBRARY && !U.detailKey.empty() ? DivaByKey(U.detailKey) : -1;
    if (U.page == PG_LIBRARY && !U.detailKey.empty() && d < 0) U.detailKey.clear();       // she left the library
    int sp = d >= 0 ? (int)PG_DETAIL : U.page;
    float intro = (float)std::min(1.0, (U.t - U.pageT) * 4.0), ease = 1 - (1 - intro) * (1 - intro) * (1 - intro); if (intro < 1) U.animating = true;
    float pad = 36; RectF a = RF(view.left + pad, 30 - U.scroll[sp] + 14 * (1 - ease), RW(view) - pad * 2, 0);
    size_t firstContentHit = U.hits.size();
    float end = 0;
    if (d >= 0) end = PageDetail(a, d);
    else switch (U.page) { case PG_LIBRARY: end = PageLibrary(a); break; case PG_STAGE: end = PageStage(a); break; case PG_MUSIC: end = PageMusic(a); break; default: end = PageSettings(a); }
    if (ease < 1) c.FillR(view, Alpha(p.surface, 0.9f * (1 - ease)));                      // page transition: rise and fade in
    U.maxScroll = std::max(0.f, end + U.scroll[sp] - c.H() + 10);
    if (U.scroll[sp] > U.maxScroll) { U.scroll[sp] = U.maxScroll; U.animating = true; }
    for (size_t i = firstContentHit; i < U.hits.size(); ++i) { RectF& r = U.hits[i].r; r.left = std::max(r.left, view.left); }   // content never steals rail clicks
    if (U.maxScroll > 0) {                          // scroll thumb
        float th = std::max(40.f, c.H() * c.H() / (c.H() + U.maxScroll)), ty = (c.H() - th) * (U.scroll[sp] / U.maxScroll);
        c.FillRR(RF(c.W() - 8, ty + 3, 4, th - 6), 2, Alpha(p.text, 0.22f));
    }
    c.PopClip();
    
    LootPaint();
    // ---- click ripple
    { float rt = (float)(U.t - U.ripT) / 0.45f; if (rt >= 0 && rt < 1) { U.animating = true; float e = 1 - (1 - rt) * (1 - rt);
        c.Ring(U.ripX, U.ripY, 6 + 34 * e, Hex(kPink, 0.55f * (1 - rt)), 2.f * (1 - rt) + 0.5f); c.Circle(U.ripX, U.ripY, 4 + 22 * e, Hex(0xFFFFFF, 0.10f * (1 - rt))); } }
    // ---- toast
    if (U.t < U.toastUntil) {
        float k = (float)std::min({ 1.0, (U.toastUntil - U.t) * 4.0, (U.t - (U.toastUntil - 3.6)) * 6.0 }); k = std::max(0.f, k); U.animating = true;
        float tw = std::min(c.W() - RAIL - 80, c.TextW(U.toast, 13) + 44);
        RectF r = RF(RAIL + (c.W() - RAIL) / 2 - tw / 2, c.H() - 62 + 12 * (1 - k), tw, 40);
        c.Shadow(r, 20, 10, 0.35f * k); c.Grad3RR(r, 20, Hex(kPink, k), Alpha(p.accent, k), Hex(kViolet, k), 0.f); c.Text(U.toast, Inset(r, 4), 13, Hex(0xFFFFFF, k), 1, true);
    }
    U.lastPaint = U.t;
    c.End();
    if (U.pendingLoot) { U.deferred = std::move(U.pendingLoot); U.pendingLoot = nullptr; PostMessageW(U.h, WM_APP + 77, 0, 0); }
    // Frame pacing: 60 Hz while something is moving, 20 Hz for the ambient backdrop, nothing when the window is idle.
    bool ambient = Fx() > 0 && GetForegroundWindow() == U.h;
    static double cost = 0; cost += ((NowSeconds() - U.t) - cost) * 0.2;                 // smoothed paint time
    UINT floorMs = (UINT)std::clamp(cost * 2000.0, 16.0, 100.0);                          // a slow machine animates slower, it does not flood the queue
    UINT want = U.animating ? floorMs : ambient ? std::max<UINT>(50, floorMs) : 0; static UINT have = 0;
    if (want != have) { if (want) SetTimer(U.h, 1, want, nullptr); else KillTimer(U.h, 1); have = want; U.timer = want != 0; }
}

int HitAt(float x, float y) { for (size_t i = U.hits.size(); i-- > 0;) if (In(U.hits[i].r, x, y)) return !U.loot.empty() && U.hits[i].id < 9000 ? -1 : (int)i; return -1; }
int ScrollSlot() { return U.page == PG_LIBRARY && !U.detailKey.empty() ? (int)PG_DETAIL : U.page; }

LRESULT CALLBACK UiProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); PaintAll(); EndPaint(h, &ps); return 0; }
    case WM_SIZE: InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_TIMER: if (wp == 1 || wp == 2) InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_ACTIVATE: InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_GETMINMAXINFO: { auto* mi = (MINMAXINFO*)lp; float s = ThemeDpi(h) / 96.f; mi->ptMinTrackSize = { (LONG)(940 * s), (LONG)(620 * s) }; return 0; }
    case WM_DPICHANGED: { RECT* r = (RECT*)lp; SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE); return 0; }
    case WM_MOUSEMOVE: {
        float s = U.cv.Scale(); U.mx = GET_X_LPARAM(lp) / s; U.my = GET_Y_LPARAM(lp) / s;
        if (U.down >= 0) { for (auto& ht : U.hits) if (ht.id == U.down && ht.drag) { auto f = ht.drag; f(U.mx); break; } InvalidateRect(h, nullptr, FALSE); return 0; }
        int i = HitAt(U.mx, U.my), id = i >= 0 ? U.hits[i].id : -1;
        if (id != U.hover) { U.hover = id; if (id >= 0) SfxPlay(SFX_HOVER); InvalidateRect(h, nullptr, FALSE); }
        TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 }; TrackMouseEvent(&t);
        return 0; }
    case WM_MOUSELEAVE: if (U.hover != -1) { U.hover = -1; InvalidateRect(h, nullptr, FALSE); } return 0;
    case WM_SETCURSOR: if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, U.hover == 120 || U.hover == 250 || U.hover == 370 ? IDC_IBEAM : U.hover >= 0 ? IDC_HAND : IDC_ARROW)); return TRUE; } break;
    case WM_LBUTTONDOWN: {
        float s = U.cv.Scale(); U.mx = GET_X_LPARAM(lp) / s; U.my = GET_Y_LPARAM(lp) / s;
        int i = HitAt(U.mx, U.my);
        if (i < 0 || U.hits[i].id != 120) U.searchFocus = false;
        if (i < 0 || U.hits[i].id != 250) U.tagFocus = false;
        if (i < 0 || U.hits[i].id != 370) U.plFocus = false;
        if (i < 0 || U.hits[i].id < 560 || U.hits[i].id >= 560 + HA_COUNT) U.hkCapture = -1;
        if (i >= 0) { U.down = U.hits[i].id; SetCapture(h); if (U.hits[i].drag) { auto f = U.hits[i].drag; f(U.mx); } else { U.ripX = U.mx; U.ripY = U.my; U.ripT = NowSeconds(); SfxPlay(SFX_CLICK); } }
        InvalidateRect(h, nullptr, FALSE);
        return 0; }
    case WM_LBUTTONUP: {
        float s = U.cv.Scale(); U.mx = GET_X_LPARAM(lp) / s; U.my = GET_Y_LPARAM(lp) / s;
        int down = U.down; U.down = -1; if (GetCapture() == h) ReleaseCapture();
        if (down >= 0) {
            std::function<void()> fn;
            for (auto& ht : U.hits) if (ht.id == down) { if (ht.drag) fn = ht.release; else if (In(ht.r, U.mx, U.my)) fn = ht.click; break; }
            if (fn) fn();                              // copied first: handlers may rebuild the hit list
        }
        InvalidateRect(h, nullptr, FALSE); return 0; }
    case WM_XBUTTONUP: if (!U.detailKey.empty()) { U.detailKey.clear(); U.pageT = NowSeconds(); InvalidateRect(h, nullptr, FALSE); } return TRUE;
    case WM_MOUSEWHEEL: {
        float steps = GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA;
        if ((GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) && U.page == PG_LIBRARY && U.detailKey.empty()) {       // Ctrl + wheel zooms the library
            g_app->set.zoom = std::clamp(g_app->set.zoom * std::pow(1.1f, steps), 0.6f, 1.8f); g_app->set.Save();
        } else { int sp = ScrollSlot(); U.scroll[sp] = std::clamp(U.scroll[sp] - steps * 80.f, 0.f, U.maxScroll); }
        InvalidateRect(h, nullptr, FALSE); return 0; }
    case WM_CHAR:
        if (U.hkCapture >= 0) return 0;
        if (U.plFocus) {
            if (wp == 8) { if (!U.plName.empty()) U.plName.pop_back(); }
            else if (wp == 27) { U.plName.clear(); U.plFocus = false; }
            else if (wp == 13) { if (g_app->SaveClipPlaylist(U.plName)) { UiToast(L"Saved playlist " + U.plName); U.plName.clear(); U.plFocus = false; } else UiToast(L"Nothing to save, or the name is empty"); }
            else if (wp >= 32 && U.plName.size() < 40 && !wcschr(L"\\/:*?\"<>|", (wchar_t)wp)) U.plName.push_back((wchar_t)wp);
            InvalidateRect(h, nullptr, FALSE); return 0;
        }
        if (U.tagFocus) {
            if (wp == 8) { if (!U.tagText.empty()) U.tagText.pop_back(); }
            else if (wp == 27) { U.tagText.clear(); U.tagFocus = false; }
            else if (wp == 13 || wp == L',') { while (!U.tagText.empty() && U.tagText.back() == L' ') U.tagText.pop_back();
                if (!U.tagText.empty() && !U.detailKey.empty()) { auto& t = g_app->user.tags[U.detailKey]; bool dup = false; for (auto& x : t) dup = dup || !_wcsicmp(x.c_str(), U.tagText.c_str()); if (!dup && t.size() < 24) { t.push_back(U.tagText); g_app->user.dirty = true; } }
                U.tagText.clear(); }
            else if (wp >= 32 && U.tagText.size() < 24) U.tagText.push_back((wchar_t)wp);
            InvalidateRect(h, nullptr, FALSE); return 0;
        }
        if (U.searchFocus) {
            if (wp == 8) { if (!U.search.empty()) U.search.pop_back(); }
            else if (wp == 27 || wp == 13) { if (wp == 27) U.search.clear(); U.searchFocus = false; }
            else if (wp >= 32 && U.search.size() < 48) U.search.push_back((wchar_t)wp);
            U.scroll[ScrollSlot()] = 0; InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_SYSKEYDOWN:
        if (U.hkCapture < 0) break;
        [[fallthrough]];
    case WM_KEYDOWN:
        if (U.hkCapture >= 0) {                         // rebinding a shortcut: the next non-modifier key, with whatever modifiers are held
            UINT vk = (UINT)wp; int a = U.hkCapture;
            if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN) return 0;
            if (vk == VK_ESCAPE) { U.hkCapture = -1; InvalidateRect(h, nullptr, FALSE); return 0; }
            if (vk == VK_BACK || vk == VK_DELETE) { g_app->SetHotkey(a, 0); U.hkCapture = -1; UiToast(std::wstring(HotActionName(a)) + L": no shortcut"); InvalidateRect(h, nullptr, FALSE); return 0; }
            UINT mods = ((GetKeyState(VK_CONTROL) & 0x8000) ? MOD_CONTROL : 0) | ((GetKeyState(VK_MENU) & 0x8000) ? MOD_ALT : 0) | ((GetKeyState(VK_SHIFT) & 0x8000) ? MOD_SHIFT : 0) |
                        (((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) ? MOD_WIN : 0);
            bool fkey = vk >= VK_F1 && vk <= VK_F24;
            if (!(mods & (MOD_CONTROL | MOD_ALT | MOD_WIN)) && !fkey) { UiToast(L"Use Ctrl, Alt or Win with the key (or an F key), so typing elsewhere is not caught"); return 0; }
            uint32_t k = ((uint32_t)mods << 16) | vk;
            if (g_app->SetHotkey(a, k)) UiToast(std::wstring(HotActionName(a)) + L": " + HotkeyText(k));
            else UiToast(HotkeyText(k) + L" is already used by another program or another Divaverse shortcut");
            U.hkCapture = -1; InvalidateRect(h, nullptr, FALSE); return 0;
        }
        if (U.searchFocus || U.tagFocus || U.plFocus) return 0;
        if (!U.loot.empty()) { if (wp == VK_ESCAPE || wp == VK_RETURN || wp == VK_SPACE) { if (U.t - U.lootT < 0.95) U.lootT = U.t - 0.95; else LootNext(false); InvalidateRect(h, nullptr, FALSE); } return 0; }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wp >= '1' && wp <= '4') GoPage((int)(wp - '1'));
        else if ((GetKeyState(VK_CONTROL) & 0x8000) && wp == 'F') { GoPage(PG_LIBRARY); U.searchFocus = true; }
        else if ((GetKeyState(VK_CONTROL) & 0x8000) && (wp == VK_OEM_PLUS || wp == VK_ADD || wp == VK_OEM_MINUS || wp == VK_SUBTRACT)) {
            g_app->set.zoom = std::clamp(g_app->set.zoom * ((wp == VK_OEM_PLUS || wp == VK_ADD) ? 1.1f : 1 / 1.1f), 0.6f, 1.8f); g_app->set.Save(); }
        else if ((wp == VK_ESCAPE || wp == VK_BACK) && !U.detailKey.empty()) { U.detailKey.clear(); U.pageT = NowSeconds(); }
        else if (wp == VK_SPACE && g_app->OnStage()) g_app->SetPaused(!g_app->Paused());
        else if (wp == VK_LEFT && g_app->OnStage() && U.page == PG_STAGE) g_app->StepFrame(-1);
        else if (wp == VK_RIGHT && g_app->OnStage() && U.page == PG_STAGE) g_app->StepFrame(1);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_APPCOMMAND:                                  // media keys while this window has the focus
        switch (GET_APPCOMMAND_LPARAM(lp)) {
        case APPCOMMAND_MEDIA_PLAY_PAUSE: case APPCOMMAND_MEDIA_PLAY: case APPCOMMAND_MEDIA_PAUSE: g_app->OnCommand(ID_MUSIC_PAUSE); return TRUE;
        case APPCOMMAND_MEDIA_NEXTTRACK: g_app->OnCommand(ID_MUSIC_NEXT); return TRUE;
        case APPCOMMAND_MEDIA_PREVIOUSTRACK: g_app->OnCommand(ID_MUSIC_PREV); return TRUE;
        }
        break;
    case WM_DROPFILES: {
        HDROP d = (HDROP)wp; UINT n = DragQueryFileW(d, 0xFFFFFFFF, nullptr, 0); std::vector<std::wstring> audio; int opened = 0;
        for (UINT i = 0; i < n; ++i) {
            wchar_t p[MAX_PATH * 2]; if (!DragQueryFileW(d, i, p, MAX_PATH * 2)) continue;
            std::wstring s = p;
            if (Music::IsAudioFile(s)) audio.push_back(s);
            else if (Music::IsPlaylistFile(s)) { int i = g_app->music.ImportPlaylist(s); if (i >= 0) { U.plSel = i; U.page = PG_MUSIC; U.musicTab = 3; } }
            else if (DirExists(s)) g_app->AddLibraryFolder(s);
            else if (!opened++) { g_app->AddLibraryFolder(DirOf(s)); if (g_app->OpenPath(s)) UiToast(L"On the desktop now"); }
        }
        DragFinish(d);
        if (!audio.empty() && g_app->music.AddFiles(audio)) { U.page = PG_MUSIC; U.musicTab = 2; UiToast(L"Added to your music"); }
        InvalidateRect(h, nullptr, FALSE); return 0; }
    case WM_APP_POSTER: InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_APP + 77: if (U.deferred) { auto f = std::move(U.deferred); U.deferred = nullptr; f(); } InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_SHOWWINDOW: if (wp) { SetTimer(h, 2, 500, nullptr); U.pageT = NowSeconds(); } else { KillTimer(h, 2); } return 0;
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);                        // keep running in the tray
        if (!U.closedHintShown) { U.closedHintShown = true; g_app->Notify(L"Divaverse is still running", L"Click the tray icon to open it again, right-click it for quick controls."); }
        return 0;
    case WM_DESTROY: U.cv.Release(); U.covers.clear(); U.thumbs.clear(); U.logo.Reset(); U.h = nullptr; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

bool EnsureWindow() {
    if (U.h) return true;
    if (!GfxInit()) return false;
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(wc) }; wc.lpfnWndProc = UiProc; wc.hInstance = g_app->Inst(); wc.lpszClassName = kUiClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(g_app->Inst(), MAKEINTRESOURCEW(IDI_APP)); wc.hIconSm = wc.hIcon;
        RegisterClassExW(&wc); reg = true;
    }
    float s = ThemeDpi(nullptr) / 96.f; int w = (int)(1180 * s), hh = (int)(780 * s);
    RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    w = std::min<int>(w, wa.right - wa.left - 40); hh = std::min<int>(hh, wa.bottom - wa.top - 40);
    U.h = CreateWindowExW(0, kUiClass, L"Divaverse", WS_OVERLAPPEDWINDOW, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - hh) / 2, w, hh,
                          nullptr, nullptr, g_app->Inst(), nullptr);
    if (!U.h) return false;
    ThemeApplyWindow(U.h);
    DragAcceptFiles(U.h, TRUE);
    return true;
}
}

HWND UiWindow() { EnsureWindow(); return U.h; }
bool UiVisible() { return U.h && IsWindowVisible(U.h); }
void UiInvalidate() { if (U.h && IsWindowVisible(U.h)) InvalidateRect(U.h, nullptr, FALSE); }
void UiThemeChanged() { if (U.h) { ThemeApplyWindow(U.h); SetWindowPos(U.h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED); InvalidateRect(U.h, nullptr, FALSE); } }
void UiToast(const std::wstring& text) { U.toast = text; U.toastUntil = NowSeconds() + 3.6; UiInvalidate(); }
void UiShow(bool activate) {
    if (!EnsureWindow()) return;
    if (IsIconic(U.h)) ShowWindow(U.h, SW_RESTORE);
    ShowWindow(U.h, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
    if (activate) SetForegroundWindow(U.h);
    InvalidateRect(U.h, nullptr, FALSE);
}
bool UiScreenshot(const std::wstring& path, int page) {
    if (!EnsureWindow()) return false;
    // QA page codes: 0-3 pages, 10 music/my files, 20 first diva's page, 21 small tiles, 22 stage with media info
    U.page = std::clamp(page, 0, PG_COUNT - 1); U.pageT = NowSeconds() - 10;
    if (page == 10) { U.page = PG_MUSIC; U.musicTab = 2; }
    if (page == 11) { U.page = PG_MUSIC; U.musicTab = 1; }
    if (page == 20) { U.page = PG_LIBRARY; std::lock_guard<std::mutex> g(g_app->lib.Mutex()); if (!g_app->lib.Divas().empty()) U.detailKey = g_app->lib.Divas()[0].key; }
    if (page == 21) { U.page = PG_LIBRARY; g_app->set.cardMode = 0; }
    if (page == 22) { U.page = PG_STAGE; U.infoOpen = true; }
    if (page == 23 || page == 24) { U.page = PG_SETTINGS; U.scroll[ScrollSlot()] = page == 23 ? 1180.f : 99999.f; if (page == 24) U.hkCapture = HA_PERF; }     // settings: shortcuts + schedule, diagnostics
    if (page == 30 || page == 31) { U.page = PG_LIBRARY; U.loot.clear(); std::lock_guard<std::mutex> g(g_app->lib.Mutex()); if (!g_app->lib.Divas().empty()) { auto& dv = g_app->lib.Divas()[0]; U.loot.push_back({ dv.key, dv.name, (int)dv.clips.size(), true }); U.lootGen = g_app->lib.Generation(); U.lootT = NowSeconds() - (page == 31 ? 2.0 : 0.6); U.lootBurst = true; } }
    for (int i = 0; i < 12; ++i) { RedrawWindow(U.h, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW); Sleep(20); }
    RECT rc; GetClientRect(U.h, &rc); int w = rc.right, hgt = rc.bottom;
    HDC wdc = GetDC(U.h), mdc = CreateCompatibleDC(wdc);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -hgt; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); HGDIOBJ old = SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, w, hgt, wdc, 0, 0, SRCCOPY); GdiFlush();
    BITMAPFILEHEADER fh{}; fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader); fh.bfSize = fh.bfOffBits + (DWORD)w * hgt * 4;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr); bool ok = f != INVALID_HANDLE_VALUE;
    if (ok) { DWORD wr; WriteFile(f, &fh, sizeof(fh), &wr, nullptr); WriteFile(f, &bi.bmiHeader, sizeof(bi.bmiHeader), &wr, nullptr); WriteFile(f, bits, (DWORD)w * hgt * 4, &wr, nullptr); CloseHandle(f); }
    SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(U.h, wdc);
    return ok;
}
