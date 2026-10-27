// Kinetik KFX-8 — the eight effects (stereo, in place: the engine mixes dry / wet with each lane's envelope)
#pragma once
#include "Params.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace kn {

// per-block context from the engine
struct FxCtx {
    float sr = 48000.f;
    double spb = 24000.0;       // samples per quarter note
    float stepPhase = 0.f;      // 0..1: progress inside the lane's current step (filter SWEEP)
};

// circular stereo history indexed by absolute sample time
struct History {
    std::vector<float> L, R; int64_t size = 0;
    void init(int n) { size = n; L.assign(n, 0.f); R.assign(n, 0.f); }
    void clear() { std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f); }
    inline void write(int64_t t, float l, float r) { const int64_t i = t % size; L[i] = l; R[i] = r; }
    // linear interpolation at a fractional absolute time (must be within the last `size` samples)
    inline void read(double t, float& l, float& r) const
    {
        const double f = std::floor(t);
        const int64_t i0 = ((int64_t)f % size + size) % size, i1 = (i0 + 1) % size;
        const float a = (float)(t - f);
        l = L[i0] + (L[i1] - L[i0]) * a; r = R[i0] + (R[i1] - R[i0]) * a;
    }
};

// 1 at the centre of [0, len], ramps of `fade` samples at both ends
inline float edgeWindow(double pos, double len, double fade)
{
    if (fade < 1.0) return 1.f;
    const double e = std::min(pos, len - pos);
    return e >= fade ? 1.f : (e <= 0.0 ? 0.f : (float)(e / fade));
}

// ---- STUTTER: first pass live, then the slice [t0, t0 + size) repeats, each repeat quieter / pitched
struct Stutter {
    History h; int64_t t0 = -1;
    double len = 1000; float decay = 1.f, pitch = 0.f, gate = 1.f;
    void init(float sr) { h.init((int)(sr * 8.f)); t0 = -1; }
    void set(const float* p, const FxCtx& c)
    {
        len = std::max(64.0, kStutSize[bin8(p[0])] * c.spb);
        decay = p[1] / 127.f;
        pitch = (p[2] - 64.f) / 64.f * 12.f;
        gate = 0.1f + p[3] / 127.f * 0.9f;
    }
    void start(int64_t now) { t0 = now; }
    inline void process(float& l, float& r, int64_t now, float fadeSmp)
    {
        h.write(now, l, r);
        if (t0 < 0) return;
        const double dt = (double)(now - t0);
        const int64_t n = (int64_t)(dt / len);
        if (n == 0) return;                                   // first pass: the sound itself
        const double ph = dt - n * len;
        const double rate = pitch != 0.f ? std::pow(2.0, n * pitch / 12.0) : 1.0;
        const double rp = ph * rate, playLen = len * gate;
        if (rp >= std::min(len, playLen * rate)) { l = r = 0.f; return; }
        float a = n > 64 ? 0.f : std::pow(decay, (float)n);
        a *= edgeWindow(rp, std::min(len, playLen * rate), fadeSmp);
        float rl, rr; h.read((double)t0 + rp, rl, rr);
        l = rl * a; r = rr * a;
    }
};

// ---- REVERSE: the `length` before the trigger, played backwards in a loop
struct Reverse {
    History h; int64_t t0 = -1;
    double len = 1000, speed = 1, fade = 100; OnePole tl, tr; bool toneOn = false;
    void init(float sr) { h.init((int)(sr * 8.f)); t0 = -1; }
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
        h.write(now, l, r);
        if (t0 < 0) return;
        const double dt = (double)(now - t0) * speed;
        const double c = std::fmod(dt, len);
        float rl, rr; h.read((double)t0 - 1.0 - c, rl, rr);
        const float w = edgeWindow(c, len, std::min(fade, len * 0.45));
        l = rl * w; r = rr * w;
        if (toneOn) { l = tl.lp(l); r = tr.lp(r); }
    }
};

