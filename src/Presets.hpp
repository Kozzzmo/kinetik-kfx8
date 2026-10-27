// Kinetik KFX-8 — factory presets (built in code)
#pragma once
#include "Bank.hpp"
#include <vector>

namespace kn {

struct PB {   // preset builder: knobs + pattern 0 (and more)
    float* v; Bank& b;
    PB(float* values, Bank& bank) : v(values), b(bank)
    {
        for (int l = 0; l < kLanes; ++l) for (int k = 0; k < kLaneParams; ++k) v[lpIndex(l, k)] = laneParamDefault(l, k);
        for (int g = 0; g < gOUT_STEP; ++g) v[kGlobalBase + g] = globalDefault(g);
        b.initDefault();
    }
    void knob(int l, int k, int val) { v[lpIndex(l, k)] = (float)val; }
    // the six main knobs, then MIX (MODEL is set with model())
    void knobs(int l, std::initializer_list<int> vals) { int k = 0; for (int x : vals) { const int idx = k < 6 ? k : kKnobs; if (x >= 0) v[lpIndex(l, idx)] = (float)x; if (++k > 6) break; } }
    void model(int l, int m, int n) { v[lpIndex(l, 6)] = (m + 0.5f) * 128.f / n; }
    void g(int gp, float val) { v[kGlobalBase + gp] = val; }
    // "x" = on, "." = off; the string length = the lane length
    void row(int p, int l, const char* s)
    {
        int st = 0;
        for (const char* c = s; *c && st < kSteps; ++c) {
            if (*c == ' ' || *c == '|') continue;
            b.patterns[p].on[l][st++] = (*c == 'x' || *c == 'X') ? 1 : 0;
        }
        b.patterns[p].len[l] = (uint8_t)std::max(1, st);
    }
    void lock(int p, int l, int step, int k, int val) { b.patterns[p].locks[l][step][k] = (uint8_t)clampi(val, 0, 127); }
    void speed(int p, int l, int sp) { b.patterns[p].speed[l] = (uint8_t)sp; }
    void always(int l) { b.always[l] = 1; }
};

static void pInit(PB&) {}
static void pWarmBus(PB& b)
{
    b.always(lnDrive); b.knobs(lnDrive, {34, 0, 100, 10, 64, 60, 110});
    b.always(lnComp);  b.knobs(lnComp, {80, 40, 40, 50, 20, 60, 127});
    b.always(lnReverb); b.knobs(lnReverb, {50, 10, 70, 40, 30, 110, 28});
}
static void pDubThrows(PB& b)
{
    b.knobs(lnDelay, {104, 90, 60, 110, 30, 20, 110});           // 1/8 dotted, long feedback, dark
    b.model(lnDelay, 1, kDelayModels);                           // tape
    b.row(0, lnDelay, "...x.......x...x");
    b.knobs(lnReverb, {90, 40, 60, 60, 40, 127, 80});
    b.row(0, lnReverb, "....x.......x...");
    b.always(lnFilter); b.knobs(lnFilter, {6, 30, 64, 64, 64, 0, 127});   // a gentle high pass (TYPE locked below)
    b.knob(lnFilter, 2, 70);
}
static void pFilterRise(PB& b)
{
    b.knobs(lnFilter, {30, 70, 0, 64, 64, 10, 127});
    b.row(0, lnFilter, "xxxxxxxxxxxxxxxx");
    for (int s = 0; s < 16; ++s) b.lock(0, lnFilter, s, 0, 30 + s * 6);
}
static void pLofi(PB& b)
{
    b.always(lnCrush); b.knobs(lnCrush, {76, 34, 20, 30, 70, 70, 127});
    b.always(lnFilter); b.knobs(lnFilter, {92, 40, 0, 64, 64, 20, 127});
    b.always(lnChorus); b.knobs(lnChorus, {20, 50, 60, 0, 60, 80, 50});
}
static void pJetFlanger(PB& b)
{
    b.always(lnFlanger); b.knobs(lnFlanger, {30, 60, 100, 30, 115, 90, 127});   // one sweep per bar
}
static void pChorusWash(PB& b)
{
    b.always(lnChorus); b.knobs(lnChorus, {36, 80, 70, 127, 127, 100, 70});
    b.always(lnReverb); b.knobs(lnReverb, {100, 60, 75, 50, 60, 127, 60}); b.model(lnReverb, 4, kReverbModels);  // shimmer
}
static void pPump(PB& b)
{
    b.always(lnComp); b.knobs(lnComp, {50, 110, 10, 30, 70, 30, 127});
    b.always(lnDrive); b.knobs(lnDrive, {50, 0, 90, 30, 64, 56, 127});
}
static void pCrushAccents(PB& b)
{
    b.knobs(lnCrush, {90, 70, 30, 60, 90, 64, 127});
    b.row(0, lnCrush, "..x...x...x...xx");
    b.lock(0, lnCrush, 15, 0, 120); b.lock(0, lnCrush, 15, 1, 110);
}
static void pDriveStabs(PB& b)
{
    b.knobs(lnDrive, {100, 64, 80, 40, 64, 56, 127});            // folded
    b.row(0, lnDrive, "....x.......x...");
    b.lock(0, lnDrive, 12, 1, 100);                              // tube on the second one
    b.knobs(lnReverb, {60, 20, 70, 50, 30, 127, 60}); b.model(lnReverb, 1, kReverbModels);    // plate
    b.row(0, lnReverb, "....x.......x...");
}
static void pSpaceEcho(PB& b)
{
    b.always(lnDelay); b.knobs(lnDelay, {104, 100, 50, 80, 70, 40, 70}); b.model(lnDelay, 2, kDelayModels);   // BBD
    b.always(lnReverb); b.knobs(lnReverb, {85, 30, 60, 50, 50, 127, 45}); b.model(lnReverb, 3, kReverbModels);   // spring
}
static void pBreakdown(PB& b)
{
    b.knobs(lnFilter, {90, 80, 0, 64, 64, 0, 127});
    b.row(0, lnFilter, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    for (int s = 0; s < 32; ++s) b.lock(0, lnFilter, s, 0, 110 - s * 3);
    b.knobs(lnDelay, {72, 80, 70, 100, 20, 0, 100});
    b.row(0, lnDelay, "..............................xx");
    b.knobs(lnReverb, {110, 50, 70, 40, 40, 127, 90}); b.model(lnReverb, 2, kReverbModels);   // hall
    b.row(0, lnReverb, "...............................x");
    b.b.patterns[0].length = 32;
}
static void pPolyFx(PB& b)
{
    b.knobs(lnFilter, {60, 90, 32, 64, 64, 0, 127});  b.row(0, lnFilter, "x..x.x...x.x.");
    b.knobs(lnFlanger, {60, 0, 90, 30, 100, 70, 127}); b.row(0, lnFlanger, "x......x.......");
    b.knobs(lnDelay, {72, 60, 70, 100, 0, 0, 90});     b.row(0, lnDelay, "...x.....x..");
    b.knobs(lnCrush, {90, 60, 0, 40, 100, 64, 127});   b.row(0, lnCrush, "..x...x..");
}
static void pWobble(PB& b)
{
    b.knobs(lnFilter, {40, 100, 0, 64, 64, 40, 127});
    b.row(0, lnFilter, "xxxxxxxxxxxxxxxx");
    static const int c[16] = {30, 80, 30, 80, 40, 100, 40, 100, 30, 80, 30, 80, 50, 110, 70, 120};
    for (int s = 0; s < 16; ++s) b.lock(0, lnFilter, s, 0, c[s]);
    b.always(lnDrive); b.knobs(lnDrive, {60, 0, 80, 20, 70, 56, 127});
}

struct PresetDef { const char* name; const char* desc; void (*build)(PB&); };
static const PresetDef kPresets[] = {
    {"Init",            "Every knob at its base, every effect in step mode, no step lit", pInit},
    {"Warm Bus",        "Drive, gentle compression and a little room, all the time",      pWarmBus},
    {"Dub Throws",      "Dotted echo and reverb throws, a light high pass",               pDubThrows},
    {"Filter Rise",     "Low pass opening across the bar",                                pFilterRise},
    {"Lo-Fi",           "Crushed, dark and slightly detuned all the time",                pLofi},
    {"Jet Flanger",     "A slow flanger sweep, one per bar",                              pJetFlanger},
    {"Chorus Wash",     "Wide four-voice chorus into a long reverb",                      pChorusWash},
    {"Pump",            "Hard compression with a fast release, driven",                   pPump},
    {"Crush Accents",   "Bit-crushed offbeats",                                           pCrushAccents},
    {"Drive Stabs",     "Folded and tube drive on the backbeats, with reverb",            pDriveStabs},
    {"Space Echo",      "Wobbly feedback echo into a reverb, all the time",               pSpaceEcho},
    {"Breakdown",       "Closing filter over two bars, echo and reverb at the end",       pBreakdown},
    {"Poly FX",         "Filter, flanger, echo and crush on 13, 15, 12 and 9 steps",      pPolyFx},
    {"Wobble Filter",   "Resonant filter locked step by step over a driven bus",          pWobble},
};
static constexpr int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

inline void buildPreset(int i, float* values, Bank& bank)
{
    PB b(values, bank);
    if (i >= 0 && i < kNumPresets) kPresets[i].build(b);
}

} // namespace kn
