// Wet-Rash: a jet-ski brawl racer in the spirit of the classic motorbike brawlers. Mini game inside Divaverse.
// Pseudo-3D course (projected segments riding on moving swells), three racers (you + two bots), punches and kicks,
// weather, procedural props, hair / bikini-tie physics, synthesised engine, sea, thunder and music.
// Art: the rider and ski key frames come from the artist's PSDs (baked by Tools/wetrash); everything else is drawn here.
#include "App.h"
#include "Gfx.h"
#include "Audio.h"
#include "Image.h"
#include "WetRashData.h"
#include "WetRashSide.h"
#include "resource.h"
#include <windowsx.h>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstdarg>

namespace {
constexpr wchar_t kClass[] = L"Divaverse.WetRash";
constexpr float SEG = 200.f, ROADW = 2000.f, CAMH = 2300.f, DEPTH = 0.84f;      // world units
constexpr int DRAW = 210, NSEG = 4600, STR = 26, SN = 7;
constexpr float PI = 3.14159265f;
std::wstring Fmt(const wchar_t* f, ...) { wchar_t b[256]; va_list a; va_start(a, f); _vsnwprintf(b, 255, f, a); va_end(a); b[255] = 0; return b; }
float Rnd(uint32_t& s) { s = s * 1664525u + 1013904223u; return ((s >> 8) & 0xFFFF) / 65536.f; }
float Hash(int i) { uint32_t s = (uint32_t)i * 2654435761u; s ^= s >> 15; s *= 2246822519u; s ^= s >> 13; return (s & 0xFFFF) / 65536.f; }

// ---------------------------------------------------------------- audio
struct GameAudio : AudioSource {
    std::atomic<float> engine{0}, rain{0}, wind{0}, music{1}; std::atomic<uint32_t> trig{0};
    enum { PUNCH, HIT, SPLASH, THUNDER, BEEP, GO, BELL, WHOOSH, NSFX };
    int rate = 48000, ch = 2; double ph = 0, ph2 = 0, mt = 0; float lp = 0, lp2 = 0, lp3 = 0, eng = 0; uint32_t rng = 99; double st[NSFX];
    GameAudio() { for (double& x : st) x = -1; }
    bool Prepare(int r, int c, std::wstring&) override { rate = r; ch = c; return true; }
    float Noise() { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) & 0xFFFF) / 32768.f - 1.f; }
    uint32_t Render(float* dst, uint32_t n) override {
        uint32_t t = trig.exchange(0); for (int i = 0; i < NSFX; ++i) if (t & (1u << i)) st[i] = 0;
        float e = engine.load(), rn = rain.load(), wd = wind.load(), mu = music.load(); double dt = 1.0 / rate;
        for (uint32_t i = 0; i < n; ++i) {
            eng += (e - eng) * 0.0005f; float nz = Noise(), s = 0;
            // engine: two detuned saws through a low-pass, pitch and brightness follow the throttle
            ph += (48 + 150 * eng) * dt; ph2 += (48 + 150 * eng) * 1.5 * dt; if (ph > 1) ph -= 1; if (ph2 > 1) ph2 -= 1;
            float saw = (float)(ph * 2 - 1) * 0.6f + (float)(ph2 * 2 - 1) * 0.3f; lp += (saw - lp) * (0.04f + 0.18f * eng); s += lp * (0.05f + 0.11f * eng);
            lp2 += (nz - lp2) * 0.25f; s += (nz - lp2) * 0.05f * eng;                              // spray hiss
            s += (nz - lp2) * 0.06f * rn;                                                          // rain
            lp3 += (nz - lp3) * (0.004f + 0.006f * (0.5f + 0.5f * (float)std::sin(mt * 0.7))); s += lp3 * 1.6f * wd;   // wind howl
            auto env = [&](int k, double len) { double x = st[k]; if (x < 0) return 0.f; st[k] += dt; if (x > len) { st[k] = -1; return 0.f; } return (float)(1 - x / len); };
            if (float a = env(PUNCH, 0.09)) s += (nz * 0.5f + (float)std::sin(st[PUNCH] * 900)) * a * a * 0.35f;
            if (float a = env(HIT, 0.16)) s += (nz * 0.3f + (float)std::sin(st[HIT] * 520)) * a * a * 0.45f;
            if (float a = env(SPLASH, 0.5)) s += lp2 * a * a * 1.4f;
            if (float a = env(THUNDER, 2.8)) s += lp3 * a * 9.f * (0.6f + 0.4f * (float)std::sin(st[THUNDER] * 31));
            if (float a = env(BEEP, 0.16)) s += (float)std::sin(st[BEEP] * 2 * PI * 660) * a * 0.25f;
            if (float a = env(GO, 0.45)) s += (float)std::sin(st[GO] * 2 * PI * 1320) * a * 0.25f;
            if (float a = env(BELL, 0.7)) s += ((float)std::sin(st[BELL] * 2 * PI * 740) + 0.5f * (float)std::sin(st[BELL] * 2 * PI * 1110)) * a * a * 0.14f;
            if (float a = env(WHOOSH, 0.14)) s += (nz - lp2) * a * 0.18f;
            if (mu > 0.01f) {                                                                      // surf loop: bass + arpeggio, 150 bpm
                static const int root[4] = { 45, 41, 48, 43 }, arp[8] = { 0, 7, 12, 7, 16, 12, 7, 12 };
                double beat = mt * 150 / 60; int step = (int)(beat * 2), bar = (step / 16) % 4; double f = step - (int)step, sp = beat * 2 - step;
                (void)f; float be = (float)std::exp(-sp * 3.5), bassF = 440.f * (float)std::pow(2.0, (root[bar] - 69 - 12) / 12.0), arpF = 440.f * (float)std::pow(2.0, (root[bar] + arp[step % 8] - 69 + 12) / 12.0);
                float bs = std::sin((float)(mt * 2 * PI * bassF)) > 0 ? 1.f : -1.f, ar = std::sin((float)(mt * 2 * PI * arpF)) > 0.3f ? 1.f : -1.f;
                float hat = (step % 2) ? (nz - lp2) * (float)std::exp(-sp * 14) * 0.5f : 0, kick = (step % 4 == 0) ? (float)std::sin(sp * 60 * std::exp(-sp * 6)) * (float)std::exp(-sp * 5) : 0;
                s += (bs * 0.05f * (step % 2 == 0 ? be : be * 0.6f) + ar * 0.022f * be + hat * 0.05f + kick * 0.16f) * mu;
            }
            mt += dt; s = s / (1 + std::fabs(s) * 0.6f);
            for (int c = 0; c < ch; ++c) dst[i * ch + c] = s;
        }
        return n;
    }
};

// ---------------------------------------------------------------- looks and sprites
struct Look { int skin = 1, bikini = 0, ski = 0, hair = 0; };
const uint32_t kSkin[] = { 0xF2C9A8, 0xC8855A, 0x9A5F3C, 0x6B3F28, 0xE8B48C, 0x4A2B1C };
const uint32_t kBikini[] = { 0xFF4FA8, 0x4FD6FF, 0xFFD23F, 0x3DDC84, 0xFF5A3C, 0x9B5BFF, 0xFFFFFF, 0x222222 };
const uint32_t kSki[] = { 0xE0262B, 0x1E88E5, 0xFFB300, 0x2EBD59, 0xB04CFF, 0xFF5FA2, 0xF2F2F2, 0x202428 };
const uint32_t kHair[] = { 0x2A1A12, 0xE8C46A, 0xB5462A, 0xFF6FB5, 0x58D0FF, 0xEDEDED };
const wchar_t* kWeather[] = { L"Sunny", L"Rain", L"Thunderstorm", L"Hurricane" };
const wchar_t* kCourse[] = { L"Lagoon (short, gentle)", L"Reef Run", L"Open Sea (long, rough)" };
ImageBGRA g_atlas, g_masks;
struct HairRoot { float x, y, nx; uint32_t c; };
struct Sprites { ComPtr<ID2D1Bitmap> f[WR_FRAMES], wet[WR_FRAMES]; std::vector<HairRoot> roots[WR_FRAMES]; unsigned gen = 0; int sig = -1; };

void Bake(Canvas& cv, const Look& lk, Sprites& sp) {
    int sig = lk.skin | (lk.bikini << 4) | (lk.ski << 8) | (lk.hair << 12);
    if (sp.sig == sig && sp.gen == cv.Generation() && sp.f[0]) return;
    sp.sig = sig; sp.gen = cv.Generation();
    if (!g_atlas.ok()) { DecodeImageResource(IDR_WR_ATLAS, 0, g_atlas); DecodeImageResource(IDR_WR_MASKS, 0, g_masks); }
    if (!g_atlas.ok() || !g_masks.ok()) return;
    auto ch = [](uint32_t c, int k) { return (float)((c >> (16 - 8 * k)) & 255); };
    uint32_t skin = kSkin[lk.skin], bik = kBikini[lk.bikini], ski = kSki[lk.ski], hair = kHair[lk.hair];
    for (int i = 0; i < WR_FRAMES; ++i) {
        const int* b = kWrFrames[i].bbox; int w = b[2] - b[0], h = b[3] - b[1], ox = (i % 5) * 360 + b[0], oy = (i / 5) * 360 + b[1];
        std::vector<uint8_t> px((size_t)w * h * 4), wet((size_t)w * h * 4, 0); std::vector<float> hgt((size_t)w * h, 0), lum((size_t)w * h, 0);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const uint8_t* s = &g_atlas.px[((size_t)(oy + y) * g_atlas.w + ox + x) * 4], *m = &g_masks.px[((size_t)(oy + y) * g_masks.w + ox + x) * 4]; uint8_t* o = &px[((size_t)y * w + x) * 4];
            float a = s[3] / 255.f; if (a <= 0) continue;
            float c[3] = { s[2] / a, s[1] / a, s[0] / a }, l = 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2];           // straight RGB
            if (i < WR_RIDE) { if (c[0] > c[1] * 1.7f && c[0] > c[2] * 1.7f) { float k = c[0] / 215.f; for (int q = 0; q < 3; ++q) c[q] = std::min(255.f, ch(ski, q) * k + 30 * (k > 1 ? k - 1 : 0)); } }   // red trim -> ski colour
            else if (m[1] > 127) { float k = std::pow(l / 90.f, 0.9f); for (int q = 0; q < 3; ++q) c[q] = std::min(255.f, ch(hair, q) * (0.18f + 0.82f * k) + (k > 1.5f ? (k - 1.5f) * 60 : 0)); }   // hair (sculpts mask): recoloured, shading kept
            else if (m[2] > 127) { float k = l / 118.f; for (int q = 0; q < 3; ++q) c[q] = std::min(255.f, ch(bik, q) * (0.25f + 0.75f * k)); }                 // bikini (mask R): keep shading, swap hue
            else { static const float base[3] = { 201, 136, 102 }; for (int q = 0; q < 3; ++q) c[q] = std::min(255.f, c[q] * ch(skin, q) / base[q]); }          // skin: scale toward the chosen tone
            o[0] = (uint8_t)(c[2] * a); o[1] = (uint8_t)(c[1] * a); o[2] = (uint8_t)(c[0] * a); o[3] = s[3]; lum[(size_t)y * w + x] = l / 255.f; hgt[(size_t)y * w + x] = 1;
        }
        // wet-look map: a height field from the silhouette (eroded distance) plus shading, turned into a normal and lit
        // by a fixed key light; drawn over the sprite with opacity = how wet she is.
        for (int pass = 0; pass < 5; ++pass) { std::vector<float> n = hgt; for (int y = 1; y < h - 1; ++y) for (int x = 1; x < w - 1; ++x) { size_t k = (size_t)y * w + x; if (hgt[k] > 0) n[k] = 1 + std::min({ hgt[k - 1], hgt[k + 1], hgt[k - w], hgt[k + w] }); } hgt.swap(n); }
        for (int y = 1; y < h - 1; ++y) for (int x = 1; x < w - 1; ++x) {
            size_t k = (size_t)y * w + x; if (!px[k * 4 + 3]) continue;
            float hx = (hgt[k + 1] + lum[k + 1] * 3) - (hgt[k - 1] + lum[k - 1] * 3), hy = (hgt[k + w] + lum[k + w] * 3) - (hgt[k - w] + lum[k - w] * 3);
            float nx = -hx, ny = -hy, nz = 1.6f, il = 1 / std::sqrt(nx * nx + ny * ny + nz * nz), d = (nx * -0.45f + ny * -0.65f + nz * 0.62f) * il;
            float spec = std::pow(std::max(0.f, d), 14.f) * (0.35f + 0.65f * Hash((int)k * 7 + i)); uint8_t v = (uint8_t)std::min(255.f, spec * 330);
            uint8_t* o = &wet[k * 4]; o[0] = o[1] = o[2] = o[3] = v;
        }
        {   // strand roots along the lower edge of the hair region, coloured from the hair pixel they grow out of
            auto& R = sp.roots[i]; R.clear(); std::vector<int> low(w, -1);
            for (int x = 0; x < w; ++x) for (int y = h - 1; y >= 0; --y) { const uint8_t* m = &g_masks.px[((size_t)(oy + y) * g_masks.w + ox + x) * 4]; if (m[1] > 127 && px[((size_t)y * w + x) * 4 + 3]) { low[x] = y; break; } }
            int x0 = -1, x1 = -1; for (int x = 0; x < w; ++x) if (low[x] >= 0) { if (x0 < 0) x0 = x; x1 = x; }
            if (x0 >= 0) for (int k = 0; k < STR; ++k) {
                int x = x0 + (int)((x1 - x0) * (k + 0.5f) / STR); while (x < x1 && low[x] < 0) ++x; if (low[x] < 0) continue;
                int y = std::max(0, low[x] - 2); const uint8_t* p = &px[((size_t)y * w + x) * 4]; float a = std::max(1, (int)p[3]) / 255.f;
                uint32_t col = ((uint32_t)std::min(255.f, p[2] / a) << 16) | ((uint32_t)std::min(255.f, p[1] / a) << 8) | (uint32_t)std::min(255.f, p[0] / a);
                R.push_back({ (float)(x + b[0]), (float)(y + b[1]), (x - (x0 + x1) * 0.5f) / std::max(1.f, (x1 - x0) * 0.5f), col });
            }
        }
        sp.f[i] = cv.MakeBitmap(px.data(), w, h); sp.wet[i] = cv.MakeBitmap(wet.data(), w, h);
    }
}

// ---------------------------------------------------------------- world
enum Prop { P_NONE, P_BUOY_L, P_BUOY_R, P_ROCK, P_CRATE, P_LOG, P_RAMP, P_ISLAND, P_BOAT, P_LIGHT, P_PIER, P_GATE };
struct Seg { float curve = 0; uint8_t prop = 0, prop2 = 0; float px = 0, px2 = 0; };
enum St { S_RIDE, S_ATK, S_HIT, S_FALL, S_SWIM };
struct V2 { float x, y; };
struct Racer {
    std::wstring name; Look look; Sprites spr; bool bot = false;
    int score = 0; float trick = 0, spin = 0, jig = 0, jigV = 0;
    float z = 0, x = 0, speed = 0, hp = 100, lean = 0, air = 0, vy = 0, wet = 0, bob = 0, bobV = 0, boost = 0;
    int st = S_RIDE, frame = WR_RIDE, prev = WR_RIDE, side = 1, atkKind = 0, place = 1; float stT = 0, frameT = 1, cool = 0, finish = -1, aiX = 0, aiT = 0, aiSkill = 1; bool hitDone = false, tuck = false;
    V2 hair[STR][SN], hairP[STR][SN]; V2 tie[4][2][3], tieP[4][2][3]; bool simInit = false;
};
struct Particle { float x, y, vx, vy, life, size; uint32_t col; };
struct Game {
    HWND h = nullptr; Canvas cv; std::unique_ptr<AudioOut> out; GameAudio* au = nullptr;
    enum { MENU, RIDER, CONTROLS, COUNT, RACE, PAUSE, RESULT, SIDE } mode = MENU; int sel = 0, weather = 0, custSel = 0, course = 1, len = NSEG;
    Seg seg[NSEG]; Racer r[3]; std::vector<Particle> parts;
    double last = 0, acc = 0; float t = 0, raceT = 0, countT = 0, flash = 0, nextBolt = 4, shake = 0, gust = 0; uint32_t rng = 1234; std::wstring msg; float msgT = 0;
    bool keyAtk[2] = {}, padPrev[8] = {}; DWORD(WINAPI* xi)(DWORD, void*) = nullptr; bool xiTried = false, auto_ = false;
    Look look;
} G;

