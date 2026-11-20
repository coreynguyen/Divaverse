// Playback statistics beside the performer: a small click-through per-pixel-alpha popup (same plumbing as the
// wheel), refreshed four times a second. Toggled with the rebindable shortcut (default Ctrl+Alt+D) or in Settings.
#include "App.h"
#include "Gfx.h"
#include <cmath>
#include <algorithm>

namespace {
constexpr wchar_t kHudClass[] = L"Divaverse.PerfHud";
constexpr float kW = 260, kH = 116;                                // DIPs
struct Hud { HWND h = nullptr; Canvas cv; HDC dc = nullptr; HBITMAP dib = nullptr, old = nullptr; int pw = 0, ph = 0; float dpi = 96; double last = 0; } H;

void Destroy() {
    if (H.h) { DestroyWindow(H.h); H.h = nullptr; }
    if (H.dc) { SelectObject(H.dc, H.old); DeleteObject(H.dib); DeleteDC(H.dc); H.dc = nullptr; H.dib = nullptr; }
}
void Paint(const App::PerfStats& p) {
    if (!H.h || !H.cv.BeginDC(H.dc, H.pw, H.ph, H.dpi)) return;
    Canvas& c = H.cv;
    c.Clear(D2D1::ColorF(0, 0, 0, 0));
    c.FillRR(RF(0, 0, kW, kH), 10, Hex(0x0B0816, 0.82f));
    c.StrokeRR(RF(0.5f, 0.5f, kW - 1, kH - 1), 10, Hex(0xFF4FA8, 0.55f), 1);
    wchar_t b[160]; float y = 8;
    auto line = [&](const wchar_t* k, const std::wstring& v, Color col) {
        c.Text(k, RF(12, y, 140, 18), 11.5f, Hex(0xB8B0D0)); c.Text(v, RF(140, y, kW - 152, 18), 11.5f, col, 2, true); y += 17.5f; };
    auto warn = [](bool bad) { return bad ? Hex(0xFF6A6A) : Hex(0xFFFFFF); };
    swprintf(b, 160, L"%.1f", p.fps); line(L"Frames / s", b, Hex(0xFFFFFF));
    swprintf(b, 160, L"%.2f ms", p.decodeMs); line(L"Decode", b, warn(p.decodeMs > 12));
    swprintf(b, 160, L"%.2f ms", p.presentMs); line(L"Present", b, warn(p.presentMs > 8));
    swprintf(b, 160, L"%llu / %llu", (unsigned long long)p.dropped, (unsigned long long)p.unpaired); line(L"Dropped / unpaired", b, warn(p.dropped + p.unpaired > 0));
    swprintf(b, 160, L"%ls | %hs", p.gpu ? L"GPU" : L"CPU", p.stage ? p.stage : ""); line(L"Path", b, Hex(0xFFFFFF));
    swprintf(b, 160, L"%lld", (long long)p.frame); line(L"Frame", b, Hex(0xFFFFFF));
    c.End();
    POINT src{ 0, 0 }; SIZE sz{ H.pw, H.ph }; BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(H.h, nullptr, nullptr, &sz, H.dc, &src, 0, &bf, ULW_ALPHA);
}
void Place() {
    RECT r{}; HWND perf = g_app ? g_app->Overlay() : nullptr;
    if (perf && IsWindowVisible(perf)) GetWindowRect(perf, &r); else { SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0); r.left += 24; r.top += 24; }
    RECT wa; HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST); MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(mon, &mi); wa = mi.rcWork;
    int x = std::clamp((int)r.left, (int)wa.left, (int)wa.right - H.pw), y = std::clamp((int)r.top - H.ph - 6, (int)wa.top, (int)wa.bottom - H.ph);
    SetWindowPos(H.h, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}
}

void PerfHudShow(bool on) {
    if (!on) { Destroy(); return; }
    if (H.h) return;
    static bool reg = false;
    if (!reg) { WNDCLASSEXW wc{ sizeof(wc) }; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = kHudClass; RegisterClassExW(&wc); reg = true; }
    H.dpi = (float)ThemeDpi(g_app ? g_app->Overlay() : nullptr); H.pw = (int)std::lround(kW * H.dpi / 96.f); H.ph = (int)std::lround(kH * H.dpi / 96.f);
    H.h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kHudClass, L"", WS_POPUP, 0, 0, H.pw, H.ph, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!H.h) return;
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = H.pw; bi.bmiHeader.biHeight = -H.ph; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HDC screen = GetDC(nullptr); H.dc = CreateCompatibleDC(screen); H.dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); ReleaseDC(nullptr, screen);
    H.old = (HBITMAP)SelectObject(H.dc, H.dib);
    Place(); ShowWindow(H.h, SW_SHOWNOACTIVATE); H.last = 0;
    if (g_app) Paint(g_app->Perf());
}
void PerfHudTick(double now) {
    if (!H.h || now - H.last < 0.25) return;
    H.last = now; Place(); Paint(g_app->Perf());
}
