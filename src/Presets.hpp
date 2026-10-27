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
    void knobs(int l, std::initializer_list<int> vals) { int k = 0; for (int x : vals) { if (x >= 0) v[lpIndex(l, k)] = (float)x; if (++k >= kLaneParams) break; } }
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
};

static void pInit(PB&) {}
static void pStutterFills(PB& b)
{
    b.knobs(lnStutter, {56, 110, 64, 127, 127});
    b.row(0, lnStutter, "............xxxx");
    b.lock(0, lnStutter, 14, 0, 24); b.lock(0, lnStutter, 15, 0, 8);     // 1/32 then 1/64 at the end of the bar
    b.row(1, lnStutter, "....xx......xxxx");
    b.lock(1, lnStutter, 4, 0, 88);
}
static void pTapeStops(PB& b)
{
    b.knobs(lnTape, {70, 64, 90, 10, 127});
    b.row(0, lnTape, "..............................xx");   // 32 steps: the last 2 of every 2 bars
    b.knob(lnFilter, 0, 127);
}
static void pReverseSwells(PB& b)
{
    b.knobs(lnReverse, {56, 64, 60, 110, 127});
    b.row(0, lnReverse, "....xxxx........");
    b.knobs(lnDelay, {72, 70, 70, 100, 70});
    b.row(0, lnDelay, ".......x........");
}
static void pFilterRise(PB& b)
{
    b.knobs(lnFilter, {30, 70, 0, 64, 127});
    b.row(0, lnFilter, "xxxxxxxxxxxxxxxx");
    for (int s = 0; s < 16; ++s) b.lock(0, lnFilter, s, 0, 30 + s * 6);
}
static void pTranceGate(PB& b)
{
    b.knobs(lnGate, {32, 64, 16, 127, 127});
    b.row(0, lnGate, "xxxxxxxxxxxxxxxx");
    b.lock(0, lnGate, 12, 0, 72); b.lock(0, lnGate, 13, 0, 72); b.lock(0, lnGate, 14, 0, 104); b.lock(0, lnGate, 15, 0, 104);
}
static void pCrushAccents(PB& b)
{
    b.knobs(lnCrush, {80, 70, 40, 100, 110});
    b.row(0, lnCrush, "..x...x...x...xx");
    b.lock(0, lnCrush, 15, 1, 120);
}
static void pDubThrows(PB& b)
{
    b.knobs(lnDelay, {104, 90, 50, 110, 100});      // 1/8D, long feedback, dark, ping-pong
    b.row(0, lnDelay, "...x.......x....");
    b.knobs(lnFilter, {75, 50, 2, 64, 127});         // high pass on the throws' bar end
    b.row(0, lnFilter, "..............xx");
}
static void pPitchRisers(PB& b)
{
    b.knobs(lnPitch, {88, 64, 60, 30, 110});
    b.row(0, lnPitch, "........xx....xx");
    b.lock(0, lnPitch, 8, 0, 85); b.lock(0, lnPitch, 9, 0, 85);       // +7
    b.lock(0, lnPitch, 14, 0, 96); b.lock(0, lnPitch, 15, 0, 96);     // +12
}
static void pGlitchMachine(PB& b)
{
    b.knobs(lnStutter, {24, 100, 70, 90, 127});
    b.row(0, lnStutter, "....x..");              // 7 steps
    b.knobs(lnReverse, {24, 80, 20, 127, 127});
    b.row(0, lnReverse, "..x..");                // 5
    b.knobs(lnCrush, {90, 90, 30, 90, 100});
    b.row(0, lnCrush, "x.........x");            // 11
    b.knobs(lnGate, {72, 50, 10, 127, 127});
    b.row(0, lnGate, ".....xx.x");               // 9
    b.knobs(lnDelay, {72, 60, 80, 127, 60});
    b.row(0, lnDelay, "..........x...");         // 14
}
static void pBreakdown(PB& b)
{
    b.knobs(lnFilter, {100, 80, 0, 40, 127});
    b.row(0, lnFilter, "xxxxxxxxxxxxxxxx");
    for (int s = 0; s < 16; ++s) b.lock(0, lnFilter, s, 0, 100 - s * 4);
    b.knobs(lnDelay, {88, 80, 60, 90, 90});
    b.row(0, lnDelay, "...x...x...x..xx");
    b.knobs(lnGate, {72, 64, 30, 127, 127});
    b.row(0, lnGate, "............xxxx");
}
static void pLofiBus(PB& b)
{
    b.knobs(lnCrush, {60, 36, 20, 80, 60});
    b.row(0, lnCrush, "xxxxxxxxxxxxxxxx");
    b.knobs(lnFilter, {95, 30, 0, 64, 127});
    b.row(0, lnFilter, "xxxxxxxxxxxxxxxx");
    b.knobs(lnTape, {40, 40, 80, 60, 127});
    b.row(0, lnTape, "..............................xx");
}
static void pHalfTime(PB& b)
{
    b.knobs(lnStutter, {88, 127, 64, 127, 127});      // 1/8 repeats = half-time feel
    b.row(0, lnStutter, "xxxx....xxxx....");
    b.lock(0, lnStutter, 4, 0, 100);
    b.knobs(lnReverse, {60, 64, 40, 100, 127});
    b.row(0, lnReverse, "...............x");
}
static void pPolyFx(PB& b)
{
    b.knobs(lnFilter, {60, 90, 1, 64, 110});
    b.row(0, lnFilter, "x..x..x..x..x");          // 13
    b.knobs(lnGate, {40, 40, 20, 127, 127});
    b.row(0, lnGate, "..x..x..x..x..x");          // 15
    b.knobs(lnPitch, {76, 64, 50, 0, 90});
    b.row(0, lnPitch, "....x...x...");            // 12
    b.speed(0, lnPitch, 3);                         // 1/2x
    b.knobs(lnDelay, {72, 60, 70, 127, 70});
    b.row(0, lnDelay, "......x..");               // 9
}