float Swell(float segIdx) {
    float a = G.weather == 0 ? 30.f : G.weather == 1 ? 60.f : G.weather == 2 ? 120.f : 250.f;
    return a * (std::sin(segIdx * 0.13f - G.t * 1.9f) + 0.5f * std::sin(segIdx * 0.37f + G.t * 2.7f));
}
void BuildTrack() {
    static const float amp[3] = { 2.6f, 4.4f, 5.8f }, dens[3] = { 0.6f, 1.f, 1.5f }, frac[3] = { 0.55f, 0.78f, 1.f };
    uint32_t s = 777 + G.weather * 31 + G.course * 1013; for (auto& g : G.seg) g = Seg{}; G.len = (int)(NSEG * frac[G.course]);
    int i = 60;
    while (i < G.len - 120) {                               // straights and bends of varying strength
        int len = 30 + (int)(Rnd(s) * 70); float c = Rnd(s) < 0.35f ? 0 : (Rnd(s) * 2 - 1) * amp[G.course];
        for (int k = 0; k < len && i < G.len - 120; ++k, ++i) { float e = std::min({ 1.f, k / 12.f, (len - k) / 12.f }); G.seg[i].curve = c * e; }
    }
    for (i = 8; i < NSEG; ++i) {
        Seg& g = G.seg[i];
        if (i % 40 == 0) { g.prop2 = (i / 40) % 2 ? P_BUOY_L : P_BUOY_R; g.px2 = (i / 40) % 2 ? -1.3f : 1.3f; continue; }
        float r = Hash(i * 13 + G.weather + G.course * 57) / dens[G.course];
        if (i > 80 && i < G.len - 60) {
            if (r < 0.018f) { g.prop = P_ROCK; g.px = Hash(i) * 1.7f - 0.85f; }
            else if (r < 0.034f) { g.prop = P_CRATE; g.px = Hash(i + 5) * 1.6f - 0.8f; }
            else if (r < 0.046f) { g.prop = P_LOG; g.px = Hash(i + 9) * 1.4f - 0.7f; }
            else if (r < 0.056f) { g.prop = P_RAMP; g.px = Hash(i + 3) * 1.2f - 0.6f; }
        }
        float q = Hash(i * 7 + 1);                         // scenery beside the course
        if (q < 0.03f) { g.prop2 = P_ISLAND; g.px2 = (Hash(i + 2) < 0.5f ? -1 : 1) * (2.2f + Hash(i + 4) * 3); }
        else if (q < 0.05f) { g.prop2 = P_BOAT; g.px2 = (Hash(i + 2) < 0.5f ? -1 : 1) * (1.9f + Hash(i + 4) * 2.5f); }
        else if (q < 0.058f) { g.prop2 = P_LIGHT; g.px2 = (Hash(i + 2) < 0.5f ? -1 : 1) * (2.6f + Hash(i + 4) * 2); }
        else if (q < 0.07f) { g.prop2 = P_PIER; g.px2 = (Hash(i + 2) < 0.5f ? -1 : 1) * 1.55f; }
    }
    G.seg[G.len - 60].prop = P_GATE; G.seg[G.len - 60].px = 0; G.seg[G.len - 60].prop2 = 0;
    G.seg[40].prop = P_GATE; G.seg[40].px = 0;
}
void Say(const std::wstring& m) { G.msg = m; G.msgT = 2.2f; }
void Sfx(int k) { if (G.au) G.au->trig.fetch_or(1u << k); }
void SetFrame(Racer& r, int f) { if (r.frame != f) { r.prev = r.frame; r.frame = f; r.frameT = 0; } }
void Spray(float x, float y, int n, float pow, uint32_t col = 0xFFFFFF) {
    for (int i = 0; i < n && G.parts.size() < 600; ++i) { float a = -PI * (0.15f + 0.7f * Rnd(G.rng)), v = pow * (0.4f + Rnd(G.rng)); G.parts.push_back({ x, y, std::cos(a) * v, std::sin(a) * v, 0.5f + Rnd(G.rng) * 0.5f, 2 + Rnd(G.rng) * 4, col }); }
}
void StartRace() {
    BuildTrack(); G.parts.clear(); G.raceT = 0; G.countT = 3.6f; G.mode = Game::COUNT; G.flash = 0;
    static const wchar_t* names[3] = { L"YOU", L"ROXY", L"KAI" };
    for (int i = 0; i < 3; ++i) {
        Racer& r = G.r[i]; Sprites keep = r.spr; r = Racer{}; r.spr = keep; r.name = names[i]; r.bot = i > 0 || G.auto_; r.z = 44 - i * 1.6f; r.x = (i - 1) * 0.55f; r.aiX = r.x; r.aiSkill = i == 1 ? 1.02f : 0.95f;
        r.look = i == 0 ? G.look : Look{ (G.look.skin + 1 + i) % 6, (G.look.bikini + 2 + i * 2) % 8, (G.look.ski + 1 + i * 2) % 8, (G.look.hair + i * 2) % 6 };
    }
    if (G.au) { G.au->rain = G.weather == 0 ? 0.f : G.weather == 1 ? 0.6f : 1.f; G.au->wind = G.weather < 2 ? 0.f : G.weather == 2 ? 0.45f : 1.f; }
}

// ---------------------------------------------------------------- simulation
void SideTick(float dt); void SideStart(); void SideRender(float W, float H);
struct Input { float steer = 0, gas = 0, brake = 0; bool punch = false, kick = false, tuck = false, taunt = false; };
void Hurt(Racer& r, float dmg, int fromSide) {
    r.hp -= dmg; r.st = S_HIT; r.stT = 0; r.side = fromSide; SetFrame(r, dmg > 14 ? WR_HIT2 : WR_HIT1); r.speed *= 0.86f; r.x -= fromSide * 0.10f; r.bobV -= 60;
    if (r.hp <= 0) { r.st = S_FALL; r.stT = 0; SetFrame(r, WR_FALL1); Sfx(GameAudio::SPLASH); if (&r == &G.r[0]) Say(L"WIPE OUT!"); }
}
void Step(Racer& r, const Input& in, float dt) {
    int si = ((int)r.z % NSEG + NSEG) % NSEG; const Seg& sg = G.seg[si];
    float lead = 0; for (Racer& o : G.r) lead = std::max(lead, o.z - r.z);
    float maxSp = (r.tuck ? 0.82f : 0.72f) * (r.bot ? r.aiSkill : (lead > 12 ? 1.10f : 1.f)) + r.boost * 0.12f, windX = (G.weather == 3 ? 0.55f : G.weather == 2 ? 0.2f : 0) * (0.6f + 0.4f * std::sin(G.t * 0.6f)) + G.gust;
    r.stT += dt; r.cool = std::max(0.f, r.cool - dt); r.boost = std::max(0.f, r.boost - dt * 0.5f); r.frameT = std::min(1.f, r.frameT + dt * 9);
    r.wet = std::clamp(r.wet + dt * ((G.weather > 0 ? 0.25f : -0.05f) + std::fabs(r.lean) * r.speed * 0.6f), 0.4f, 1.f);
    if (r.st == S_FALL) { r.speed *= 1 - 2.5f * dt; if (r.stT > 0.35f) SetFrame(r, WR_FALL2); if (r.stT > 0.9f) { r.st = S_SWIM; r.stT = 0; SetFrame(r, WR_SWIM); r.wet = 1; } }
    else if (r.st == S_SWIM) { r.speed *= 1 - 3 * dt; if (r.stT > 1.1f) { r.st = S_RIDE; r.hp = 100; r.speed = 0.45f; r.boost = 1; SetFrame(r, WR_RIDE); if (&r == &G.r[0]) Say(L"Back on!"); } }
    else {
        r.tuck = in.tuck && r.st == S_RIDE;
        float acc = in.gas * 1.15f * (1 - r.speed / maxSp) - in.brake * 1.4f - 0.06f;                 // near top speed in about two seconds
        r.speed = std::clamp(r.speed + acc * dt, 0.f, 1.1f);
        float grip = 0.45f + 0.55f * std::min(1.f, r.speed / 0.5f); r.lean += (in.steer - r.lean) * std::min(1.f, dt * 16);            // the hull answers at once
        r.x += (r.lean * 2.3f * grip - sg.curve * 0.035f * r.speed + windX * 0.05f) * dt;
        if (std::fabs(r.x) > 0.97f) {                                      // lane rope: it gives a little and pushes you back, nothing more
            float sgn = r.x > 0 ? 1.f : -1.f; r.x = sgn * 0.97f - sgn * 0.02f; r.lean -= sgn * 0.5f; r.speed *= 1 - 0.5f * dt; r.bobV -= 30;
            if (&r == &G.r[0] && Hash((int)(G.t * 30)) < 0.3f) Sfx(GameAudio::BELL); }
        if (r.st == S_HIT && r.stT > 0.4f) r.st = S_RIDE;
        if (r.st == S_ATK) {
            if (!r.hitDone && r.stT > 0.10f) {                                 // contact frame: anyone alongside on that side?
                r.hitDone = true; Sfx(GameAudio::WHOOSH);
                for (Racer& o : G.r) if (&o != &r && o.st <= S_HIT) { float dz = o.z - r.z, dx = o.x - r.x; if (std::fabs(dz) < 1.6f && dx * r.side > 0.05f && std::fabs(dx) < (r.atkKind ? 0.62f : 0.48f)) { Hurt(o, r.atkKind ? 26.f : 17.f, -r.side); r.score += r.atkKind ? 150 : 100; Sfx(r.atkKind ? GameAudio::HIT : GameAudio::PUNCH); G.shake = 0.25f; if (&r == &G.r[0]) Say(r.atkKind ? L"KICK!" : L"POW!"); } }
            }
            if (r.stT > 0.34f) r.st = S_RIDE;
        } else if (r.st == S_RIDE && r.cool <= 0 && (in.punch || in.kick)) {
            float best = 9; r.side = in.steer < -0.2f ? -1 : in.steer > 0.2f ? 1 : r.side;
            for (Racer& o : G.r) if (&o != &r && std::fabs(o.z - r.z) < 2.5f && std::fabs(o.x - r.x) < best) { best = std::fabs(o.x - r.x); r.side = o.x > r.x ? 1 : -1; }     // aim at whoever is closest
            r.st = S_ATK; r.stT = 0; r.hitDone = false; r.atkKind = in.kick; r.cool = in.kick ? 0.7f : 0.45f; SetFrame(r, in.kick ? WR_KICK : WR_PUNCH);
        }
        if (r.st == S_RIDE) {
            bool close_ = false; for (Racer& o : G.r) if (&o != &r && std::fabs(o.z - r.z) < 3 && std::fabs(o.x - r.x) < 0.8f) { close_ = true; if (!r.tuck && std::fabs(in.steer) < 0.3f) r.side = o.x > r.x ? 1 : -1; }
            SetFrame(r, in.taunt ? WR_TAUNT : r.tuck ? WR_TUCK : std::fabs(r.lean) > 0.45f ? WR_LEAN : close_ ? WR_LOOK : WR_RIDE);
            if (std::fabs(r.lean) > 0.45f) r.side = r.lean > 0 ? 1 : -1;
            if (in.taunt && r.speed > 0.2f) r.boost = std::min(1.f, r.boost + dt * 0.8f);                                        // showing off pays
        }
        // props on this segment
        if (r.air <= 1) {
            auto touch = [&](uint8_t p, float px) {
                if (!p || std::fabs(px - r.x) > (p == P_RAMP ? 0.22f : 0.16f)) return;
                if (p == P_RAMP) { if (r.speed > 0.15f) { r.vy = 1500 + r.speed * 2200; r.trick = 0; r.air = 2; Sfx(GameAudio::WHOOSH); if (&r == &G.r[0]) Say(L"AIR!"); } }
                else if (p == P_ROCK || p == P_CRATE || p == P_LOG) { if (r.cool < 0.3f) { r.speed *= 0.8f; r.x += (px > r.x ? -0.16f : 0.16f); r.bobV -= 120; r.cool = 0.45f; r.wet = 1;      // a bump and a splash: only rivals can knock you off
                    if (&r == &G.r[0]) { Sfx(GameAudio::SPLASH); G.shake = 0.15f; } } }
            };
            touch(sg.prop, sg.px);
        }
        for (Racer& o : G.r) if (&o < &r && o.st <= S_HIT && std::fabs(o.z - r.z) < 0.9f && std::fabs(o.x - r.x) < 0.17f) { float d = r.x > o.x ? 1.f : -1.f; r.x += d * 0.5f * dt; o.x -= d * 0.5f * dt; }   // hulls rub
    }
    if (r.air > 0 || r.vy != 0) {
        r.air += r.vy * dt; r.vy -= 5200 * dt;
        if (r.air > 60 && r.st == S_RIDE && (in.taunt || r.bot)) { r.trick += dt; r.spin += dt * 360 / 0.7f; SetFrame(r, WR_TAUNT); }
        if (r.air <= 0) { r.air = 0; r.vy = 0; r.bobV += 240; r.wet = 1; if (&r == &G.r[0]) Sfx(GameAudio::SPLASH);
            if (r.trick > 0.2f) { int pts = std::min(1000, 250 + (int)(r.trick * 1000) / 50 * 50); r.score += pts; r.boost = 1; if (&r == &G.r[0]) { Say(Fmt(L"TRICK  +%d", pts)); Sfx(GameAudio::GO); } }
            r.trick = 0; r.spin = 0; }
    }
    r.z += r.speed * 60 * dt * (1.f / 1.f);
    if (r.air <= 0 && r.st <= S_HIT) r.bobV += std::sin(G.t * 17 + r.z * 0.7f) * r.speed * 1500 * dt;      // chop slapping the hull
    r.bobV += (-r.bob * 90 - r.bobV * 9) * dt; r.bob += r.bobV * dt;
    r.jigV += ((-r.bobV * 0.035f + (r.st == S_HIT ? 3.f : 0) - r.jig) * 210 - r.jigV * 9) * dt; r.jig = std::clamp(r.jig + r.jigV * dt, -4.f, 4.f);   // soft-body follow-through, sprung and damped
    if (r.finish < 0 && r.z >= G.len - 60) { r.finish = G.raceT; if (&r == &G.r[0]) Sfx(GameAudio::GO); }
}
Input BotInput(Racer& r, float dt) {
    Input in; in.gas = 1; r.aiT -= dt; int si = (int)r.z;
    if (r.aiT <= 0) { r.aiT = 0.25f + Hash(si + (int)(&r - G.r)) * 0.4f; float tgt = std::clamp(-G.seg[(si + 8) % NSEG].curve * 0.12f, -0.6f, 0.6f);
        for (int k = 2; k < 16; ++k) { const Seg& g = G.seg[(si + k) % NSEG]; if (g.prop >= P_ROCK && g.prop <= P_LOG && std::fabs(g.px - tgt) < 0.25f) tgt = g.px + (g.px > 0 ? -0.45f : 0.45f); if (g.prop == P_RAMP && Hash(si) < 0.5f) tgt = g.px; }
        for (Racer& o : G.r) if (&o != &r && std::fabs(o.z - r.z) < 4 && o.st <= S_HIT && Hash(si * 3 + 1) < 0.55f) tgt = o.x + (r.x > o.x ? 0.3f : -0.3f);        // close in for a swing
        r.aiX = tgt; }
    in.steer = std::clamp((r.aiX - r.x) * 4, -1.f, 1.f);
    for (Racer& o : G.r) if (&o != &r && o.st <= S_HIT && std::fabs(o.z - r.z) < 1.4f && std::fabs(o.x - r.x) < 0.5f && Hash((int)(G.t * 6) + si) < 0.06f) (Hash(si + 2) < 0.4f ? in.kick : in.punch) = true;
    in.tuck = std::fabs(G.seg[si % NSEG].curve) < 0.5f && r.speed > 0.5f;
    float lead = G.r[0].z - r.z; r.aiSkill += ((lead > 25 ? 1.12f : lead < -25 ? 0.90f : 1.0f) - r.aiSkill) * dt * 0.5f;                                            // rubber band
    return in;
}
Input PlayerInput() {
    Input in; if (GetForegroundWindow() != G.h) return in;
    auto k = [](int v) { return (GetAsyncKeyState(v) & 0x8000) != 0; };
    in.steer = (k(VK_RIGHT) || k('D') ? 1.f : 0) - (k(VK_LEFT) || k('A') ? 1.f : 0); in.gas = k(VK_UP) || k('W') ? 1.f : 0; in.brake = k(VK_DOWN) || k('S') ? 1.f : 0;
    in.punch = k('J') || k('Z'); in.kick = k('K') || k('X'); in.tuck = k(VK_SPACE) || k(VK_SHIFT); in.taunt = k('L') || k('C');
    if (!G.xiTried) { G.xiTried = true; for (auto* dll : { L"xinput1_4.dll", L"xinput9_1_0.dll", L"xinput1_3.dll" }) if (HMODULE m = LoadLibraryW(dll)) { G.xi = (DWORD(WINAPI*)(DWORD, void*))(void*)GetProcAddress(m, "XInputGetState"); if (G.xi) break; } }
    struct XS { DWORD pkt; WORD btn; BYTE lt, rt; SHORT lx, ly, rx, ry; } xs{};
    if (G.xi && G.xi(0, &xs) == 0) {
        float lx = xs.lx / 32767.f; if (std::fabs(lx) > 0.18f) in.steer = lx; if (xs.btn & 0x0004) in.steer = -1; if (xs.btn & 0x0008) in.steer = 1;
        in.gas = std::max(in.gas, std::max(xs.rt / 255.f, (xs.btn & 0x1000) ? 1.f : 0.f)); in.brake = std::max(in.brake, xs.lt / 255.f);
        in.punch |= (xs.btn & 0x4000) != 0; in.kick |= (xs.btn & 0x2000) != 0; in.tuck |= (xs.btn & 0x0200) != 0 || (xs.btn & 0x0100) != 0; in.taunt |= (xs.btn & 0x8000) != 0;
    }
    return in;
}
void Tick(float dt) {
    G.t += dt; G.msgT -= dt; G.flash = std::max(0.f, G.flash - dt * 2.5f); G.shake = std::max(0.f, G.shake - dt);
    G.gust += ((G.weather == 3 ? (Hash((int)(G.t * 0.5f)) - 0.5f) * 1.2f : 0) - G.gust) * dt;
    if (G.weather >= 2) { G.nextBolt -= dt; if (G.nextBolt <= 0) { G.nextBolt = (G.weather == 3 ? 2.5f : 5) + Rnd(G.rng) * 6; G.flash = 1; Sfx(GameAudio::THUNDER); } }
    for (auto& p : G.parts) { p.x += p.vx * dt; p.y += p.vy * dt; p.vy += 900 * dt; p.life -= dt; }
    G.parts.erase(std::remove_if(G.parts.begin(), G.parts.end(), [](const Particle& p) { return p.life <= 0; }), G.parts.end());
    if (G.mode == Game::COUNT) { float b = G.countT; G.countT -= dt; if ((int)b != (int)G.countT && G.countT > 0.5f) Sfx(GameAudio::BEEP); if (G.countT <= 0.6f && b > 0.6f) { Sfx(GameAudio::GO); G.mode = Game::RACE; Say(L"GO!"); } }
    if (G.mode == Game::SIDE) { SideTick(dt); return; }
    if (G.mode != Game::RACE && G.mode != Game::RESULT) { if (G.au) G.au->engine = G.mode == Game::COUNT ? 0.15f : 0.05f; return; }
    G.raceT += dt;
    for (int i = 0; i < 3; ++i) { Racer& r = G.r[i]; Input in = r.finish >= 0 ? Input{} : r.bot ? BotInput(r, dt) : PlayerInput(); if (r.finish >= 0) { in.brake = 0.3f; } Step(r, in, dt); }
    for (int i = 0; i < 3; ++i) { int p = 1; for (int j = 0; j < 3; ++j) if (j != i && (G.r[j].finish >= 0 && (G.r[i].finish < 0 || G.r[j].finish < G.r[i].finish) ? true : G.r[i].finish < 0 && G.r[j].finish < 0 && G.r[j].z > G.r[i].z)) ++p; G.r[i].place = p; }
    if (G.au) G.au->engine = G.r[0].st >= S_FALL ? 0.f : std::min(1.f, G.r[0].speed / 0.8f);
    if (G.mode == Game::RACE && G.r[0].finish >= 0 && G.raceT - G.r[0].finish > 2.5f) { G.mode = Game::RESULT; G.sel = 0; }
}

