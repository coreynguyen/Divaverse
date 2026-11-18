#include "Gfx.h"
#include <cmath>
#include <cwctype>
#include <algorithm>

Palette g_pal;
namespace {
ComPtr<ID2D1Factory> g_d2d; ComPtr<IDWriteFactory> g_dw; ComPtr<ID2D1StrokeStyle> g_round;
std::map<std::wstring, ComPtr<IDWriteTextFormat>> g_formats;
std::wstring g_font = L"Segoe UI";
ThemeId g_theme = ThemeId::System;
using PFN_Dwm = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
PFN_Dwm pDwm; UINT(WINAPI* pGetDpiForWindow)(HWND);
int(WINAPI* pSetPreferredAppMode)(int); void(WINAPI* pFlushMenuThemes)();
bool g_sysInit = false;

void SysInit() {
    if (g_sysInit) return;
    g_sysInit = true;
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) pDwm = (PFN_Dwm)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (HMODULE u32 = GetModuleHandleW(L"user32.dll")) pGetDpiForWindow = (UINT(WINAPI*)(HWND))(void*)GetProcAddress(u32, "GetDpiForWindow");
    using PFN_Rtl = LONG(WINAPI*)(OSVERSIONINFOW*); OSVERSIONINFOW vi{ sizeof(vi) };
    auto rtl = (PFN_Rtl)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    if (rtl && rtl(&vi) == 0 && vi.dwBuildNumber >= 18362)             // uxtheme ordinals 135/136 exist from 1903 on
        if (HMODULE ux = LoadLibraryW(L"uxtheme.dll")) {
            pSetPreferredAppMode = (int(WINAPI*)(int))(void*)GetProcAddress(ux, MAKEINTRESOURCEA(135));
            pFlushMenuThemes = (void(WINAPI*)())(void*)GetProcAddress(ux, MAKEINTRESOURCEA(136));
        }
}
COLORREF Ref(Color c) { return RGB((int)(c.r * 255 + .5f), (int)(c.g * 255 + .5f), (int)(c.b * 255 + .5f)); }
uint32_t Key(Color c) {
    auto b = [](float v) { return (uint32_t)(std::min(1.f, std::max(0.f, v)) * 255.f + 0.5f); };
    return b(c.r) << 24 | b(c.g) << 16 | b(c.b) << 8 | b(c.a);
}
IDWriteTextFormat* Fmt(float size, bool bold, bool wrap) {
    wchar_t key[96]; swprintf(key, 96, L"%.2f|%d|%d", size, (int)bold, (int)wrap);
    auto it = g_formats.find(key);
    if (it != g_formats.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> f;
    g_dw->CreateTextFormat(g_font.c_str(), nullptr, bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                           DWRITE_FONT_STRETCH_NORMAL, size, L"", &f);
    if (f) {
        f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!wrap) {
            // The ellipsis sign is made exactly once per format. Making one per draw call chains them (each new sign
            // keeps the format alive, the format keeps the previous sign): the chain grows with every text draw and is
            // torn down recursively at exit, which overflowed the stack.
            ComPtr<IDWriteInlineObject> ell; DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            if (SUCCEEDED(g_dw->CreateEllipsisTrimmingSign(f.Get(), &ell))) f->SetTrimming(&trim, ell.Get());
        }
    }
    g_formats[key] = f;
    return f.Get();
}