// ---- TAPE STOP: the playback speed falls to zero over TIME
struct TapeStop {
    History h; int64_t t0 = -1; double readPos = 0, T = 10000;
    float curve = 1.f, tone = 0.5f, wobble = 0.f, wph = 0.f, sr = 48000.f;
    OnePole tl, tr;
    void init(float s) { sr = s; h.init((int)(sr * 8.f)); t0 = -1; }
    void set(const float* p, const FxCtx& c)
    {
        T = std::max(64.0, 0.0625 * std::pow(64.0, p[0] / 127.0) * c.spb);
        curve = std::pow(2.5f, (p[1] - 64.f) / 64.f * 1.3f);       // < 1: slows down late (soft), > 1: early (hard)
        tone = p[2] / 127.f; wobble = p[3] / 127.f; sr = c.sr;
    }
    void start(int64_t now) { t0 = now; readPos = (double)now; }
    inline void process(float& l, float& r, int64_t now)
    {
        h.write(now, l, r);
        if (t0 < 0) return;
        const double u = (double)(now - t0) / T;
        float rate = u >= 1.0 ? 0.f : (float)std::pow(1.0 - u, (double)curve);
        if (wobble > 0.f) { wph += 4.1f / sr; if (wph > 1.f) wph -= 1.f; rate *= 1.f + wobble * 0.06f * std::sin(kTwoPi * wph); }
        readPos += rate;
        if (readPos > (double)now) readPos = (double)now;
        float rl, rr; h.read(readPos - 1.0, rl, rr);
        const float amp = std::min(1.f, rate * 6.f);                  // no frozen DC at the end
        const float fc = std::max(150.f, 18000.f * std::pow(std::max(rate, 0.001f), 0.5f + tone * 2.5f));
        tl.setLP(fc, sr); tr.setLP(fc, sr);
        l = tl.lp(rl) * amp; r = tr.lp(rr) * amp;
    }
};

// ---- PITCH: two crossfaded taps sweeping a delay line (window), feedback = spiral
struct Pitch {
    std::vector<float> L, R; int size = 0, w = 0;
    double ratio = 1, win = 2400, ph = 0; float fb = 0, lastL = 0, lastR = 0;
    void init(float sr) { size = (int)(sr * 0.3f); L.assign(size, 0.f); R.assign(size, 0.f); w = 0; ph = 0; }
    void set(const float* p, const FxCtx& c)
    {
        const double semis = (p[0] - 64.0) / 64.0 * 24.0 + (p[1] - 64.0) / 64.0 * 0.5;
        ratio = std::pow(2.0, semis / 12.0);
        win = (15.0 + p[2] / 127.0 * 105.0) * c.sr / 1000.0;
        fb = p[3] / 127.f * 0.9f;
    }
    inline void tap(double d, float& l, float& r) const
    {
        double pos = (double)w - 1.0 - d; if (pos < 0) pos += size;
        const int i0 = (int)pos % size, i1 = (i0 + 1) % size; const float a = (float)(pos - std::floor(pos));
        l = L[i0] + (L[i1] - L[i0]) * a; r = R[i0] + (R[i1] - R[i0]) * a;
    }
    inline void process(float& l, float& r)
    {
        L[w] = l + lastL * fb; R[w] = r + lastR * fb;
        ph += (1.0 - ratio) / win; ph -= std::floor(ph);
        const double p2 = std::fmod(ph + 0.5, 1.0);
        float al, ar, bl, br; tap(ph * win, al, ar); tap(p2 * win, bl, br);
        const float g1 = (float)std::sin(3.14159265358979 * ph), g2 = (float)std::sin(3.14159265358979 * p2);
        const float a = g1 * g1, b = g2 * g2;
        l = al * a + bl * b; r = ar * a + br * b;
        lastL = l; lastR = r;
        if (++w >= size) w = 0;
    }
};

// ---- CRUSH: sample-and-hold rate, bit depth, drive, tone
struct Crush {
    int hold = 1, cnt = 0; float levels = 32768.f, drv = 0.f, hl = 0, hr = 0; OnePole tl, tr;
    void init(float) { cnt = 0; }
    void set(const float* p, const FxCtx& c)
    {
        const float bits = 16.f - p[0] / 127.f * 15.f;
        levels = std::pow(2.f, bits - 1.f);
        hold = std::max(1, (int)std::lround(std::pow(64.f, p[1] / 127.f)));
        drv = p[2] / 127.f;
        const float fc = 500.f * std::pow(40.f, p[3] / 127.f);
        tl.setLP(fc, c.sr); tr.setLP(fc, c.sr);
    }
    inline void process(float& l, float& r)
    {
        if (cnt++ % hold == 0) { hl = l; hr = r; }
        float a = std::round(hl * levels) / levels, b = std::round(hr * levels) / levels;
        if (drv > 0.f) { const float g = 1.f + drv * 7.f, n = 1.f / std::tanh(g); a = std::tanh(a * g) * n * 0.8f + a * 0.2f; b = std::tanh(b * g) * n * 0.8f + b * 0.2f; }
        l = tl.lp(a); r = tr.lp(b);
    }
};