// ---------------------------------------------------------------- drawing
struct Pal { uint32_t skyT, skyB, deep, lite, fog; };
const Pal kPal[4] = { { 0x2E7BE0, 0xBFE6FF, 0x0B5FA8, 0x2FA3E0, 0xBFE6FF }, { 0x5B6C80, 0xA9B8C6, 0x1D4F78, 0x3E7FA6, 0xA9B8C6 }, { 0x232B3D, 0x58637A, 0x12324F, 0x2A5876, 0x58637A }, { 0x141821, 0x3A4252, 0x0D2436, 0x24485C, 0x3A4252 } };
Canvas& C() { return G.cv; }

void SimStrings(Racer& r, int f, float dt) {
    const WrFrameInfo& fi = kWrFrames[f]; float hx = fi.head[0], hy = fi.head[1], hr = fi.head[2];
    float flip = (float)r.side, wy = 500 + r.speed * 5200, wx = (-r.lean * 900 + G.gust * 2500 + (G.weather == 3 ? 1200.f : 0)) * flip;
    auto chain = [&](V2* p, V2* q, int n, V2 a, float seg, float phase) {
        if (!r.simInit) for (int i = 0; i < n; ++i) p[i] = q[i] = { a.x, a.y + seg * i };
        p[0] = a;
        for (int i = 1; i < n; ++i) { V2 v = { (p[i].x - q[i].x) * 0.94f, (p[i].y - q[i].y) * 0.94f }; q[i] = p[i];
            float fl = std::sin(G.t * 11 + phase + i * 0.9f) * (120 + r.speed * 2600); p[i].x += v.x + (wx + fl) * dt * dt; p[i].y += v.y + wy * dt * dt; }
        for (int it = 0; it < 3; ++it) for (int i = 1; i < n; ++i) { float dx = p[i].x - p[i - 1].x, dy = p[i].y - p[i - 1].y, d = std::sqrt(dx * dx + dy * dy) + 1e-4f, k = (d - seg) / d; p[i].x -= dx * k; p[i].y -= dy * k; }
    };
    (void)hx; (void)hy;
    const auto& R = r.spr.roots[f];
    if (hr > 0 && !R.empty()) for (int s = 0; s < STR; ++s) { const HairRoot& o = R[s % R.size()]; chain(r.hair[s], r.hairP[s], SN, { o.x + (s >= (int)R.size() ? 1.5f : 0), o.y }, hr * (0.24f + 0.12f * Hash(s * 3 + 1)) * (1.2f - 0.6f * std::fabs(o.nx)), s * 1.7f); }
    for (int t = 0; t < fi.nTies && t < 4; ++t) for (int k = 0; k < 2; ++k) chain(r.tie[t][k], r.tieP[t][k], 3, { fi.ties[t][0] + (k ? 1.5f : -1.5f), fi.ties[t][1] }, 4.5f, t * 3 + k * 5.f);
    r.simInit = true;
}
void DrawRacer(Racer& r, float sx, float sy, float ppu, float alpha = 1.f) {
    Bake(C(), r.look, r.spr); if (!r.spr.f[0]) return;
    float k = ppu * 4.3f; if (k < 0.04f) return;
    int ski = r.speed < 0.06f ? WR_SKI_STOP : (r.air > 0 || (r.boost > 0.3f && r.speed > 0.7f)) ? WR_SKI_FAST : WR_SKI_FWD;       // nose-up only in the air or on boost
    bool off = r.st >= S_FALL;
    float lift = (r.air * ppu) - r.bob * k * 0.03f, rot = r.lean * 9.f + (r.air > 0 ? r.lean * 10 : 0) + r.spin;
    // wake: foam fan behind the stern (toward the camera) and a dark contact patch
    if (r.air <= 0) { float w = 70 * k, len = (60 + r.speed * 520) * k; C().Quad(sx - w * 0.35f, sy - 6 * k, sx + w * 0.35f, sy - 6 * k, sx + w * (0.6f + r.speed * 1.6f), sy + len, sx - w * (0.6f + r.speed * 1.6f), sy + len, Hex(0xFFFFFF, 0.20f * alpha * std::min(1.f, r.speed * 5)));
        C().Quad(sx - w * 0.12f, sy, sx + w * 0.12f, sy, sx + w * 0.3f, sy + len * 0.8f, sx - w * 0.3f, sy + len * 0.8f, Hex(0xFFFFFF, 0.35f * alpha * std::min(1.f, r.speed * 5))); }
    else C().Circle(sx, sy, 50 * k, Hex(0x04121E, 0.25f));
    auto M = [&](bool flip) { return D2D1::Matrix3x2F::Translation(-182, -296) * D2D1::Matrix3x2F::Scale(flip ? -k : k, k) * D2D1::Matrix3x2F::Rotation(rot) * D2D1::Matrix3x2F::Translation(sx, sy - lift); };
    auto blit = [&](int f, float op) { const int* b = kWrFrames[f].bbox; RectF d = RF((float)b[0], (float)b[1], (float)(b[2] - b[0]), (float)(b[3] - b[1])); C().BitmapPx(r.spr.f[f].Get(), d, op * alpha); if (r.wet > 0.02f) { float wv = std::min(1.f, r.wet * 1.5f) * op * alpha; C().BitmapPx(r.spr.wet[f].Get(), d, wv); C().BitmapPx(r.spr.wet[f].Get(), d, wv * 0.6f); } };   // wet skin: the baked highlight map, laid on twice
    if (r.air <= 0) {                                           // mirror image on the water, squashed and faint
        C().SetTransform(D2D1::Matrix3x2F::Translation(-182, -296) * D2D1::Matrix3x2F::Scale(k, -k * 0.5f) * D2D1::Matrix3x2F::Translation(sx + r.lean * 14 * k, sy + 26 * k));
        const int* b = kWrFrames[ski].bbox; C().BitmapPx(r.spr.f[ski].Get(), RF((float)b[0], (float)b[1], (float)(b[2] - b[0]), (float)(b[3] - b[1])), 0.16f * alpha);
        if (!off) { const int* q = kWrFrames[r.frame].bbox; C().BitmapPx(r.spr.f[r.frame].Get(), RF((float)q[0], (float)q[1], (float)(q[2] - q[0]), (float)(q[3] - q[1])), 0.13f * alpha); }
    }
    C().SetTransform(M(false));
    if (r.st == S_SWIM) C().SetTransform(D2D1::Matrix3x2F::Translation(-150 * (float)r.side, 0) * M(false));          // the ski drifts off while she swims back
    blit(ski, 1);
    { const WrFrameInfo& s = kWrFrames[ski]; if (r.speed > 0.05f && r.air <= 0) for (int q = 0; q < 3; ++q) C().Circle(s.jet[0] + (Hash((int)(G.t * 60) + q) - 0.5f) * 10, s.jet[1] + 6 + q * 7.f, 7 + q * 3.f, Hex(0xFFFFFF, 0.55f - q * 0.15f)); }   // jet exhaust
    bool flip = r.side < 0 && r.frame != WR_RIDE && r.frame != WR_TUCK;
    C().SetTransform(M(flip));
    if (r.frameT < 1 && r.prev != r.frame) blit(r.prev, 1 - r.frameT);
    blit(r.frame, r.frameT < 1 ? 0.35f + 0.65f * r.frameT : 1);
    { const WrFrameInfo& q = kWrFrames[r.frame]; int x0 = std::max(q.chest[0], q.bbox[0]), y0 = std::max(q.chest[1], q.bbox[1]), x1 = std::min(q.chest[2], q.bbox[2]), y1 = std::min(q.chest[3], q.bbox[3]);
      if (x1 > x0 && y1 > y0 && std::fabs(r.jig) > 0.08f) { float w = (float)(x1 - x0), h = (float)(y1 - y0), sx2 = 1 + r.jig * 0.012f;      // the chest slice redrawn a few pixels off, squashing as it travels
          C().BitmapSrc(r.spr.f[r.frame].Get(), RF(x0 - w * (sx2 - 1) / 2, y0 + r.jig, w * sx2, h * (2 - sx2)), RF((float)(x0 - q.bbox[0]), (float)(y0 - q.bbox[1]), w, h), alpha); } }
    // hair and bikini ties (simulated in sprite space, so they follow every pose and the flip)
    const WrFrameInfo& fi = kWrFrames[r.frame]; Color hc = Hex(kHair[r.look.hair], alpha), hd = Mix(hc, Hex(0x000000, alpha), 0.35f), bc = Hex(kBikini[r.look.bikini], alpha);
    for (int t = 0; t < fi.nTies && t < 4; ++t) { for (int q = 0; q < 2; ++q) for (int i = 1; i < 3; ++i) C().Line(r.tie[t][q][i - 1].x, r.tie[t][q][i - 1].y, r.tie[t][q][i].x, r.tie[t][q][i].y, bc, 1.3f); C().Circle(fi.ties[t][0], fi.ties[t][1], 2.2f, bc); }
    {   // loose ends: thin, tapered, semi-transparent, two passes (shadow underneath, lit strand on top)
        const auto& R = r.spr.roots[r.frame];
        if (fi.head[2] > 0 && !R.empty()) for (int pass = 0; pass < 2; ++pass) for (int s = 0; s < STR; ++s) {
            const HairRoot& o = R[s % R.size()]; Color c0 = Hex(o.c, alpha * (pass ? 0.55f : 0.35f)); if (!pass) c0 = Mix(c0, Hex(0x000000, c0.a), 0.45f);
            else if (s % 4 == 1) c0 = Mix(c0, Hex(0xFFFFFF, c0.a), 0.15f);
            for (int i = 1; i < SN; ++i) { float t = (float)i / SN; C().Line(r.hair[s][i - 1].x + (pass ? 0 : 0.6f), r.hair[s][i - 1].y + (pass ? 0 : 0.6f), r.hair[s][i].x + (pass ? 0 : 0.6f), r.hair[s][i].y + (pass ? 0 : 0.6f), Alpha(c0, 1 - t * 0.85f), (pass ? 1.15f : 1.6f) * (1 - t * 0.7f)); }
        }
    }
    C().ResetTransform();
    if (r.air <= 0) {                                           // she sits IN the water: a translucent sheet over the lower hull with foam along the waterline
        const Pal& p = kPal[G.weather]; float sp = std::min(1.f, r.speed / 0.7f), d = (62 - 22 * sp) * k, hw = 84 * k, top = sy - lift - d;
        C().Quad(sx - hw, top, sx + hw, top, sx + hw * 1.25f, sy + 30 * k, sx - hw * 1.25f, sy + 30 * k, Mix(Hex(p.deep, 0.62f * alpha), Hex(p.lite, 0.62f * alpha), 0.55f));
        for (int q = 0; q < 9; ++q) { float fx = sx - hw + hw * 2 * q / 8.f, w = std::sin(G.t * 9 + q * 1.7f + r.z) * 3 * k; C().Circle(fx, top + w, (5 + 3 * Hash(q + (int)(G.t * 12))) * k * (0.6f + sp), Hex(0xFFFFFF, 0.75f * alpha)); }
        if (sp > 0.2f) { C().Tri(sx - hw, top, sx - hw * (1.5f + sp), top + 26 * k, sx - hw * 0.9f, top + 40 * k, Hex(0xFFFFFF, 0.5f * alpha)); C().Tri(sx + hw, top, sx + hw * (1.5f + sp), top + 26 * k, sx + hw * 0.9f, top + 40 * k, Hex(0xFFFFFF, 0.5f * alpha)); }   // bow wave peeling off both sides
    }
    if (r.wet > 0.3f && !off && Hash((int)(G.t * 300) + 7 * (int)(&r - G.r)) < r.wet * 0.5f && G.parts.size() < 600)                // water running off her
        G.parts.push_back({ sx + (Rnd(G.rng) - 0.5f) * 90 * k, sy - lift - (70 + Rnd(G.rng) * 170) * k, (Rnd(G.rng) - 0.5f) * 30, 40 + Rnd(G.rng) * 60, 0.45f, 1.2f + 1.3f * k, 0xD8F2FF });
    if (r.air <= 0 && r.speed > 0.1f && !off && Hash((int)(G.t * 240) + (int)(&r - G.r)) < 0.5f) Spray(sx + (Hash((int)(G.t * 97)) - 0.5f) * 150 * k - r.lean * 60 * k, sy - 20 * k, 1 + (int)(std::fabs(r.lean) * 3), (260 + r.speed * 900) * std::min(1.f, k * 1.2f));
}
// ---- baked prop sprites: small shaded, posterised, outlined pictures made at load, drawn with nearest-neighbour
// scaling so they sit in the same pixel-art world as the rider and ski (flat vector props looked pasted on).
struct Pix {
    int w, h; std::vector<uint8_t> px;                                   // straight RGBA
    Pix(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0) {}
    void put(int x, int y, uint32_t rgb, float f) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        f += ((x + y) & 1) ? 0.05f : -0.05f;                             // ordered dither across the tone steps
        float k = f < 0.32f ? 0.46f : f < 0.56f ? 0.68f : f < 0.82f ? 0.90f : 1.12f; uint8_t* o = &px[((size_t)y * w + x) * 4];
        o[0] = (uint8_t)std::min(255.f, ((rgb >> 16) & 255) * k); o[1] = (uint8_t)std::min(255.f, ((rgb >> 8) & 255) * k); o[2] = (uint8_t)std::min(255.f, (rgb & 255) * k); o[3] = 255;
    }
    void ell(float cx, float cy, float rx, float ry, uint32_t rgb, float flat = -1) {
        for (int y = (int)(cy - ry); y <= (int)(cy + ry); ++y) for (int x = (int)(cx - rx); x <= (int)(cx + rx); ++x) {
            float nx = (x - cx) / rx, ny = (y - cy) / ry, d = nx * nx + ny * ny; if (d > 1) continue;
            put(x, y, rgb, flat >= 0 ? flat : 0.22f + 0.95f * std::max(0.f, -0.5f * nx - 0.6f * ny + 0.62f * std::sqrt(1 - d))); }
    }
    void trap(float xl0, float xr0, float y0, float xl1, float xr1, float y1, uint32_t rgb, float f0, float f1) {      // lit from the left
        for (int y = (int)y0; y <= (int)y1; ++y) { float t = y1 > y0 ? (y - y0) / (y1 - y0) : 0, xl = xl0 + (xl1 - xl0) * t, xr = xr0 + (xr1 - xr0) * t;
            for (int x = (int)xl; x <= (int)xr; ++x) put(x, y, rgb, f0 + (f1 - f0) * (xr > xl ? (x - xl) / (xr - xl) : 0)); }
    }
    void stroke(float x0, float y0, float x1, float y1, float r, uint32_t rgb, float f) { int n = (int)(std::fabs(x1 - x0) + std::fabs(y1 - y0)) + 1; for (int i = 0; i <= n; ++i) ell(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, r, r, rgb, f); }
    void foam(int y0) { for (int x = 0; x < w; ++x) for (int y = y0; y < h; ++y) { const uint8_t* up = &px[((size_t)(y0 - 1) * w + x) * 4]; if (up[3] && ((x * 7 + y * 3) % 5) < 3) { uint8_t* o = &px[((size_t)y * w + x) * 4]; o[0] = o[1] = o[2] = 245; o[3] = 255; } } }
    void outline() { std::vector<uint8_t> a((size_t)w * h); for (size_t i = 0; i < a.size(); ++i) a[i] = px[i * 4 + 3];
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { size_t k = (size_t)y * w + x; if (a[k]) continue;
            if ((x > 0 && a[k - 1]) || (x < w - 1 && a[k + 1]) || (y > 0 && a[k - w]) || (y < h - 1 && a[k + w])) { uint8_t* o = &px[k * 4]; o[0] = 22; o[1] = 18; o[2] = 30; o[3] = 255; } } }
};
enum { SP_ROCK, SP_CRATE, SP_LOG, SP_BUOY_R, SP_BUOY_G, SP_RAMP, SP_ISLAND, SP_BOAT_A, SP_BOAT_B, SP_LIGHT, SP_PIER, SP_N };
struct PropSpr { ComPtr<ID2D1Bitmap> b; int w = 1, h = 1; float ww = 1; } g_prop[SP_N]; unsigned g_propGen = 0;
void BakeProps() {
    if (g_propGen == C().Generation() && g_prop[0].b) return; g_propGen = C().Generation();
    auto fin = [&](int id, Pix& p, float worldW, int foamY) { if (foamY > 0) p.foam(foamY); p.outline(); std::vector<uint8_t> o(p.px.size());
        for (size_t i = 0; i < p.px.size(); i += 4) { uint8_t a = p.px[i + 3]; o[i] = (uint8_t)(p.px[i + 2] * a / 255); o[i + 1] = (uint8_t)(p.px[i + 1] * a / 255); o[i + 2] = (uint8_t)(p.px[i] * a / 255); o[i + 3] = a; }
        g_prop[id].b = C().MakeBitmap(o.data(), p.w, p.h); g_prop[id].w = p.w; g_prop[id].h = p.h; g_prop[id].ww = worldW; };
    { Pix p(96, 60); p.ell(30, 40, 26, 18, 0x5C6470); p.ell(60, 32, 30, 26, 0x6E7682); p.ell(52, 22, 14, 10, 0x8A929C); p.ell(78, 44, 14, 12, 0x4E5560); fin(SP_ROCK, p, 640, 56); }
    { Pix p(52, 46); p.trap(4, 47, 6, 4, 47, 14, 0xD0A064, 1.0f, 0.8f); p.trap(4, 47, 14, 4, 47, 42, 0xA87840, 0.85f, 0.45f); for (int x = 14; x < 44; x += 11) p.trap((float)x, (float)x, 14, (float)x, (float)x, 42, 0x6B4622, 0.4f, 0.4f);
      p.stroke(5, 15, 46, 41, 1, 0x7A5028, 0.5f); p.stroke(46, 15, 5, 41, 1, 0x7A5028, 0.5f); fin(SP_CRATE, p, 300, 43); }
    { Pix p(120, 34); for (int x = 14; x < 104; ++x) p.ell((float)x, 16, 4, 12, 0x7A5632); for (int x = 20; x < 100; x += 9) p.stroke((float)x, 8, (float)x + 4, 12, 1, 0x4E341C, 0.4f); p.ell(104, 16, 7, 12, 0xD2A878, 0.9f); p.ell(104, 16, 4, 7, 0xB08858, 0.7f); p.ell(104, 16, 1.5f, 3, 0x7A5632, 0.5f); fin(SP_LOG, p, 720, 29); }
    for (int v = 0; v < 2; ++v) { Pix p(30, 84); uint32_t c = v ? 0x2EBD59 : 0xE53935; p.trap(4, 25, 70, 9, 20, 78, 0x3A3F47, 0.9f, 0.4f); p.trap(11, 18, 16, 5, 24, 70, c, 1.05f, 0.35f); p.trap(8, 21, 40, 7, 22, 50, 0xF4F4F4, 1.1f, 0.5f); p.ell(14.5f, 10, 6, 6, 0xFFD54F); fin(v ? SP_BUOY_G : SP_BUOY_R, p, 200, 79); }
    { Pix p(128, 62); p.trap(18, 110, 6, 4, 124, 52, 0x2A7FE0, 1.1f, 0.5f); for (int k = 0; k < 3; ++k) { float cx = 34 + k * 30.f; p.trap(cx, cx, 16, cx - 9, cx + 9, 36, 0xFFFFFF, 1.0f, 0.9f); p.trap(cx, cx, 26, cx - 9, cx + 9, 46, 0x2A7FE0, 0.8f, 0.6f); } p.trap(4, 124, 52, 4, 124, 58, 0xFFD23F, 1.0f, 0.7f); fin(SP_RAMP, p, 820, 58); }
    { Pix p(230, 210); p.ell(115, 186, 108, 20, 0xE6D29A); p.ell(90, 180, 60, 12, 0xF2E2B0, 0.95f);
      for (int k = 0; k < 2; ++k) { float bx = 80 + k * 66.f, top = 60 + k * 26.f, tx = bx + 22; for (int i = 0; i < 12; ++i) { float t = i / 11.f; p.ell(bx + (tx - bx) * t * t, 180 + (top - 180) * t, 5 - 2 * t, 6, i % 2 ? 0x8A6238 : 0x7A5230); }
          for (int l = 0; l < 7; ++l) { float a = PI * (0.02f + 0.16f * l), ex = tx + std::cos(a) * 62, ey = top - std::sin(a) * 30 + 26; for (int i = 0; i <= 10; ++i) { float t = i / 10.f, x = tx + (ex - tx) * t, y = top + (ey - top) * t - std::sin(t * PI) * 22; p.ell(x, y, 7 - 4 * t, 4 - 2 * t, l % 2 ? 0x2E9D4A : 0x237A3A); } }
          p.ell(tx - 3, top + 5, 4, 4, 0x6B4226); p.ell(tx + 5, top + 6, 4, 4, 0x5A3418); }
      fin(SP_ISLAND, p, 2000, 203); }
    for (int v = 0; v < 2; ++v) { Pix p(150, 176); p.trap(73, 76, 6, 73, 76, 140, 0x6D4C41, 0.9f, 0.5f); p.trap(79, 79, 12, 79, 140, 128, v ? 0xFFD23F : 0xFAFAFA, 1.1f, 0.6f); p.trap(70, 70, 26, 24, 70, 128, v ? 0xFF7043 : 0x4FC3F7, 1.0f, 0.55f);
      p.trap(8, 142, 140, 26, 124, 166, 0xF4F4F4, 1.1f, 0.55f); p.trap(10, 140, 146, 14, 136, 151, v ? 0x1E88E5 : 0xC62828, 1.0f, 0.6f); fin(v ? SP_BOAT_B : SP_BOAT_A, p, 1500, 166); }
    { Pix p(70, 210); p.ell(35, 196, 32, 12, 0x5A616B); for (int k = 0; k < 6; ++k) { float y0 = 48 + k * 24.f, w0 = 10 + k * 1.6f, w1 = 10 + (k + 1) * 1.6f; p.trap(35 - w0, 35 + w0, y0, 35 - w1, 35 + w1, y0 + 24, k % 2 ? 0xE53935 : 0xFAFAFA, 1.1f, 0.4f); }
      p.trap(21, 49, 42, 21, 49, 48, 0x37474F, 0.9f, 0.5f); p.trap(26, 44, 22, 26, 44, 42, 0xFFF3B0, 1.2f, 0.9f); p.trap(35, 35, 10, 22, 48, 22, 0xC62828, 1.0f, 0.5f); fin(SP_LIGHT, p, 760, 204); }
    { Pix p(160, 74); for (int k = 0; k < 5; ++k) p.trap(12 + k * 33.f, 17 + k * 33.f, 34, 12 + k * 33.f, 17 + k * 33.f, 68, 0x5D4037, 0.9f, 0.4f); p.trap(4, 156, 26, 4, 156, 30, 0xBC9A84, 1.0f, 0.9f); p.trap(4, 156, 30, 4, 156, 37, 0x8D6E63, 0.8f, 0.5f);
      for (int x = 10; x < 156; x += 12) p.trap((float)x, (float)x, 26, (float)x, (float)x, 30, 0x6D4C41, 0.5f, 0.5f); p.trap(120, 122, 2, 120, 122, 26, 0x5D4037, 0.8f, 0.5f); p.trap(123, 123, 3, 123, 150, 10, 0xFF4FA8, 1.0f, 0.7f); p.trap(123, 150, 10, 123, 123, 17, 0xFF4FA8, 0.8f, 0.5f); fin(SP_PIER, p, 1500, 68); }
}
void Spr(int id, float x, float y, float u, float bob = 0, float rot = 0, float fade = 1) {
    const PropSpr& s = g_prop[id]; if (!s.b) return; float dw = s.ww * u, dh = dw * s.h / s.w; if (dw < 1.5f) return;
    if (rot != 0) C().SetTransform(D2D1::Matrix3x2F::Rotation(rot, { x, y }));
    C().BitmapPx(s.b.Get(), RF(x - dw / 2, y - dh * 0.95f + bob, dw, dh), fade);
    if (rot != 0) C().ResetTransform();
}
void DrawProp(int p, float x, float y, float u, int seed) {
    Canvas& c = C(); float t = G.t, bob = std::sin(t * 2.2f + seed) * 14 * u, storm = (float)G.weather;
    switch (p) {
    case P_BUOY_L: Spr(SP_BUOY_R, x, y, u, bob, std::sin(t * 1.8f + seed) * (5 + storm * 4)); break;
    case P_BUOY_R: Spr(SP_BUOY_G, x, y, u, bob, std::sin(t * 1.8f + seed) * (5 + storm * 4)); break;
    case P_ROCK: Spr(SP_ROCK, x, y, u); break;
    case P_CRATE: Spr(SP_CRATE, x, y, u, bob * 1.4f, std::sin(t * 1.5f + seed) * 6); break;
    case P_LOG: Spr(SP_LOG, x, y, u, bob, std::sin(t * 1.3f + seed) * 3); break;
    case P_RAMP: Spr(SP_RAMP, x, y, u, bob * 0.5f); break;
    case P_ISLAND: Spr(SP_ISLAND, x, y, u); break;
    case P_BOAT: Spr(seed % 2 ? SP_BOAT_A : SP_BOAT_B, x, y, u, bob * 2, std::sin(t * 1.1f + seed) * (3 + storm * 4)); break;
    case P_LIGHT: { Spr(SP_LIGHT, x, y, u); float beam = 0.5f + 0.5f * std::sin(t * 2.2f + seed), ly = y - 760 * u * 210 / 70 * 0.80f; c.Glow(x, ly, 700 * u * (0.5f + beam), 380 * u, Hex(0xFFF3B0, 0.20f + 0.45f * beam)); break; }
    case P_PIER: Spr(SP_PIER, x, y, u); break;
    case P_GATE: { float w = 2300 * u, hh = 1500 * u; c.FillR(RF(x - w - 60 * u, y - hh, 120 * u, hh), Hex(0xECEFF1)); c.FillR(RF(x + w - 60 * u, y - hh, 120 * u, hh), Hex(0xECEFF1)); float cw = w * 2 / 16, chh = 150 * u;
        for (int ry = 0; ry < 2; ++ry) for (int cx = 0; cx < 16; ++cx) c.FillR(RF(x - w + cx * cw, y - hh + ry * chh, cw + 0.5f, chh + 0.5f), Hex((cx + ry) % 2 ? 0x111111 : 0xFFFFFF)); break; }
    }
}
struct Proj { float x, y, w, u; float clip; bool ok; };
void World(float W, float H, bool hud) {
    Canvas& c = C(); const Pal& p = kPal[G.weather]; Racer& me = G.r[0]; BakeProps();
    float yc = H * 0.27f, camZ = me.z * SEG - 2900, camX = me.x * ROADW, camY = CAMH + Swell(me.z) * 0.6f + me.air * 0.3f, shake = G.shake > 0 ? (Hash((int)(G.t * 90)) - 0.5f) * 18 * G.shake : 0;
    int base = (int)std::floor(camZ / SEG); float frac = camZ / SEG - base;
    // sky, sun, clouds, far shore
    c.GradRR(RF(0, 0, W, yc + 60), 0, Hex(p.skyT), Hex(p.skyB), true);
    static float skyOff = 0; skyOff += G.seg[((int)me.z % NSEG + NSEG) % NSEG].curve * me.speed * 0.9f;
    if (G.weather == 0) { c.Glow(W * 0.72f, yc * 0.42f, 260, 260, Hex(0xFFF6C8, 0.55f)); c.Circle(W * 0.72f, yc * 0.42f, 44, Hex(0xFFFBE6)); }
    for (int i = 0; i < 9; ++i) { float cx = std::fmod(i * 317.f - skyOff * (0.6f + (i % 3) * 0.2f) + G.t * (6 + G.weather * 14) + 4000 * W, W + 400) - 200, cy = yc * (0.12f + 0.5f * Hash(i * 5)), s = 22 + 30 * Hash(i * 9);
        Color cc = G.weather == 0 ? Hex(0xFFFFFF, 0.9f) : Hex(G.weather >= 2 ? 0x1B2030 : 0x7C8896, 0.8f); c.Circle(cx, cy, s, cc); c.Circle(cx + s * 0.9f, cy + s * 0.2f, s * 0.75f, cc); c.Circle(cx - s * 0.9f, cy + s * 0.25f, s * 0.65f, cc); }
    for (int i = 0; i < 6; ++i) { float ix = std::fmod(i * 431.f - skyOff * 1.6f + 6000 * W, W + 600) - 300, iw = 180 + 160 * Hash(i * 3 + 7); c.Tri(ix - iw, yc + 2, ix + iw, yc + 2, ix + (Hash(i) - 0.5f) * iw, yc - 30 - 60 * Hash(i * 11), Mix(Hex(0x2C5F4A), Hex(p.fog), 0.55f)); }
    if (G.flash > 0.6f) { float bx = W * (0.2f + 0.6f * Hash((int)(G.nextBolt * 100))), by = 0; for (int i = 0; i < 7; ++i) { float nx = bx + (Hash(i * 17 + (int)(G.nextBolt * 50)) - 0.5f) * 90, ny = by + yc / 7; c.Line(bx, by, nx, ny, Hex(0xFFFFFF), 3); bx = nx; by = ny; } }
    // water: one band per segment, front to back, each riding its swell; nearer crests hide what is behind them
    static Proj pr[DRAW + 2]; float x = 0, dx = -G.seg[(base % NSEG + NSEG) % NSEG].curve * frac, maxy = H;
    auto proj = [&](int n, float xoff) { Proj q; float wz = (n - frac) * SEG; q.ok = wz > 60; if (!q.ok) wz = 60; float sc = DEPTH / wz; q.u = sc * W / 2;
        q.x = W / 2 + sc * (-camX - xoff) * W / 2; q.y = yc - sc * (Swell((float)(base + n)) - camY) * H / 2 + shake; q.w = sc * ROADW * W / 2; q.clip = H; return q; };
    c.FillR(RF(0, yc, W, H - yc), Hex(p.deep));
    for (int n = 0; n <= DRAW; ++n) { pr[n] = proj(n, x); x += dx; dx += G.seg[((base + n) % NSEG + NSEG) % NSEG].curve; }
    for (int n = 1; n < DRAW; ++n) {
        Proj& a = pr[n], &b = pr[n + 1]; a.clip = maxy; if (!a.ok || b.y >= maxy) continue;
        int i = base + n; float fog = std::pow((float)n / DRAW, 1.6f), y1 = std::min(a.y, maxy), slope = Swell((float)i + 1) - Swell((float)i);
        Color wc = Mix(Hex(p.deep), Hex(p.lite), ((i / 3) & 1 ? 0.30f : 0.62f) + std::clamp(slope * 0.012f, -0.25f, 0.3f)); wc = Mix(wc, Hex(p.fog), fog * 0.85f);
        c.FillR(RF(0, b.y, W, y1 - b.y + 1), wc);
        if (i >= 0 && i < NSEG) {
            c.Quad(a.x - a.w, a.y, a.x + a.w, a.y, b.x + b.w, b.y, b.x - b.w, b.y, Hex(0xFFFFFF, 0.07f * (1 - fog)));                        // the course channel
            {   // pool-style lane ropes: a line of floats on each side, bobbing on the swell
                float fr = std::max(1.2f, a.w * 0.022f); Color fc = Mix(Hex((i % 6) < 3 ? 0x1E6FD9 : 0xF4F6F8), Hex(p.fog), fog * 0.7f), rc = Hex(0x0A2A4A, 0.5f * (1 - fog));
                for (float sd : { -1.f, 1.f }) { c.Quad(a.x + sd * a.w - fr * 0.25f, a.y, a.x + sd * a.w + fr * 0.25f, a.y, b.x + sd * b.w + fr * 0.2f, b.y, b.x + sd * b.w - fr * 0.2f, b.y, rc); c.FillRR(RF(a.x + sd * a.w - fr, a.y - fr * 0.9f, fr * 2, fr * 1.3f), fr * 0.6f, fc); c.FillR(RF(a.x + sd * a.w - fr * 0.8f, a.y - fr * 0.8f, fr * 1.6f, fr * 0.3f), Hex(0xFFFFFF, 0.5f * (1 - fog))); } }
        }
        if (Hash(i * 3 + (int)(G.t * 7)) < 0.30f) { float gx = a.x + (Hash(i * 5 + (int)(G.t * 7)) - 0.5f) * a.w * 6, gw = a.w * (0.10f + 0.18f * Hash(i)); c.FillRR(RF(gx, (a.y + b.y) / 2, gw, std::max(2.f, (y1 - b.y) * 0.5f)), 2, Hex(0xFFFFFF, (G.weather == 0 ? 0.55f : 0.3f) * (1 - fog))); }   // glints / whitecaps
        if (slope > 6 && G.weather >= 2 && (i % 4) == 0) c.FillR(RF(0, b.y, W, std::max(1.f, (y1 - b.y) * 0.18f)), Hex(0xFFFFFF, 0.25f * (1 - fog)));                // breaking crests in a storm
        maxy = b.y;
    }
    // props and racers, far to near
    for (int n = DRAW - 1; n >= 1; --n) {
        int i = base + n; Proj& a = pr[n]; if (!a.ok) continue;
        if (i >= 0 && i < NSEG) { const Seg& s = G.seg[i]; c.PushClip(RF(0, 0, W, a.clip + 2));
            if (s.prop2) DrawProp(s.prop2, a.x + s.px2 * a.w, a.y, a.u, i); if (s.prop) DrawProp(s.prop, a.x + s.px * a.w, a.y, a.u, i); c.PopClip(); }
        for (int k = 2; k >= 0; --k) { Racer& r = G.r[k]; if ((int)std::floor(r.z) != i) continue; float f = r.z - i; Proj& b = pr[n + 1];
            float sx = a.x + (b.x - a.x) * f + r.x * (a.w + (b.w - a.w) * f), sy = a.y + (b.y - a.y) * f, u = a.u + (b.u - a.u) * f; SimStrings(r, r.frame, 1 / 60.f);
            float behind = me.z - r.z; if (k && behind > 3.0f) continue;                    // rivals between the lens and you would fill the screen: the red arrow marks them instead
            DrawRacer(r, sx, sy, u, k && behind > 1.2f ? 1 - (behind - 1.2f) / 1.8f : 1.f); }
    }
    for (auto& q : G.parts) c.Circle(q.x, q.y, q.size, Hex(q.col, std::min(1.f, q.life * 1.6f) * 0.8f));
    // weather in front of the lens
    if (G.weather > 0) { int nrain = G.weather == 1 ? 90 : G.weather == 2 ? 160 : 260; float slant = G.weather == 3 ? 0.9f + G.gust : 0.18f * G.weather, len = 26 + G.weather * 8;
        for (int i = 0; i < nrain; ++i) { float rx = std::fmod(Hash(i * 3) * (W + 400) + G.t * slant * 900 + i * 13.f, W + 400) - 200, ry = std::fmod(Hash(i * 7) * H + G.t * (900 + 300 * Hash(i)), H); c.Line(rx, ry, rx - slant * len, ry - len, Hex(0xDDEEFF, 0.35f), 1.2f); }
        c.FillR(RF(0, 0, W, H), Hex(0x0A1420, 0.10f * G.weather)); }
    if (G.flash > 0) c.FillR(RF(0, 0, W, H), Hex(0xFFFFFF, G.flash * 0.55f));
    if (!hud) return;
    // HUD
    static const wchar_t* ord[] = { L"1st", L"2nd", L"3rd" };
    c.FillRR(RF(12, 8, 244, 204), 14, Hex(0x04121E, 0.38f)); c.FillRR(RF(W - 236, 8, 224, 116), 14, Hex(0x04121E, 0.38f));
    c.Text(ord[me.place - 1], RF(24, 14, 200, 64), 46, Hex(0xFFFFFF), 0, true); c.Text(Fmt(L"%d:%05.2f", (int)G.raceT / 60, std::fmod(G.raceT, 60.f)), RF(24, 74, 100, 26), 18, Hex(0xFFFFFF, 0.85f)); c.Text(Fmt(L"%d pts", me.score), RF(120, 74, 124, 26), 18, Hex(0xFFE38A), 2, true);
    c.Text(Fmt(L"%d", (int)(me.speed * 210)), RF(W - 220, 10, 196, 64), 50, Hex(0xFFFFFF), 2, true); c.Text(L"km/h", RF(W - 220, 70, 196, 22), 14, Hex(0xFFFFFF, 0.8f), 2);
    c.FillRR(RF(W - 224, 98, 200, 8), 4, Hex(0x000000, 0.35f)); c.FillRR(RF(W - 224, 98, 200 * std::min(1.f, me.speed / 0.9f), 8), 4, Hex(me.tuck ? 0xFFD23F : 0x4FD6FF)); if (me.boost > 0.02f) c.FillRR(RF(W - 224, 110, 200 * me.boost, 5), 2.5f, Hex(0xFF4FA8));
    for (int i = 0; i < 3; ++i) { Racer& r = G.r[i]; float y = 118 + i * 30.f; c.Text(r.name, RF(24, y, 70, 22), 13, Hex(0xFFFFFF), 0, true); c.FillRR(RF(90, y + 6, 150, 10), 5, Hex(0x000000, 0.4f)); c.FillRR(RF(90, y + 6, 150 * std::max(0.f, r.hp) / 100, 10), 5, Hex(r.hp > 35 ? kBikini[r.look.bikini] : 0xFF3B30)); }
    { float bw = W * 0.5f, bx = W * 0.25f, by = H - 26; c.FillRR(RF(bx, by, bw, 6), 3, Hex(0x000000, 0.4f)); for (int i = 2; i >= 0; --i) c.Circle(bx + bw * std::clamp((G.r[i].z - 40) / (G.len - 100), 0.f, 1.f), by + 3, i ? 6.f : 8.f, Hex(kBikini[G.r[i].look.bikini])); c.FillR(RF(bx + bw - 2, by - 6, 4, 18), Hex(0xFFFFFF)); }
    for (int i = 1; i < 3; ++i) { float dz = me.z - G.r[i].z; if (dz > 0.5f && dz < 9) { float ax = G.r[i].x > me.x ? W - 60 : 60, s = G.r[i].x > me.x ? 1.f : -1.f, pu = 0.6f + 0.4f * std::sin(G.t * 12); c.Tri(ax + s * 22, H - 90, ax - s * 14, H - 112, ax - s * 14, H - 68, Hex(0xFF3B30, pu)); c.Text(G.r[i].name, RF(ax - 50, H - 62, 100, 20), 12, Hex(0xFFFFFF), 1, true); } }
    if (G.msgT > 0) c.Text(G.msg, RF(0, H * 0.22f, W, 70), 54, Hex(0xFFE38A, std::min(1.f, G.msgT * 2)), 1, true);
    if (G.mode == Game::COUNT && G.countT > 0.6f) c.Text(Fmt(L"%d", (int)G.countT), RF(0, H * 0.2f, W, 120), 96, Hex(0xFFFFFF), 1, true);
}
void Panel(const RectF& r) { C().FillRR(r, 18, Hex(0x0B1626, 0.72f)); C().StrokeRR(r, 18, Hex(0xFFFFFF, 0.25f), 1.5f); }
void Title(float W, float y) { C().Text(L"WET-RASH", RF(0, y + 4, W, 90), 76, Hex(0x04121E, 0.6f), 1, true); C().Text(L"WET-RASH", RF(0, y, W, 90), 76, Hex(0xFFE38A), 1, true); }
void Render() {
    if (!G.cv.Begin(G.h)) return;
    Canvas& c = C(); float W = c.W(), H = c.H(); bool racing = G.mode == Game::COUNT || G.mode == Game::RACE || G.mode == Game::PAUSE || G.mode == Game::RESULT;
    if (G.mode == Game::SIDE) { SideRender(W, H); c.End(); return; }
    World(W, H, racing);
    auto row = [&](float y, const std::wstring& a, const std::wstring& b, bool sel) { RectF r = RF(W / 2 - 280, y, 560, 42); if (sel) { c.FillRR(r, 12, Hex(0xFF4FA8, 0.85f)); } c.Text(a, RF(r.left + 20, y, 240, 44), 20, Hex(0xFFFFFF), 0, sel); if (!b.empty()) c.Text(L"<  " + b + L"  >", RF(r.left + 170, y, 380, 42), 16, Hex(0xFFFFFF, sel ? 1.f : 0.8f), 2); };
    if (G.mode == Game::MENU) {
        Title(W, H * 0.10f); c.Text(L"jet-ski brawl racing", RF(0, H * 0.10f + 92, W, 26), 18, Hex(0xFFFFFF, 0.9f), 1);
        Panel(RF(W / 2 - 300, H * 0.34f, 600, 346)); static const wchar_t* it[] = { L"Race", L"Spear Run (side view)", L"Rider and ski", L"Course", L"Weather", L"Controls", L"Quit" };
        for (int i = 0; i < 7; ++i) row(H * 0.34f + 14 + i * 46, it[i], i == 3 ? kCourse[G.course] : i == 4 ? kWeather[G.weather] : L"", G.sel == i);
        c.Text(L"Up / Down choose    Left / Right change    Enter or (A) select", RF(0, H - 44, W, 24), 14, Hex(0xFFFFFF, 0.85f), 1);
    } else if (G.mode == Game::RIDER) {
        Title(W, 20); Panel(RF(60, 150, 520, 300)); static const wchar_t* it[] = { L"Skin tone", L"Bikini", L"Jet ski", L"Hair" }; int* v[] = { &G.look.skin, &G.look.bikini, &G.look.ski, &G.look.hair }; const uint32_t* pal[] = { kSkin, kBikini, kSki, kHair }; int cnt[] = { 6, 8, 8, 6 };
        for (int i = 0; i < 4; ++i) { float y = 172 + i * 62.f; bool sel = G.custSel == i; if (sel) c.FillRR(RF(76, y - 6, 488, 54), 12, Hex(0xFF4FA8, 0.35f)); c.Text(it[i], RF(92, y, 150, 42), 18, Hex(0xFFFFFF), 0, sel);
            for (int k = 0; k < cnt[i]; ++k) { float sx = 250 + k * 38.f; c.Circle(sx, y + 21, *v[i] == k ? 16.f : 12.f, Hex(pal[i][k])); if (*v[i] == k) c.Ring(sx, y + 21, 18, Hex(0xFFFFFF), 2); } }
        c.Text(L"Up / Down row    Left / Right colour    Enter or Esc back", RF(60, 462, 520, 24), 14, Hex(0xFFFFFF, 0.85f), 1);
        Racer& r = G.r[0]; r.look = G.look; r.speed = 0.3f; r.lean = std::sin(G.t * 1.3f) * 0.5f; SetFrame(r, std::fmod(G.t, 6.f) < 2 ? WR_RIDE : std::fmod(G.t, 6.f) < 4 ? WR_LEAN : WR_TAUNT); r.side = r.lean > 0 ? 1 : -1; r.frameT = std::min(1.f, r.frameT + 0.1f);
        Panel(RF(W - 470, 130, 410, H - 190)); SimStrings(r, r.frame, 1 / 60.f); DrawRacer(r, W - 265, H - 150, 0.36f);
    } else if (G.mode == Game::CONTROLS) {
        Title(W, 20); Panel(RF(W / 2 - 430, 140, 860, 400));
        static const wchar_t* rows[][3] = { { L"", L"Keyboard", L"Controller" }, { L"Steer", L"Left / Right  or  A / D", L"Left stick or D-pad" }, { L"Throttle", L"Up  or  W", L"Right trigger or A" }, { L"Brake", L"Down  or  S", L"Left trigger" },
            { L"Punch", L"J  or  Z", L"X" }, { L"Kick", L"K  or  X", L"B" }, { L"Tuck (faster on straights)", L"Space or Shift", L"Bumpers" }, { L"Taunt / air trick (boost, points)", L"L  or  C", L"Y" }, { L"Pause", L"Esc or P", L"Start" } };
        c.Text(L"Spear Run: Left / Right move, Up hop, Down duck, J (or X on the pad) throw.", RF(0, 528, W, 22), 14, Hex(0xFFE38A), 1);
        for (int i = 0; i < 9; ++i) { float y = 160 + i * 40.f; c.Text(rows[i][0], RF(W / 2 - 400, y, 260, 36), 16, Hex(0xFFFFFF), 0, true); c.Text(rows[i][1], RF(W / 2 - 130, y, 250, 36), 16, Hex(i ? 0xFFFFFF : 0xFFE38A), 0, i == 0); c.Text(rows[i][2], RF(W / 2 + 140, y, 270, 36), 16, Hex(i ? 0xFFFFFF : 0xFFE38A), 0, i == 0); }
        c.Text(L"Attacks swing toward the nearest rival. Only rivals can knock you off. Hold taunt in the air off a ramp for trick points.", RF(0, 552, W, 24), 14, Hex(0xFFFFFF, 0.9f), 1); c.Text(L"Enter or Esc: back", RF(0, H - 44, W, 24), 14, Hex(0xFFFFFF, 0.85f), 1);
    } else if (G.mode == Game::PAUSE) { c.FillR(RF(0, 0, W, H), Hex(0x000000, 0.45f)); c.Text(L"PAUSED", RF(0, H * 0.35f, W, 80), 64, Hex(0xFFFFFF), 1, true); c.Text(L"Enter: resume      Esc: back to menu", RF(0, H * 0.35f + 90, W, 30), 18, Hex(0xFFFFFF), 1);
    } else if (G.mode == Game::RESULT) {
        Panel(RF(W / 2 - 300, H * 0.22f, 600, 330)); c.Text(G.r[0].place == 1 ? L"YOU WIN!" : G.r[0].place == 2 ? L"SECOND PLACE" : L"THIRD PLACE", RF(0, H * 0.22f + 14, W, 70), 48, Hex(0xFFE38A), 1, true);
        for (int pl = 1; pl <= 3; ++pl) for (Racer& r : G.r) if (r.place == pl) { float y = H * 0.22f + 100 + pl * 46.f; c.Circle(W / 2 - 230, y + 18, 12, Hex(kBikini[r.look.bikini])); c.Text(Fmt(L"%d.  %ls", pl, r.name.c_str()), RF(W / 2 - 200, y, 200, 36), 22, Hex(0xFFFFFF), 0, true); c.Text(Fmt(L"%d pts", r.score), RF(W / 2 - 40, y, 120, 36), 16, Hex(0xFFE38A), 0); c.Text(r.finish >= 0 ? Fmt(L"%d:%05.2f", (int)r.finish / 60, std::fmod(r.finish, 60.f)) : std::wstring(L"still racing"), RF(W / 2, y, 240, 36), 20, Hex(0xFFFFFF, 0.9f), 2); }
        c.Text(L"Enter: race again      Esc: menu", RF(0, H * 0.22f + 280, W, 30), 16, Hex(0xFFFFFF), 1);
    }
    c.End();
}

