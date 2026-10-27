// Kinetik KFX-8 — the eight effects (all original DSP). Each takes its 6 knob values (0..127, already locked or not)
// in set(), called every 32-sample segment, and processes a stereo sample in place (or returns the wet signal).
#pragma once
#include "Params.hpp"
#include <vector>
#include <algorithm>

namespace kn {

struct FxCtx {
    float sr = 48000.f;
    double spb = 24000.0;       // samples per quarter note
    double beat = 0.0;          // song position (quarter notes) at the segment start
    float stepPhase = 0.f;      // 0..1: progress inside the lane's current step (filter SWEEP)
};

// fractional read from a ring buffer
inline float ringRead(const std::vector<float>& b, int w, double d)
{
    const int n = (int)b.size();
    double pos = (double)w - d; while (pos < 0) pos += n;
    const int i0 = (int)pos % n, i1 = (i0 + 1) % n; const float a = (float)(pos - std::floor(pos));
    return b[(size_t)i0] + (b[(size_t)i1] - b[(size_t)i0]) * a;
}

// ---------------------------------------------------------------- DRIVE
struct Drive {
    float g = 1.f, bias = 0.f, level = 1.f, comp = 1.f; int type = 0;
    OnePole toneL, toneR, cutL, cutR, dcL, dcR; bool cut = false;
    void init(float sr) { dcL.setLP(8.f, sr); dcR.setLP(8.f, sr); }
    void set(const float* p, const FxCtx& c)
    {
        const float a = p[0] / 127.f;
        g = 1.f + a * a * 30.f;
        type = bin4(p[1]);
        toneL.setLP(hzExp(p[2], 1000.f, 20000.f), c.sr); toneR.a = toneL.a;
        cut = p[3] >= 0.5f; if (cut) { cutL.setLP(hzExp(p[3], 20.f, 800.f), c.sr); cutR.a = cutL.a; }
        bias = (p[4] - 64.f) / 64.f * 0.6f;
        level = std::pow(10.f, levelDb(p[5]) / 20.f);
        comp = 1.f / std::sqrt(std::min(g, 8.f));            // keeps the loudness roughly even across AMOUNT
    }
    inline float shape(float x) const
    {
        x = x * g + bias;
        switch (type) {
        case 0: return std::tanh(x);
        case 1: return clampf(x, -1.f, 1.f);
        case 2: { float y = x * 0.25f + 0.25f; y -= std::floor(y); return 4.f * std::fabs(y - 0.5f) - 1.f; }   // triangle fold
        default: return x >= 0.f ? std::tanh(x) : std::tanh(x * 0.6f) / 0.6f * 0.7f;                         // uneven halves
        }
    }
    inline void process(float& l, float& r)
    {
        if (cut) { l -= cutL.lp(l); r -= cutR.lp(r); }
        const float b0 = shape(0.f);
        float a = shape(l) - b0, b = shape(r) - b0;
        a -= dcL.lp(a); b -= dcR.lp(b);
        const float k = (type == 0 || type == 3) ? std::max(comp, 0.45f) * 1.6f : comp * 1.4f;
        l = toneL.lp(a) * k * level; r = toneR.lp(b) * k * level;
    }
};

// ---------------------------------------------------------------- CRUSH
struct Crush {
    int hold = 1, cnt = 0; float levels = 32768.f, drv = 0.f, hl = 0, hr = 0, noise = 0.f, level = 1.f; OnePole tl, tr; Rng rng;
    void init(float) { cnt = 0; }
    void set(const float* p, const FxCtx& c)
    {
        const float bits = 16.f - p[0] / 127.f * 15.f;
        levels = std::pow(2.f, bits - 1.f);
        hold = std::max(1, (int)std::lround(std::pow(64.f, p[1] / 127.f)));
        noise = std::pow(p[2] / 127.f, 2.f) * 0.15f;
        drv = p[3] / 127.f;
        const float fc = hzExp(p[4], 500.f, 20000.f);
        tl.setLP(fc, c.sr); tr.setLP(fc, c.sr);
        level = std::pow(10.f, levelDb(p[5]) / 20.f);
    }
    inline void process(float& l, float& r)
    {
        if (cnt++ % hold == 0) { hl = l; hr = r; }
        float a = std::round(hl * levels) / levels, b = std::round(hr * levels) / levels;
        if (noise > 0.f) { const float n = rng.next() * noise * (std::fabs(hl) + std::fabs(hr) + 0.02f); a += n; b += n; }
        if (drv > 0.f) { const float g = 1.f + drv * 7.f, k = 1.f / std::tanh(g); a = std::tanh(a * g) * k * 0.8f + a * 0.2f; b = std::tanh(b * g) * k * 0.8f + b * 0.2f; }
        l = tl.lp(a) * level; r = tr.lp(b) * level;
    }
};

// ---------------------------------------------------------------- FILTER
struct Filter {
    Svf fl, fr; int type = 0; float env = 0.f, envAmt = 0.f, drive = 0.f, atk = 0.f, rel = 0.f;
    void init(float sr) { fl.reset(); fr.reset(); atk = 1.f - std::exp(-1.f / (0.005f * sr)); rel = 1.f - std::exp(-1.f / (0.12f * sr)); }
    void set(const float* p, const FxCtx& c)
    {
        type = bin4(p[2]);
        const float sweepOct = (p[3] - 64.f) / 64.f * 4.f * c.stepPhase;
        envAmt = (p[4] - 64.f) / 64.f * 4.f;
        const float envOct = envAmt * clampf(env * 3.f, 0.f, 1.f);
        const float hz = hzExp(p[0], 20.f, 20000.f) * std::pow(2.f, sweepOct + envOct);
        const float q = resoQ(p[1], 0.6f, 18.f);
        fl.setup(hz, q, c.sr); fr.setup(hz, q, c.sr);
        drive = p[5] / 127.f;
    }
    inline void process(float& l, float& r)
    {
        const float lev = std::max(std::fabs(l), std::fabs(r));
        env += (lev - env) * (lev > env ? atk : rel);
        if (drive > 0.f) { const float g = 1.f + drive * 6.f, k = 1.f / std::tanh(g); l = std::tanh(l * g) * k; r = std::tanh(r * g) * k; }
        fl.tick(l); fr.tick(r);
        switch (type) {
        case 0: l = fl.lp; r = fr.lp; break;
        case 1: l = fl.bp; r = fr.bp; break;
        case 2: l = fl.hp; r = fr.hp; break;
        default: l = fl.lp + fl.hp; r = fr.lp + fr.hp; break;
        }
    }
};

// ---------------------------------------------------------------- COMP (stereo linked, feed-forward)
struct Comp {
    float thr = -20.f, ratio = 4.f, knee = 4.f, makeup = 1.f, atk = 0.f, rel = 0.f, grDb = 0.f, env = 0.f, sr = 48000.f;
    float meter = 0.f;                                       // gain reduction (dB, positive) for the UI
    void init(float s) { sr = s; grDb = 0.f; env = 0.f; }
    void set(const float* p, const FxCtx&)
    {
        thr = compThreshDb(p[0]); ratio = compRatio(p[1]);
        atk = 1.f - std::exp(-1.f / (compAttackMs(p[2]) * 0.001f * sr));
        rel = 1.f - std::exp(-1.f / (compReleaseMs(p[3]) * 0.001f * sr));
        makeup = std::pow(10.f, p[4] / 127.f * 24.f / 20.f);
        knee = p[5] / 127.f * 12.f;
    }
    inline void process(float& l, float& r)
    {
        const float lev = std::max(std::fabs(l), std::fabs(r));
        env = lev > env ? lev : env + (lev - env) * 0.0005f;                     // peak hold for the detector
        const float db = 20.f * std::log10(std::max(env, 1e-6f));
        const float over = db - thr;
        float gr;
        if (knee > 0.01f && std::fabs(over) <= knee * 0.5f) { const float x = over + knee * 0.5f; gr = (1.f - 1.f / ratio) * x * x / (2.f * knee); }
        else gr = over > 0.f ? over * (1.f - 1.f / ratio) : 0.f;
        grDb += (gr - grDb) * (gr > grDb ? atk : rel);
        const float g = std::pow(10.f, -grDb / 20.f) * makeup;
        l *= g; r *= g;
        meter = grDb;
    }
};

// ---------------------------------------------------------------- CHORUS
struct Chorus {
    std::vector<float> L, R; int w = 0, voices = 2; float ph = 0.f, inc = 0.f, depth = 0.f, base = 0.f, width = 1.f, sr = 48000.f; OnePole tl, tr;
    void init(float s) { sr = s; L.assign((size_t)(s * 0.06f), 0.f); R.assign(L.size(), 0.f); w = 0; }
    void set(const float* p, const FxCtx& c)
    {
        inc = rateHz(p[0], 0.05f, 5.f) / c.sr;
        depth = p[1] / 127.f * 0.006f * c.sr;                      // up to 6 ms
        base = (0.005f + p[2] / 127.f * 0.025f) * c.sr;
        voices = bin4(p[3]) + 1;
        width = p[4] / 127.f;
        tl.setLP(hzExp(p[5], 1000.f, 20000.f), c.sr); tr.a = tl.a;
    }
    inline void process(float& l, float& r)
    {
        L[(size_t)w] = l; R[(size_t)w] = r;
        ph += inc; if (ph >= 1.f) ph -= 1.f;
        float wl = 0.f, wr = 0.f;
        for (int v = 0; v < voices; ++v) {
            const float p = ph + v / (float)voices;
            const float m = std::sin(kTwoPi * p);
            const double d = base + depth * (0.5f + 0.5f * m) + v * 0.0013f * sr;
            const float a = ringRead(L, w, d), b = ringRead(R, w, d);
            const float pan = (v & 1) ? 1.f : -1.f;
            const float gl = 1.f - std::max(0.f, pan) * width * 0.8f, gr = 1.f + std::min(0.f, pan) * width * 0.8f;
            wl += a * gl; wr += b * gr;
        }
        const float n = 1.f / std::sqrt((float)voices);
        if (++w >= (int)L.size()) w = 0;
        l = tl.lp(wl * n); r = tr.lp(wr * n);
    }
};

// ---------------------------------------------------------------- FLANGER
struct Flanger {
    std::vector<float> L, R; int w = 0; float ph = 0.f, inc = 0.f, depth = 0.f, base = 0.f, fb = 0.f, width = 0.f, sr = 48000.f, yl = 0.f, yr = 0.f;
    bool synced = false; double period = 0.0;
    void init(float s) { sr = s; L.assign((size_t)(s * 0.03f), 0.f); R.assign(L.size(), 0.f); w = 0; }
    void set(const float* p, const FxCtx& c)
    {
        const int sy = bin8(p[1]);
        synced = sy > 0;
        if (synced) { period = kFlSync[sy]; ph = (float)(std::fmod(c.beat, period) / period); inc = (float)(1.0 / (period * c.spb)); }
        else inc = rateHz(p[0], 0.02f, 10.f) / c.sr;
        depth = p[2] / 127.f;
        base = (0.0001f + p[3] / 127.f * 0.0099f) * c.sr;
        fb = (p[4] - 64.f) / 64.f * 0.92f;
        width = p[5] / 127.f;
    }
    inline void process(float& l, float& r)
    {
        ph += inc; if (ph >= 1.f) ph -= 1.f;
        auto tri = [](float p) { p -= std::floor(p); return p < 0.5f ? p * 2.f : 2.f - p * 2.f; };
        const double dl = 1.0 + base * (1.f - depth) + base * depth * 2.f * tri(ph);
        const double dr = 1.0 + base * (1.f - depth) + base * depth * 2.f * tri(ph + width * 0.25f);
        const float a = ringRead(L, w, std::min(dl, (double)L.size() - 2.0)), b = ringRead(R, w, std::min(dr, (double)R.size() - 2.0));
        L[(size_t)w] = l + a * fb; R[(size_t)w] = r + b * fb;
        if (++w >= (int)L.size()) w = 0;
        l = (l + a) * 0.6f; r = (r + b) * 0.6f;
        (void)yl; (void)yr;
    }
};

// Schroeder allpass (diffusion / dispersion)
struct Allpass {
    std::vector<float> b; int w = 0; float g = 0.6f;
    void init(int len, float gain) { b.assign((size_t)std::max(1, len), 0.f); w = 0; g = gain; }
    void clear() { std::fill(b.begin(), b.end(), 0.f); }
    inline float process(float x) { const float d = b[(size_t)w]; const float y = d - g * x; b[(size_t)w] = x + g * y; if (++w >= (int)b.size()) w = 0; return y; }
};

// ---------------------------------------------------------------- DELAY (wet only; the input is scaled by the lane envelope)
// models: DIGITAL (clean), TAPE (wow + flutter, saturated and darker repeats), BBD (dark, soft-clipped, slight chorus),
// DIFFUSE (allpasses in the loop: the repeats smear into a wash), MULTI-TAP (four taps inside the delay time)
struct Delay {
    std::vector<float> L, R; int size = 0, w = 0; double d = 1000; float fb = 0.5f, width = 0.5f, mod = 0.f, modPh = 0.f, modInc = 0.f, flPh = 0.f, duck = 0.f, env = 0.f, envRel = 0.f;
    OnePole tl, tr, tl2, tr2, hl, hr; float sr = 48000.f; int idle = 0, model = 0; Allpass apL[3], apR[3]; Rng rng;
    void init(float s)
    {
        sr = s; size = (int)(s * 4.5f); L.assign(size, 0.f); R.assign(size, 0.f); w = 0; hl.setLP(80.f, s); hr.setLP(80.f, s);
        modInc = 0.6f / s; envRel = 1.f - std::exp(-1.f / (0.15f * s)); idle = size + 1;
        static const int ap[3] = {142, 107, 379};
        for (int k = 0; k < 3; ++k) { apL[k].init((int)(ap[k] * s / 48000.f), 0.6f); apR[k].init((int)(ap[k] * 1.13f * s / 48000.f), 0.6f); }
    }
    void set(const float* p, const FxCtx& c)
    {
        d = std::min((double)size - 400.0, std::max(16.0, kDelTime[bin8(p[0])] * c.spb));
        fb = p[1] / 127.f * 0.95f;
        model = modelOf(p[6], kDelayModels);
        float fc = hzExp(p[2], 800.f, 18000.f);
        if (model == 1) fc = std::min(fc, 6000.f); else if (model == 2) fc = std::min(fc, 2800.f);
        tl.setLP(fc, c.sr); tr.a = tl.a; tl2.a = tl.a; tr2.a = tl.a;
        width = p[3] / 127.f;
        mod = p[4] / 127.f * 0.003f * c.sr + (model == 1 ? 0.0012f * c.sr : (model == 2 ? 0.0004f * c.sr : 0.f));
        duck = p[5] / 127.f;
    }
    inline float tap(const std::vector<float>& b, double dd) const { return ringRead(b, w, std::min(dd, (double)size - 2.0)); }
    inline void process(float inL, float inR, float dryL, float dryR, float& outL, float& outR)
    {
        modPh += modInc; if (modPh >= 1.f) modPh -= 1.f;
        flPh += 7.f / sr; if (flPh >= 1.f) flPh -= 1.f;
        double dd = d + (mod > 0.f ? mod * (0.5f + 0.5f * std::sin(kTwoPi * modPh)) : 0.f);
        if (model == 1) dd += 0.00015f * sr * std::sin(kTwoPi * flPh);                   // tape flutter
        float yl = tap(L, dd), yr = tap(R, dd);
        float fl = tl.lp(yl), fr = tr.lp(yr);
        if (model == 2) { fl = tl2.lp(fl); fr = tr2.lp(fr); }                            // BBD: 2 poles
        float flh = fl - hl.lp(fl), frh = fr - hr.lp(fr);
        if (model == 1) { flh = std::tanh(flh * 1.4f) / 1.4f; frh = std::tanh(frh * 1.4f) / 1.4f; }
        else if (model == 2) { flh = std::tanh(flh * 1.2f) / 1.2f + rng.next() * 0.0004f; frh = std::tanh(frh * 1.2f) / 1.2f + rng.next() * 0.0004f; }
        else if (model == 3) { for (auto& a : apL) flh = a.process(flh); for (auto& a : apR) frh = a.process(frh); }
        const float mono = (inL + inR) * 0.5f;
        const float sendL = inL * (1.f - width) + mono * width, sendR = inR * (1.f - width);
        L[(size_t)w] = sendL + (flh * (1.f - width) + frh * width) * fb;
        R[(size_t)w] = sendR + (frh * (1.f - width) + flh * width) * fb;
        if (model == 4) {                                                                // multi-tap: 1/4, 1/2, 3/4 of the time too
            yl = (yl + 0.75f * tap(L, dd * 0.75) + 0.55f * tap(L, dd * 0.5) + 0.4f * tap(L, dd * 0.25)) * 0.5f;
            yr = (yr + 0.75f * tap(R, dd * 0.75) + 0.55f * tap(R, dd * 0.5) + 0.4f * tap(R, dd * 0.25)) * 0.5f;
        } else if (model == 1 || model == 2) { yl = fl; yr = fr; }
        if (++w >= size) w = 0;
        float g = 1.f;
        if (duck > 0.f) { const float lev = std::max(std::fabs(dryL), std::fabs(dryR)); env = lev > env ? lev : env + (lev - env) * envRel; g = 1.f - duck * clampf(env * 3.f, 0.f, 0.9f); }
        outL = yl * g; outR = yr * g;
        idle = (std::fabs(yl) + std::fabs(yr) + std::fabs(inL) + std::fabs(inR) < 1e-6f) ? idle + 1 : 0;
    }
    bool asleep() const { return idle > size; }
    void clear() { std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f); for (int k = 0; k < 3; ++k) { apL[k].clear(); apR[k].clear(); } idle = size + 1; }
};