// ---- FILTER: resonant state variable filter, SWEEP moves the cutoff across each step
struct Filter {
    Svf fl, fr; int type = 0;
    void init(float) { fl.reset(); fr.reset(); }
    void set(const float* p, const FxCtx& c)
    {
        type = bin3(p[2]);
        const float sweepOct = (p[3] - 64.f) / 64.f * 4.f * c.stepPhase;
        const float hz = 20.f * std::pow(1000.f, p[0] / 127.f) * std::pow(2.f, sweepOct);
        const float q = resoQ(p[1], 0.6f, 18.f);
        fl.setup(hz, q, c.sr); fr.setup(hz, q, c.sr);
    }
    inline void process(float& l, float& r)
    {
        fl.tick(l); fr.tick(r);
        if (type == 0) { l = fl.lp; r = fr.lp; } else if (type == 1) { l = fl.bp; r = fr.bp; } else { l = fl.hp; r = fr.hp; }
    }
};

// ---- GATE: rhythmic chop locked to the song position
struct Gate {
    double rate = 0.25; float duty = 0.5f, depth = 1.f, coef = 0.f, g = 1.f;
    void init(float) { g = 1.f; }
    void set(const float* p, const FxCtx& c)
    {
        rate = kGateRate[bin8(p[0])];
        duty = 0.05f + p[1] / 127.f * 0.9f;
        coef = std::exp(-1.f / ((0.5f + p[2] / 127.f * 19.5f) * 0.001f * c.sr));
        depth = p[3] / 127.f;
    }
    inline void process(float& l, float& r, double beat)
    {
        const double ph = beat / rate - std::floor(beat / rate);
        const float target = ph < duty ? 1.f : 1.f - depth;
        g = target + coef * (g - target);
        l *= g; r *= g;
    }
};

// ---- DELAY: synced stereo echo, ping-pong WIDTH, TONE in the loop; fed only while the lane is active
struct Delay {
    std::vector<float> L, R; int size = 0, w = 0; double d = 1000; float fb = 0.5f, width = 0.5f;
    OnePole tl, tr, hl, hr; float sr = 48000.f; int idle = 0;
    void init(float s) { sr = s; size = (int)(s * 4.5f); L.assign(size, 0.f); R.assign(size, 0.f); w = 0; hl.setLP(80.f, s); hr.setLP(80.f, s); }
    void set(const float* p, const FxCtx& c)
    {
        d = std::min((double)size - 4.0, std::max(16.0, kDelTime[bin8(p[0])] * c.spb));
        fb = p[1] / 127.f * 0.95f;
        const float fc = 800.f * std::pow(22.f, p[2] / 127.f);
        tl.setLP(fc, c.sr); tr.setLP(fc, c.sr);
        width = p[3] / 127.f;
    }
    inline void read(float& l, float& r) const
    {
        double pos = (double)w - d; if (pos < 0) pos += size;
        const int i0 = (int)pos % size, i1 = (i0 + 1) % size; const float a = (float)(pos - std::floor(pos));
        l = L[i0] + (L[i1] - L[i0]) * a; r = R[i0] + (R[i1] - R[i0]) * a;
    }
    // in = what goes into the echo (already scaled by the lane envelope); returns the echo
    inline void process(float inL, float inR, float& outL, float& outR)
    {
        float yl, yr; read(yl, yr);
        const float fl = tl.lp(yl), fr = tr.lp(yr);
        const float flh = fl - hl.lp(fl), frh = fr - hr.lp(fr);
        // WIDTH 0 = mono echo, 1 = full ping-pong
        const float mono = (inL + inR) * 0.5f;
        const float sendL = inL * (1.f - width) + mono * width, sendR = inR * (1.f - width);
        L[w] = sendL + (flh * (1.f - width) + frh * width) * fb;
        R[w] = sendR + (frh * (1.f - width) + flh * width) * fb;
        if (++w >= size) w = 0;
        outL = yl; outR = yr;
        idle = (std::fabs(yl) + std::fabs(yr) + std::fabs(inL) + std::fabs(inR) < 1e-6f) ? idle + 1 : 0;
    }
    bool asleep() const { return idle > size; }
    void clear() { std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f); idle = size + 1; }
};

} // namespace kn
