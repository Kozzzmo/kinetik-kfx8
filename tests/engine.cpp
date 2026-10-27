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
    {   // locks: stutter size locked on one step
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
        e->bank.patterns[1].on[lnGate][0] = 1;
        std::vector<float> oL(256), oR(256);
        int switchedAt = -1;
        for (int p = 0; p < frames; p += 256) {
            if (p == 256 * 10) e->global[gPATTERN] = 1.f;
            e->process(inL.data() + p, inR.data() + p, oL.data(), oR.data(), 256, Transport());
            if (switchedAt < 0 && e->outPattern == 1) switchedAt = p;
        }
        check(switchedAt >= (int)(2.0 * sr) - 512 && switchedAt <= (int)(2.0 * sr) + 512, "queued pattern starts at the next bar");
    }
    {   // MIDI hold: a lane on while a note is held, even when stopped
        std::vector<float> oL, oR;
        run([](Engine& e) { e.global[gPLAY] = 0.f; e.midiHold = 1u << lnCrush; }, oL, oR);
        check(diff(oL, inL, 4800, frames) > 0.05, "MIDI-held lane works with the transport stopped");
    }
    std::printf(fails ? "%d failures\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