// octave-up shifter (two crossfaded taps reading twice as fast), for the SHIMMER reverb
struct OctaveUp {
    std::vector<float> b; int w = 0; float ph = 0.f, inc = 0.f; int win = 2048;
    void init(float sr) { win = (int)(0.045f * sr); b.assign((size_t)win * 2 + 4, 0.f); w = 0; ph = 0.f; inc = 1.f / win; }
    void clear() { std::fill(b.begin(), b.end(), 0.f); }
    inline float process(float x)
    {
        b[(size_t)w] = x;
        ph += inc; if (ph >= 1.f) ph -= 1.f;
        float p2 = ph + 0.5f; if (p2 >= 1.f) p2 -= 1.f;
        const float a = ringRead(b, w, 1.0 + (1.f - ph) * win), c = ringRead(b, w, 1.0 + (1.f - p2) * win);
        const float ga = std::sin(kPi * ph), gc = std::sin(kPi * p2);
        if (++w >= (int)b.size()) w = 0;
        return a * ga * ga + c * gc * gc;
    }
};

// ---------------------------------------------------------------- REVERB (8-line FDN; wet only)
// models: ROOM (short, early), PLATE (dense input diffusion, bright), HALL (long lines, slow movement),
// SPRING (short tank, dispersive allpasses = the chirp, band-limited), SHIMMER (hall + an octave up fed back)
struct Reverb {
    static constexpr int kN = 8;
    std::vector<float> buf[kN]; int len[kN] = {}, pos[kN] = {}, model = -1;
    float g[kN] = {}, z[kN] = {}, damp = 0.3f, sr = 48000.f, mod = 0.f, modPh = 0.f, width = 1.f, rt = 2.f;
    std::vector<float> pre; int pw = 0; int preD = 0; bool cut = false; OnePole cutL, sprLp, sprHp;
    Allpass diff[4], disp[8]; OctaveUp oct; float shim = 0.f;
    int idle = 0;
    void init(float s)
    {
        sr = s;
        for (int i = 0; i < kN; ++i) buf[i].assign((size_t)(73.3f * 0.001f * s * 1.5f * 1.7f) + 64, 0.f);
        pre.assign((size_t)(0.21f * s) + 4, 0.f);
        static const int dl[4] = {142, 107, 379, 277};
        for (int k = 0; k < 4; ++k) diff[k].init((int)(dl[k] * s / 48000.f), 0.7f);
        for (int k = 0; k < 8; ++k) disp[k].init(2 + k % 3, 0.62f);
        oct.init(s);
        sprLp.setLP(4200.f, s); sprHp.setLP(180.f, s);
        model = -1; setModel(1);
        clear();
    }
    void setModel(int m)
    {
        if (m == model) return;
        model = m;
        static const float ms[kN] = {31.3f, 37.9f, 41.1f, 47.3f, 53.9f, 59.1f, 67.7f, 73.3f};
        static const float scale[kReverbModels] = {0.55f, 0.8f, 1.6f, 0.35f, 1.4f};
        for (int i = 0; i < kN; ++i) len[i] = std::max(16, std::min((int)buf[i].size() - 2, (int)(ms[i] * 0.001f * sr * 1.5f * scale[m])));
        clear();
    }
    void clear()
    {
        for (int i = 0; i < kN; ++i) { std::fill(buf[i].begin(), buf[i].end(), 0.f); pos[i] = 0; z[i] = 0.f; }
        std::fill(pre.begin(), pre.end(), 0.f); pw = 0; idle = 1 << 30; shim = 0.f;
        for (auto& a : diff) a.clear();
        for (auto& a : disp) a.clear();
        oct.clear();
    }
    void set(const float* p, const FxCtx& c)
    {
        setModel(modelOf(p[6], kReverbModels));
        rt = revDecaySec(p[0]) * (model == 0 ? 0.5f : (model == 3 ? 0.7f : 1.f));
        for (int i = 0; i < kN; ++i) g[i] = std::pow(10.f, -3.f * len[i] / (rt * c.sr));
        preD = std::min((int)pre.size() - 2, (int)(p[1] / 127.f * 0.2f * c.sr));
        float fc = hzExp(p[2], 1500.f, 16000.f); if (model == 1) fc = std::min(16000.f, fc * 1.5f);
        damp = std::exp(-kTwoPi * std::min(fc, c.sr * 0.45f) / c.sr);
        cut = p[3] >= 0.5f; if (cut) cutL.setLP(hzExp(p[3], 20.f, 1000.f), c.sr);
        mod = p[4] / 127.f * (model == 2 || model == 4 ? 20.f : 10.f);
        width = p[5] / 127.f;
    }
    bool asleep() const { return idle > (int)(sr * 20.f); }
    inline void process(float inL, float inR, float& outL, float& outR)
    {
        if (inL != 0.f || inR != 0.f) idle = 0;
        if (asleep()) { outL = outR = 0.f; return; }
        float x = (inL + inR) * 0.5f; const float side = (inL - inR) * 0.5f;
        if (cut) x -= cutL.lp(x);
        if (model == 1) for (auto& a : diff) x = a.process(x);                          // plate: dense from the start
        if (model == 3) { x = sprLp.lp(x); x -= sprHp.lp(x); for (auto& a : disp) x = a.process(x); }   // spring chirp
        if (model == 4) x += shim * 0.45f;
        pre[(size_t)pw] = x;
        int rp = pw - preD; if (rp < 0) rp += (int)pre.size();
        const float xin = pre[(size_t)rp];
        if (++pw >= (int)pre.size()) pw = 0;
        modPh += 0.7f / sr; if (modPh >= 1.f) modPh -= 1.f;
        float o[kN];
        for (int i = 0; i < kN; ++i) {
            if (mod > 0.f && (i == 1 || i == 6)) {
                const float off = mod * (0.5f + 0.5f * std::sin(kTwoPi * (modPh + i * 0.37f)));
                double rd = pos[i] - off; const int n = len[i]; while (rd < 0) rd += n;
                const int i0 = (int)rd % n, i1 = (i0 + 1) % n; const float a = (float)(rd - std::floor(rd));
                o[i] = buf[i][(size_t)i0] + (buf[i][(size_t)i1] - buf[i][(size_t)i0]) * a;
            } else o[i] = buf[i][(size_t)pos[i]];
        }
        float a[kN]; for (int i = 0; i < kN; ++i) a[i] = o[i];
        for (int h = 1; h < kN; h <<= 1)
            for (int i = 0; i < kN; i += h * 2)
                for (int j = i; j < i + h; ++j) { const float u = a[j], v = a[j + h]; a[j] = u + v; a[j + h] = u - v; }
        for (int i = 0; i < kN; ++i) {
            float y = a[i] * 0.35355339f * g[i];
            z[i] = y + (z[i] - y) * damp;
            const float in = (i & 1) ? xin - side * 0.5f : xin + side * 0.5f;
            buf[i][(size_t)pos[i]] = z[i] + in * 0.3f;
            if (++pos[i] >= len[i]) pos[i] = 0;
        }
        float l = (o[0] + o[2] + o[4] + o[6]) * 0.5f, r = (o[1] + o[3] + o[5] + o[7]) * 0.5f;
        if (model == 4) shim = oct.process((l + r) * 0.5f);
        const float mid = (l + r) * 0.5f, sd = (l - r) * 0.5f * width;
        outL = mid + sd; outR = mid - sd;
        if (std::fabs(outL) + std::fabs(outR) < 1e-6f) ++idle; else idle = 0;
    }
};