// ---- icons: SVG-style path strings on a 16x16 grid ------------------------------------------------
struct IconDef { const wchar_t* path; bool fill; };
const IconDef kIcons[I_COUNT] = {
    { L"M2.5 2.5H7V7H2.5Z M9 2.5H13.5V7H9Z M2.5 9H7V13.5H2.5Z M9 9H13.5V13.5H9Z", false },                                   // grid
    { L"M8 1.8A2.2 2.2 0 1 1 8 6.2A2.2 2.2 0 1 1 8 1.8Z M3.5 14C3.5 10.6 5.4 8.4 8 8.4C10.6 8.4 12.5 10.6 12.5 14", false },  // stage (performer)
    { L"M6 12.2V3.2L13 2V11 M6 12.2A1.9 1.9 0 1 1 2.2 12.2A1.9 1.9 0 1 1 6 12.2Z M13 11A1.9 1.9 0 1 1 9.2 11A1.9 1.9 0 1 1 13 11Z", false },   // music
    { nullptr, false },                                                                                                      // gear (built)
    { L"M4.5 2.8L13 8L4.5 13.2Z", true },                                                                                    // play
    { L"M4 3H6.6V13H4Z M9.4 3H12V13H9.4Z", true },                                                                           // pause
    { L"M3 3L10 8L3 13Z M11.2 3H13V13H11.2Z", true },                                                                        // next
    { L"M13 3L6 8L13 13Z M3 3H4.8V13H3Z", true },                                                                            // prev
    { L"M1.5 8C3.2 4.9 5.4 3.4 8 3.4C10.6 3.4 12.8 4.9 14.5 8C12.8 11.1 10.6 12.6 8 12.6C5.4 12.6 3.2 11.1 1.5 8Z M10.1 8A2.1 2.1 0 1 1 5.9 8A2.1 2.1 0 1 1 10.1 8Z", false }, // eye
    { L"M1.5 8C3.2 4.9 5.4 3.4 8 3.4C10.6 3.4 12.8 4.9 14.5 8C12.8 11.1 10.6 12.6 8 12.6C5.4 12.6 3.2 11.1 1.5 8Z M2.5 13.5L13.5 2.5", false },                             // eye off
    { L"M8 1.5V14.5 M1.5 8H14.5 M8 1.5L6 3.5 M8 1.5L10 3.5 M8 14.5L6 12.5 M8 14.5L10 12.5 M1.5 8L3.5 6 M1.5 8L3.5 10 M14.5 8L12.5 6 M14.5 8L12.5 10", false },             // move
    { L"M8 3V13M3 8H13", false },                                                                                            // add
    { L"M4 4L12 12M12 4L4 12", false },                                                                                      // close
    { L"M3 8.5L6.5 12L13 4.5", false },                                                                                      // check
    { L"M1.5 4.5V12.5H14.5V5.5H8L6.5 3.5H1.5Z", false },                                                                     // folder
    { L"M7 1.5C7.6 5 9.6 7 13 7.6C9.6 8.2 7.6 10.2 7 13.7C6.4 10.2 4.4 8.2 1 7.6C4.4 7 6.4 5 7 1.5Z M12.8 10.2C13 11.4 13.6 12 14.8 12.2C13.6 12.4 13 13 12.8 14.2C12.6 13 12 12.4 10.8 12.2C12 12 12.6 11.4 12.8 10.2Z", true }, // sparkle
    { L"M8 13.6C3.2 10.4 1.6 8 1.6 5.7C1.6 3.9 3 2.6 4.7 2.6C6 2.6 7.2 3.3 8 4.6C8.8 3.3 10 2.6 11.3 2.6C13 2.6 14.4 3.9 14.4 5.7C14.4 8 12.8 10.4 8 13.6Z", true },     // heart
    { L"M8 1.5V14.5 M6 4L2 12H6Z M10 4L14 12H10Z", false },                                                                  // mirror
    { L"M5.5 2H10.5L9.8 6.6L12.2 9.4H3.8L6.2 6.6Z M8 9.4V14.5", false },                                                     // pin
    { L"M2.5 4.2H13.5 M6 4.2V2.5H10V4.2 M4 4.2L4.8 14H11.2L12 4.2 M6.8 6.8V11.4 M9.2 6.8V11.4", false },                     // trash
    { L"M5.5 5.5H13.5V14.5H5.5Z M3.5 11.5H2.5V1.5H10.5V3.5", false },                                                        // copy
    { L"M1.5 4.5H4.5L9.5 11.5H14 M1.5 11.5H4.5L6.2 9.1 M9.5 4.5H14 M7.8 6.9L9.5 4.5 M12 2.5L14 4.5L12 6.5 M12 9.5L14 11.5L12 13.5", false },                               // shuffle
    { L"M8 1.8V7.6 M4.6 3.9A5.4 5.4 0 1 0 11.4 3.9", false },                                                                // power
    { L"M8 1.6A6.4 6.4 0 1 1 8 14.4A6.4 6.4 0 1 1 8 1.6Z M8 7.2V11.4 M8 4.6V5", false },                                     // info
    { L"M8 5A3 3 0 1 1 8 11A3 3 0 1 1 8 5Z M8 1V2.6 M8 13.4V15 M1 8H2.6 M13.4 8H15 M3 3L4.2 4.2 M11.8 11.8L13 13 M3 13L4.2 11.8 M11.8 4.2L13 3", false },                  // sun
    { L"M12.8 9.8A5.6 5.6 0 1 1 6.2 3.2A4.6 4.6 0 0 0 12.8 9.8Z", false },                                                   // moon
    { L"M1.5 3H14.5V13H1.5Z M1.5 5.6H14.5", false },                                                                         // window
    { L"M3 12.5C5 11.4 11 11.4 13 12.5C11 13.6 5 13.6 3 12.5Z M8 2.2A2 2 0 1 1 8 6.2A2 2 0 1 1 8 2.2Z M5.4 10.4C5.6 8.2 6.6 7.2 8 7.2C9.4 7.2 10.4 8.2 10.6 10.4", false },  // shadow
    { L"M4.5 4.5H11.5V11.5H4.5Z M6.5 2V4.5 M9.5 2V4.5 M6.5 11.5V14 M9.5 11.5V14 M2 6.5H4.5 M2 9.5H4.5 M11.5 6.5H14 M11.5 9.5H14", false },                                 // chip
    { L"M13 8A5 5 0 1 1 11.2 4.2 M11.4 1.6V4.4H8.6", false },                                                                // refresh
    { L"M11.2 7A4.2 4.2 0 1 1 2.8 7A4.2 4.2 0 1 1 11.2 7Z M10.2 10.2L14 14", false },                                        // search
    { L"M8 1.3L10 5.8L14.9 6.3L11.2 9.6L12.3 14.4L8 11.9L3.7 14.4L4.8 9.6L1.1 6.3L6 5.8Z", true },                           // star
    { L"M8 13.6C3.2 10.4 1.6 8 1.6 5.7C1.6 3.9 3 2.6 4.7 2.6C6 2.6 7.2 3.3 8 4.6C8.8 3.3 10 2.6 11.3 2.6C13 2.6 14.4 3.9 14.4 5.7C14.4 8 12.8 10.4 8 13.6Z", false },    // heart outline
    { L"M3.5 7.2H12.5V14H3.5Z M5.4 7.2V5A2.6 2.6 0 1 1 10.6 5V7.2", false },                                                 // lock
    { L"M3.5 2L12.5 8.6L8.4 9.2L10.6 13.6L9 14.4L6.8 10L3.5 12.6Z", false },                                                 // cursor
    { L"M3 6.5V5.5C3 4.4 3.9 3.5 5 3.5H12.5 M10.5 1.5L12.5 3.5L10.5 5.5 M13 9.5V10.5C13 11.6 12.1 12.5 11 12.5H3.5 M5.5 10.5L3.5 12.5L5.5 14.5", false },                  // loop
    { L"M2 2H5V5H2Z M6.5 2H9.5V5H6.5Z M11 2H14V5H11Z M2 6.5H5V9.5H2Z M6.5 6.5H9.5V9.5H6.5Z M11 6.5H14V9.5H11Z M2 11H5V14H2Z M6.5 11H9.5V14H6.5Z M11 11H14V14H11Z", false }, // small tiles
    { L"M2 2H7V14H2Z M9 2H14V14H9Z", false },                                                                                // large cards
    { L"M9.5 3L4.5 8L9.5 13", false },                                                                                       // back
    { L"M1.5 5H14.5V11H1.5Z M4 5V7.5 M6.5 5V8.5 M9 5V7.5 M11.5 5V8.5", false },                                              // ruler
    { L"M2 3H14V13H2Z M2 5.5H14 M2 10.5H14 M5 3V5.5 M8 3V5.5 M11 3V5.5 M5 10.5V13 M8 10.5V13 M11 10.5V13", false },          // film
};
std::wstring GearPath() {
    std::wstring p; wchar_t b[64]; const float PI = 3.14159265f;
    for (int i = 0; i < 16; ++i) {
        float a0 = (i * 22.5f - 6.f) * PI / 180.f, a1 = (i * 22.5f + 6.f) * PI / 180.f, r = (i & 1) ? 5.1f : 6.7f;
        swprintf(b, 64, L"%ls%.2f %.2f L%.2f %.2f ", i ? L"L" : L"M", 8 + r * cosf(a0), 8 + r * sinf(a0), 8 + r * cosf(a1), 8 + r * sinf(a1));
        p += b;
    }
    return p + L"Z M10.3 8A2.3 2.3 0 1 1 5.7 8A2.3 2.3 0 1 1 10.3 8Z";
}
ComPtr<ID2D1PathGeometry> BuildPath(const wchar_t* s) {
    ComPtr<ID2D1PathGeometry> g; g_d2d->CreatePathGeometry(&g);
    ComPtr<ID2D1GeometrySink> k; g->Open(&k);
    k->SetFillMode(D2D1_FILL_MODE_WINDING);
    bool open = false; D2D1_POINT_2F cur{ 0, 0 }, start{ 0, 0 }; wchar_t cmd = 0; const wchar_t* p = s;
    auto num = [&]() -> float { while (*p == L' ' || *p == L',') ++p; wchar_t* e; float v = wcstof(p, &e); p = e; return v; };
    auto hasNum = [&]() { while (*p == L' ' || *p == L',') ++p; return (*p >= L'0' && *p <= L'9') || *p == L'-' || *p == L'.'; };
    auto begin = [&](D2D1_POINT_2F pt) { if (open) k->EndFigure(D2D1_FIGURE_END_OPEN); k->BeginFigure(pt, D2D1_FIGURE_BEGIN_FILLED); open = true; start = cur = pt; };
    while (*p) {
        while (*p == L' ') ++p;
        if (!*p) break;
        if (iswalpha(*p)) cmd = *p++;
        switch (cmd) {
        case L'M': { float x = num(), y = num(); begin({ x, y }); cmd = L'L'; break; }
        case L'L': { float x = num(), y = num(); cur = { x, y }; k->AddLine(cur); break; }
        case L'H': { cur.x = num(); k->AddLine(cur); break; }
        case L'V': { cur.y = num(); k->AddLine(cur); break; }
        case L'C': { float a = num(), b = num(), c = num(), d = num(), x = num(), y = num();
                     k->AddBezier(D2D1::BezierSegment({ a, b }, { c, d }, { x, y })); cur = { x, y }; break; }
        case L'A': {   // SVG elliptical arc flattened to line segments (endpoint -> centre parameterisation)
            float rx = std::fabs(num()), ry = std::fabs(num()), rot = num(), large = num(), sweep = num(), x = num(), y = num();
            float phi = rot * 3.14159265f / 180.f, cp = cosf(phi), sp = sinf(phi);
            float dx = (cur.x - x) / 2, dy = (cur.y - y) / 2, x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
            if (rx < 1e-4f || ry < 1e-4f) { k->AddLine({ x, y }); cur = { x, y }; break; }
            float lam = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
            if (lam > 1) { rx *= sqrtf(lam); ry *= sqrtf(lam); }
            float n2 = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1, den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
            float co = (den > 0 && n2 > 0) ? sqrtf(n2 / den) : 0.f;
            if ((large != 0) == (sweep != 0)) co = -co;
            float cxp = co * rx * y1 / ry, cyp = -co * ry * x1 / rx;
            float cx = cp * cxp - sp * cyp + (cur.x + x) / 2, cy = sp * cxp + cp * cyp + (cur.y + y) / 2;
            auto ang = [](float ux, float uy, float vx, float vy) { return atan2f(ux * vy - uy * vx, ux * vx + uy * vy); };
            float t1 = ang(1, 0, (x1 - cxp) / rx, (y1 - cyp) / ry);
            float dt = ang((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
            if (!sweep && dt > 0) dt -= 6.2831853f; else if (sweep && dt < 0) dt += 6.2831853f;
            int segs = std::max(6, (int)(std::fabs(dt) / 0.1f));
            const wchar_t* q = p; while (*q == L' ') ++q;
            bool closesNext = (*q == L'Z' || *q == L'z') && std::fabs(x - start.x) < 1e-3f && std::fabs(y - start.y) < 1e-3f;
            for (int i = 1; i <= segs - (closesNext ? 1 : 0); ++i) {
                float t = t1 + dt * i / segs, ex = rx * cosf(t), ey = ry * sinf(t);
                k->AddLine({ cp * ex - sp * ey + cx, sp * ex + cp * ey + cy });
            }
            cur = { x, y }; break; }
        case L'Z': case L'z': if (open) { k->EndFigure(D2D1_FIGURE_END_CLOSED); open = false; cur = start; } break;
        default: ++p; break;
        }
        if (cmd == L'Z' || cmd == L'z') continue;
        if (!hasNum() && *p && !iswalpha(*p)) ++p;
    }
    if (open) k->EndFigure(D2D1_FIGURE_END_OPEN);
    k->Close();
    return g;
}
}

// ---------------------------------------------------------------------------------------- theme
bool ThemeSystemDark() {
    HKEY hk; DWORD val = 1, sz = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hk) != ERROR_SUCCESS) return false;
    RegQueryValueExW(hk, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&val, &sz);
    RegCloseKey(hk);
    return val == 0;
}
ThemeId ThemeGet() { return g_theme; }
void ThemeSet(ThemeId id) {
    SysInit();
    g_theme = id;
    bool pink = id == ThemeId::Pink, dark = !pink && (id == ThemeId::System ? ThemeSystemDark() : id == ThemeId::Dark);
    Palette t; t.dark = dark; t.pink = pink;
    if (pink) {            // soft blush chrome, rose accent, plum text
        t.chrome = Hex(0xFBE3EF); t.surface = Hex(0xFFF0F6); t.card = Hex(0xFFFFFF); t.text = Hex(0x4A1C38); t.dim = Hex(0x8C5A77); t.faint = Hex(0xC39AB2);
        t.border = Hex(0xF2C4DA); t.accent = Hex(0xE0569B); t.accent2 = Hex(0xB565D9); t.accentText = Hex(0xFFFFFF);
        t.hover = Hex(0xE0569B, 0.12f); t.press = Hex(0xE0569B, 0.22f);
    } else if (dark) {     // midnight violet with neon magenta / cyan
        t.chrome = Hex(0x0E0A1A); t.surface = Hex(0x130E22); t.card = Hex(0x1D1532); t.text = Hex(0xFFFFFF); t.dim = Hex(0xB9B0D4); t.faint = Hex(0x7A7096);
        t.border = Hex(0x33285A); t.accent = Hex(0xC65BFF); t.accent2 = Hex(0x4FD6FF); t.accentText = Hex(0xFFFFFF);
        t.hover = Hex(0xFFFFFF, 0.08f); t.press = Hex(0xFFFFFF, 0.14f);
    } else {
        t.chrome = Hex(0xEFEEF3); t.surface = Hex(0xF8F8FB); t.card = Hex(0xFFFFFF); t.text = Hex(0x1B1A20); t.dim = Hex(0x5F5D6B); t.faint = Hex(0xA09EAC);
        t.border = Hex(0xE2E0EA); t.accent = Hex(0x8A3FFC); t.accent2 = Hex(0x1AA7EC); t.accentText = Hex(0xFFFFFF);
        t.hover = Hex(0x000000, 0.06f); t.press = Hex(0x000000, 0.11f);
    }
    t.danger = Hex(0xE5484D); t.good = Hex(0x30A46C);
    g_pal = t;
    if (pSetPreferredAppMode) { pSetPreferredAppMode(dark ? 2 : 3); if (pFlushMenuThemes) pFlushMenuThemes(); }   // common dialogs follow
}
void ThemeApplyWindow(HWND hwnd) {
    SysInit();
    if (!pDwm) return;
    BOOL dark = g_pal.dark;
    if (FAILED(pDwm(hwnd, 20, &dark, sizeof(dark)))) pDwm(hwnd, 19, &dark, sizeof(dark));       // immersive dark mode (20H1+ / 1809)
    DWORD corner = 2; pDwm(hwnd, 33, &corner, sizeof(corner));                                 // Win11: rounded corners
    COLORREF cap = Ref(g_pal.chrome), txt = Ref(g_pal.text), bord = Ref(g_pal.border);
    pDwm(hwnd, 35, &cap, sizeof(cap)); pDwm(hwnd, 36, &txt, sizeof(txt)); pDwm(hwnd, 34, &bord, sizeof(bord));   // Win11: caption / text / border colour
}
int ThemeDpi(HWND hwnd) {
    SysInit();
    if (pGetDpiForWindow && hwnd) { UINT d = pGetDpiForWindow(hwnd); if (d) return (int)d; }
    HDC dc = GetDC(nullptr); int d = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
    return d ? d : 96;
}

