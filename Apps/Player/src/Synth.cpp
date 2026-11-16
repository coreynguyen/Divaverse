#include "Synth.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

namespace {
const SynthStation kStations[] = {
    { L"Neon Drift",     L"slow pads, glass arps",        0xD1FA01u,  84, 0 },
    { L"Velvet Lounge",  L"warm keys, soft brushed beat", 0x5EA7B2u,  92, 1 },
    { L"Midnight Pulse", L"four on the floor, deep bass", 0xC0FFEEu, 118, 2 },
    { L"Rose Quartz",    L"dreamy, bright and light",     0xA11CE5u,  76, 0 },
    { L"Afterglow",      L"laid-back groove",             0x7E57ACu, 104, 1 },
    { L"Starline",       L"driving arps, wide chorus",    0x0B5E55u, 126, 2 },
};
const double kTau = 6.283185307179586;

struct Rng { uint32_t s; uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; } float f() { return (next() >> 8) * (1.f / 16777216.f); } int range(int n) { return (int)(next() % (uint32_t)n); } };

struct Voice {                       // one enveloped oscillator
    double phase = 0, inc = 0; float amp = 0, env = 0, decay = 0, attack = 0; int wave = 0; bool on = false, rising = false; float pan = 0.5f, lp = 0, cutoff = 1;
    void start(double hz, int rate, float a, float att, float dec, int w, float p, float cut) {
        inc = hz / rate; amp = a; attack = att > 0 ? 1.f / (att * rate) : 1.f; decay = (float)std::exp(-1.0 / (dec * rate));
        env = 0; rising = true; on = true; wave = w; pan = p; cutoff = cut; if (w != 3) phase = 0;
    }
    inline float tick() {
        if (!on) return 0;
        if (rising) { env += attack; if (env >= 1) { env = 1; rising = false; } } else { env *= decay; if (env < 0.0004f) { on = false; return 0; } }
        phase += inc; if (phase >= 1) phase -= 1;
        float x = (float)phase, v;
        switch (wave) {
        case 0: v = std::sin((float)kTau * x); break;                                        // sine
        case 1: v = 4.f * std::fabs(x - 0.5f) - 1.f; break;                                  // triangle
        case 2: v = 2.f * x - 1.f; break;                                                    // saw
        default: v = std::sin((float)kTau * x + 1.8f * std::sin((float)kTau * x * 2.f)); break;   // 2-op FM bell / e-piano
        }
        lp += cutoff * (v - lp);                                                            // one-pole low-pass
        return lp * env * amp;
    }
};

struct Synth : AudioSource {
    SynthStation st{}; int rate = 48000, ch = 2;
    Rng rng{ 1 }; Rng noise{ 0x9E3779B9u };
    std::vector<Voice> pad, pluck; Voice bass, kick; int padAt = 0, pluckAt = 0;
    float snareEnv = 0, hatEnv = 0, snareLp = 0, hatHp = 0, kickPitch = 0;
    std::vector<float> dl, dr; size_t dpos = 0; float dfb = 0.38f;
    double samplesPerStep = 0, stepAcc = 0; int step = 0, bar = 0;
    int scale[7] = { 0, 2, 3, 5, 7, 8, 10 }; int root = 45; int prog[4] = { 0, 5, 3, 4 };
    int density = 2; bool drums = false; uint16_t arpMask = 0;
    float master = 0, lpL = 0, lpR = 0;

