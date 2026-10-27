// Kinetik KFX-8 — sequenced multi-effect engine: 8 lanes of steps switch 8 effects on and off (with locks)
#pragma once
#include "Bank.hpp"
#include "Fx.hpp"
#include <climits>
#include <cstring>
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace kn {

inline void knDenormalsOff()
{
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(_mm_getcsr() | 0x8040);
#elif defined(__aarch64__)
    uint64_t fpcr; __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr)); __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr | (1ull << 24)));
#endif
}

struct Transport { bool hostValid = false, hostPlaying = false; double beat = 0, bpm = 120; };

class Engine {
public:
    float laneParam[kLanes][kLaneParams];
    float global[kGlobalParams];
    Bank bank;                                   // audio-side copy (the plugin syncs it)
    // outputs for the UI
    int outStep = -1;                            // 1/24 of a 16th since the song start, modulo 32768 steps (-1 = stopped)
    uint32_t outActive = 0;                      // lanes whose effect is on now
    int outPattern = 0;
    uint32_t midiHold = 0;                       // lanes held on by MIDI notes (C2..G2)
    int midiPattern = -1;                        // pattern asked by a MIDI note (C3..D#4), -1 = none
    static constexpr int kFine = 24;

    Engine()
    {
        for (int l = 0; l < kLanes; ++l) for (int k = 0; k < kLaneParams; ++k) laneParam[l][k] = laneParamDefault(l, k);
        for (int g = 0; g < kGlobalParams; ++g) global[g] = globalDefault(g);
        bank.initDefault();
    }