// ================================================================ Spear Run (side view)
// The same riders, colours and weather seen from the side. You ride at the left, the two rivals ahead of you; throw
// spears at them, duck or hop theirs. Art: the artist's side-view frames (body / rider / hull / handle kept as
// separate layers so her feet go behind the hull); sea, sky, spray, hair, ties and backdrop are generated.
struct SideSpr { ComPtr<ID2D1Bitmap> f[SD_FRAMES], red[SD_FRAMES]; unsigned gen = 0; int sig = -1; };
ImageBGRA g_sAtlas, g_sMasks;
void BakeSide(const Look& lk, SideSpr& sp) {
    int sig = lk.skin | (lk.bikini << 4) | (lk.ski << 8) | (lk.hair << 12);
    if (sp.sig == sig && sp.gen == C().Generation() && sp.f[0]) return;
    sp.sig = sig; sp.gen = C().Generation();
    if (!g_sAtlas.ok()) { DecodeImageResource(IDR_WR_SATLAS, 0, g_sAtlas); DecodeImageResource(IDR_WR_SMASKS, 0, g_sMasks); }
    if (!g_sAtlas.ok() || !g_sMasks.ok()) return;
    auto ch = [](uint32_t c, int k) { return (float)((c >> (16 - 8 * k)) & 255); };
    uint32_t skin = kSkin[lk.skin], bik = kBikini[lk.bikini], ski = kSki[lk.ski], hair = kHair[lk.hair];
    for (int i = 0; i < SD_FRAMES; ++i) {
        const int* b = kSdFrames[i].bbox; int w = b[2] - b[0], h = b[3] - b[1], ox = (i % 5) * SD_CW + b[0], oy = (i / 5) * SD_CH + b[1];
        std::vector<uint8_t> px((size_t)w * h * 4), rd((size_t)w * h * 4);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const uint8_t* s = &g_sAtlas.px[((size_t)(oy + y) * g_sAtlas.w + ox + x) * 4], *m = &g_sMasks.px[((size_t)(oy + y) * g_sMasks.w + ox + x) * 4]; size_t k = ((size_t)y * w + x) * 4;
            float a = s[3] / 255.f; if (a <= 0) continue;
            if (i == SD_T4) { int cx = b[0] + x, cy = b[1] + y; if ((cx >= 146 && cy <= 40) || (cx >= 206 && cy <= 58)) continue; }    // the painted spear leaves as a real projectile
            float c[3] = { s[2] / a, s[1] / a, s[0] / a }, l = 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2];
            if (i >= SD_BODY) { if (i != SD_SPEAR && c[0] > c[1] * 1.7f && c[0] > c[2] * 1.7f) { float q = c[0] / 200.f; for (int j = 0; j < 3; ++j) c[j] = std::min(255.f, ch(ski, j) * q); } }
            else if (m[1] > 127) { float q = 0.30f + 0.70f * std::pow(l / 150.f, 1.3f) + ((x * 3 + y) % 4 == 0 ? 0.10f : 0); for (int j = 0; j < 3; ++j) c[j] = std::min(255.f, ch(hair, j) * q); }   // scalp region painted as hair, strand texture
            else if (m[2] > 127) { float q = l / 120.f; for (int j = 0; j < 3; ++j) c[j] = std::min(255.f, ch(bik, j) * (0.25f + 0.75f * q)); }
            else { static const float base[3] = { 201, 136, 102 }; for (int j = 0; j < 3; ++j) c[j] = std::min(255.f, c[j] * ch(skin, j) / base[j]); }
            px[k] = (uint8_t)(c[2] * a); px[k + 1] = (uint8_t)(c[1] * a); px[k + 2] = (uint8_t)(c[0] * a); px[k + 3] = s[3];
            rd[k] = (uint8_t)(c[2] * 0.3f * a); rd[k + 1] = (uint8_t)(c[1] * 0.3f * a); rd[k + 2] = (uint8_t)(std::min(255.f, c[0] * 0.5f + 150) * a); rd[k + 3] = s[3];     // hit flash copy
        }
        sp.f[i] = C().MakeBitmap(px.data(), w, h); if (i < SD_BODY) sp.red[i] = C().MakeBitmap(rd.data(), w, h);
    }
}
struct SRider { float x = 0, hop = 0, vy = 0, duck = 0, throwT = -1, flash = 0, cool = 0, hp = 3, out = 0, aiT = 1, aiX = 0, face = 1; bool threw = false, bot = true; Look look; SideSpr spr; V2 hair[10][7], hairP[10][7], tie[2][3], tieP[2][3]; bool init = false; std::wstring name; };
struct Spear { float x, y, vx, vy; int owner; bool live; };
struct Side { SRider r[3]; std::vector<Spear> sp; float time = 90, scroll = 0, S = 1, sea = 0; int score = 0, kos = 0; bool over = false, freeze = false; } SD;
float SeaY(float sx) { float a = G.weather == 0 ? 5.f : G.weather == 1 ? 9.f : G.weather == 2 ? 16.f : 28.f; return SD.sea + a * (std::sin(sx * 0.011f + G.t * 2.1f + SD.scroll * 0.004f) + 0.45f * std::sin(sx * 0.027f - G.t * 3.0f)); }
void SideStart() {
    static const wchar_t* names[3] = { L"YOU", L"ROXY", L"KAI" }; SD.sp.clear(); SD.time = 90; SD.score = 0; SD.kos = 0; SD.over = false; SD.freeze = false; G.parts.clear();
    for (int i = 0; i < 3; ++i) { SRider& r = SD.r[i]; SideSpr keep = r.spr; r = SRider{}; r.spr = keep; r.name = names[i]; r.bot = i > 0 || G.auto_; r.x = i == 0 ? 0.20f : i == 1 ? 0.58f : 0.82f; r.aiX = r.x; r.hp = i == 0 ? 5.f : 3.f; r.aiT = 1.5f + i;
        r.look = i == 0 ? G.look : Look{ (G.look.skin + 1 + i) % 6, (G.look.bikini + 2 + i * 2) % 8, (G.look.ski + 1 + i * 2) % 8, (G.look.hair + i * 2) % 6 }; }
    G.mode = Game::SIDE; Say(L"SPEAR RUN");
    if (G.au) { G.au->rain = G.weather == 0 ? 0.f : G.weather == 1 ? 0.6f : 1.f; G.au->wind = G.weather < 2 ? 0.f : G.weather == 2 ? 0.45f : 1.f; G.au->engine = 0.6f; }
}
int SideFrame(const SRider& r) {
    if (r.throwT >= 0) return r.throwT < 0.12f ? SD_T1 : r.throwT < 0.30f ? SD_T2 : r.throwT < 0.40f ? SD_T3 : SD_T4;
    return SD_SIT + std::clamp((int)(r.duck * 4 + 0.5f), 0, 4);
}
void SideTick(float dt) {
    float W = C().W(); if (W < 10) W = 1280;
    if (SD.freeze) return;
    SD.scroll += dt * 520; if (SD.over) return;
    SD.time -= dt; if (SD.time <= 0 || SD.r[0].hp <= 0) { SD.over = true; Sfx(GameAudio::GO); return; }
    Input pin = PlayerInput();
    for (int i = 0; i < 3; ++i) {
        SRider& r = SD.r[i]; Input in;
        r.flash = std::max(0.f, r.flash - dt); r.cool = std::max(0.f, r.cool - dt);
        if (r.out > 0) { r.out -= dt; r.x += (1.25f - r.x) * dt * 2; if (r.out <= 0) { r.hp = 3; r.x = 1.15f; } continue; }      // knocked out: drops back off screen, then rejoins
        if (!r.bot) in = pin;
        else {
            r.aiT -= dt; bool me = i == 0;
            if (r.aiT <= 0) { r.aiT = (me ? 0.9f : 2.4f) + Hash((int)(G.t * 7) + i * 13) * (me ? 1.2f : 3.2f); r.aiX = me ? 0.12f + 0.2f * Hash((int)G.t + 3) : (i == 1 ? 0.50f : 0.70f) + 0.22f * Hash((int)G.t * 3 + i); in.punch = true; }
            in.steer = std::clamp((r.aiX - r.x) * 8, -1.f, 1.f);
            for (auto& s : SD.sp) if (s.live && s.owner != i && (s.owner == 0) != me && std::fabs(s.x / W - r.x) < 0.16f && Hash((int)(s.x * 0.05f) + i * 7) < (me ? 0.75f : 0.45f)) { if (Hash(i + (int)s.y) < 0.5f) in.brake = 1; else in.gas = 1; }
        }
        float lo = i == 0 ? 0.06f : 0.42f, hi = i == 0 ? 0.40f : 0.94f;
        r.x = std::clamp(r.x + in.steer * 0.32f * dt, lo, hi);
        r.duck += (((in.brake > 0.5f || in.tuck) && r.throwT < 0 ? 1.f : 0.f) - r.duck) * std::min(1.f, dt * 12);
        if (in.gas > 0.5f && r.hop <= 0 && r.vy == 0) { r.vy = 760; if (i == 0) Sfx(GameAudio::WHOOSH); }
        if (r.hop > 0 || r.vy != 0) { r.hop += r.vy * dt; r.vy -= 1700 * dt; if (r.hop <= 0) { r.hop = 0; r.vy = 0; if (i == 0) Sfx(GameAudio::SPLASH); Spray(r.x * W, SeaY(r.x * W), 14, 380); } }
        r.face = i == 0 ? 1.f : -1.f;                                   // rivals turn to face you when they throw
        if (in.punch && r.throwT < 0 && r.cool <= 0 && r.duck < 0.3f) { r.throwT = 0; r.threw = false; r.cool = i == 0 ? 0.55f : 1.2f; }
        if (r.throwT >= 0) { r.throwT += dt;
            if (!r.threw && r.throwT >= 0.40f) { r.threw = true; float S = SD.S, dir = r.face, sx = r.x * W + dir * 110.6f * S, sy = SeaY(r.x * W) - r.hop - 177.5f * S;
                SD.sp.push_back({ sx, sy, dir * (i == 0 ? 980.f : 620.f), -60.f, i, true }); if (i == 0) Sfx(GameAudio::WHOOSH); }
            if (r.throwT > 0.62f) r.throwT = -1; }
    }
    for (auto& s : SD.sp) if (s.live) {
        s.x += s.vx * dt; s.y += s.vy * dt; s.vy += 160 * dt; if (s.x < -300 || s.x > W + 300) { s.live = false; continue; }
        if (s.y > SeaY(s.x)) { s.live = false; Spray(s.x, SeaY(s.x), 8, 260); continue; }
        for (int i = 0; i < 3; ++i) { SRider& r = SD.r[i]; if (i == s.owner || (s.owner != 0 && i != 0) || r.out > 0 || r.flash > 0) continue;
            float S = SD.S, rx = r.x * W, base = SeaY(rx) - r.hop, top = base - (r.duck > 0.6f ? 150 : 215) * S, bot = base - 96 * S, tip = s.x + (s.vx > 0 ? 100 : -100) * S;
            if (std::fabs(tip - rx) < 34 * S && s.y > top && s.y < bot) {
                s.live = false; r.hp -= 1; r.flash = 0.6f; Sfx(GameAudio::HIT); G.shake = 0.2f; Spray(rx, s.y, 10, 300, 0xFF6060);
                if (s.owner == 0) { SD.score += 100; if (r.hp <= 0) { r.out = 3.5f; SD.score += 500; ++SD.kos; Say(Fmt(L"%ls OUT!  +500", r.name.c_str())); Sfx(GameAudio::SPLASH); } }
                else if (r.hp > 0) Say(L"OUCH!"); } }
    }
    SD.sp.erase(std::remove_if(SD.sp.begin(), SD.sp.end(), [](const Spear& s) { return !s.live; }), SD.sp.end());
}
void SideRider(SRider& r, float W) {
    BakeSide(r.look, r.spr); if (!r.spr.f[0]) return;
    float S = SD.S, sx = r.x * W, base = SeaY(sx), slope = (SeaY(sx + 40) - SeaY(sx - 40)) / 80.f, rot = std::atan(slope) * 57.3f * 0.7f - (r.hop > 0 ? r.vy * 0.012f : 0); int f = SideFrame(r);
    // sprite space: 318x237 cell, ski centre x = 117, waterline about y = 215
    auto M = D2D1::Matrix3x2F::Translation(-117, -215) * D2D1::Matrix3x2F::Scale(r.face * S, S) * D2D1::Matrix3x2F::Rotation(rot * r.face) * D2D1::Matrix3x2F::Translation(sx, base - r.hop + 4 * S);
    auto blit = [&](int k, bool red) { const int* b = kSdFrames[k].bbox; C().BitmapPx((red ? r.spr.red[k] : r.spr.f[k]).Get(), RF((float)b[0], (float)b[1], (float)(b[2] - b[0]), (float)(b[3] - b[1]))); };
    bool red = r.flash > 0 && std::fmod(r.flash, 0.12f) > 0.05f;
    C().SetTransform(M);
    blit(SD_BODY, false);
    { // hair: a ponytail of fine strands from the back of the painted scalp, and the bikini tie
        const SdFrameInfo& fi = kSdFrames[f]; float wind = (-420 - G.gust * 900 - (G.weather == 3 ? 500.f : 0)) * 1.f, dt = 1 / 60.f; V2 root = { fi.head[0] - fi.head[2] * 0.78f, fi.head[1] - fi.head[2] * 0.15f };
        auto chain = [&](V2* p, V2* q, int n, V2 a, float seg, float ph, float grav) { if (!r.init) for (int i = 0; i < n; ++i) p[i] = q[i] = { a.x - seg * i, a.y }; p[0] = a;
            for (int i = 1; i < n; ++i) { V2 v = { (p[i].x - q[i].x) * 0.93f, (p[i].y - q[i].y) * 0.93f }; q[i] = p[i]; p[i].x += v.x + wind * dt * dt; p[i].y += v.y + (grav + std::sin(G.t * 12 + ph + i) * 260) * dt * dt; }
            for (int it = 0; it < 3; ++it) for (int i = 1; i < n; ++i) { float dx = p[i].x - p[i - 1].x, dy = p[i].y - p[i - 1].y, d = std::sqrt(dx * dx + dy * dy) + 1e-4f, k = (d - seg) / d; p[i].x -= dx * k; p[i].y -= dy * k; } };
        for (int s = 0; s < 10; ++s) chain(r.hair[s], r.hairP[s], 7, { root.x + (s % 3) * 0.8f, root.y + (s - 5) * 0.7f }, 3.2f + 0.5f * Hash(s), s * 1.3f, 520 + 40.f * s);
        for (int k = 0; k < 2 && fi.nTies; ++k) chain(r.tie[k], r.tieP[k], 3, { fi.ties[0][0], fi.ties[0][1] }, 4.f, k * 3.f, 700);
        r.init = true;
        Color hc = Hex(kHair[r.look.hair]), bc = Hex(kBikini[r.look.bikini]);
        for (int s = 0; s < 10; ++s) for (int i = 1; i < 7; ++i) { float t = i / 7.f; C().Line(r.hair[s][i - 1].x, r.hair[s][i - 1].y, r.hair[s][i].x, r.hair[s][i].y, Alpha(s % 3 == 0 ? Mix(hc, Hex(0x000000), 0.4f) : s % 3 == 1 ? hc : Mix(hc, Hex(0xFFFFFF), 0.2f), 1 - t * 0.6f), 1.5f * (1 - t * 0.6f)); }
        blit(f, red);
        if (fi.nTies) for (int k = 0; k < 2; ++k) for (int i = 1; i < 3; ++i) C().Line(r.tie[k][i - 1].x, r.tie[k][i - 1].y, r.tie[k][i].x, r.tie[k][i].y, bc, 1.2f);
        C().Circle(root.x + 1, root.y, 2.6f, Mix(hc, Hex(0x000000), 0.25f));                      // hair tie knot
    }
    blit(SD_HULL, false);                                                                          // hull over her feet, as layered by the artist
    { // steering column: pivots where it leaves the body and is stretched so the grip lands in her hand
        const float* g0 = kSdFrames[SD_HANDLE].grip, *g1 = kSdFrames[f].grip; D2D1_MATRIX_3X2_F H = D2D1::Matrix3x2F::Identity();
        if (g1[2] - g1[0] != 0) { float ax = (g0[0] + g0[2]) / 2 - kSdPivot[0], ay = (g0[1] + g0[3]) / 2 - kSdPivot[1], bx = (g1[0] + g1[2]) / 2 - kSdPivot[0], by = (g1[1] + g1[3]) / 2 - kSdPivot[1];
            float sc = std::sqrt(bx * bx + by * by) / std::sqrt(ax * ax + ay * ay), an = (std::atan2(by, bx) - std::atan2(ay, ax)) * 57.2958f; D2D1_POINT_2F pv{ kSdPivot[0], kSdPivot[1] };
            H = D2D1::Matrix3x2F::Scale(sc, sc, pv) * D2D1::Matrix3x2F::Rotation(an, pv); }
        C().SetTransform(H * M); blit(SD_HANDLE, false); C().SetTransform(M);
    }
    if (r.throwT >= 0 && !r.threw) {}                                                               // spear is part of the throw frames until release
    C().ResetTransform();
    // water in front of the hull, foam at the waterline, jet from the exhaust point
    if (r.hop < 20) { const Pal& p = kPal[G.weather]; float hw = 122 * S;
        C().Quad(sx - hw, base + 2 * S, sx + hw, base + 2 * S, sx + hw, base + 40 * S, sx - hw, base + 40 * S, Mix(Hex(p.deep, 0.55f), Hex(p.lite, 0.55f), 0.5f));
        for (int q = 0; q < 12; ++q) C().Circle(sx - hw + hw * 2 * q / 11.f, base + 2 * S + std::sin(G.t * 9 + q * 1.3f + r.x * 20) * 2 * S, (3 + 3 * Hash(q + (int)(G.t * 12))) * S, Hex(0xFFFFFF, 0.7f));
        float ex = sx + r.face * (kSdExhaust[0] - 117) * S, ey = base + (kSdExhaust[1] - 215) * S; for (int q = 0; q < 5; ++q) C().Circle(ex - r.face * q * 13 * S + (Hash((int)(G.t * 60) + q) - 0.5f) * 6, ey - q * 3 * S, (6 + q * 3.5f) * S, Hex(0xFFFFFF, 0.6f - q * 0.1f));
        if (Hash((int)(G.t * 240) + (int)(r.x * 100)) < 0.6f) Spray(ex - r.face * 30 * S, ey, 1, 300); }
}
void SideRender(float W, float H) {
    Canvas& c = C(); const Pal& p = kPal[G.weather]; BakeProps(); SD.S = H / 237.f * 0.46f; SD.sea = H * 0.70f; float S = SD.S, shake = G.shake > 0 ? (Hash((int)(G.t * 90)) - 0.5f) * 14 * G.shake : 0;
    c.GradRR(RF(0, 0, W, SD.sea + 40), 0, Hex(p.skyT), Hex(p.skyB), true);
    if (G.weather == 0) { c.Glow(W * 0.78f, H * 0.2f, 240, 240, Hex(0xFFF6C8, 0.55f)); c.Circle(W * 0.78f, H * 0.2f, 40, Hex(0xFFFBE6)); }
    for (int i = 0; i < 9; ++i) { float cx = std::fmod(i * 317.f - SD.scroll * (0.03f + (i % 3) * 0.01f) + 90000, W + 400) - 200, cy = H * (0.06f + 0.3f * Hash(i * 5)), s = 26 + 34 * Hash(i * 9);
        Color cc = G.weather == 0 ? Hex(0xFFFFFF, 0.9f) : Hex(G.weather >= 2 ? 0x1B2030 : 0x7C8896, 0.85f); c.Circle(cx, cy, s, cc); c.Circle(cx + s * 0.9f, cy + s * 0.2f, s * 0.75f, cc); c.Circle(cx - s * 0.9f, cy + s * 0.25f, s * 0.65f, cc); }
    if (G.flash > 0.6f) { float bx = W * (0.2f + 0.6f * Hash((int)(G.nextBolt * 100))), by = 0; for (int i = 0; i < 7; ++i) { float nx = bx + (Hash(i * 17 + (int)(G.nextBolt * 50)) - 0.5f) * 90, ny = by + SD.sea / 7; c.Line(bx, by, nx, ny, Hex(0xFFFFFF), 3); bx = nx; by = ny; } }
    // far sea and the things on it, drifting past slowly
    float hz = SD.sea - 70 * S; c.FillR(RF(0, hz, W, H - hz), Mix(Hex(p.deep), Hex(p.fog), 0.45f));
    static const int farP[] = { SP_ISLAND, SP_BOAT_A, SP_LIGHT, SP_BOAT_B, SP_ISLAND, SP_PIER };
    for (int i = 0; i < 6; ++i) { float x = std::fmod(i * 520.f - SD.scroll * 0.12f + 60000, W + 900) - 450; const PropSpr& ps = g_prop[farP[i]]; float dw = (farP[i] == SP_ISLAND ? 330.f : farP[i] == SP_LIGHT ? 60.f : 150.f) * S * 0.55f; Spr(farP[i], x, hz + 6 + std::sin(G.t * 1.3f + i) * 2, dw / ps.ww, 0, 0, 0.85f); }
    auto band = [&](float off, float amp, Color col, float par) { for (float x = 0; x < W; x += 6) { float y = SeaY(x * par + SD.scroll * (par - 1)) * amp + SD.sea * (1 - amp) + off; c.FillR(RF(x, y + shake, 6.5f, H - y), col); } };
    band(-46 * S, 0.5f, Mix(Hex(p.deep), Hex(p.fog), 0.25f), 0.6f); band(-22 * S, 0.8f, Mix(Hex(p.deep), Hex(p.lite), 0.35f), 0.8f);
    for (int i = 2; i >= 0; --i) SideRider(SD.r[i], W);                                           // rivals behind, you in front
    for (auto& s : SD.sp) { const int* b = kSdFrames[SD_SPEAR].bbox; float w = (b[2] - b[0]) * S, h = (b[3] - b[1]) * S, an = std::atan2(s.vy, std::fabs(s.vx)) * 57.3f; bool left = s.vx < 0;
        c.SetTransform(D2D1::Matrix3x2F::Scale(left ? -1.f : 1.f, 1, { s.x, s.y }) * D2D1::Matrix3x2F::Rotation(left ? -an : an, { s.x, s.y })); c.BitmapPx(SD.r[s.owner].spr.f[SD_SPEAR].Get(), RF(s.x - w * 0.6f, s.y - h / 2, w, h)); c.ResetTransform(); }
    // front water: translucent, so hulls read as sitting in it
    for (float x = 0; x < W; x += 6) { float y = SeaY(x) + 26 * S + shake; c.FillR(RF(x, y, 6.5f, H - y), Mix(Hex(p.deep, 0.78f), Hex(p.lite, 0.78f), 0.4f)); if (Hash((int)(x + SD.scroll) / 40 + (int)(G.t * 6)) < 0.12f) c.FillR(RF(x, y, 6.5f, 2.5f), Hex(0xFFFFFF, 0.6f)); }
    for (int i = 0; i < 14; ++i) { float x = std::fmod(i * 173.f - SD.scroll * 1.4f + 90000, W + 200) - 100, y = SD.sea + (60 + 26 * (i % 4)) * S; c.FillRR(RF(x, y, 90 * S, 3 * S), 1.5f, Hex(0xFFFFFF, 0.35f)); }   // streaks give the sense of speed
    for (auto& q : G.parts) c.Circle(q.x, q.y, q.size, Hex(q.col, std::min(1.f, q.life * 1.6f) * 0.8f));
    if (G.weather > 0) { int nrain = G.weather == 1 ? 90 : G.weather == 2 ? 160 : 260; float slant = G.weather == 3 ? 0.9f + G.gust : 0.18f * G.weather, len = 26 + G.weather * 8;
        for (int i = 0; i < nrain; ++i) { float rx = std::fmod(Hash(i * 3) * (W + 400) + G.t * slant * 900 + i * 13.f, W + 400) - 200, ry = std::fmod(Hash(i * 7) * H + G.t * (900 + 300 * Hash(i)), H); c.Line(rx, ry, rx - slant * len, ry - len, Hex(0xDDEEFF, 0.35f), 1.2f); }
        c.FillR(RF(0, 0, W, H), Hex(0x0A1420, 0.10f * G.weather)); }
    if (G.flash > 0) c.FillR(RF(0, 0, W, H), Hex(0xFFFFFF, G.flash * 0.55f));
    // HUD
    c.FillRR(RF(12, 8, 300, 96), 14, Hex(0x04121E, 0.4f)); c.Text(Fmt(L"%d pts", SD.score), RF(24, 12, 200, 40), 30, Hex(0xFFE38A), 0, true); c.Text(Fmt(L"%d knocked out", SD.kos), RF(24, 52, 200, 22), 14, Hex(0xFFFFFF, 0.85f));
    for (int k = 0; k < 5; ++k) c.IconAt(I_HEART, 190 + k * 22.f, 68, 16, k < (int)SD.r[0].hp ? Hex(0xFF4FA8) : Hex(0xFFFFFF, 0.25f), 1);
    c.FillRR(RF(W - 150, 8, 138, 60), 14, Hex(0x04121E, 0.4f)); c.Text(Fmt(L"%d", (int)std::ceil(std::max(0.f, SD.time))), RF(W - 150, 8, 138, 60), 40, SD.time < 10 ? Hex(0xFF5A5A) : Hex(0xFFFFFF), 1, true);
    for (int i = 1; i < 3; ++i) { SRider& r = SD.r[i]; if (r.out > 0) continue; float x = r.x * W, y = SeaY(x) - r.hop - 232 * S; c.Text(r.name, RF(x - 50, y - 20, 100, 16), 11, Hex(0xFFFFFF), 1, true); for (int k = 0; k < 3; ++k) c.FillRR(RF(x - 26 + k * 18.f, y - 4, 15, 5), 2.5f, k < (int)r.hp ? Hex(kBikini[r.look.bikini]) : Hex(0x000000, 0.4f)); }
    if (G.msgT > 0) c.Text(G.msg, RF(0, H * 0.16f, W, 60), 44, Hex(0xFFE38A, std::min(1.f, G.msgT * 2)), 1, true);
    c.Text(L"Left / Right move    Up hop    Down duck    J throw    Esc menu", RF(0, H - 30, W, 22), 13, Hex(0xFFFFFF, 0.8f), 1);
    if (SD.over) { c.FillR(RF(0, 0, W, H), Hex(0x000000, 0.45f)); c.FillRR(RF(W / 2 - 280, H * 0.3f, 560, 220), 18, Hex(0x0B1626, 0.8f));
        c.Text(SD.r[0].hp <= 0 ? L"SKEWERED" : L"TIME!", RF(0, H * 0.3f + 16, W, 60), 46, Hex(0xFFE38A), 1, true); c.Text(Fmt(L"%d points    %d knocked out", SD.score, SD.kos), RF(0, H * 0.3f + 90, W, 36), 24, Hex(0xFFFFFF), 1, true);
        c.Text(L"Enter: again      Esc: menu", RF(0, H * 0.3f + 160, W, 30), 16, Hex(0xFFFFFF), 1); }
}