struct PresetDef { const char* name; const char* desc; void (*build)(PB&); };
static const PresetDef kPresets[] = {
    {"Init",            "Every knob at its base, no step lit",                      pInit},
    {"Stutter Fills",   "Repeats at the end of the bar, getting faster",             pStutterFills},
    {"Tape Stops",      "The tape slows to a stop at the end of every 2 bars", pTapeStops},
    {"Reverse Swells",  "The second beat backwards, one echo throw",                 pReverseSwells},
    {"Filter Rise",     "Low pass opening across the bar",                           pFilterRise},
    {"Trance Gate",     "16th chop, faster at the end of the bar",                    pTranceGate},
    {"Crush Accents",   "Bit-crushed offbeats",                                      pCrushAccents},
    {"Dub Throws",      "Dotted-eighth echo throws, high pass at the bar end",       pDubThrows},
    {"Pitch Risers",    "Fifth then octave up with a short spiral",                  pPitchRisers},
    {"Glitch Machine",  "Five effects on lanes of 7, 5, 11, 9 and 14 steps",          pGlitchMachine},
    {"Breakdown",       "Closing filter, echoes and a gated end",                    pBreakdown},
    {"Lo-Fi Bus",       "Crushed and dark all the time, a tape stop every 2 bars",   pLofiBus},
    {"Half Time",       "Eighth-note repeats every other beat, a reverse lead-in",   pHalfTime},
    {"Poly FX",         "Filter, gate, pitch and echo on 13, 15, 12 and 9 steps",    pPolyFx},
};
static constexpr int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

inline void buildPreset(int i, float* values, Bank& bank)
{
    PB b(values, bank);
    if (i >= 0 && i < kNumPresets) kPresets[i].build(b);
}

} // namespace kn