// --------------------------------------------------------------------------------------- canvas
bool GfxInit() {
    if (g_d2d && g_dw) return true;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, (void**)g_d2d.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)g_dw.GetAddressOf()))) return false;
    D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    g_d2d->CreateStrokeStyle(sp, nullptr, 0, &g_round);
    // Prefer the Windows 11 UI face when installed.
    ComPtr<IDWriteFontCollection> fc; UINT32 idx = 0; BOOL exists = FALSE;
    if (SUCCEEDED(g_dw->GetSystemFontCollection(&fc)) && SUCCEEDED(fc->FindFamilyName(L"Segoe UI Variable Text", &idx, &exists)) && exists) g_font = L"Segoe UI Variable Text";
    else if (fc && SUCCEEDED(fc->FindFamilyName(L"Segoe UI", &idx, &exists)) && !exists) g_font = L"Tahoma";
    return true;
}

bool Canvas::Begin(HWND hwnd) {
    if (!g_d2d) return false;
    RECT rc; GetClientRect(hwnd, &rc);
    UINT pw = (UINT)std::max<LONG>(1, rc.right), ph = (UINT)std::max<LONG>(1, rc.bottom);
    float dpi = (float)ThemeDpi(hwnd);
    if (!rt_ || hwnd_ != hwnd) {
        Release(); hwnd_ = hwnd;
        D2D1_RENDER_TARGET_PROPERTIES rp = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
        if (FAILED(g_d2d->CreateHwndRenderTarget(rp, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(pw, ph)), &hrt_))) return false;
        rt_ = hrt_;
        rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        ++gen_;
    }
    D2D1_SIZE_U cur = rt_->GetPixelSize();
    if (cur.width != pw || cur.height != ph) hrt_->Resize(D2D1::SizeU(pw, ph));
    rt_->SetDpi(dpi, dpi);
    scale_ = dpi / 96.f; w_ = pw / scale_; h_ = ph / scale_;
    rt_->BeginDraw(); rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    return true;
}
bool Canvas::BeginDC(HDC dc, int pw, int ph, float dpi) {
    if (!g_d2d) return false;
    if (!dcrt_) {
        D2D1_RENDER_TARGET_PROPERTIES rp = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
        if (FAILED(g_d2d->CreateDCRenderTarget(&rp, &dcrt_))) return false;
        brushes_.clear(); rt_ = dcrt_; ++gen_;
        rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);        // ClearType cannot be used on a transparent surface
    }
    RECT rc{ 0, 0, pw, ph };
    if (FAILED(dcrt_->BindDC(dc, &rc))) return false;
    rt_->SetDpi(dpi, dpi); scale_ = dpi / 96.f; w_ = pw / scale_; h_ = ph / scale_;
    rt_->BeginDraw(); rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    return true;
}
void Canvas::SetTransform(const D2D1_MATRIX_3X2_F& m) { rt_->SetTransform(m); }
void Canvas::ResetTransform() { rt_->SetTransform(D2D1::Matrix3x2F::Identity()); }
void Canvas::Tri(float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
    ComPtr<ID2D1PathGeometry> g; ComPtr<ID2D1GeometrySink> sk;
    if (FAILED(g_d2d->CreatePathGeometry(&g)) || FAILED(g->Open(&sk))) return;
    sk->BeginFigure({ x0, y0 }, D2D1_FIGURE_BEGIN_FILLED); sk->AddLine({ x1, y1 }); sk->AddLine({ x2, y2 }); sk->EndFigure(D2D1_FIGURE_END_CLOSED); sk->Close();
    rt_->FillGeometry(g.Get(), Brush(c));
}
void Canvas::Arc(float cx, float cy, float r, float a0, float a1, Color c, float w) {
    ComPtr<ID2D1PathGeometry> g; ComPtr<ID2D1GeometrySink> sk;
    if (FAILED(g_d2d->CreatePathGeometry(&g)) || FAILED(g->Open(&sk))) return;
    float r0 = a0 * 3.14159265f / 180.f, r1 = a1 * 3.14159265f / 180.f;
    sk->BeginFigure({ cx + r * cosf(r0), cy + r * sinf(r0) }, D2D1_FIGURE_BEGIN_HOLLOW);
    sk->AddArc(D2D1::ArcSegment({ cx + r * cosf(r1), cy + r * sinf(r1) }, { r, r }, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, (a1 - a0) > 180 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sk->EndFigure(D2D1_FIGURE_END_OPEN); sk->Close();
    rt_->DrawGeometry(g.Get(), Brush(c), w);
}
void Canvas::End() {
    if (!rt_) return;
    while (!clipIsLayer_.empty()) PopClip();
    HRESULT hr = rt_->EndDraw();
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET) Release();                  // device lost: rebuilt on the next paint
    else if (FAILED(hr)) { static HRESULT last = S_OK; if (hr != last) { last = hr; Log("Direct2D frame failed: 0x%08X", (unsigned)hr); } }
}
void Canvas::Release() { brushes_.clear(); glows_.clear(); rt_.Reset(); hrt_.Reset(); dcrt_.Reset(); clipIsLayer_.clear(); ++gen_; }

ID2D1SolidColorBrush* Canvas::Brush(Color c) {
    uint32_t k = Key(c);
    auto it = brushes_.find(k);
    if (it != brushes_.end()) return it->second.Get();
    ComPtr<ID2D1SolidColorBrush> b; rt_->CreateSolidColorBrush(c, &b);
    if (brushes_.size() > 2000) brushes_.clear();
    brushes_[k] = b; return b.Get();
}
void Canvas::Clear(Color c) { rt_->Clear(c); }
void Canvas::FillR(const RectF& r, Color c) { rt_->FillRectangle(r, Brush(c)); }
void Canvas::FillRR(const RectF& r, float rad, Color c) { rt_->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), Brush(c)); }
void Canvas::StrokeRR(const RectF& r, float rad, Color c, float w) {
    RectF q{ r.left + w / 2, r.top + w / 2, r.right - w / 2, r.bottom - w / 2 };
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(q, rad, rad), Brush(c), w);
}
void Canvas::GradRR(const RectF& r, float rad, Color a, Color b, bool vertical) {
    D2D1_GRADIENT_STOP st[2] = { { 0.f, a }, { 1.f, b } };
    ComPtr<ID2D1GradientStopCollection> sc; ComPtr<ID2D1LinearGradientBrush> br;
    if (FAILED(rt_->CreateGradientStopCollection(st, 2, &sc))) { FillRR(r, rad, a); return; }
    D2D1_POINT_2F p0{ r.left, r.top }, p1 = vertical ? D2D1_POINT_2F{ r.left, r.bottom } : D2D1_POINT_2F{ r.right, r.bottom };
    if (FAILED(rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(p0, p1), sc.Get(), &br))) { FillRR(r, rad, a); return; }
    if (rad > 0) rt_->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), br.Get()); else rt_->FillRectangle(r, br.Get());
}
void Canvas::Grad3RR(const RectF& r, float rad, Color a, Color b, Color c, float ang) {
    D2D1_GRADIENT_STOP st[3] = { { 0.f, a }, { 0.5f, b }, { 1.f, c } };
    ComPtr<ID2D1GradientStopCollection> sc; ComPtr<ID2D1LinearGradientBrush> br;
    if (FAILED(rt_->CreateGradientStopCollection(st, 3, &sc))) { FillRR(r, rad, b); return; }
    float t = ang * 3.14159265f / 180.f, dx = cosf(t) * RW(r) / 2, dy = sinf(t) * RH(r) / 2, cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    if (FAILED(rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({ cx - dx, cy - dy }, { cx + dx, cy + dy }), sc.Get(), &br))) { FillRR(r, rad, b); return; }
    if (rad > 0) rt_->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), br.Get()); else rt_->FillRectangle(r, br.Get());
}
void Canvas::GradStrokeRR(const RectF& r, float rad, Color a, Color b, float w, float ang) {
    D2D1_GRADIENT_STOP st[3] = { { 0.f, a }, { 0.5f, b }, { 1.f, a } };
    ComPtr<ID2D1GradientStopCollection> sc; ComPtr<ID2D1LinearGradientBrush> br;
    if (FAILED(rt_->CreateGradientStopCollection(st, 3, &sc))) { StrokeRR(r, rad, a, w); return; }
    float t = ang * 3.14159265f / 180.f, dx = cosf(t) * RW(r) / 2, dy = sinf(t) * RH(r) / 2, cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    if (FAILED(rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({ cx - dx, cy - dy }, { cx + dx, cy + dy }), sc.Get(), &br))) { StrokeRR(r, rad, a, w); return; }
    RectF q{ r.left + w / 2, r.top + w / 2, r.right - w / 2, r.bottom - w / 2 };
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(q, rad, rad), br.Get(), w);
}
// Soft light. A low-alpha gradient spans only a few 8-bit levels, so any analytic gradient shows steps on screen.
// The falloff is therefore baked once per colour into a small premultiplied texture whose rounding is randomised
// per pixel and per channel (dither), then stretched; the steps dissolve into fine grain.
void Canvas::Glow(float cx, float cy, float rx, float ry, Color c) {
    if (c.a <= 0.002f || rx <= 0 || ry <= 0) return;
    int bucket = std::clamp((int)std::ceil(c.a * 16.f), 1, 16); float peak = bucket / 16.f;      // texture strength; the rest is draw opacity
    uint64_t key = ((uint64_t)bucket << 24) | ((uint64_t)(c.r * 255 + .5f) << 16) | ((uint64_t)(c.g * 255 + .5f) << 8) | (uint64_t)(c.b * 255 + .5f);
    GlowTex& t = glows_[key];
    if (!t.bmp || t.gen != gen_) {
        const int N = 320; std::vector<uint8_t> px((size_t)N * N * 4); uint32_t seed = (uint32_t)key * 2654435761u + 12345u;
        auto rnd = [&] { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (seed & 0xFFFF) / 65536.f; };
        for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
            float dx = (x + .5f) / N * 2 - 1, dy = (y + .5f) / N * 2 - 1, d2 = dx * dx + dy * dy, f = d2 >= 1 ? 0 : (1 - d2) * (1 - d2);
            float a = peak * f * 255.f; uint8_t* p = &px[((size_t)y * N + x) * 4];
            auto q = [&](float v) { return (uint8_t)std::clamp((int)std::floor(v + (rnd() + rnd()) * 0.5f), 0, 255); };      // triangular dither
            uint8_t A = q(a); p[3] = A; p[0] = std::min(A, q(a * c.b)); p[1] = std::min(A, q(a * c.g)); p[2] = std::min(A, q(a * c.r));
        }
        t.bmp = MakeBitmap(px.data(), N, N); t.gen = gen_;
    }
    if (t.bmp) rt_->DrawBitmap(t.bmp.Get(), D2D1::RectF(cx - rx, cy - ry, cx + rx, cy + ry), std::min(1.f, c.a / peak), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}