    void init(double sampleRate)
    {
        sr = (float)sampleRate;
        stut.init(sr); rev.init(sr); tape.init(sr); pitch.init(sr); crush.init(sr); filt.init(sr); gate.init(sr); dly.init(sr);
        reset();
    }
    void reset()
    {
        for (auto& s : lane) s = LaneState();
        stut.h.clear(); rev.h.clear(); tape.h.clear(); dly.clear();
        wasPlaying = false; usingInternal = false; internalBeat = 0; prevPlayParam = false; now = 0;
        inG = outG = dwG = -1.f;
    }

    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames, const Transport& tp)
    {
        knDenormalsOff();
        if (frames == 0) return;
        // ---- transport
        const bool playParam = global[gPLAY] > 0.5f;
        double bpm = tp.hostValid && tp.bpm > 1.0 ? tp.bpm : (double)global[gTEMPO];
        bpm = std::max(20.0, std::min(400.0, bpm));
        bool playing = false, restarted = false; double b0 = 0;
        if (tp.hostValid && tp.hostPlaying) {
            playing = true; b0 = tp.beat;
            if (!wasPlaying || usingInternal || std::fabs(b0 - prevB1) > 0.01) restarted = true;
            usingInternal = false;
        } else if (playParam) {
            if (!prevPlayParam || !wasPlaying || !usingInternal) { internalBeat = 0; restarted = true; }
            playing = true; b0 = internalBeat; usingInternal = true;
        }
        prevPlayParam = playParam;
        const double bpf = bpm / (60.0 * sr);
        const double b1 = b0 + bpf * frames;
        const int want = clampi(midiPattern >= 0 ? midiPattern : (int)global[gPATTERN], 0, kPatterns - 1);
        if (!playing || restarted) curPattern = want;
        FxCtx ctx; ctx.sr = sr; ctx.spb = 60.0 * sr / bpm;
        const float tIn = trimGain(global[gINPUT]), tOut = trimGain(global[gOUTPUT]), tDw = global[gDRYWET] / 127.f;
        if (inG < 0.f) { inG = tIn; outG = tOut; dwG = tDw; }
        const float sm = 1.f - std::exp(-1.f / (0.02f * sr));
        const float envStep = 1.f / (0.004f * sr);           // 4 ms on / off ramps
        const float fadeSmp = 0.0015f * sr;

        uint32_t pos = 0;
        while (pos < frames) {
            const uint32_t n = std::min<uint32_t>(kSeg, frames - pos);
            const double beat = playing ? b0 + pos * bpf : freeBeat;
            // pattern change at the end of the master length
            if (playing && want != curPattern) {
                const Pattern& P = bank.patterns[curPattern];
                const int64_t k16 = (int64_t)std::floor(beat / 0.25 + 1e-9), len = std::max<int>(1, P.length);
                if (lastK16 != INT64_MIN && k16 / len != lastK16 / len) curPattern = want;
            }
            lastK16 = playing ? (int64_t)std::floor(beat / 0.25 + 1e-9) : INT64_MIN;
            const Pattern& P = bank.patterns[curPattern];
            // ---- which lanes are on, with which knob values
            for (int l = 0; l < kLanes; ++l) {
                LaneState& L = lane[l];
                bool on = false; int step = -1; float stepPh = 0.f;
                if (playing && !bank.mute[l]) {
                    const double sbt = 0.25 / kSpeedMult[P.speed[l] % kSpeeds];
                    const double off = (P.swing / 100.0 * 2.0 - 1.0) * sbt;
                    int64_t k = (int64_t)std::floor(beat / sbt + 1e-9);
                    double start = k * sbt + ((k & 1) ? off : 0.0);
                    if (beat < start) { --k; start = k * sbt + ((k & 1) ? off : 0.0); }
                    const double dur = sbt + ((k & 1) ? -off : off);
                    if (k >= 0) {
                        step = P.laneStep(l, k);
                        on = P.on[l][step] != 0;
                        stepPh = (float)clampf((float)((beat - start) / std::max(1e-9, dur)), 0.f, 1.f);
                    }
                }
                if ((midiHold >> l) & 1) on = true;
                if (on && !L.on) L.runStart = true;
                L.on = on; L.step = step; L.stepPh = stepPh;
                for (int k = 0; k < kKnobs; ++k) {
                    const uint8_t lk = step >= 0 ? P.locks[l][step][k] : kNoLock;
                    L.p[k] = lk != kNoLock && on ? (float)lk : laneParam[l][k];
                }
                L.mix = laneParam[l][4] / 127.f;
            }
            outStep = playing ? (int)((int64_t)std::floor(beat / 0.25 * kFine) % (32768LL * kFine)) : -1;
            // ---- set the effects for this segment
            ctx.stepPhase = lane[lnFilter].stepPh;
            stut.set(lane[lnStutter].p, ctx); rev.set(lane[lnReverse].p, ctx); tape.set(lane[lnTape].p, ctx);
            pitch.set(lane[lnPitch].p, ctx); crush.set(lane[lnCrush].p, ctx); filt.set(lane[lnFilter].p, ctx);
            gate.set(lane[lnGate].p, ctx); dly.set(lane[lnDelay].p, ctx);
            if (lane[lnStutter].runStart) stut.start(now);
            if (lane[lnReverse].runStart) rev.start(now);
            if (lane[lnTape].runStart) tape.start(now);
            for (auto& L : lane) L.runStart = false;
            // ---- samples
            for (uint32_t i = 0; i < n; ++i) {
                inG += (tIn - inG) * sm; outG += (tOut - outG) * sm; dwG += (tDw - dwG) * sm;
                const float dryL = inL[pos + i] * inG, dryR = inR[pos + i] * inG;
                float l = dryL, r = dryR;
                const double sb = beat + i * (playing ? bpf : bpf);
                for (int ln = 0; ln < kLanes; ++ln) {
                    LaneState& L = lane[ln];
                    const float target = L.on ? 1.f : 0.f;
                    if (L.env != target) L.env = L.env < target ? std::min(target, L.env + envStep) : std::max(target, L.env - envStep);
                    if (ln == lnDelay) {
                        if (L.env <= 0.f && dly.asleep()) continue;
                        float el, er; dly.process(l * L.env, r * L.env, el, er);
                        l += el * L.mix; r += er * L.mix;
                        continue;
                    }
                    float wl = l, wr = r;
                    switch (ln) {
                    case lnStutter: if (L.env <= 0.f) { stut.h.write(now + i, l, r); continue; } stut.process(wl, wr, now + i, fadeSmp); break;
                    case lnReverse: if (L.env <= 0.f) { rev.h.write(now + i, l, r); continue; } rev.process(wl, wr, now + i); break;
                    case lnTape:    if (L.env <= 0.f) { tape.h.write(now + i, l, r); continue; } tape.process(wl, wr, now + i); break;
                    case lnPitch:   pitch.process(wl, wr); break;          // keeps its delay line filled
                    default:
                        if (L.env <= 0.f) continue;
                        if (ln == lnCrush) crush.process(wl, wr);
                        else if (ln == lnFilter) filt.process(wl, wr);
                        else gate.process(wl, wr, sb);
                        break;
                    }
                    if (L.env <= 0.f) continue;
                    const float m = L.env * L.mix;
                    l += (wl - l) * m; r += (wr - r) * m;
                }
                outL[pos + i] = (dryL + (l - dryL) * dwG) * outG;
                outR[pos + i] = (dryR + (r - dryR) * dwG) * outG;
            }
            uint32_t act = 0;
            for (int l = 0; l < kLanes; ++l) if (lane[l].env > 0.5f) act |= 1u << l;
            outActive = act;
            now += n; pos += n;
            if (!playing) freeBeat += bpf * n;
        }
        outPattern = curPattern;
        wasPlaying = playing; prevB1 = b1;
        if (playing && usingInternal) internalBeat = b1;
        if (playing) freeBeat = b1;
    }

private:
    struct LaneState { bool on = false, runStart = false; int step = -1; float stepPh = 0.f, env = 0.f, mix = 1.f; float p[kKnobs] = {64, 64, 64, 64}; };
    static constexpr uint32_t kSeg = 32;
    float sr = 48000.f;
    LaneState lane[kLanes];
    Stutter stut; Reverse rev; TapeStop tape; Pitch pitch; Crush crush; Filter filt; Gate gate; Delay dly;
    int64_t now = 0, lastK16 = INT64_MIN;
    int curPattern = 0;
    bool wasPlaying = false, usingInternal = false, prevPlayParam = false;
    double internalBeat = 0, prevB1 = 0, freeBeat = 0;
    float inG = -1.f, outG = -1.f, dwG = -1.f;
};

} // namespace kn
