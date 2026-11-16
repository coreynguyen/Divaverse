#include "ProcIcon.h"
#include <cmath>
#include <algorithm>

namespace {
struct C4 { float r, g, b, a; };          // premultiplied accumulation
inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
inline float smooth(float e0, float e1, float x) { float t = clamp01((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t); }
inline void over(C4& d, float r, float g, float b, float a) {   // straight src OVER premult dst
    d.r = r * a + d.r * (1 - a); d.g = g * a + d.g * (1 - a); d.b = b * a + d.b * (1 - a); d.a = a + d.a * (1 - a);
}
inline void add(C4& d, float r, float g, float b, float k) { d.r += r * k; d.g += g * k; d.b += b * k; }

void palette(float t, IconState st, float& r, float& g, float& b) {
    // brand ramp: magenta -> violet -> cyan, mirrored so the arc ends match
    t = t - std::floor(t); t = t < 0.5f ? t * 2 : (1 - t) * 2;
    static const float N[3][3] = { {1.00f, 0.27f, 0.66f}, {0.62f, 0.36f, 1.00f}, {0.28f, 0.86f, 1.00f} };
    static const float E[3][3] = { {0.25f, 0.95f, 1.00f}, {0.30f, 1.00f, 0.80f}, {0.75f, 1.00f, 0.55f} };
    const float (*P)[3] = st == IconState::Edit ? E : N;
    int i = t < 0.5f ? 0 : 1; float k = t < 0.5f ? t * 2 : (t - 0.5f) * 2; k = k * k * (3 - 2 * k);
    r = P[i][0] + (P[i + 1][0] - P[i][0]) * k; g = P[i][1] + (P[i + 1][1] - P[i][1]) * k; b = P[i][2] + (P[i + 1][2] - P[i][2]) * k;
    if (st == IconState::Paused) { float l = 0.3f * r + 0.59f * g + 0.11f * b; r = l + (r - l) * 0.2f; g = l + (g - l) * 0.2f; b = l + (b - l) * 0.2f; }
}

// signed distance to a 4-point "astroid" sparkle of radius s (approximate, good for AA)
float sparkle(float x, float y, float s) {
    float ax = std::fabs(x) / s, ay = std::fabs(y) / s;
    float v = std::pow(ax, 2.0f / 3.0f) + std::pow(ay, 2.0f / 3.0f);
    return (std::pow(v, 1.5f) - 1.0f) * s * 0.55f;
}

// shade one sample at p in [-1,1]^2; px = size of one pixel in that space
C4 shade(float x, float y, float px, IconState st) {
    C4 c{ 0, 0, 0, 0 };
    // 1) squircle badge with vertical gradient + inner radial bloom
    const float n = 4.6f, R = 0.94f;
    float se = std::pow(std::pow(std::fabs(x), n) + std::pow(std::fabs(y), n), 1.0f / n) - R;
    float badge = 1.0f - smooth(-px, px, se);
    if (badge <= 0) return c;
    float t = (y + 1) * 0.5f;
    float br = 0.165f + (0.070f - 0.165f) * t, bg = 0.078f + (0.040f - 0.078f) * t, bb = 0.314f + (0.150f - 0.314f) * t;
    float bloom = std::exp(-3.0f * (x * x + (y + 0.05f) * (y + 0.05f)));
    br += 0.20f * bloom; bg += 0.05f * bloom; bb += 0.26f * bloom;
    over(c, br, bg, bb, badge);

    // 2) halo arc: ring r=0.56, open at the lower-left, rounded ends, angular gradient
    float ang = std::atan2(y, x);                       // -pi..pi, y down
    float rr = std::sqrt(x * x + y * y);
    const float ringR = 0.57f, ringW = 0.072f;
    const float gapC = 2.30f, gapHalf = 0.42f;          // gap centred at ~132 deg (lower-left)
    float da = std::remainder(ang - gapC, 6.2831853f);
    float dRing;
    if (std::fabs(da) > gapHalf) dRing = std::fabs(rr - ringR) - ringW;
    else {                                              // distance to the nearest rounded cap
        float a1 = gapC + gapHalf, a2 = gapC - gapHalf;
        float d1 = std::hypot(x - ringR * std::cos(a1), y - ringR * std::sin(a1));
        float d2 = std::hypot(x - ringR * std::cos(a2), y - ringR * std::sin(a2));
        dRing = std::min(d1, d2) - ringW;
    }
    float ring = (1.0f - smooth(-px, px, dRing)) * badge;
    float pr, pg, pb; palette(std::remainder(ang - gapC, 6.2831853f) / 6.2831853f + 0.5f, st, pr, pg, pb);
    float spot = 0.75f + 0.25f * smooth(0.4f, -0.8f, y);   // brighter toward the top (spotlight)
    over(c, std::min(1.f, pr * spot), std::min(1.f, pg * spot), std::min(1.f, pb * spot), ring);
    float ringGlow = std::exp(-std::max(dRing, 0.0f) * 22.0f) * 0.35f * badge * (1 - ring);
    add(c, pr, pg, pb, ringGlow * c.a);

    // 3) central sparkle (white core -> pink rim) + bloom; Paused draws bars instead
    if (st == IconState::Paused) {
        auto bar = [&](float cx) { float dx = std::fabs(x - cx) - 0.075f, dy = std::fabs(y) - 0.26f;
            float d = std::hypot(std::max(dx, 0.f), std::max(dy, 0.f)) + std::min(std::max(dx, dy), 0.f) - 0.03f;
            return 1.0f - smooth(-px, px, d); };
        float pbars = std::max(bar(-0.13f), bar(0.13f)) * badge;
        over(c, 0.92f, 0.90f, 0.97f, pbars);
    } else {
        float ds = sparkle(x, y + 0.01f, 0.40f);
        float core = (1.0f - smooth(-px, px, ds)) * badge;
        float k = clamp01(rr / 0.36f);
        float sr = 1.0f, sg = 0.95f - 0.35f * k, sb = 0.98f - 0.12f * k;
        if (st == IconState::Edit) { sr = 0.85f - 0.3f * k; sg = 1.0f; sb = 1.0f; }
        float glow = std::exp(-std::max(ds, 0.0f) * 14.0f) * 0.45f * badge * (1 - core);
        add(c, sr, sg * 0.8f, sb, glow * c.a);
        over(c, sr, sg, sb, core);
        // small companion sparkle, upper right
        float d2 = sparkle(x - 0.47f, y + 0.47f, 0.13f);
        float s2 = (1.0f - smooth(-px, px, d2)) * badge;
        over(c, 1.0f, 0.92f, 0.98f, s2 * 0.95f);
    }
    // 4) top gloss
    float gloss = smooth(0.05f, -0.85f, y) * 0.10f * badge * smooth(0.0f, -0.04f, se);
    add(c, 1, 1, 1, gloss * c.a);
    c.r = std::min(c.r, c.a); c.g = std::min(c.g, c.a); c.b = std::min(c.b, c.a);
    return c;
}
}

void ProcIconRGBA(int size, IconState st, std::vector<uint8_t>& out) {
    out.assign((size_t)size * size * 4, 0);
    const int S = size <= 32 ? 6 : size <= 64 ? 4 : 2;     // supersampling
    const float px = 2.0f / size;                           // AA width in [-1,1] space
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            C4 acc{ 0, 0, 0, 0 };
            for (int sy = 0; sy < S; ++sy)
                for (int sx = 0; sx < S; ++sx) {
                    float fx = ((x + (sx + 0.5f) / S) / size) * 2 - 1;
                    float fy = ((y + (sy + 0.5f) / S) / size) * 2 - 1;
                    C4 c = shade(fx, fy, px * 0.5f, st);
                    acc.r += c.r; acc.g += c.g; acc.b += c.b; acc.a += c.a;
                }
            float inv = 1.0f / (S * S);
            float a = acc.a * inv;
            uint8_t* o = &out[((size_t)y * size + x) * 4];
            if (a > 1e-5f) {
                o[0] = (uint8_t)std::lround(clamp01(acc.r * inv / a) * 255);
                o[1] = (uint8_t)std::lround(clamp01(acc.g * inv / a) * 255);
                o[2] = (uint8_t)std::lround(clamp01(acc.b * inv / a) * 255);
            }
            o[3] = (uint8_t)std::lround(clamp01(a) * 255);
        }
}