// ---------------------------------------------------------------- window
void Key(int vk) {
    bool ok = vk == VK_RETURN || vk == VK_SPACE, back = vk == VK_ESCAPE;
    switch (G.mode) {
    case Game::MENU:
        if (vk == VK_UP) G.sel = (G.sel + 6) % 7; else if (vk == VK_DOWN) G.sel = (G.sel + 1) % 7;
        else if ((vk == VK_LEFT || vk == VK_RIGHT) && G.sel == 4) { G.course = (G.course + (vk == VK_RIGHT ? 1 : 2)) % 3; BuildTrack(); }
        else if ((vk == VK_LEFT || vk == VK_RIGHT) && G.sel == 3) { G.weather = (G.weather + (vk == VK_RIGHT ? 1 : 3)) % 4; BuildTrack(); if (G.au) { G.au->rain = G.weather == 0 ? 0.f : 0.5f; G.au->wind = G.weather >= 2 ? 0.5f : 0.f; } }
        else if (ok) { if (G.sel == 0) StartRace(); else if (G.sel == 1) SideStart(); else if (G.sel == 2) G.mode = Game::RIDER; else if (G.sel == 3 || G.sel == 4) Key(VK_RIGHT); else if (G.sel == 5) G.mode = Game::CONTROLS; else PostMessageW(G.h, WM_CLOSE, 0, 0); }
        else if (back) PostMessageW(G.h, WM_CLOSE, 0, 0);
        Sfx(GameAudio::BEEP); break;
    case Game::RIDER: { int* v[] = { &G.look.skin, &G.look.bikini, &G.look.ski, &G.look.hair }; int cnt[] = { 6, 8, 8, 6 };
        if (vk == VK_UP) G.custSel = (G.custSel + 3) % 4; else if (vk == VK_DOWN) G.custSel = (G.custSel + 1) % 4; else if (vk == VK_LEFT) *v[G.custSel] = (*v[G.custSel] + cnt[G.custSel] - 1) % cnt[G.custSel]; else if (vk == VK_RIGHT) *v[G.custSel] = (*v[G.custSel] + 1) % cnt[G.custSel];
        else if (ok || back) { G.mode = Game::MENU; g_app->set.wrLook = G.look.skin | (G.look.bikini << 4) | (G.look.ski << 8) | (G.look.hair << 12); g_app->set.Save(); G.r[0].speed = 0; G.r[0].lean = 0; SetFrame(G.r[0], WR_RIDE); }
        Sfx(GameAudio::BEEP); break; }
    case Game::CONTROLS: if (ok || back) G.mode = Game::MENU; break;
    case Game::COUNT: case Game::RACE: if (back || vk == 'P') G.mode = Game::PAUSE; break;
    case Game::PAUSE: if (ok || vk == 'P') G.mode = Game::RACE; else if (back) { G.mode = Game::MENU; G.sel = 0; } break;
    case Game::SIDE: if (back) { G.mode = Game::MENU; G.sel = 1; } else if (SD.over && ok) SideStart(); break;
    case Game::RESULT: if (ok) StartRace(); else if (back) { G.mode = Game::MENU; G.sel = 0; } break;
    }
}
void PadMenu() {              // controller edges drive the same menu keys
    if (!G.xi || GetForegroundWindow() != G.h) return;
    struct XS { DWORD pkt; WORD btn; BYTE lt, rt; SHORT lx, ly, rx, ry; } xs{}; if (G.xi(0, &xs) != 0) return;
    bool now[8] = { (xs.btn & 1) || xs.ly > 16000, (xs.btn & 2) || xs.ly < -16000, (xs.btn & 4) || xs.lx < -16000, (xs.btn & 8) || xs.lx > 16000, (xs.btn & 0x1000) != 0, (xs.btn & 0x2000) != 0, (xs.btn & 0x0010) != 0, false };
    static const int vk[7] = { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_ESCAPE, 'P' }; bool race = G.mode == Game::RACE || G.mode == Game::COUNT;
    for (int i = 0; i < 7; ++i) { if (now[i] && !G.padPrev[i] && (!race || i == 6)) Key(race ? VK_ESCAPE : vk[i] == 'P' ? (G.mode == Game::PAUSE ? VK_RETURN : VK_ESCAPE) : vk[i]); G.padPrev[i] = now[i]; }
}
void Frame() {
    double now = NowSeconds(); double dt = std::min(0.1, now - G.last); G.last = now; G.acc += dt;
    PlayerInput(); PadMenu();
    for (int n = 0; G.acc >= 1 / 60.0 && n < 5; ++n) { G.acc -= 1 / 60.0; if (G.mode != Game::PAUSE) Tick(1 / 60.f); }
    if (G.acc > 0.1) G.acc = 0;
    InvalidateRect(G.h, nullptr, FALSE);
}
LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Render(); EndPaint(h, &ps); return 0; }
    case WM_TIMER: Frame(); return 0;
    case WM_KEYDOWN: if (!(lp & 0x40000000)) Key((int)wp); return 0;
    case WM_KILLFOCUS: if (G.mode == Game::RACE) G.mode = Game::PAUSE; return 0;
    case WM_GETMINMAXINFO: { auto* mi = (MINMAXINFO*)lp; float s = ThemeDpi(h) / 96.f; mi->ptMinTrackSize = { (LONG)(900 * s), (LONG)(620 * s) }; return 0; }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: KillTimer(h, 1); g_app->SetGameOpen(false); G.out.reset(); G.au = nullptr; for (auto& r : G.r) r.spr = Sprites{}; G.cv.Release(); G.h = nullptr; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
