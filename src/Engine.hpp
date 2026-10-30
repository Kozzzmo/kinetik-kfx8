// Kinetik KFX-8 — sequenced multi-effect engine: 8 effects, each on all the time or switched by its lane of steps (with locks)
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
    uint32_t midiHold = 0;                       // lanes held on by MIDI notes (C2..G#2) / PUSH; ONE-SHOT lanes start on the press
    bool fill = false;                           // FILL (MIDI A2 or the FILL button): steps with FILL / NOT FILL conditions
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
        drive.init(sr); crush.init(sr); filt.init(sr); comp.init(sr); chorus.init(sr); flanger.init(sr); dly.init(sr); rev.init(sr);
        hist.init((int)(sr * 8.f)); stut.h = rv.h = tape.h = &hist;
        envAtk = envRel = 0.f;
        reset();
    }
    void reset()
    {
        for (auto& s : lane) s = LaneState();
        dly.clear(); rev.clear(); hist.clear(); stut.t0 = rv.t0 = tape.t0 = -1;
        wasPlaying = false; usingInternal = false; internalBeat = 0; prevPlayParam = false; now = 0;
        inG = outG = dwG = -1.f; chainPl.reset(); envVal = 0.f; prevHold = 0;
    }
    float compMeter() const { return comp.meter; }
    float lfoOut[kMods] = {};
    const float* scL = nullptr; const float* scR = nullptr;   // side-chain input of the next process() (nullptr = none)
    float scPeak = 0.f;                                     // its level, for the UI                     // last modulation values (-1..1, depth applied) for the UI
    float fltLfoNow = 0.f;                                  // FILTER LFO (-1..1, before its depth), for the UI

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
        if (!playing || restarted) {
            const int c = playing ? chainPl.start(bank.chain) : -1;
            if (!playing) chainPl.reset();
            const int np = c >= 0 ? c : want;
            if (np != curPattern || restarted) { curPattern = np; patStartBeat = playing ? b0 : 0.0; for (auto& L : lane) L.lastK = INT64_MIN; }
        }
        FxCtx ctx; ctx.sr = sr; ctx.spb = 60.0 * sr / bpm;
        const float tIn = trimGain(global[gINPUT]), tOut = trimGain(global[gOUTPUT]), tDw = global[gDRYWET] / 127.f;
        if (inG < 0.f) { inG = tIn; outG = tOut; dwG = tDw; }
        const float sm = 1.f - std::exp(-1.f / (0.02f * sr));
        const float envStep = 1.f / (0.004f * sr);           // 4 ms on / off ramps
        const float fadeSmp = 0.0015f * sr;
        // ONE-SHOT lanes: a new press starts them at their next step
        const uint32_t pressed = midiHold & ~prevHold; prevHold = midiHold;
        for (int l = 0; l < kLanes; ++l) if (bank.always[l] == lmOneShot && ((pressed >> l) & 1)) lane[l].shotPending = true;
        // envelope follower coefficients
        envAtk = 1.f - std::exp(-(float)kSeg / ((0.001f + global[gENV_ATTACK] / 127.f * 0.199f) * sr));
        envRel = 1.f - std::exp(-(float)kSeg / ((0.01f + global[gENV_RELEASE] / 127.f * 1.99f) * sr));

        uint32_t pos = 0;
        while (pos < frames) {
            const uint32_t n = std::min<uint32_t>(kSeg, frames - pos);
            const double beat = playing ? b0 + pos * bpf : freeBeat;
            ctx.beat = beat;
            // pattern change at the end of the master length (chain, or a queued pattern)
            if (playing) {
                const Pattern& P = bank.patterns[curPattern];
                const int64_t k16 = (int64_t)std::floor(beat / 0.25 + 1e-9), len = std::max<int>(1, P.length);
                if (lastK16 != INT64_MIN && k16 / len != lastK16 / len) {
                    const int c = chainPl.next(bank.chain);
                    const int np = c >= 0 ? c : want;
                    if (np != curPattern) { curPattern = np; patStartBeat = beat; for (auto& L : lane) L.lastK = INT64_MIN; }
                }
                lastK16 = k16;
            } else lastK16 = INT64_MIN;
            const Pattern& P = bank.patterns[curPattern];
            // ---- which lanes are on, with which knob values
            for (int l = 0; l < kLanes; ++l) {
                LaneState& L = lane[l];
                const int mode = bank.always[l];
                bool lit = false; int step = -1; float stepPh = 0.f;
                if (playing || mode == lmOneShot) {
                    const double sbt = 0.25 / kSpeedMult[P.speed[l] % kSpeeds];
                    const double off = (P.swing / 100.0 * 2.0 - 1.0) * sbt;
                    int64_t k = (int64_t)std::floor(beat / sbt + 1e-9);
                    double start = k * sbt + ((k & 1) ? off : 0.0);
                    if (beat < start) { --k; start = k * sbt + ((k & 1) ? off : 0.0); }
                    const double dur = sbt + ((k & 1) ? -off : off);
                    const int len = P.len[l] ? P.len[l] : 16;
                    int64_t kk = k;
                    if (mode == lmOneShot) {                       // plays its steps once from step 1, then stops
                        if (L.shotPending) { L.shotK0 = k + 1; L.shotPending = false; }
                        kk = (L.shotK0 != INT64_MIN && k >= L.shotK0 && k < L.shotK0 + len) ? k - L.shotK0 : -1;
                    }
                    if (kk >= 0) {
                        step = P.laneStep(l, kk);
                        // the step's condition, decided once when the step starts (a tie keeps its run's decision)
                        if (k != L.lastK) {
                            L.lastK = k;
                            if (!P.tie[l][step]) {
                                const int64_t k0 = (int64_t)std::floor(patStartBeat / sbt + 1e-9);
                                const int64_t loop = mode == lmOneShot ? 0 : std::max<int64_t>(0, (k - k0) / len);
                                L.condOk = condPasses(P.cond[l][step], loop);
                            }
                        }
                        lit = P.lit(l, step) && L.condOk;
                        stepPh = (float)clampf((float)((beat - start) / std::max(1e-9, dur)), 0.f, 1.f);
                        if (lit) {                        // tied steps: one long step, the locks of its first step
                            int back = 0;
                            const int st = P.runStart(l, step, back);
                            const int rl = P.runLength(l, st);
                            stepPh = std::min(1.f, (back + stepPh) / (float)std::max(1, rl));
                            step = st;
                        }
                    }
                }
                if (mode != lmOneShot && ((midiHold >> l) & 1)) lit = true;
                const bool on = !bank.mute[l] && (mode == lmAlways || lit);
                if (on && !L.on) L.runStart = true;
                L.on = on; L.step = step; L.stepPh = stepPh;
                for (int k = 0; k < kKnobs; ++k) {
                    const uint8_t lk = step >= 0 && lit ? P.locks[l][step][k] : kNoLock;
                    L.p[k] = lk != kNoLock ? (float)lk : laneParam[l][k];
                }
                L.mix = laneParam[l][kKnobs] / 127.f;
            }
            outStep = playing ? (int)((int64_t)std::floor(beat / 0.25 * kFine) % (32768LL * kFine)) : -1;
            // ---- modulation: two LFOs and the envelope follower (the input level of this segment)
            {
                float pk = 0.f, sp = 0.f;
                if (scL && scR) for (uint32_t i = 0; i < n; ++i) sp = std::max(sp, std::max(std::fabs(scL[pos + i]), std::fabs(scR[pos + i])));
                scPeak = std::max(sp, scPeak * 0.995f);
                if (sideSource(global[gENV_SRC])) pk = sp;
                else for (uint32_t i = 0; i < n; ++i) pk = std::max(pk, std::max(std::fabs(inL[pos + i]), std::fabs(inR[pos + i])));
                pk *= std::pow(10.f, (global[gENV_SENS] - 64.f) / 64.f * 24.f / 20.f);
                envVal += (pk - envVal) * (pk > envVal ? envAtk : envRel);
            }
            applyLfos(beat, playing, (float)n / sr);
            // ---- set the effects for this segment
            ctx.stepPhase = lane[lnFilter].stepPh;
            fltLfoNow = fltLfoShape(clampi((int)global[gFLFO_WAVE], 0, kFltLfoWaves - 1), beat / kFltLfoBeats[clampi((int)global[gFLFO_RATE], 0, kFltLfoRates - 1)]);
            ctx.fltLfo = fltLfoNow;
            drive.set(lane[lnDrive].p, ctx); crush.set(lane[lnCrush].p, ctx); filt.set(lane[lnFilter].p, ctx); comp.set(lane[lnComp].p, ctx);
            chorus.set(lane[lnChorus].p, ctx); flanger.set(lane[lnFlanger].p, ctx); dly.set(lane[lnDelay].p, ctx); rev.set(lane[lnReverb].p, ctx);
            const int pm = modelOf(lane[lnPerform].p[6], kPerformModels);
            if (pm != perfModel) { perfModel = pm; lane[lnPerform].runStart = lane[lnPerform].on; }
            if (pm == 0) stut.set(lane[lnPerform].p, ctx); else if (pm == 1) rv.set(lane[lnPerform].p, ctx); else tape.set(lane[lnPerform].p, ctx);
            if (lane[lnPerform].runStart) { if (pm == 0) stut.start(now); else if (pm == 1) rv.start(now); else tape.start(now); }
            for (auto& L : lane) L.runStart = false;
            const bool compSide = sideSource(lane[lnComp].p[6]);     // no side-chain connected = nothing to duck to
            // ---- samples, through the lanes in their order
            for (uint32_t i = 0; i < n; ++i) {
                inG += (tIn - inG) * sm; outG += (tOut - outG) * sm; dwG += (tDw - dwG) * sm;
                const float dryL = inL[pos + i] * inG, dryR = inR[pos + i] * inG;
                float l = dryL, r = dryR;
                for (int oi = 0; oi < kLanes; ++oi) {
                    const int ln = bank.order[oi];
                    LaneState& L = lane[ln];
                    const float target = L.on ? 1.f : 0.f;
                    if (L.env != target) L.env = L.env < target ? std::min(target, L.env + envStep) : std::max(target, L.env - envStep);
                    if (ln == lnDelay || ln == lnReverb) {          // tails: the input goes in while the lane is on, the tail rings on
                        float el, er;
                        if (ln == lnDelay) { if (L.env <= 0.f && dly.asleep()) continue; dly.process(l * L.env, r * L.env, l, r, el, er); }
                        else { if (L.env <= 0.f && rev.asleep()) continue; rev.process(l * L.env, r * L.env, el, er); }
                        l += el * L.mix; r += er * L.mix;
                        continue;
                    }
                    if (ln == lnPerform) {                          // keeps its 8 s history even while off
                        hist.write(now + i, l, r);
                        if (L.env <= 0.f) continue;
                        float wl = l, wr = r;
                        if (pm == 0) stut.process(wl, wr, now + i, fadeSmp); else if (pm == 1) rv.process(wl, wr, now + i); else tape.process(wl, wr, now + i);
                        const float m = L.env * L.mix;
                        l += (wl - l) * m; r += (wr - r) * m;
                        continue;
                    }
                    if (L.env <= 0.f) continue;
                    float wl = l, wr = r;
                    switch (ln) {
                    case lnDrive: drive.process(wl, wr); break;
                    case lnCrush: crush.process(wl, wr); break;
                    case lnFilter: filt.process(wl, wr); break;
                    case lnComp: if (compSide) comp.process(wl, wr, scL ? scL[pos + i] : 0.f, scR ? scR[pos + i] : 0.f); else comp.process(wl, wr); break;
                    case lnChorus: chorus.process(wl, wr); break;
                    default: flanger.process(wl, wr); break;
                    }
                    const float m = L.env * L.mix;
                    l += (wl - l) * m; r += (wr - r) * m;
                }
                const float L2 = (dryL + (l - dryL) * dwG) * outG, R2 = (dryR + (r - dryR) * dwG) * outG;
                outL[pos + i] = std::fabs(L2) > 2.f ? std::tanh(L2 * 0.5f) * 2.f : L2;
                outR[pos + i] = std::fabs(R2) > 2.f ? std::tanh(R2 * 0.5f) * 2.f : R2;
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
    float lfoPh[kLfos] = {}, lfoRnd[kLfos] = {}, lfoPrev[kLfos] = {}; uint32_t lfoSeed = 0x6C8E9CF5u, condSeed = 0x1F123BB5u;
    float lfoRandom() { lfoSeed ^= lfoSeed << 13; lfoSeed ^= lfoSeed >> 17; lfoSeed ^= lfoSeed << 5; return (lfoSeed & 0xFFFFFF) / 8388608.f - 1.f; }
    float condRandom() { condSeed ^= condSeed << 13; condSeed ^= condSeed >> 17; condSeed ^= condSeed << 5; return (condSeed & 0xFFFFFF) / 16777216.f; }
    bool condPasses(int c, int64_t loop)
    {
        static const float chance[6] = {1.f, 0.9f, 0.75f, 0.5f, 0.25f, 0.1f};
        if (c <= 0) return true;
        if (c < 6) return condRandom() < chance[c];
        if (c < 15) {                        // a:b
            static const int ab[9][2] = {{1, 2}, {2, 2}, {1, 3}, {2, 3}, {3, 3}, {1, 4}, {2, 4}, {3, 4}, {4, 4}};
            const int a = ab[c - 6][0], b = ab[c - 6][1];
            return (int)(loop % b) == a - 1;
        }
        if (c == 15) return fill;
        if (c == 16) return !fill;
        if (c == 17) return loop == 0;
        return loop != 0;
    }
    void modulate(int dest, float amount)
    {
        if (dest <= 0 || dest >= kLfoDests || amount == 0.f) return;
        const int l = (dest - 1) / kLaneParams, k = (dest - 1) % kLaneParams;
        if (k < kKnobs) lane[l].p[k] = clampf(lane[l].p[k] + amount * 63.5f, 0.f, 127.f);
        else lane[l].mix = clampf(lane[l].mix + amount * 0.5f, 0.f, 1.f);
    }
    // two LFOs and the envelope follower: added to the knob (or the step's lock) of their destination, for this segment
    void applyLfos(double beat, bool playing, float segSec)
    {
        for (int i = 0; i < kLfos; ++i) {
            const int dest = (int)std::lround(global[lfoParam(i, 4)]);
            const float depth = (global[lfoParam(i, 3)] - 64.f) / 64.f;
            const int sy = bin8(global[lfoParam(i, 2)]);
            const float off = global[lfoParam(i, 5)] / 127.f;
            float ph;
            if (sy > 0 && playing) ph = (float)(beat / kFlSync[sy] - std::floor(beat / kFlSync[sy]));
            else if (sy > 0) { lfoPh[i] += (float)(segSec * 2.0 / kFlSync[sy]); ph = lfoPh[i]; }          // stopped: 120 BPM
            else { lfoPh[i] += lfoRateHz(global[lfoParam(i, 1)]) * segSec; ph = lfoPh[i]; }
            lfoPh[i] -= std::floor(lfoPh[i]);
            ph += off; ph -= std::floor(ph);
            if (ph < lfoPrev[i]) lfoRnd[i] = lfoRandom();                                               // a new cycle
            lfoPrev[i] = ph;
            float v;
            switch (lfoWave(global[lfoParam(i, 0)])) {
            case 0: v = std::sin(kTwoPi * ph); break;
            case 1: v = ph < 0.5f ? 4.f * ph - 1.f : 3.f - 4.f * ph; break;
            case 2: v = 2.f * ph - 1.f; break;
            case 3: v = 1.f - 2.f * ph; break;
            case 4: v = ph < 0.5f ? 1.f : -1.f; break;
            default: v = lfoRnd[i]; break;
            }
            lfoOut[i] = v * depth;
            modulate(dest, v * depth);
        }
        const float ed = (global[gENV_DEPTH] - 64.f) / 64.f;
        lfoOut[2] = clampf(envVal * 2.f, 0.f, 1.f) * ed;
        modulate((int)std::lround(global[gENV_DEST]), lfoOut[2]);
    }
    struct LaneState {
        bool on = false, runStart = false, shotPending = false, condOk = true; int step = -1; float stepPh = 0.f, env = 0.f, mix = 1.f;
        int64_t lastK = INT64_MIN, shotK0 = INT64_MIN;
        float p[kKnobs] = {64, 64, 64, 64, 64, 64, 0};
    };
    static constexpr uint32_t kSeg = 32;
    float sr = 48000.f;
    LaneState lane[kLanes];
    Drive drive; Crush crush; Filter filt; Comp comp; Chorus chorus; Flanger flanger; Delay dly; Reverb rev;
    History hist; Stutter stut; Reverse rv; TapeStop tape; int perfModel = 0;
    ChainPlayer chainPl;
    int64_t lastK16 = INT64_MIN, now = 0;
    int curPattern = 0; double patStartBeat = 0.0;
    uint32_t prevHold = 0;
    float envVal = 0.f, envAtk = 0.f, envRel = 0.f;
    bool wasPlaying = false, usingInternal = false, prevPlayParam = false;
    double internalBeat = 0, prevB1 = 0, freeBeat = 0;
    float inG = -1.f, outG = -1.f, dwG = -1.f;
};

} // namespace kn
