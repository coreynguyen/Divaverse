#include "Sfx.h"
#include "Audio.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace {
struct Tone { float f0, f1, dur, gain, noise, delay; };       // sweep f0 -> f1 Hz over dur seconds; noise 0..1 mixes filtered noise
struct Def { Tone t[3]; int n; };
const Def kDefs[SFX_COUNT] = {
    /* HOVER      */ { { { 2100, 1800, 0.030f, 0.10f, 0.0f, 0 } }, 1 },
    /* CLICK      */ { { { 880, 1320, 0.070f, 0.30f, 0.0f, 0 }, { 1760, 2640, 0.050f, 0.12f, 0.0f, 0.012f } }, 2 },
    /* PAGE       */ { { { 420, 980, 0.160f, 0.16f, 0.6f, 0 }, { 1320, 1320, 0.090f, 0.10f, 0.0f, 0.070f } }, 2 },
    /* BACK       */ { { { 980, 520, 0.130f, 0.18f, 0.4f, 0 } }, 1 },
    /* TOGGLE     */ { { { 1200, 1500, 0.045f, 0.24f, 0.0f, 0 } }, 1 },
    /* STAGE      */ { { { 660, 660, 0.200f, 0.22f, 0.0f, 0 }, { 990, 990, 0.220f, 0.20f, 0.0f, 0.080f }, { 1320, 1340, 0.380f, 0.20f, 0.0f, 0.160f } }, 3 },
    /* WHEEL_OPEN */ { { { 300, 1100, 0.120f, 0.14f, 0.8f, 0 }, { 1500, 1800, 0.060f, 0.10f, 0.0f, 0.090f } }, 2 },
    /* WHEEL_TICK */ { { { 1700, 1500, 0.022f, 0.12f, 0.0f, 0 } }, 1 },
    /* WHEEL_PICK */ { { { 1050, 1580, 0.080f, 0.28f, 0.0f, 0 }, { 2100, 2100, 0.100f, 0.10f, 0.0f, 0.040f } }, 2 },
    /* DROP       */ { { { 190, 70, 0.150f, 0.42f, 0.25f, 0 } }, 1 },
    /* HIDE       */ { { { 900, 240, 0.180f, 0.16f, 0.7f, 0 } }, 1 },
    /* LOOT_SHAKE */ { { { 120, 420, 0.850f, 0.20f, 0.85f, 0 }, { 220, 880, 0.850f, 0.10f, 0.0f, 0 } }, 2 },
    /* LOOT       */ { { { 523, 523, 0.500f, 0.24f, 0.0f, 0 }, { 784, 784, 0.550f, 0.22f, 0.0f, 0.090f }, { 1047, 1568, 0.900f, 0.24f, 0.15f, 0.180f } }, 3 },
};
struct Voice { int id = -1; double t = 0; double ph[3] = {}; float lp = 0; };

