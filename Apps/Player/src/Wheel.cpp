// Radial quick menu for the performer on the desktop: a per-pixel-alpha popup (layered window fed from a Direct2D
// DC target), six wedges around a hub. Picking posts WM_COMMAND(id) to the owner, like the flyout.
#include "App.h"
#include "Gfx.h"
#include "Sfx.h"
#include <windowsx.h>
#include <cmath>
#include <algorithm>

namespace {
constexpr wchar_t kWheelClass[] = L"Divaverse.Wheel";
constexpr float kOuter = 118, kInner = 44, kPad = 22;           // DIPs
struct WheelState {
    HWND h = nullptr, owner = nullptr; Canvas cv; std::vector<WheelItem> items; std::wstring title;
    HDC dc = nullptr; HBITMAP dib = nullptr, old = nullptr; int px = 0; float dpi = 96;
    int hot = -2; double t0 = 0; float glow[8] = {}; double last = 0; bool closing = false;
} W;

void Destroy();
int Pick(float x, float y) {                                    // -1 hub, -2 outside, else wedge index
    float c = kOuter + kPad, dx = x - c, dy = y - c, d = std::sqrt(dx * dx + dy * dy);
    if (d < kInner - 4) return -1;
    if (d > kOuter + 6 || W.items.empty()) return -2;
    float step = 360.f / W.items.size(), a = std::atan2(dy, dx) * 180.f / 3.14159265f + 90.f + step / 2;
    while (a < 0) a += 360;
    while (a >= 360) a -= 360;
    int i = (int)(a / step);
    return W.items[i].enabled ? i : -2;
}
void Paint() {
    if (!W.h || !W.cv.BeginDC(W.dc, W.px, W.px, W.dpi)) return;
    Canvas& c = W.cv; const Palette& p = g_pal; double now = NowSeconds(); float dt = (float)std::min(0.05, now - W.last); W.last = now;
    float k = std::min(1.f, (float)(now - W.t0) / (W.closing ? 0.13f : 0.16f)), e = 1 - (1 - k) * (1 - k) * (1 - k);     // pop-in
    if (W.closing) { if (k >= 1) { Destroy(); return; } e = (1 - k) * (1 - k); }                         // spiral back out
    c.Clear(D2D1::ColorF(0, 0, 0, 0));
    float cc = kOuter + kPad, mid = (kOuter + kInner) / 2, wdt = kOuter - kInner, n = (float)W.items.size(), step = 360.f / n;
    c.SetTransform(D2D1::Matrix3x2F::Scale(0.6f + 0.4f * e, 0.6f + 0.4f * e, { cc, cc }) * D2D1::Matrix3x2F::Rotation((W.closing ? 30.f : -24.f) * (1 - e), { cc, cc }));
    c.Glow(cc, cc, kOuter + kPad, kOuter + kPad, Hex(0xFF4FA8, 0.30f * e));
    c.Ring(cc, cc, mid, Alpha(p.dark ? Hex(0x130E22) : p.card, 0.94f * e), wdt);                          // body
    for (int i = 0; i < (int)n; ++i) {
        float a0 = -90.f - step / 2 + i * step, target = W.hot == i ? 1.f : 0.f; W.glow[i] += (target - W.glow[i]) * std::min(1.f, dt * 18.f); float g = W.glow[i];
        if (g > 0.01f) { c.Arc(cc, cc, mid, a0 + 1.6f, a0 + step - 1.6f, Hex(0xFF4FA8, 0.85f * g * e), wdt - 4); c.Arc(cc, cc, mid, a0 + step * 0.45f, a0 + step - 1.6f, Hex(0x8E5BFF, 0.55f * g * e), wdt - 4); }
        float r0 = (a0) * 3.14159265f / 180.f; c.Line(cc + kInner * cosf(r0), cc + kInner * sinf(r0), cc + kOuter * cosf(r0), cc + kOuter * sinf(r0), Alpha(p.text, 0.10f * e), 1);
        float am = (a0 + step / 2) * 3.14159265f / 180.f, rr = mid + 3 * g, ix = cc + rr * cosf(am), iy = cc + rr * sinf(am);
        Color fg = !W.items[i].enabled ? Alpha(p.text, 0.25f * e) : g > 0.5f ? Hex(0xFFFFFF, e) : Alpha(p.text, e);
        c.IconAt((Icon)W.items[i].icon, ix, iy - 8, 19 + 3 * g, fg, 1.6f);
        c.Text(W.items[i].label, RF(ix - 40, iy + 5, 80, 16), 10.f, fg, 1, g > 0.5f);
    }
    c.Ring(cc, cc, kOuter, Hex(0xFF4FA8, 0.55f * e), 1.5f); c.Ring(cc, cc, kInner, Alpha(p.text, 0.16f * e), 1);
    // hub: performer name, closes the wheel
    bool hubHot = W.hot == -1;
    c.Circle(cc, cc, kInner - 6, Alpha(p.dark ? Hex(0x1D1532) : p.card, 0.96f * e)); if (hubHot) c.Ring(cc, cc, kInner - 6, Hex(0xFF4FA8, e), 1.5f);
    c.IconAt(I_CLOSE, cc, cc - 7, 13, Alpha(p.text, (hubHot ? 1.f : 0.6f) * e), 1.6f);
    c.Text(W.title, RF(cc - kInner + 10, cc + 4, (kInner - 10) * 2, 16), 9.5f, Alpha(p.text, 0.8f * e), 1, true);
    c.ResetTransform();
    c.End();
    POINT src{ 0, 0 }; SIZE sz{ W.px, W.px }; BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(W.h, nullptr, nullptr, &sz, W.dc, &src, 0, &bf, ULW_ALPHA);
    bool moving = k < 1 || W.closing; for (int i = 0; i < (int)n; ++i) if (std::fabs(W.glow[i] - (W.hot == i ? 1.f : 0.f)) > 0.01f) moving = true;
    if (moving) SetTimer(W.h, 1, 16, nullptr); else KillTimer(W.h, 1);
}
void Destroy() {
    if (!W.h) return;
    HWND h = W.h; W.h = nullptr; W.cv.Release();
    if (W.dc) { SelectObject(W.dc, W.old); DeleteObject(W.dib); DeleteDC(W.dc); W.dc = nullptr; W.dib = nullptr; }
    DestroyWindow(h);
}
void BeginClose() { if (!W.h || W.closing) return; W.closing = true; W.hot = -2; W.t0 = W.last = NowSeconds(); SetTimer(W.h, 1, 16, nullptr); }
LRESULT CALLBACK WheelProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (W.closing && (m == WM_MOUSEMOVE || m == WM_LBUTTONUP || m == WM_RBUTTONUP || m == WM_KEYDOWN || m == WM_ACTIVATE || m == WM_MOUSELEAVE)) return 0;
    switch (m) {
    case WM_TIMER: Paint(); return 0;
    case WM_MOUSEMOVE: {
        float s = W.dpi / 96.f; int hot = Pick(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
        if (hot != W.hot) { W.hot = hot; if (hot >= 0) SfxPlay(SFX_WHEEL_TICK); Paint(); }
        TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 }; TrackMouseEvent(&t);
        return 0; }
    case WM_MOUSELEAVE: if (W.hot != -2) { W.hot = -2; Paint(); } return 0;
    case WM_LBUTTONUP: case WM_RBUTTONUP: {
        float s = W.dpi / 96.f; int i = Pick(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
        if (m == WM_RBUTTONUP && NowSeconds() - W.t0 < 0.35) return 0;        // the release of the click that opened the wheel
        HWND owner = W.owner; UINT id = i >= 0 ? W.items[i].id : 0;
        BeginClose();
        if (id) { SfxPlay(SFX_WHEEL_PICK); PostMessageW(owner, WM_COMMAND, id, 0); }
        return 0; }
    case WM_KEYDOWN: if (wp == VK_ESCAPE) BeginClose(); return 0;
    case WM_ACTIVATE: if (LOWORD(wp) == WA_INACTIVE) BeginClose(); return 0;     // click anywhere else dismisses
    case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr, W.hot >= -1 ? IDC_HAND : IDC_ARROW)); return TRUE;
    }
    return DefWindowProcW(h, m, wp, lp);
}
}