// ---------------------------------------------------------------- PERFORM lane (from KFX-8 0.1): one shared 8 s history
struct History {
    std::vector<float> L, R; int64_t size = 0;
    void init(int n) { size = n; L.assign(n, 0.f); R.assign(n, 0.f); }
    void clear() { std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f); }
    inline void write(int64_t t, float l, float r) { const int64_t i = t % size; L[i] = l; R[i] = r; }
    inline void read(double t, float& l, float& r) const
    {
        const double f = std::floor(t);
        const int64_t i0 = ((int64_t)f % size + size) % size, i1 = (i0 + 1) % size;
        const float a = (float)(t - f);
        l = L[i0] + (L[i1] - L[i0]) * a; r = R[i0] + (R[i1] - R[i0]) * a;
    }
};
inline float edgeWindow(double pos, double len, double fade)
{
    if (fade < 1.0) return 1.f;
    const double e = std::min(pos, len - pos);
    return e >= fade ? 1.f : (e <= 0.0 ? 0.f : (float)(e / fade));
}
// STUTTER: first pass live, then the slice [t0, t0 + size) repeats, each repeat quieter / pitched
struct Stutter {
    const History* h = nullptr; int64_t t0 = -1;
    double len = 1000; float decay = 1.f, pitch = 0.f, gate = 1.f;
    void set(const float* p, const FxCtx& c)
    {
        len = std::max(64.0, kStutSize[bin8(p[0])] * c.spb);
        decay = p[1] / 127.f; pitch = (p[2] - 64.f) / 64.f * 12.f; gate = 0.1f + p[3] / 127.f * 0.9f;
    }
    void start(int64_t now) { t0 = now; }
    inline void process(float& l, float& r, int64_t now, float fadeSmp)
    {
        if (t0 < 0) return;
        const double dt = (double)(now - t0);
        const int64_t n = (int64_t)(dt / len);
        if (n == 0) return;
        const double ph = dt - n * len;
        const double rate = pitch != 0.f ? std::pow(2.0, n * pitch / 12.0) : 1.0;
        const double rp = ph * rate, playLen = len * gate;
        if (rp >= std::min(len, playLen * rate)) { l = r = 0.f; return; }
        float a = n > 64 ? 0.f : std::pow(decay, (float)n);
        a *= edgeWindow(rp, std::min(len, playLen * rate), fadeSmp);
        float rl, rr; h->read((double)t0 + rp, rl, rr);
        l = rl * a; r = rr * a;
    }
};
// REVERSE: the LENGTH before the trigger, played backwards in a loop
struct Reverse {
    const History* h = nullptr; int64_t t0 = -1;
    double len = 1000, speed = 1, fade = 100; OnePole tl, tr; bool toneOn = false;
    void set(const float* p, const FxCtx& c)
    {
        len = std::max(256.0, kRevLen[bin8(p[0])] * c.spb);
        speed = std::pow(2.0, (p[1] - 64.0) / 64.0);
        fade = (1.0 + p[2] / 127.0 * 59.0) * c.sr / 1000.0;
        toneOn = p[3] < 126.5f;
        const float fc = 300.f * std::pow(60.f, p[3] / 127.f);
        tl.setLP(fc, c.sr); tr.setLP(fc, c.sr);
    }
    void start(int64_t now) { t0 = now; }
    inline void process(float& l, float& r, int64_t now)
    {
        if (t0 < 0) return;
        const double dt = (double)(now - t0) * speed;
        const double c = std::fmod(dt, len);
        float rl, rr; h->read((double)t0 - 1.0 - c, rl, rr);
        const float w = edgeWindow(c, len, std::min(fade, len * 0.45));
        l = rl * w; r = rr * w;
        if (toneOn) { l = tl.lp(l); r = tr.lp(r); }
    }
};
// TAPE STOP: the playback speed falls to zero over TIME
struct TapeStop {
    const History* h = nullptr; int64_t t0 = -1; double readPos = 0, T = 10000;
    float curve = 1.f, tone = 0.5f, wobble = 0.f, wph = 0.f, sr = 48000.f;
    OnePole tl, tr;
    void set(const float* p, const FxCtx& c)
    {
        T = std::max(64.0, 0.0625 * std::pow(64.0, p[0] / 127.0) * c.spb);
        curve = std::pow(2.5f, (p[1] - 64.f) / 64.f * 1.3f);
        tone = p[2] / 127.f; wobble = p[3] / 127.f; sr = c.sr;
    }
    void start(int64_t now) { t0 = now; readPos = (double)now; }
    inline void process(float& l, float& r, int64_t now)
    {
        if (t0 < 0) return;
        const double u = (double)(now - t0) / T;
        float rate = u >= 1.0 ? 0.f : (float)std::pow(1.0 - u, (double)curve);
        if (wobble > 0.f) { wph += 4.1f / sr; if (wph > 1.f) wph -= 1.f; rate *= 1.f + wobble * 0.06f * std::sin(kTwoPi * wph); }
        readPos += rate;
        if (readPos > (double)now) readPos = (double)now;
        float rl, rr; h->read(readPos - 1.0, rl, rr);
        const float amp = std::min(1.f, rate * 6.f);
        const float fc = std::max(150.f, 18000.f * std::pow(std::max(rate, 0.001f), 0.5f + tone * 2.5f));
        tl.setLP(fc, sr); tr.setLP(fc, sr);
        l = tl.lp(rl) * amp; r = tr.lp(rr) * amp;
    }
};

} // namespace kn