    static double Hz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }
    int note(int degree, int octave) const { int d = ((degree % 7) + 7) % 7; return root + scale[d] + 12 * (octave + (degree >= 7 ? degree / 7 : 0)); }

    bool Prepare(int r, int channels, std::wstring&) override {
        rate = r; ch = channels; rng.s = st.seed | 1;
        static const int minor[7] = { 0, 2, 3, 5, 7, 8, 10 }, dorian[7] = { 0, 2, 3, 5, 7, 9, 10 }, major[7] = { 0, 2, 4, 5, 7, 9, 11 }, lydian[7] = { 0, 2, 4, 6, 7, 9, 11 };
        const int* sc[] = { minor, dorian, major, lydian };
        memcpy(scale, sc[rng.range(4)], sizeof(scale));
        root = 41 + rng.range(8);
        static const int progs[][4] = { { 0, 5, 3, 4 }, { 0, 3, 5, 4 }, { 0, 2, 5, 3 }, { 5, 3, 0, 4 }, { 0, 4, 5, 3 }, { 0, 5, 1, 4 } };
        memcpy(prog, progs[rng.range(6)], sizeof(prog));
        pad.assign(12, Voice{}); pluck.assign(10, Voice{});
        size_t dn = (size_t)(rate * 60.0 / st.bpm * 0.75);                 // dotted-eighth delay
        dl.assign(dn, 0.f); dr.assign(dn + (size_t)(rate * 0.011), 0.f);
        samplesPerStep = rate * 60.0 / st.bpm / 4.0;                       // 16th notes
        stepAcc = samplesPerStep; step = -1; bar = -1; master = 0;
        return true;
    }

    void newBar() {
        ++bar;
        if (bar % 8 == 0) {                                                // new section: change the arrangement
            density = 1 + rng.range(3);
            drums = st.style != 0;
            if (st.style == 2 && bar % 32 == 24) drums = false;            // breakdown
            arpMask = 0; int hits = st.style == 0 ? 3 + density : 5 + 2 * density;
            for (int i = 0; i < hits; ++i) arpMask |= (uint16_t)(1u << (st.style == 0 ? rng.range(8) * 2 : rng.range(16)));
        }
        int deg = prog[bar % 4];
        float cut = st.style == 0 ? 0.10f : st.style == 1 ? 0.14f : 0.20f;
        float att = st.style == 2 ? 0.25f : 0.9f, dec = (float)(60.0 / st.bpm * 4.0 * (st.style == 2 ? 0.9 : 1.6));
        for (int k = 0; k < 4; ++k) {                                      // chord pad: root, 3rd, 5th, 7th/9th, two detuned saws each
            int m = note(deg + 2 * k, 1);
            for (int dtn = 0; dtn < 2; ++dtn) {
                Voice& v = pad[(size_t)(padAt++ % (int)pad.size())];
                v.start(Hz(m) * (dtn ? 1.004 : 0.996), rate, 0.10f, att, dec, 2, dtn ? 0.86f : 0.14f, cut);
            }
        }
    }

    void onStep() {
        step = (step + 1) & 15;
        if (step == 0) newBar();
        int deg = prog[bar % 4];
        // bass
        bool bassHit = st.style == 2 ? (step % 4 == 2 || step == 0 || (density > 1 && step == 11)) : st.style == 1 ? (step == 0 || step == 6 || step == 10) : step == 0;
        if (bassHit) bass.start(Hz(note(deg, 0)), rate, st.style == 0 ? 0.11f : 0.17f, 0.004f, st.style == 0 ? 1.6f : 0.28f, 1, 0.5f, 0.10f);
        // arp / keys
        if (arpMask & (1u << step)) {
            int d2 = deg + 2 * rng.range(4) + (rng.range(5) == 0 ? 1 : 0);
            int oct = 2 + (rng.range(3) == 0 ? 1 : 0);
            Voice& v = pluck[(size_t)(pluckAt++ % (int)pluck.size())];
            if (st.style == 1) v.start(Hz(note(d2, oct - 1)), rate, 0.17f, 0.003f, 0.55f, 3, 0.2f + 0.6f * rng.f(), 0.30f);
            else v.start(Hz(note(d2, oct)), rate, st.style == 2 ? 0.13f : 0.15f, 0.002f, st.style == 2 ? 0.16f : 0.5f, st.style == 2 ? 2 : 1, 0.15f + 0.7f * rng.f(), st.style == 2 ? 0.22f : 0.35f);
        }
        // drums (all synthesised: sine sweep kick, filtered-noise snare and hats)
        if (drums) {
            bool k = st.style == 2 ? step % 4 == 0 : (step == 0 || step == 10 || (density > 2 && step == 7));
            if (k) { kick.start(52, rate, 0.34f, 0.001f, 0.14f, 0, 0.5f, 1.f); kickPitch = 1.f; }
            if (step == 4 || step == 12) snareEnv = st.style == 2 ? 0.42f : 0.26f;
            if (st.style == 2 ? step % 2 == 0 : step % 4 == 2) hatEnv = (step % 4 == 2 ? 0.22f : 0.12f);
        }
    }

    uint32_t Render(float* dst, uint32_t n) override {
        const float snDec = (float)std::exp(-1.0 / (0.09 * rate)), hatDec = (float)std::exp(-1.0 / (0.03 * rate)), kpDec = (float)std::exp(-1.0 / (0.03 * rate));
        for (uint32_t i = 0; i < n; ++i) {
            stepAcc += 1.0;
            if (stepAcc >= samplesPerStep) { stepAcc -= samplesPerStep; onStep(); }
            float l = 0, r = 0, wetIn = 0;
            for (auto& v : pad) if (v.on) { float s = v.tick(); l += s * (1 - v.pan); r += s * v.pan; }
            for (auto& v : pluck) if (v.on) { float s = v.tick(); l += s * (1 - v.pan); r += s * v.pan; wetIn += s; }
            float c = bass.tick();
            if (kick.on) { kick.inc = (52.0 + 140.0 * kickPitch) / rate; kickPitch *= kpDec; c += kick.tick(); }
            float nz = noise.f() * 2.f - 1.f;
            if (snareEnv > 0.0005f) { snareLp += 0.55f * (nz - snareLp); c += snareLp * snareEnv; wetIn += snareLp * snareEnv * 0.3f; snareEnv *= snDec; }
            if (hatEnv > 0.0005f) { hatHp += 0.7f * (nz - hatHp); float h = (nz - hatHp) * hatEnv; l += h * 0.4f; r += h * 0.6f; hatEnv *= hatDec; }
            l += c; r += c;
            // ping-pong delay
            float ol = dl[dpos % dl.size()], orr = dr[dpos % dr.size()];
            dl[dpos % dl.size()] = wetIn * 0.5f + orr * dfb; dr[dpos % dr.size()] = ol * dfb;
            ++dpos;
            l += ol * 0.55f; r += orr * 0.55f;
            if (master < 1) master += 1.f / (2.f * rate);                  // 2 s fade-in
            l = std::tanh(l * 1.4f) * 0.8f * master; r = std::tanh(r * 1.4f) * 0.8f * master;
            if (ch == 1) dst[i] = 0.5f * (l + r);
            else { float* o = dst + (size_t)i * ch; o[0] = l; o[1] = r; for (int k = 2; k < ch; ++k) o[k] = 0; }
        }
        return n;
    }
};
}

const SynthStation* SynthStations(int* count) { if (count) *count = (int)(sizeof(kStations) / sizeof(kStations[0])); return kStations; }
std::unique_ptr<AudioSource> MakeSynthSource(int station) {
    int n; const SynthStation* s = SynthStations(&n);
    auto p = std::make_unique<Synth>(); p->st = s[((station % n) + n) % n]; return p;
}