bool WheelOpen() { return W.h != nullptr; }
void WheelClose() { Destroy(); }
void WheelShow(HWND owner, POINT pt, const std::wstring& title, std::vector<WheelItem> items) {
    Destroy(); FlyoutClose();
    if (items.empty() || items.size() > 8 || !GfxInit()) return;
    static bool reg = false;
    if (!reg) { WNDCLASSEXW wc{ sizeof(wc) }; wc.lpfnWndProc = WheelProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = kWheelClass; RegisterClassExW(&wc); reg = true; }
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST); MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(mon, &mi);
    W.dpi = (float)ThemeDpi(owner); W.px = (int)std::lround((kOuter + kPad) * 2 * W.dpi / 96.f);
    int x = std::clamp<int>(pt.x - W.px / 2, mi.rcMonitor.left, mi.rcMonitor.right - W.px), y = std::clamp<int>(pt.y - W.px / 2, mi.rcMonitor.top, mi.rcMonitor.bottom - W.px);
    W.h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kWheelClass, L"", WS_POPUP, x, y, W.px, W.px, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!W.h) return;
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = W.px; bi.bmiHeader.biHeight = -W.px; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HDC screen = GetDC(nullptr); W.dc = CreateCompatibleDC(screen); W.dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); ReleaseDC(nullptr, screen);
    if (!W.dc || !W.dib) { Destroy(); return; }
    W.old = (HBITMAP)SelectObject(W.dc, W.dib);
    W.closing = false; W.owner = owner; W.items = std::move(items); W.title = title; W.hot = -2; W.t0 = W.last = NowSeconds(); for (float& g : W.glow) g = 0;
    Paint();
    ShowWindow(W.h, SW_SHOW); SetForegroundWindow(W.h); SetFocus(W.h);
    SfxPlay(SFX_WHEEL_OPEN);
}
bool WheelScreenshot(const std::wstring& bmp) {                // QA: writes the wheel's own premultiplied surface
    if (!W.h) return false;
    for (int i = 0; i < 20; ++i) { W.hot = 1; Paint(); Sleep(16); }
    BITMAP bm{}; GetObjectW(W.dib, sizeof(bm), &bm);
    BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{}; ih.biSize = sizeof(ih); ih.biWidth = W.px; ih.biHeight = -W.px; ih.biPlanes = 1; ih.biBitCount = 32;
    DWORD bytes = (DWORD)W.px * W.px * 4; fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + bytes;
    HANDLE f = CreateFileW(bmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr); if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wr; WriteFile(f, &fh, sizeof(fh), &wr, nullptr); WriteFile(f, &ih, sizeof(ih), &wr, nullptr); WriteFile(f, bm.bmBits, bytes, &wr, nullptr); CloseHandle(f);
    return true;
}