void Canvas::BitmapCover(ID2D1Bitmap* b, const RectF& dst, float rad, float opacity, float focusY, float zoom) {
    if (!b) return;
    D2D1_SIZE_F sz = b->GetSize(); if (sz.width < 1 || sz.height < 1) return;
    float k = std::max(RW(dst) / sz.width, RH(dst) / sz.height) * zoom, w = sz.width * k, h = sz.height * k;
    float x = (dst.left + dst.right) / 2 - w / 2, y = dst.top - (h - RH(dst)) * focusY;
    ComPtr<ID2D1BitmapBrush> br;
    if (FAILED(rt_->CreateBitmapBrush(b, D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR),
                                      D2D1::BrushProperties(opacity, D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(x, y)), &br))) return;
    if (rad > 0) rt_->FillRoundedRectangle(D2D1::RoundedRect(dst, rad, rad), br.Get()); else rt_->FillRectangle(dst, br.Get());
}
void Canvas::Sheen(const RectF& r, float rad, float t, float alpha) {
    D2D1_GRADIENT_STOP st[3] = { { 0.f, Hex(0xFFFFFF, 0.f) }, { 0.5f, Hex(0xFFFFFF, alpha) }, { 1.f, Hex(0xFFFFFF, 0.f) } };
    ComPtr<ID2D1GradientStopCollection> sc; ComPtr<ID2D1LinearGradientBrush> br;
    if (FAILED(rt_->CreateGradientStopCollection(st, 3, &sc))) return;
    float band = RW(r) * 0.35f, x = r.left - band + (RW(r) + band * 2) * t;
    if (FAILED(rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({ x - band, r.top }, { x + band, r.top + band * 0.6f }), sc.Get(), &br))) return;
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), br.Get());
}
void Canvas::Star(float cx, float cy, float r, Color c) { IconAt(I_STAR, cx, cy, r * 2, c); }
void Canvas::Circle(float cx, float cy, float rad, Color c) { rt_->FillEllipse(D2D1::Ellipse({ cx, cy }, rad, rad), Brush(c)); }
void Canvas::Ring(float cx, float cy, float rad, Color c, float w) { rt_->DrawEllipse(D2D1::Ellipse({ cx, cy }, rad, rad), Brush(c), w); }
void Canvas::Line(float x0, float y0, float x1, float y1, Color c, float w) { rt_->DrawLine({ x0, y0 }, { x1, y1 }, Brush(c), w, g_round.Get()); }

