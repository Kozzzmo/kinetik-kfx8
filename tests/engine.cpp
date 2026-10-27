// KFX-8 engine: each lane switches on with its steps, every effect changes the sound, no NaN / runaway level,
// dry when nothing is lit, pattern change at the end of the master length.
//   g++ -O2 -std=c++17 tests/engine.cpp -o /tmp/t && /tmp/t
#include "../src/Engine.hpp"
#include <functional>
#include <cstdio>
#include <memory>
#include <vector>
using namespace kn;

static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; }

// a drum-like test signal: decaying noise bursts + a tone, 120 BPM
static void source(std::vector<float>& L, std::vector<float>& R, int frames, float sr)
{
    L.resize(frames); R.resize(frames); Rng rng; float env = 0;
    for (int i = 0; i < frames; ++i) {
        if (i % (int)(sr / 4) == 0) env = 1.f;          // every 16th... no: every 1/4 s = 8th note at 120
        env *= 0.9995f;
        const float t = (float)i / sr;
        const float x = env * rng.next() * 0.4f + 0.2f * std::sin(kTwoPi * 220.f * t);
        L[i] = x; R[i] = x * 0.9f;
    }
}

int main()
{
    const float sr = 48000.f; const int frames = (int)(sr * 4);      // 2 bars at 120 BPM
    std::vector<float> inL, inR; source(inL, inR, frames, sr);
    auto run = [&](std::function<void(Engine&)> setup, std::vector<float>& oL, std::vector<float>& oR, uint32_t* activeSeen = nullptr) {
        std::unique_ptr<Engine> e(new Engine()); e->init(sr);
        e->global[gPLAY] = 1.f; e->global[gTEMPO] = 120.f;
        setup(*e);
        oL.assign(frames, 0); oR.assign(frames, 0);
        uint32_t seen = 0;
        for (int p = 0; p < frames; p += 256) {
            const uint32_t n = std::min(256, frames - p);
            e->process(inL.data() + p, inR.data() + p, oL.data() + p, oR.data() + p, n, Transport());
            seen |= e->outActive;
        }
        if (activeSeen) *activeSeen = seen;
    };
    auto diff = [&](const std::vector<float>& a, const std::vector<float>& b, int from, int to) {
        double d = 0, e = 0; for (int i = from; i < to; ++i) { d += (a[i] - b[i]) * (a[i] - b[i]); e += b[i] * b[i]; } return std::sqrt(d / std::max(e, 1e-12));
    };
    auto finite = [&](const std::vector<float>& a) { float pk = 0; for (float v : a) { if (!std::isfinite(v)) return 99.f; pk = std::max(pk, std::fabs(v)); } return pk; };

    std::vector<float> dL, dR;
    run([](Engine&) {}, dL, dR);
    check(diff(dL, inL, 0, frames) < 1e-4, "nothing lit: the output is the input (unity gain)");

    for (int l = 0; l < kLanes; ++l) {
        std::vector<float> oL, oR; uint32_t seen = 0;
        run([l](Engine& e) { for (int s = 4; s < 8; ++s) e.bank.patterns[0].on[l][s] = 1; }, oL, oR, &seen);
        // steps 5-8 of each bar = 0.5 .. 1.0 s and 2.5 .. 3.0 s; outside them (plus fades / tails) it is dry
        const int a = (int)(0.55 * sr), b = (int)(0.98 * sr);
        const double inside = diff(oL, inL, a, b), before = diff(oL, inL, 0, (int)(0.49 * sr));
        const float pk = finite(oL);
        char msg[160];
        std::snprintf(msg, sizeof(msg), "%-9s changes the sound on its steps (%.2f), dry before (%.5f), peak %.2f, LED", kLaneNames[l], inside, before, pk);
        check(inside > 0.05 && before < 1e-4 && pk < 4.f && ((seen >> l) & 1), msg);
    }
    {   // locks: filter cutoff locked on one step
        std::vector<float> a, b, c, d;
        run([](Engine& e) { e.bank.patterns[0].on[lnFilter][4] = 1; }, a, b);
        run([](Engine& e) { e.bank.patterns[0].on[lnFilter][4] = 1; e.bank.patterns[0].locks[lnFilter][4][0] = 10; }, c, d);
        check(diff(a, c, (int)(0.52 * sr), (int)(0.62 * sr)) > 0.05, "a locked knob changes that step");
    }
    {   // muted lane = no effect
        std::vector<float> oL, oR;
        run([](Engine& e) { e.bank.patterns[0].on[lnCrush][4] = 1; e.bank.mute[lnCrush] = 1; }, oL, oR);
        check(diff(oL, inL, 0, frames) < 1e-4, "a bypassed lane does nothing");
    }
    {   // DRY/WET 0 = dry
        std::vector<float> oL, oR;
        run([](Engine& e) { for (int s = 0; s < 16; ++s) e.bank.patterns[0].on[lnCrush][s] = 1; e.global[gDRYWET] = 0.f; }, oL, oR);
        check(diff(oL, inL, 4800, frames) < 1e-3, "DRY/WET at 0: dry");
    }
    {   // pattern change waits for the end of the master length
        std::unique_ptr<Engine> e(new Engine()); e->init(sr);
        e->global[gPLAY] = 1.f; e->global[gTEMPO] = 120.f;
        e->bank.patterns[1].on[lnCrush][0] = 1;
        std::vector<float> oL(256), oR(256);
        int switchedAt = -1;
        for (int p = 0; p < frames; p += 256) {
            if (p == 256 * 10) e->global[gPATTERN] = 1.f;
            e->process(inL.data() + p, inR.data() + p, oL.data(), oR.data(), 256, Transport());
            if (switchedAt < 0 && e->outPattern == 1) switchedAt = p;
        }
        check(switchedAt >= (int)(2.0 * sr) - 512 && switchedAt <= (int)(2.0 * sr) + 512, "queued pattern starts at the next bar");
    }
    {   // ALWAYS mode: on from the start, lit steps only bring their locks
        std::vector<float> oL, oR, aL, aR;
        run([](Engine& e) { e.bank.always[lnDrive] = 1; e.laneParam[lnDrive][0] = 90.f; }, oL, oR);
        check(diff(oL, inL, 4800, (int)(0.45 * sr)) > 0.05, "ALWAYS: the effect works without any lit step");
        run([](Engine& e) { e.bank.always[lnFilter] = 1; e.bank.patterns[0].on[lnFilter][4] = 1; e.bank.patterns[0].locks[lnFilter][4][0] = 10; }, aL, aR);
        std::vector<float> bL, bR;
        run([](Engine& e) { e.bank.always[lnFilter] = 1; }, bL, bR);
        check(diff(aL, bL, (int)(0.52 * sr), (int)(0.62 * sr)) > 0.05 && diff(aL, bL, 4800, (int)(0.45 * sr)) < 1e-4, "ALWAYS: a lit step applies its locks, only there");
    }
    {   // effect details
        std::vector<float> oL, oR, lo, hi;
        // compressor: lower peak at a high ratio and low threshold
        run([](Engine& e) { e.bank.always[lnComp] = 1; e.laneParam[lnComp][0] = 20.f; e.laneParam[lnComp][1] = 127.f; e.laneParam[lnComp][4] = 0.f; }, oL, oR);
        float pi = 0, po = 0; for (int i = 24000; i < frames; ++i) { pi = std::max(pi, std::fabs(inL[i])); po = std::max(po, std::fabs(oL[i])); }
        check(po < pi * 0.6f, "COMP pulls the peaks down");
        // reverb tail after its last step, delay repeats
        run([](Engine& e) { e.bank.patterns[0].on[lnReverb][4] = 1; e.laneParam[lnReverb][kKnobs] = 127.f; }, oL, oR);
        check(diff(oL, inL, (int)(0.7 * sr), (int)(1.4 * sr)) > 0.05, "REVERB: the tail rings after the step");
        // flanger synced: same output twice (deterministic LFO from the song position)
        run([](Engine& e) { e.bank.always[lnFlanger] = 1; e.laneParam[lnFlanger][1] = 60.f; }, lo, hi);
        run([](Engine& e) { e.bank.always[lnFlanger] = 1; e.laneParam[lnFlanger][1] = 60.f; }, oL, oR);
        check(diff(lo, oL, 0, frames) < 1e-6, "FLANGER synced to the song position");
        // every effect at extreme values in ALWAYS mode: finite and bounded
        bool fin = true;
        for (int pass = 0; pass < 2; ++pass) {
            run([pass](Engine& e) { for (int l = 0; l < kLanes; ++l) { e.bank.always[l] = 1; for (int k = 0; k < kLaneParams; ++k) e.laneParam[l][k] = pass ? 127.f : (k == kKnobs ? 127.f : 0.f); } }, oL, oR);
            if (finite(oL) > 4.f || finite(oR) > 4.f) fin = false;
        }
        check(fin, "all effects at once, extreme knobs: finite and bounded");
    }
    {   // ties: step 6 tied to step 5 keeps step 5's lock; a plain lit step 6 uses the knob
        std::vector<float> tL, tR, rL, rR, pL, pR;
        run([](Engine& e) { Pattern& P = e.bank.patterns[0]; P.on[lnFilter][4] = 1; P.tie[lnFilter][5] = 1; P.locks[lnFilter][4][0] = 10; }, tL, tR);
        run([](Engine& e) { Pattern& P = e.bank.patterns[0]; P.on[lnFilter][4] = 1; P.on[lnFilter][5] = 1; P.locks[lnFilter][4][0] = 10; P.locks[lnFilter][5][0] = 10; }, rL, rR);
        run([](Engine& e) { Pattern& P = e.bank.patterns[0]; P.on[lnFilter][4] = 1; P.on[lnFilter][5] = 1; P.locks[lnFilter][4][0] = 10; }, pL, pR);
        const int a = (int)(0.64 * sr), b = (int)(0.74 * sr);      // inside step 6
        check(diff(tL, rL, a, b) < 1e-3 && diff(pL, rL, a, b) > 0.05, "a tied step continues the first step (its locks), a plain one does not");
        check(diff(tL, inL, (int)(0.80 * sr), (int)(0.98 * sr)) < 1e-3, "after the tied run the lane is off again");
    }
    {   // delay and reverb models: each sounds different, finite
        bool okm = true; std::vector<std::vector<float>> outs;
        for (int lnI = 0; lnI < 2; ++lnI) {
            const int ln = lnI ? lnReverb : lnDelay, n = lnI ? kReverbModels : kDelayModels;
            outs.clear();
            for (int m = 0; m < n; ++m) {
                std::vector<float> oL, oR;
                run([ln, m, n](Engine& e) { e.bank.patterns[0].on[ln][0] = 1; e.laneParam[ln][6] = (m + 0.5f) * 128.f / n; e.laneParam[ln][kKnobs] = 127.f; e.laneParam[ln][1] = 90.f; }, oL, oR);
                if (finite(oL) > 4.f || finite(oR) > 4.f) okm = false;
                for (auto& prev : outs) if (diff(oL, prev, (int)(0.3 * sr), frames) < 0.02) okm = false;
                outs.push_back(oL);
            }
        }
        check(okm, "5 delay and 5 reverb models: different and bounded");
    }
    {   // LFOs
        std::vector<float> aL, aR, bL, bR, cL, cR;
        auto setup = [](float depth) { return [depth](Engine& e) { e.bank.always[lnFilter] = 1; e.laneParam[lnFilter][0] = 70.f;
            e.global[gLFO1_DEST] = (float)(1 + lpIndex(lnFilter, 0)); e.global[gLFO1_DEPTH] = depth; e.global[gLFO1_SYNC] = 70.f; }; };
        run(setup(64.f), aL, aR); run(setup(127.f), bL, bR); run(setup(127.f), cL, cR);
        check(diff(bL, aL, 4800, frames) > 0.05, "LFO 1 on FILTER CUTOFF moves the sound");
        check(diff(bL, cL, 0, frames) < 1e-6, "a synced LFO follows the song position (same result twice)");
        std::vector<float> mL, mR;
        run([](Engine& e) { e.bank.always[lnDrive] = 1; e.global[gLFO2_DEST] = (float)(1 + lpIndex(lnDrive, kKnobs)); e.global[gLFO2_DEPTH] = 0.f; e.global[gLFO2_WAVE] = 100.f; }, mL, mR);
        check(finite(mL) < 4.f, "LFO on a MIX, random wave: bounded");
    }
    {   // step conditions: 1:2 = first loop only of every two; FILL only while FILL is held
        std::vector<float> oL, oR;
        run([](Engine& e) { Pattern& P = e.bank.patterns[0]; P.on[lnCrush][0] = 1; P.cond[lnCrush][0] = 6; }, oL, oR);   // 1:2
        check(diff(oL, inL, 200, 5000) > 0.05 && diff(oL, inL, 96000 + 200, 96000 + 5000) < 1e-4, "condition 1:2: loop 1 plays, loop 2 does not");
        run([](Engine& e) { Pattern& P = e.bank.patterns[0]; P.on[lnCrush][0] = 1; P.cond[lnCrush][0] = 15; }, oL, oR);  // FILL
        check(diff(oL, inL, 0, frames) < 1e-4, "condition FILL: silent without FILL");
        run([](Engine& e) { Pattern& P = e.bank.patterns[0]; P.on[lnCrush][0] = 1; P.cond[lnCrush][0] = 15; e.fill = true; }, oL, oR);
        check(diff(oL, inL, 200, 5000) > 0.05, "condition FILL: plays with FILL");
    }
    {   // ONE-SHOT: a press plays the lane once (from the next step), then it stops
        std::unique_ptr<Engine> e(new Engine()); e->init(sr);
        e->global[gPLAY] = 1.f; e->bank.always[lnCrush] = lmOneShot;
        Pattern& P = e->bank.patterns[0]; for (int st = 0; st < 4; ++st) P.on[lnCrush][st] = 1; P.len[lnCrush] = 4;
        std::vector<float> oL(frames), oR(frames);
        for (int p = 0; p < frames; p += 256) {
            if (p == 256 * 20) e->midiHold = 1u << lnCrush;
            if (p == 256 * 22) e->midiHold = 0;
            e->process(inL.data() + p, inR.data() + p, oL.data() + p, oR.data() + p, std::min(256, frames - p), Transport());
        }
        // pressed at ~5120 = step 0 (6000/step): plays steps 1..4 = 6000 .. 30000
        check(diff(oL, inL, 0, 5000) < 1e-4 && diff(oL, inL, 7000, 29000) > 0.05 && diff(oL, inL, 31000, frames) < 1e-4, "ONE-SHOT: plays its 4 steps once after the press");
    }
    {   // processing order: drive then filter differs from filter then drive
        std::vector<float> aL, aR, bL, bR;
        auto setup = [](bool swap) { return [swap](Engine& e) {
            e.bank.always[lnDrive] = e.bank.always[lnFilter] = 1; e.laneParam[lnDrive][0] = 120.f; e.laneParam[lnFilter][0] = 40.f;
            if (swap) { int a = e.bank.position(lnDrive), b = e.bank.position(lnFilter); std::swap(e.bank.order[a], e.bank.order[b]); } }; };
        run(setup(false), aL, aR); run(setup(true), bL, bR);
        check(diff(aL, bL, 4800, frames) > 0.05, "the effect order changes the sound");
    }
    {   // envelope follower: the input level opens the filter
        std::vector<float> aL, aR, bL, bR;
        auto setup = [](float depth) { return [depth](Engine& e) { e.bank.always[lnFilter] = 1; e.laneParam[lnFilter][0] = 30.f;
            e.global[gENV_DEST] = (float)(1 + lpIndex(lnFilter, 0)); e.global[gENV_DEPTH] = depth; }; };
        run(setup(64.f), aL, aR); run(setup(127.f), bL, bR);
        check(diff(bL, aL, 4800, frames) > 0.05, "ENV follower moves its destination");
    }
    {   // side-chain: COMP SOURCE = SIDECHAIN ducks to the side-chain, ENV SOURCE follows it
        std::vector<float> scL(frames, 0.f), scR(frames, 0.f);
        for (int i = 0; i < frames; ++i) if ((i / (int)(sr / 2)) % 2 == 0) scL[i] = scR[i] = 0.8f * std::sin(kTwoPi * 60.f * i / sr);   // on 0-0.5 s, off 0.5-1 s...
        auto runSc = [&](std::function<void(Engine&)> setup, std::vector<float>& oL, bool feed) {
            std::unique_ptr<Engine> e(new Engine()); e->init(sr); e->global[gPLAY] = 1.f; setup(*e);
            oL.assign(frames, 0); std::vector<float> oR(frames);
            for (int p = 0; p < frames; p += 256) {
                const uint32_t n = std::min(256, frames - p);
                e->scL = feed ? scL.data() + p : nullptr; e->scR = feed ? scR.data() + p : nullptr;
                e->process(inL.data() + p, inR.data() + p, oL.data() + p, oR.data() + p, n, Transport());
            }
        };
        auto rmsR = [&](const std::vector<float>& x, int a, int b) { double q = 0; for (int i = a; i < b; ++i) q += x[i] * x[i]; return std::sqrt(q / (b - a)); };
        auto comp = [](Engine& e) { e.bank.always[lnComp] = 1; e.laneParam[lnComp][0] = 50.f; e.laneParam[lnComp][1] = 110.f; e.laneParam[lnComp][2] = 5.f; e.laneParam[lnComp][3] = 30.f; e.laneParam[lnComp][6] = 127.f; };
        std::vector<float> o, o2;
        runSc(comp, o, true);
        const double gOn = rmsR(o, (int)(1.1 * sr), (int)(1.45 * sr)) / rmsR(inL, (int)(1.1 * sr), (int)(1.45 * sr));
        const double gOff = rmsR(o, (int)(1.75 * sr), (int)(1.98 * sr)) / rmsR(inL, (int)(1.75 * sr), (int)(1.98 * sr));
        std::printf("     side-chain on: x%.2f  off: x%.2f\n", gOn, gOff);
        check(gOn < gOff * 0.6, "COMP SOURCE = SIDECHAIN: ducks while the side-chain plays");
        runSc(comp, o2, false);
        const double gNo = rmsR(o2, (int)(1.1 * sr), (int)(1.45 * sr)) / rmsR(inL, (int)(1.1 * sr), (int)(1.45 * sr));
        check(gNo > 0.9 && gNo < 1.1, "COMP SOURCE = SIDECHAIN with nothing connected: no gain reduction");
        std::vector<float> ea, eb;
        auto env = [](float src) { return [src](Engine& e) { e.bank.always[lnFilter] = 1; e.laneParam[lnFilter][0] = 30.f;
            e.global[gENV_DEST] = (float)(1 + lpIndex(lnFilter, 0)); e.global[gENV_DEPTH] = 127.f; e.global[gENV_SRC] = src; }; };
        runSc(env(0.f), ea, true); runSc(env(127.f), eb, true);
        check(diff(eb, ea, 4800, frames) > 0.05, "ENV SOURCE = SIDECHAIN: follows the side-chain, not the input");
    }
    {   // PERFORM models: stutter, reverse, tape stop all different, bounded
        std::vector<std::vector<float>> outs; bool ok2 = true;
        for (int m = 0; m < kPerformModels; ++m) {
            std::vector<float> oL, oR;
            run([m](Engine& e) { for (int st = 4; st < 8; ++st) e.bank.patterns[0].on[lnPerform][st] = 1; e.laneParam[lnPerform][6] = (m + 0.5f) * 128.f / kPerformModels; }, oL, oR);
            if (finite(oL) > 4.f) ok2 = false;
            for (auto& o : outs) if (diff(oL, o, (int)(0.5 * sr), (int)(1.0 * sr)) < 0.05) ok2 = false;
            outs.push_back(oL);
        }
        check(ok2, "PERFORM: stutter, reverse and tape stop each do their own thing");
    }
    {   // CHAIN: A02 x2 then A03, looped
        std::unique_ptr<Engine> e(new Engine()); e->init(sr);
        e->global[gPLAY] = 1.f; e->global[gTEMPO] = 120.f;
        e->bank.chain.parse("1 2 1:2 2:1", kPatterns);
        std::vector<float> oL(256), oR(256); std::vector<int> seq;
        for (int p = 0; p < (int)(sr * 2 * 8); p += 256) {
            const int ii = p % frames;
            e->process(inL.data() + std::min(ii, frames - 256), inR.data() + std::min(ii, frames - 256), oL.data(), oR.data(), 256, Transport());
            if (seq.empty() || seq.back() != e->outPattern) seq.push_back(e->outPattern);
        }
        // 8 bars of 16 steps (2 s each = 0.5 bar of 4/4? 16 16ths = 1 bar = 2 s): 1 1 2 1 1 2 1 1 ...
        for (int x : seq) std::printf(" %d", x); std::printf("\n");
        check(seq.size() >= 4 && seq[0] == 1 && seq[1] == 2 && seq[2] == 1 && seq[3] == 2, "CHAIN plays A02 twice, A03 once, and loops");
    }
    {   // MIDI hold: a lane on while a note is held, even when stopped
        std::vector<float> oL, oR;
        run([](Engine& e) { e.global[gPLAY] = 0.f; e.midiHold = 1u << lnCrush; }, oL, oR);
        check(diff(oL, inL, 4800, frames) > 0.05, "MIDI-held lane works with the transport stopped");
    }
    std::printf(fails ? "%d failures\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