bool Create(bool audio) {
    if (G.h) { ShowWindow(G.h, SW_SHOW); SetForegroundWindow(G.h); return true; }
    if (!GfxInit()) return false;
    static bool reg = false;
    if (!reg) { WNDCLASSEXW wc{ sizeof(wc) }; wc.lpfnWndProc = Proc; wc.hInstance = g_app->Inst(); wc.lpszClassName = kClass; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(g_app->Inst(), MAKEINTRESOURCEW(IDI_APP)); RegisterClassExW(&wc); reg = true; }
    float s = ThemeDpi(nullptr) / 96.f; RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = std::min<int>((int)(1280 * s), wa.right - wa.left - 40), hh = std::min<int>((int)(760 * s), wa.bottom - wa.top - 40);
    G.h = CreateWindowExW(0, kClass, L"Wet-Rash", WS_OVERLAPPEDWINDOW, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - hh) / 2, w, hh, nullptr, nullptr, g_app->Inst(), nullptr);
    if (!G.h) return false;
    ThemeApplyWindow(G.h);
    int lk = g_app->set.wrLook; G.look = { lk & 15, (lk >> 4) & 15, (lk >> 8) & 15, (lk >> 12) & 15 }; G.look.skin %= 6; G.look.bikini %= 8; G.look.ski %= 8; G.look.hair %= 6;
    G.mode = Game::MENU; G.sel = 0; G.t = 0; G.last = NowSeconds(); G.acc = 0; BuildTrack();
    static const wchar_t* names[3] = { L"YOU", L"ROXY", L"KAI" };
    for (int i = 0; i < 3; ++i) { Racer& r = G.r[i]; r = Racer{}; r.name = names[i]; r.z = 44 - i * 1.6f; r.x = (i - 1) * 0.55f; r.look = i == 0 ? G.look : Look{ (G.look.skin + 1 + i) % 6, (G.look.bikini + 2 + i * 2) % 8, (G.look.ski + 1 + i * 2) % 8, (G.look.hair + i * 2) % 6 }; }
    if (audio) { auto src = std::make_unique<GameAudio>(); GameAudio* raw = src.get(); raw->music = 1.f; g_app->SetGameOpen(true); G.out = std::make_unique<AudioOut>(); std::wstring err;
        if (G.out->Open(std::move(src), 0.8f, false, err)) G.au = raw; else { G.out.reset(); Log("wet-rash: no audio (%s)", WToUtf8(err).c_str()); } }
    ShowWindow(G.h, SW_SHOW); SetForegroundWindow(G.h); SetTimer(G.h, 1, 8, nullptr);
    return true;
}
}

