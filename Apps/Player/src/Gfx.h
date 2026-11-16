#pragma once
// Direct2D / DirectWrite drawing layer for the Divaverse UI. Everything on screen is drawn from code:
// vector icons (path strings on a 16x16 grid), gradients, text. No image assets.
#include "Common.h"
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <map>
#include <unordered_map>

using Color = D2D1_COLOR_F;
using RectF = D2D1_RECT_F;
inline Color Hex(uint32_t rgb, float a = 1.f) { return D2D1::ColorF(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a); }
inline Color Alpha(Color c, float a) { c.a *= a; return c; }
inline Color Mix(Color a, Color b, float t) { return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t }; }
inline RectF RF(float x, float y, float w, float h) { return { x, y, x + w, y + h }; }
inline bool In(const RectF& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
inline float RW(const RectF& r) { return r.right - r.left; }
inline float RH(const RectF& r) { return r.bottom - r.top; }
inline RectF Inset(const RectF& r, float d) { return { r.left + d, r.top + d, r.right - d, r.bottom - d }; }

enum class ThemeId { System = 0, Light = 1, Dark = 2, Pink = 3 };
struct Palette {
    bool dark = false, pink = false;
    Color chrome, surface, card, text, dim, faint, border, hover, press, accent, accent2, accentText, danger, good;
};
extern Palette g_pal;
void ThemeSet(ThemeId id);
ThemeId ThemeGet();
bool ThemeSystemDark();
void ThemeApplyWindow(HWND hwnd);              // title bar colour / dark mode / rounded corners where the OS supports them
int  ThemeDpi(HWND hwnd);

enum Icon {
    I_GRID, I_STAGE, I_MUSIC, I_GEAR, I_PLAY, I_PAUSE, I_NEXT, I_PREV, I_EYE, I_EYEOFF, I_MOVE, I_ADD, I_CLOSE, I_CHECK,
    I_FOLDER, I_SPARKLE, I_HEART, I_MIRROR, I_PIN, I_TRASH, I_COPY, I_SHUFFLE, I_POWER, I_INFO, I_SUN, I_MOON, I_WINDOW,
    I_SHADOW, I_CHIP, I_REFRESH, I_SEARCH, I_STAR, I_HEARTO, I_LOCK, I_CURSOR, I_LOOP, I_TILES, I_CARDS, I_BACK, I_RULER, I_FILM, I_COUNT
};

bool GfxInit();                                // factories; false = Direct2D / DirectWrite unavailable

// One per window.
class Canvas {
public:
    ~Canvas() { Release(); }
    bool Begin(HWND hwnd);                     // (re)creates the target as needed; coordinates are DIPs
    void End();
    // Per-pixel-alpha popup: draws into a 32-bit DIB selected into `dc` (premultiplied), for UpdateLayeredWindow.
    bool BeginDC(HDC dc, int pw, int ph, float dpi);
    void SetTransform(const D2D1_MATRIX_3X2_F& m); void ResetTransform();
    void Tri(float x0, float y0, float x1, float y1, float x2, float y2, Color c);                // filled triangle
    void Arc(float cx, float cy, float r, float a0deg, float a1deg, Color c, float w);          // stroked arc, flat caps (a wedge when w is large)
    void Release();
    float W() const { return w_; } float H() const { return h_; } float Scale() const { return scale_; }
    ID2D1RenderTarget* RT() { return rt_.Get(); }
    // Changes whenever the render target is recreated: device bitmaps made under an older generation are dead.
    unsigned Generation() const { return gen_; }

    void Clear(Color c);
    void FillR(const RectF& r, Color c);
    void FillRR(const RectF& r, float rad, Color c);
    void StrokeRR(const RectF& r, float rad, Color c, float w = 1.f);
    void GradRR(const RectF& r, float rad, Color a, Color b, bool vertical = true);
    void Circle(float cx, float cy, float rad, Color c);
    void Ring(float cx, float cy, float rad, Color c, float w);
    void Grad3RR(const RectF& r, float rad, Color a, Color b, Color c, float angleDeg);          // three-stop linear sweep
    void GradStrokeRR(const RectF& r, float rad, Color a, Color b, float w, float angleDeg);     // gradient outline
    void Glow(float cx, float cy, float rx, float ry, Color c);                                  // soft radial light, fades to nothing
    void BitmapCover(ID2D1Bitmap* b, const RectF& dst, float rad, float opacity = 1.f, float focusY = 0.f, float zoom = 1.f);   // crop-to-fill, rounded
    void Star(float cx, float cy, float r, Color c);
    void Sheen(const RectF& r, float rad, float t, float alpha);                                  // slanted highlight band, t 0..1 sweeps left to right
    void Line(float x0, float y0, float x1, float y1, Color c, float w = 1.f);
    void Text(const std::wstring& s, const RectF& r, float size, Color c, int halign = 0, bool bold = false, bool vcenter = true, bool wrap = false);
    float TextW(const std::wstring& s, float size, bool bold = false);
    void IconAt(Icon i, float cx, float cy, float size, Color c, float stroke = 1.4f);
    void Shadow(const RectF& r, float rad, float spread, float alpha);     // soft drop shadow built from stacked strokes
    void PushClip(const RectF& r, float rad = 0);
    void PopClip();
    // Premultiplied BGRA pixels -> device bitmap (cached by the caller).
    ComPtr<ID2D1Bitmap> MakeBitmap(const uint8_t* bgra, int w, int h);
    void Bitmap(ID2D1Bitmap* b, const RectF& dst, float opacity = 1.f);
    void BitmapSrc(ID2D1Bitmap* b, const RectF& dst, const RectF& src, float opacity = 1.f);    // part of a bitmap, nearest-neighbour
    void BitmapPx(ID2D1Bitmap* b, const RectF& dst, float opacity = 1.f);                       // nearest-neighbour (pixel art)
    void Quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Color c);
    void Logo(float cx, float cy, float size);                             // the Divaverse mark, drawn live

private:
    ID2D1SolidColorBrush* Brush(Color c);
    HWND hwnd_ = nullptr; ComPtr<ID2D1RenderTarget> rt_; ComPtr<ID2D1HwndRenderTarget> hrt_; ComPtr<ID2D1DCRenderTarget> dcrt_;
    struct GlowTex { unsigned gen; ComPtr<ID2D1Bitmap> bmp; }; std::unordered_map<uint64_t, GlowTex> glows_;
    std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>> brushes_;
    unsigned gen_ = 0;
    float w_ = 0, h_ = 0, scale_ = 1; int clipLayers_ = 0; std::vector<bool> clipIsLayer_;
};