struct SfxSource : AudioSource {
    std::atomic<uint32_t> pending{0};      // bit per SfxId, set by the UI thread
    Voice v[8]; int rate = 48000, ch = 2; uint32_t rng = 0x1234567u;
    bool Prepare(int r, int c, std::wstring&) override { rate = r; ch = c; return true; }
    float Sample(Voice& vo) {
        const Def& d = kDefs[vo.id]; float out = 0; bool alive = false;
        for (int i = 0; i < d.n; ++i) {
            const Tone& t = d.t[i]; double lt = vo.t - t.delay;
            if (lt < 0) { alive = true; continue; }
            if (lt >= t.dur) continue;
            alive = true;
            float k = (float)(lt / t.dur), f = t.f0 + (t.f1 - t.f0) * k;
            float env = std::min(1.f, (float)lt / 0.003f) * (1 - k) * (1 - k);               // 3 ms attack, quadratic decay
            vo.ph[i] += 2 * 3.14159265358979 * f / rate;
            float s = (float)std::sin(vo.ph[i]);
            if (t.noise > 0) { rng = rng * 1664525u + 1013904223u; float n = ((rng >> 8) & 0xFFFF) / 32768.f - 1.f; vo.lp += (n - vo.lp) * std::min(1.f, f * 4.f / rate); s = s * (1 - t.noise) + vo.lp * 3.f * t.noise; }
            out += s * env * t.gain;
        }
        vo.t += 1.0 / rate;
        if (!alive) vo.id = -1;
        return out;
    }
    uint32_t Render(float* dst, uint32_t n) override {
        uint32_t p = pending.exchange(0);
        for (int id = 0; p && id < SFX_COUNT; ++id) if (p & (1u << id)) {
            int slot = 0; double oldest = -1;
            for (int i = 0; i < 8; ++i) { if (v[i].id < 0) { slot = i; oldest = 1e9; break; } if (v[i].t > oldest) { oldest = v[i].t; slot = i; } }
            v[slot] = Voice{}; v[slot].id = id;
        }
        for (uint32_t i = 0; i < n; ++i) {
            float s = 0; for (auto& vo : v) if (vo.id >= 0) s += Sample(vo);
            s = s / (1 + std::fabs(s) * 0.5f);                                                  // soft limit
            for (int c = 0; c < ch; ++c) dst[i * ch + c] = s;
        }
        return n;                                                                                // never ends
    }
};
AudioOut* g_out = nullptr; SfxSource* g_src = nullptr; bool g_on = true, g_failed = false; float g_vol = 0.5f; double g_lastHover = 0;
}

void SfxConfigure(bool enabled, float volume) {
    g_on = enabled; g_vol = volume;
    if (!enabled) SfxShutdown(); else if (g_out) g_out->SetVolume(volume);
}
void SfxShutdown() { if (g_out) { g_out->Close(); delete g_out; g_out = nullptr; g_src = nullptr; } }
void SfxPlay(SfxId id) {
    if (!g_on || g_failed || id < 0 || id >= SFX_COUNT) return;
    if (id == SFX_HOVER || id == SFX_WHEEL_TICK) { double now = NowSeconds(); if (now - g_lastHover < 0.045) return; g_lastHover = now; }
    if (!g_out) {
        auto src = std::make_unique<SfxSource>(); SfxSource* raw = src.get(); std::wstring err;
        g_out = new AudioOut();
        if (!g_out->Open(std::move(src), g_vol, false, err)) { Log("interface sounds unavailable: %s", WToUtf8(err).c_str()); delete g_out; g_out = nullptr; g_failed = true; return; }
        g_src = raw;
    }
    g_src->pending.fetch_or(1u << id);
}
bool SfxRenderWav(const std::wstring& path) {
    SfxSource s; std::wstring e; s.Prepare(48000, 2, e);
    std::vector<int16_t> pcm; std::vector<float> buf(480 * 2); float peak = 0;
    for (int id = 0; id < SFX_COUNT; ++id) {
        s.pending.fetch_or(1u << id); float pk = 0; double rms = 0;
        for (int blk = 0; blk < 60; ++blk) {                                                    // 0.6 s per sound
            s.Render(buf.data(), 480);
            for (float x : buf) { pk = std::max(pk, std::fabs(x)); rms += x * x; pcm.push_back((int16_t)std::lround(std::clamp(x, -1.f, 1.f) * 32767)); }
        }
        Log("sfx %d: peak %.3f rms %.4f", id, pk, std::sqrt(rms / (60 * 960))); peak = std::max(peak, pk);
        if (pk < 0.02f || pk > 0.98f) { Log("sfx %d out of range", id); return false; }
    }
    FILE* f = _wfopen(path.c_str(), L"wb"); if (!f) return false;
    uint32_t bytes = (uint32_t)pcm.size() * 2, v32; uint16_t v16;
    auto w32 = [&](uint32_t v) { v32 = v; fwrite(&v32, 4, 1, f); }; auto w16 = [&](uint16_t v) { v16 = v; fwrite(&v16, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(1); w16(2); w32(48000); w32(48000 * 4); w16(4); w16(16);
    fwrite("data", 1, 4, f); w32(bytes); fwrite(pcm.data(), 1, bytes, f); fclose(f);
    return true;
}