void WetRashOpen() { Create(true); }
void WetRashClose() { if (G.h) DestroyWindow(G.h); }
// QA: scene 0 menu, 1 race in sun, 2 race in a hurricane, 3 rider screen, 4 controls, 5 result. The race scenes run the
// real simulation with a bot on the player's ski, and fail if nobody finished a sane distance.
bool WetRashShot(const std::wstring& path, int scene) {
    if (!Create(false)) return false;
    KillTimer(G.h, 1); bool ok = true;
    if (scene == 1 || scene == 2 || scene == 5) {
        G.weather = scene == 2 ? 3 : 0; G.course = scene == 2 ? 2 : scene == 5 ? 0 : 1; G.auto_ = true; StartRace();
        int ticks = scene == 5 ? 60 * 200 : 60 * 22; for (int i = 0; i < ticks && G.mode != Game::RESULT; ++i) Tick(1 / 60.f);
        float zmin = 1e9f, zmax = 0; int falls = 0; for (Racer& r : G.r) { zmin = std::min(zmin, r.z); zmax = std::max(zmax, r.z); falls += r.hp < 100; }
        Log("wet-rash qa: scene %d mode %d t %.1f z %.0f..%.0f places %d %d %d hp %.0f %.0f %.0f particles %zu", scene, (int)G.mode, G.raceT, zmin, zmax, G.r[0].place, G.r[1].place, G.r[2].place, G.r[0].hp, G.r[1].hp, G.r[2].hp, G.parts.size());
        ok = scene == 5 ? G.mode == Game::RESULT : (zmin > 250 && G.mode == Game::RACE);
    } else if (scene == 6 || scene == 7) {                    // Spear Run: 25 s of real simulation with bots on all three skis
        G.weather = scene == 7 ? 2 : 0; G.auto_ = true; SideStart(); RedrawWindow(G.h, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        for (int i = 0; i < 60 * 25 && !SD.over; ++i) { G.t += 1 / 60.f; SideTick(1 / 60.f); if (i == 60 * 9 && SD.r[0].throwT < 0) { SD.r[0].throwT = 0.2f; SD.r[0].threw = false; break; } }
        Log("wet-rash qa: side scene %d score %d kos %d hp %.0f %.0f %.0f spears %zu time %.0f", scene, SD.score, SD.kos, SD.r[0].hp, SD.r[1].hp, SD.r[2].hp, SD.sp.size(), SD.time);
        ok = SD.time < 85 && !SD.over;
    } else if (scene == 8) {                                  // pose sheet: the three throw stages, frozen, to check the handle follows the hands
        G.weather = 0; SideStart(); static const float tt[3] = { 0.05f, 0.2f, 0.45f }; for (int i = 0; i < 3; ++i) { SD.r[i].throwT = tt[i]; SD.r[i].threw = true; SD.r[i].x = 0.18f + i * 0.31f; }
        SD.freeze = true;
    } else if (scene == 3) G.mode = Game::RIDER; else if (scene == 4) G.mode = Game::CONTROLS;
    for (int i = 0; i < 40; ++i) {              // let the window map and paint a few real frames before reading it back
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        if (G.mode == Game::MENU || G.mode == Game::RIDER || G.mode == Game::SIDE) Tick(1 / 60.f); else { G.t += 1 / 60.f; for (auto& q : G.parts) { q.x += q.vx / 60; q.y += q.vy / 60; q.life -= 1 / 60.f; } }
        RedrawWindow(G.h, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW); Sleep(16);
    }
    RECT rc; GetClientRect(G.h, &rc); int w = rc.right, hgt = rc.bottom; HDC wdc = GetDC(G.h), mdc = CreateCompatibleDC(wdc);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -hgt; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); HGDIOBJ old = SelectObject(mdc, bmp); BitBlt(mdc, 0, 0, w, hgt, wdc, 0, 0, SRCCOPY); GdiFlush();
    BITMAPFILEHEADER fh{}; fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader); fh.bfSize = fh.bfOffBits + (DWORD)w * hgt * 4;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) { DWORD wr; WriteFile(f, &fh, sizeof(fh), &wr, nullptr); WriteFile(f, &bi.bmiHeader, sizeof(bi.bmiHeader), &wr, nullptr); WriteFile(f, bits, (DWORD)w * hgt * 4, &wr, nullptr); CloseHandle(f); } else ok = false;
    SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(G.h, wdc);
    return ok;
}