std::vector<uint8_t> ProcIconICO(const int* sizes, int count, IconState st) {
    std::vector<uint8_t> f;
    auto u16 = [&](uint32_t v) { f.push_back(v & 255); f.push_back((v >> 8) & 255); };
    auto u32 = [&](uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); };
    u16(0); u16(1); u16(count);
    size_t dirAt = f.size(); f.resize(f.size() + 16 * (size_t)count);
    for (int i = 0; i < count; ++i) {
        int s = sizes[i];
        std::vector<uint8_t> px; ProcIconRGBA(s, st, px);
        size_t start = f.size();
        u32(40); u32(s); u32(s * 2); u16(1); u16(32); u32(0); u32(0); u32(0); u32(0); u32(0); u32(0);
        for (int y = s - 1; y >= 0; --y)                    // bottom-up BGRA
            for (int x = 0; x < s; ++x) { const uint8_t* p = &px[((size_t)y * s + x) * 4]; f.push_back(p[2]); f.push_back(p[1]); f.push_back(p[0]); f.push_back(p[3]); }
        int maskStride = ((s + 31) / 32) * 4;
        for (int y = s - 1; y >= 0; --y) {                  // AND mask (1 = transparent)
            std::vector<uint8_t> row(maskStride, 0);
            for (int x = 0; x < s; ++x) if (px[((size_t)y * s + x) * 4 + 3] == 0) row[x >> 3] |= 0x80 >> (x & 7);
            f.insert(f.end(), row.begin(), row.end());
        }
        uint32_t bytes = (uint32_t)(f.size() - start);
        uint8_t* d = &f[dirAt + 16 * (size_t)i];
        d[0] = s >= 256 ? 0 : s; d[1] = s >= 256 ? 0 : s; d[2] = 0; d[3] = 0; d[4] = 1; d[5] = 0; d[6] = 32; d[7] = 0;
        for (int k = 0; k < 4; ++k) { d[8 + k] = (bytes >> (8 * k)) & 255; d[12 + k] = ((uint32_t)start >> (8 * k)) & 255; }
    }
    return f;
}