void Canvas::Text(const std::wstring& s, const RectF& r, float size, Color c, int halign, bool bold, bool vcenter, bool wrap) {
    IDWriteTextFormat* f = Fmt(size, bold, wrap);
    if (!f || s.empty()) return;
    f->SetTextAlignment(halign == 1 ? DWRITE_TEXT_ALIGNMENT_CENTER : halign == 2 ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    f->SetParagraphAlignment(vcenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    rt_->DrawText(s.c_str(), (UINT32)s.size(), f, r, Brush(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
float Canvas::TextW(const std::wstring& s, float size, bool bold) {
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(g_dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), Fmt(size, bold, false), 10000.f, 1000.f, &l))) return 0;
    DWRITE_TEXT_METRICS m; l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}
void Canvas::IconAt(Icon i, float cx, float cy, float size, Color c, float stroke) {
    if (i < 0 || i >= I_COUNT) return;
    if (g_pal.pink && i == I_SPARKLE) i = I_HEART;                         // Pink swaps the sparkle for a heart
    static ComPtr<ID2D1PathGeometry> cache[I_COUNT];
    if (!cache[i]) { static std::wstring gear = GearPath(); cache[i] = BuildPath(i == I_GEAR ? gear.c_str() : kIcons[i].path); }
    D2D1_MATRIX_3X2_F old; rt_->GetTransform(&old);
    float k = size / 16.f;
    rt_->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(cx - size / 2, cy - size / 2) * old);
    if (kIcons[i].fill) rt_->FillGeometry(cache[i].Get(), Brush(c));
    else rt_->DrawGeometry(cache[i].Get(), Brush(c), stroke / k, g_round.Get());
    rt_->SetTransform(old);
}
void Canvas::Shadow(const RectF& r, float rad, float spread, float alpha) {
    int n = 8;
    for (int i = 0; i < n; ++i) {
        float t = (float)(i + 1) / n, g = spread * t;
        RectF q{ r.left - g, r.top - g + spread * 0.35f, r.right + g, r.bottom + g + spread * 0.35f };
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(q, rad + g, rad + g), Brush(Hex(0x000000, alpha * (1 - t) * (1 - t) * 0.5f)), spread / n + 0.6f);
    }
}
void Canvas::PushClip(const RectF& r, float rad) {
    (void)rad; rt_->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED); clipIsLayer_.push_back(false);
}
void Canvas::PopClip() {
    if (clipIsLayer_.empty()) return;
    if (clipIsLayer_.back()) rt_->PopLayer(); else rt_->PopAxisAlignedClip();
    clipIsLayer_.pop_back();
}
ComPtr<ID2D1Bitmap> Canvas::MakeBitmap(const uint8_t* bgra, int w, int h) {
    ComPtr<ID2D1Bitmap> b;
    if (rt_) rt_->CreateBitmap(D2D1::SizeU((UINT32)w, (UINT32)h), bgra, (UINT32)w * 4,
                               D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &b);
    return b;
}
void Canvas::Bitmap(ID2D1Bitmap* b, const RectF& dst, float opacity) {
    if (b) rt_->DrawBitmap(b, dst, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}
void Canvas::BitmapSrc(ID2D1Bitmap* b, const RectF& dst, const RectF& src, float opacity) {
    if (b) rt_->DrawBitmap(b, dst, opacity, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, src);
}
void Canvas::BitmapPx(ID2D1Bitmap* b, const RectF& dst, float opacity) {
    if (b) rt_->DrawBitmap(b, dst, opacity, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}
void Canvas::Quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Color c) {
    ComPtr<ID2D1PathGeometry> g; ComPtr<ID2D1GeometrySink> sk;
    if (FAILED(g_d2d->CreatePathGeometry(&g)) || FAILED(g->Open(&sk))) return;
    sk->BeginFigure({ x0, y0 }, D2D1_FIGURE_BEGIN_FILLED); sk->AddLine({ x1, y1 }); sk->AddLine({ x2, y2 }); sk->AddLine({ x3, y3 }); sk->EndFigure(D2D1_FIGURE_END_CLOSED); sk->Close();
    rt_->FillGeometry(g.Get(), Brush(c));
}
// One filled polygon (winding fill, so a folded outline still fills solid). Far cheaper than a quad or a gradient rectangle per column.
static ComPtr<ID2D1PathGeometry> PolyGeo(const D2D1_POINT_2F* p, int n) {
    ComPtr<ID2D1PathGeometry> g; ComPtr<ID2D1GeometrySink> sk; if (n < 3 || FAILED(g_d2d->CreatePathGeometry(&g)) || FAILED(g->Open(&sk))) return nullptr;
    sk->SetFillMode(D2D1_FILL_MODE_WINDING); sk->BeginFigure(p[0], D2D1_FIGURE_BEGIN_FILLED); sk->AddLines(p + 1, (UINT32)(n - 1)); sk->EndFigure(D2D1_FIGURE_END_CLOSED); sk->Close(); return g;
}
void Canvas::Poly(const D2D1_POINT_2F* p, int n, Color c) { if (auto g = PolyGeo(p, n)) rt_->FillGeometry(g.Get(), Brush(c)); }
void Canvas::PolyGrad(const D2D1_POINT_2F* p, int n, Color top, Color bot, float y0, float y1) {
    auto g = PolyGeo(p, n); if (!g) return; D2D1_GRADIENT_STOP st[2] = { { 0.f, top }, { 1.f, bot } }; ComPtr<ID2D1GradientStopCollection> sc; ComPtr<ID2D1LinearGradientBrush> br;
    if (FAILED(rt_->CreateGradientStopCollection(st, 2, &sc)) || FAILED(rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({ 0, y0 }, { 0, y1 }), sc.Get(), &br))) { rt_->FillGeometry(g.Get(), Brush(top)); return; }
    rt_->FillGeometry(g.Get(), br.Get());
}
void Canvas::Logo(float cx, float cy, float s) {
    // Squircle with the magenta -> violet -> cyan sweep, a halo arc and a four-point sparkle.
    RectF r = RF(cx - s / 2, cy - s / 2, s, s);
    GradRR(r, s * 0.28f, Hex(0xE0409B), Hex(0x5B5BFF), false);
    Ring(cx, cy + s * 0.04f, s * 0.27f, Hex(0xFFFFFF, 0.85f), std::max(1.f, s * 0.045f));
    IconAt(I_SPARKLE, cx + s * 0.02f, cy, s * 0.5f, Hex(0xFFFFFF), 1.f);
}
