// Tray flyout: a themed popup drawn with Direct2D (no HMENU, so no system frame, margins or colours leak in).
#include "App.h"
#include "Gfx.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace {
constexpr wchar_t kFlyClass[] = L"Divaverse.Flyout";
struct Fly {
    HWND h = nullptr, owner = nullptr; Canvas cv; std::vector<FlyItem> items; std::vector<RectF> rects;
    int hover = -1; float w = 0, hgt = 0; bool keyboard = false;
} F;
constexpr float PAD = 6, ITEM_H = 34, SEP_H = 9;

void LayoutItems() {
    Canvas tmp; float tw = 0, hw = 0;
    for (auto& it : F.items) { if (it.sep) continue; tw = std::max(tw, tmp.TextW(it.text, 13.5f)); if (!it.hint.empty()) hw = std::max(hw, tmp.TextW(it.hint, 12)); }
    F.w = std::max(220.f, PAD * 2 + 44 + tw + (hw > 0 ? 28 + hw : 0) + 18);
    float y = PAD; F.rects.clear();
    for (auto& it : F.items) { float h = it.sep ? SEP_H : ITEM_H; F.rects.push_back(RF(PAD, y, F.w - PAD * 2, h)); y += h; }
    F.hgt = y + PAD;
}
void Paint() {
    if (!F.cv.Begin(F.h)) return;
    Canvas& c = F.cv; const Palette& p = g_pal;
    c.Clear(p.card);
    c.StrokeRR(RF(0, 0, c.W(), c.H()), 0, p.border, 1);
    for (size_t i = 0; i < F.items.size(); ++i) {
        const FlyItem& it = F.items[i]; const RectF& r = F.rects[i];
        if (it.sep) { float y = (r.top + r.bottom) / 2; c.Line(r.left + 8, y, r.right - 8, y, p.border, 1); continue; }
        bool hot = (int)i == F.hover && it.enabled;
        if (hot) c.FillRR(r, 7, Mix(p.card, p.accent, p.dark ? 0.26f : 0.16f));
        Color fg = it.enabled ? p.text : p.faint;
        float cy = (r.top + r.bottom) / 2;
        if (it.checked) { c.FillRR(RF(r.left + 9, cy - 10, 20, 20), 6, Alpha(p.accent, 0.18f)); c.IconAt(I_CHECK, r.left + 19, cy, 13, p.accent, 1.8f); }
        else c.IconAt((Icon)it.icon, r.left + 19, cy, 15, it.enabled ? (hot ? p.accent : p.dim) : p.faint, 1.4f);
        c.Text(it.text, RF(r.left + 40, r.top, RW(r) - 48, RH(r)), 13.5f, fg);
        if (!it.hint.empty()) c.Text(it.hint, RF(r.left + 40, r.top, RW(r) - 52, RH(r)), 12, it.enabled ? p.dim : p.faint, 2);
    }
    c.End();
}
int HitTest(float x, float y) {
    for (size_t i = 0; i < F.rects.size(); ++i) if (In(F.rects[i], x, y) && !F.items[i].sep) return (int)i;
    return -1;
}
void Pick(int i) {
    if (i < 0 || i >= (int)F.items.size() || F.items[i].sep || !F.items[i].enabled) return;
    UINT id = F.items[i].id; HWND owner = F.owner;
    FlyoutClose();
    PostMessageW(owner, WM_COMMAND, id, 0);
}
void Step(int dir) {
    int n = (int)F.items.size(), i = F.hover;
    for (int k = 0; k < n; ++k) { i = ((i + dir) % n + n) % n; if (!F.items[i].sep && F.items[i].enabled) { F.hover = i; break; } }
    InvalidateRect(F.h, nullptr, FALSE);
}
LRESULT CALLBACK FlyProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
    case WM_MOUSEMOVE: {
        float s = F.cv.Scale(); int hit = HitTest(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
        if (hit != F.hover) { F.hover = hit; InvalidateRect(h, nullptr, FALSE); }
        TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 }; TrackMouseEvent(&t);
        return 0; }
    case WM_MOUSELEAVE: if (F.hover != -1 && !F.keyboard) { F.hover = -1; InvalidateRect(h, nullptr, FALSE); } return 0;
    case WM_LBUTTONUP: { float s = F.cv.Scale(); Pick(HitTest(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s)); return 0; }
    case WM_KEYDOWN:
        F.keyboard = true;
        if (wp == VK_ESCAPE) FlyoutClose(); else if (wp == VK_DOWN) Step(1); else if (wp == VK_UP) Step(-1); else if (wp == VK_RETURN || wp == VK_SPACE) Pick(F.hover);
        return 0;
    case WM_ACTIVATE: if (LOWORD(wp) == WA_INACTIVE) FlyoutClose(); return 0;     // click anywhere else dismisses
    case WM_DESTROY: F.cv.Release(); if (F.h == h) F.h = nullptr; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
}

bool FlyoutOpen() { return F.h != nullptr; }
void FlyoutClose() { if (F.h) { HWND h = F.h; F.h = nullptr; DestroyWindow(h); } }

void FlyoutShow(HWND owner, POINT pt, std::vector<FlyItem> items) {
    FlyoutClose();                                     // one flyout at a time; a second request replaces the first
    if (!GfxInit()) {                                  // no Direct2D: plain system menu so the app stays operable
        HMENU m = CreatePopupMenu();
        for (auto& it : items) { if (it.sep) AppendMenuW(m, MF_SEPARATOR, 0, nullptr); else AppendMenuW(m, MF_STRING | (it.checked ? MF_CHECKED : 0) | (it.enabled ? 0 : MF_GRAYED), it.id, it.text.c_str()); }
        SetForegroundWindow(owner);
        UINT id = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, owner, nullptr);
        DestroyMenu(m); if (id) PostMessageW(owner, WM_COMMAND, id, 0);
        return;
    }
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(wc) }; wc.lpfnWndProc = FlyProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = kFlyClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.style = CS_DROPSHADOW;
        RegisterClassExW(&wc); reg = true;
    }
    F.items = std::move(items); F.owner = owner; F.hover = -1; F.keyboard = false;
    LayoutItems();
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST); MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(mon, &mi);
    float s = ThemeDpi(owner) / 96.f;
    int w = (int)std::ceil(F.w * s), hh = (int)std::ceil(F.hgt * s);
    RECT wa = mi.rcWork;
    int x = pt.x - w / 2, y = pt.y - hh - 8;           // above the cursor (the tray sits at the bottom on most setups)
    if (y < wa.top) y = pt.y + 8;
    x = std::clamp<int>(x, wa.left + 4, std::max<int>(wa.left + 4, wa.right - w - 4));
    y = std::clamp<int>(y, wa.top + 4, std::max<int>(wa.top + 4, wa.bottom - hh - 4));
    F.h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kFlyClass, L"", WS_POPUP, x, y, w, hh, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!F.h) return;
    ThemeApplyWindow(F.h);
    ShowWindow(F.h, SW_SHOW);
    SetForegroundWindow(F.h); SetFocus(F.h);
}
