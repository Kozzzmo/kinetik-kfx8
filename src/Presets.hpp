// Kinetik KFX-8 — factory presets (built in code)
#pragma once
#include "Bank.hpp"
#include <vector>
#include <string>

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

// ---- 0.5 factory presets: 4-bar (64-step) pieces using lane modes, ties (one long gesture), conditions, per-step locks,
// lane lengths / speeds, the two LFOs, the envelope follower, the FILTER LFO, PERFORM gestures, the processing order and,
// where it helps, a second pattern in a CHAIN. Output levels are matched to the dry signal (OUTPUT trim, `trimDb` below).
struct PB2 {
    PB& p;
    explicit PB2(PB& b) : p(b) {}
    Pattern& P(int i) { return p.b.patterns[i]; }
    // "x" = on, "-" = on, tied to the previous step (one gesture), "." = off; spaces and "|" ignored; length = lane length
    void row(int i, int l, const std::string& s)
    {
        int st = 0;
        for (char c : s) {
            if (c == ' ' || c == '|') continue;
            if (st >= kSteps) break;
            P(i).on[l][st] = (c == 'x' || c == 'X' || c == '-') ? 1 : 0;
            P(i).tie[l][st] = c == '-' ? 1 : 0;
            ++st;
        }
        P(i).len[l] = (uint8_t)std::max(1, st);
    }
    void lock(int i, int l, int s, int k, int v) { p.lock(i, l, s, k, v); }
    // a value cycling over the lit (not tied) steps of a lane
    void cyc(int i, int l, int k, std::initializer_list<int> vals)
    {
        const int n = (int)vals.size(); int c = 0; const int* a = vals.begin();
        for (int s = 0; s < P(i).len[l]; ++s) if (P(i).on[l][s] && !P(i).tie[l][s]) lock(i, l, s, k, a[c++ % n]);
    }
    void cond(int i, int l, std::initializer_list<int> steps, int c) { for (int s : steps) P(i).cond[l][s] = (uint8_t)c; }
    void length(int i, int len, int swing = 50) { P(i).length = (uint8_t)len; P(i).swing = (uint8_t)swing; }
    void copy(int src, int dst) { p.b.patterns[dst] = p.b.patterns[src]; }
    void chain(const char* spec) { p.b.chain.parse(spec, kPatterns); }
    // LFO i: wave SINE TRIANGLE SAW UP SAW DOWN SQUARE RANDOM; sync FREE 4 BARS 2 BARS 1 BAR 1/2 1/4 1/8 1/16 (0 = free rate);
    // depth -63..+63; destination = lane knob (k 7 = MIX)
    void lfo(int i, int wave, int sync, int depth, int lane, int k, int rate = 50)
    {
        p.g(lfoParam(i, 0), wave * 128.f / kLfoWaves + 4); p.g(lfoParam(i, 1), (float)rate); p.g(lfoParam(i, 2), sync * 16.f + 8);
        p.g(lfoParam(i, 3), (float)clampi(64 + depth, 0, 127)); p.g(lfoParam(i, 4), (float)(1 + lpIndex(lane, k)));
    }
    void env(int sens, int atk, int rel, int depth, int lane, int k)
    {
        p.g(gENV_SENS, (float)sens); p.g(gENV_ATTACK, (float)atk); p.g(gENV_RELEASE, (float)rel);
        p.g(gENV_DEPTH, (float)clampi(64 + depth, 0, 127)); p.g(gENV_DEST, (float)(1 + lpIndex(lane, k)));
    }
    void filterLfo(int rateIdx, int wave, int depth) { p.g(gFLFO_RATE, (float)rateIdx); p.g(gFLFO_WAVE, (float)wave); p.knob(lnFilter, 6, depth); }
    void order(std::initializer_list<int> lanes) { int i = 0; for (int l : lanes) p.b.order[i++] = (uint8_t)l; }
};
enum { syFree, sy4Bars, sy2Bars, sy1Bar, syHalf, sy4, sy8, sy16 };
enum { cAlways, c90, c75, c50, c25, c10, c1of2, c2of2, c1of3, c2of3, c3of3, c1of4, c2of4, c3of4, c4of4, cFill, cNotFill, cFirst, cNotFirst };
enum { dl32, dl16T, dl16, dl8T, dl16D, dl8, dl8D, dl4 };
static int dlv(int i) { return i * 16 + 8; }      // DELAY TIME value of a division
inline std::string fxBars(std::initializer_list<const char*> bs) { std::string s; for (auto x : bs) s += x; return s; }
inline std::string fxRep(const char* bar, int n) { std::string s; for (int i = 0; i < n; ++i) s += bar; return s; }

static void pInit(PB&) {}
// Warm Bus: drive, glue compression and a little plate all the time; every 4th bar the drive pushes harder.
static void pWarmBus(PB& b)
{
    PB2 q(b);
    b.always(lnDrive); b.knobs(lnDrive, {34, 0, 100, 10, 64, 60, 110});
    b.always(lnComp);  b.knobs(lnComp, {80, 40, 40, 50, 20, 60, 127});
    b.always(lnReverb); b.knobs(lnReverb, {50, 10, 70, 40, 30, 110, 24}); b.model(lnReverb, 1, kReverbModels);
    q.row(0, lnDrive, fxBars({"................", "................", "................", "x---------------"}));
    q.lock(0, lnDrive, 48, 0, 60); q.lock(0, lnDrive, 48, 5, 54);
    q.length(0, 64);
}
// Dub Throws: dotted tape echo thrown on the off-beats (some every other loop, some by chance), reverb throws at the bar
// ends, a high pass that LFO 1 opens and closes over 4 bars.
static void pDubThrows(PB& b)
{
    PB2 q(b);
    b.knobs(lnDelay, {dlv(dl8D), 90, 60, 110, 30, 20, 110}); b.model(lnDelay, 1, kDelayModels);   // tape
    q.row(0, lnDelay, fxBars({"...x.......x....", "...x.......x...x", "...x.......x....", "...x...x...x.x.x"}));
    q.cond(0, lnDelay, {15, 55, 57}, c50); q.cond(0, lnDelay, {27}, c1of2);
    q.cyc(0, lnDelay, 2, {60, 50, 70, 44});
    b.knobs(lnReverb, {90, 40, 60, 60, 40, 127, 80}); b.model(lnReverb, 2, kReverbModels);
    q.row(0, lnReverb, fxBars({"............x...", "............x---", "............x...", "........x-------"}));
    b.always(lnFilter); b.knobs(lnFilter, {10, 30, 70, 64, 64, 0, 127});    // high pass (TYPE)
    q.lfo(0, 0, sy4Bars, 24, lnFilter, 0);
    q.length(0, 64, 54);
}
// Filter Rise: a low pass opening over 4 bars as one tied gesture (SWEEP), resonance climbing; B = open, the FILTER LFO
// moves it in eighths.
static void pFilterRise(PB& b)
{
    PB2 q(b);
    b.knobs(lnFilter, {24, 56, 0, 64, 64, 0, 127});
    q.row(0, lnFilter, "x" + std::string(63, '-'));
    q.lock(0, lnFilter, 0, 3, 127); q.lock(0, lnFilter, 0, 0, 20);
    q.lock(0, lnFilter, 0, 1, 66);
    q.length(0, 64);
    b.knobs(lnFilter, {24, 56, 0, 64, 64, 0, 127});
    q.row(1, lnFilter, fxRep("xxxxxxxxxxxxxxxx", 4));
    q.cyc(1, lnFilter, 0, {100, 100, 96, 100});
    q.filterLfo(5, 1, 70);
    q.length(1, 64);
    q.chain("1 2 0:1 1:1");
}
// Lo-Fi Tape: crushed, dark, slow random wow on the chorus, a quiet tape echo; the last bar drops the rate further.
static void pLofi(PB& b)
{
    PB2 q(b);
    b.always(lnCrush); b.knobs(lnCrush, {76, 34, 20, 30, 70, 70, 127});
    b.always(lnFilter); b.knobs(lnFilter, {88, 40, 0, 64, 64, 20, 127});
    b.always(lnChorus); b.knobs(lnChorus, {20, 50, 60, 0, 60, 80, 50});
    b.always(lnDelay); b.knobs(lnDelay, {dlv(dl8), 40, 40, 60, 60, 0, 30}); b.model(lnDelay, 1, kDelayModels);
    q.row(0, lnCrush, fxBars({"................", "................", "................", "........x-------"}));
    q.lock(0, lnCrush, 56, 1, 14); q.lock(0, lnCrush, 56, 0, 56);
    q.lfo(0, 5, syFree, 30, lnChorus, 1, 30);
    q.length(0, 64);
}
// Jet Flanger: a flanger in bars 2 and 4 (one sweep per bar), a REVERSE gesture into bar 1.
static void pJetFlanger(PB& b)
{
    PB2 q(b);
    b.knobs(lnFlanger, {30, 56, 100, 30, 115, 90, 127});   // synced 1 bar
    q.row(0, lnFlanger, fxBars({"................", "x---------------", "................", "x---------------"}));
    q.lock(0, lnFlanger, 48, 4, 122);
    b.knobs(lnPerform, {56, 127, 64, 127, 0, 0, 127}); b.model(lnPerform, 1, kPerformModels);   // reverse
    q.row(0, lnPerform, fxBars({"................", "................", "................", "............x---"}));
    q.length(0, 64);
}
// Chorus Wash: wide four-voice chorus into a long shimmer; LFO 2 breathes the reverb MIX over 2 bars.
static void pChorusWash(PB& b)
{
    PB2 q(b);
    b.always(lnChorus); b.knobs(lnChorus, {36, 80, 70, 127, 127, 100, 70});
    b.always(lnReverb); b.knobs(lnReverb, {86, 60, 75, 50, 60, 127, 50}); b.model(lnReverb, 4, kReverbModels);
    q.lfo(1, 0, sy2Bars, 20, lnReverb, 7);
    q.length(0, 64);
}
// Pump: hard compression with a fast release, driven; the envelope follower opens a low pass with the level.
static void pPump(PB& b)
{
    PB2 q(b);
    b.always(lnComp); b.knobs(lnComp, {50, 110, 10, 30, 70, 30, 127});
    b.always(lnDrive); b.knobs(lnDrive, {50, 0, 90, 30, 64, 56, 127});
    b.always(lnFilter); b.knobs(lnFilter, {70, 50, 0, 64, 64, 0, 127});
    q.env(80, 10, 40, 40, lnFilter, 0);
    q.length(0, 64);
}
// Crush Accents: bit-crushed off-beats, the bits cycling, a full crushed bar on FILL.
static void pCrushAccents(PB& b)
{
    PB2 q(b);
    b.knobs(lnCrush, {90, 70, 30, 60, 90, 64, 127});
    q.row(0, lnCrush, fxBars({"..x...x...x...x.", "..x...x...x...xx", "..x...x...x...x.", "..x...x.x.x.xxxx"}));
    q.cyc(0, lnCrush, 0, {90, 70, 100, 60, 110});
    q.cyc(0, lnCrush, 1, {70, 80, 60, 100});
    q.cond(0, lnCrush, {15, 56, 60, 61, 62, 63}, c50);
    q.length(0, 64);
}
// Drive Stabs: folded and tube drive on the backbeats, plate throws; DRIVE goes after the reverb (order).
static void pDriveStabs(PB& b)
{
    PB2 q(b);
    b.knobs(lnDrive, {100, 64, 80, 40, 64, 56, 127});
    q.row(0, lnDrive, fxRep("....x.......x...", 4));
    q.cyc(0, lnDrive, 1, {64, 100, 64, 100, 64, 100, 64, 127});
    b.knobs(lnReverb, {60, 20, 70, 50, 30, 127, 60}); b.model(lnReverb, 1, kReverbModels);
    q.row(0, lnReverb, fxRep("....x.......x...", 4));
    q.cond(0, lnReverb, {4, 20, 36}, c1of2);
    q.length(0, 64);
}
// Space Echo: BBD echo into a spring all the time; LFO 1 moves the echo time slowly (random) for a worn-machine wobble.
static void pSpaceEcho(PB& b)
{
    PB2 q(b);
    b.always(lnDelay); b.knobs(lnDelay, {dlv(dl8D), 100, 50, 80, 70, 40, 70}); b.model(lnDelay, 2, kDelayModels);
    b.always(lnReverb); b.knobs(lnReverb, {85, 30, 60, 50, 50, 127, 45}); b.model(lnReverb, 3, kReverbModels);
    q.row(0, lnDelay, fxBars({"................", "................", "................", "............x---"}));
    q.lock(0, lnDelay, 60, 1, 118);
    q.lfo(0, 5, sy1Bar, 6, lnDelay, 4);
    q.length(0, 64);
}
// Stutter Fills: STUTTER gestures at the end of bars 2 and 4 (smaller and smaller), a TAPE STOP on FILL.
static void pStutterFills(PB& b)
{
    PB2 q(b);
    b.knobs(lnPerform, {56, 127, 64, 127, 0, 0, 127}); b.model(lnPerform, 0, kPerformModels);   // stutter
    q.row(0, lnPerform, fxBars({"................", "............x-x-", "................", "........x---x-x-"}));
    q.lock(0, lnPerform, 28, 0, 64); q.lock(0, lnPerform, 30, 0, 40); q.lock(0, lnPerform, 56, 0, 70); q.lock(0, lnPerform, 60, 0, 40); q.lock(0, lnPerform, 62, 0, 20);
    q.length(0, 64);
    q.copy(0, 1);
    b.model(lnPerform, 0, kPerformModels);
    q.lock(1, lnPerform, 56, 6, 110);   // tape stop on the last gesture of B
    q.lock(1, lnPerform, 60, 6, 110); q.lock(1, lnPerform, 62, 6, 110);
    b.knobs(lnFilter, {60, 60, 2 * 32 + 16, 64, 64, 0, 127});
    q.row(1, lnFilter, fxBars({"................", "................", "................", "x---------------"}));
    q.lock(1, lnFilter, 48, 3, 110);
    q.chain("1 2 0:3 1:1");
}
// Reverse Swells: REVERSE gestures before every second bar into a hall, the reverb tied over the downbeat.
static void pReverseSwells(PB& b)
{
    PB2 q(b);
    b.knobs(lnPerform, {64, 127, 64, 127, 0, 0, 127}); b.model(lnPerform, 1, kPerformModels);
    q.row(0, lnPerform, fxBars({"................", "........x-------", "................", "........x-------"}));
    b.knobs(lnReverb, {110, 30, 70, 40, 50, 127, 90}); b.model(lnReverb, 2, kReverbModels);
    q.row(0, lnReverb, fxBars({"x---............", "............x---", "x---............", "............x---"}));
    q.length(0, 64);
}
// Breakdown: the low pass closes over 4 bars, echo throws in bar 4, a hall on the last beat, a tape stop at the very end.
static void pBreakdown(PB& b)
{
    PB2 q(b);
    b.knobs(lnFilter, {110, 80, 0, 64, 64, 0, 127});
    q.row(0, lnFilter, "x" + std::string(63, '-'));
    q.lock(0, lnFilter, 0, 3, 0);         // SWEEP down across the whole run
    b.knobs(lnDelay, {dlv(dl16D), 80, 70, 100, 20, 0, 100});
    q.row(0, lnDelay, fxBars({"................", "................", "................", "....x.......x.x."}));
    b.knobs(lnReverb, {110, 50, 70, 40, 40, 127, 90}); b.model(lnReverb, 2, kReverbModels);
    q.row(0, lnReverb, fxBars({"................", "................", "................", "............x---"}));
    b.knobs(lnPerform, {56, 127, 64, 127, 0, 0, 127}); b.model(lnPerform, 2, kPerformModels);
    q.row(0, lnPerform, fxBars({"................", "................", "................", "..............x-"}));
    q.length(0, 64);
}
// Poly FX: filter, flanger, echo, crush and chorus on 13, 15, 12, 9 and 7 steps; the echo lane runs at 3/2x.
static void pPolyFx(PB& b)
{
    PB2 q(b);
    b.knobs(lnFilter, {60, 90, 32, 64, 64, 0, 127});  q.row(0, lnFilter, "x..x.x...x.x.");
    q.cyc(0, lnFilter, 0, {60, 80, 40, 100, 70});
    b.knobs(lnFlanger, {60, 0, 90, 30, 100, 70, 127}); q.row(0, lnFlanger, "x-.....x-......");
    b.knobs(lnDelay, {dlv(dl16D), 60, 70, 100, 0, 0, 90}); q.row(0, lnDelay, "...x.....x.."); b.speed(0, lnDelay, 6);
    b.knobs(lnCrush, {90, 60, 0, 40, 100, 64, 127});   q.row(0, lnCrush, "..x...x..");
    b.knobs(lnChorus, {60, 90, 50, 64, 127, 100, 90}); q.row(0, lnChorus, "x--....");
    q.cond(0, lnCrush, {6}, c50);
    q.length(0, 64);
}
// Wobble Filter: the FILTER LFO in eighths, the rate locked to sixteenths in the last bar, LFO 1 on the resonance,
// over a driven bus.
static void pWobble(PB& b)
{
    PB2 q(b);
    b.always(lnFilter); b.knobs(lnFilter, {50, 72, 0, 64, 64, 10, 127});
    q.filterLfo(5, 1, 90);
    q.row(0, lnFilter, fxBars({"................", "................", "................", "x---x---x-x-x-x-"}));
    q.cyc(0, lnFilter, 6, {110, 110, 127, 127, 127, 127});
    q.lfo(0, 1, sy2Bars, 20, lnFilter, 1);
    b.always(lnDrive); b.knobs(lnDrive, {60, 0, 80, 20, 70, 56, 127});
    q.length(0, 64);
}
// Envelope Wah: a band pass that follows the input level (auto-wah), a little drive and chorus.
static void pEnvWah(PB& b)
{
    PB2 q(b);
    b.always(lnFilter); b.knobs(lnFilter, {40, 90, 32, 64, 64, 20, 110});
    q.env(90, 5, 30, 50, lnFilter, 0);
    b.always(lnDrive); b.knobs(lnDrive, {30, 3 * 32 + 16, 90, 20, 64, 62, 100});
    b.always(lnChorus); b.knobs(lnChorus, {40, 50, 50, 32, 100, 110, 40});
    q.length(0, 64);
}
// Glitch Chain: crush, stutter, reverse and flanger flicker by chance on short steps; B is denser; A A A B.
static void pGlitchChain(PB& b)
{
    PB2 q(b);
    b.knobs(lnCrush, {100, 80, 20, 50, 90, 64, 127});
    b.knobs(lnPerform, {40, 127, 64, 127, 0, 0, 127}); b.model(lnPerform, 0, kPerformModels);
    b.knobs(lnFlanger, {80, 0, 100, 20, 110, 90, 127});
    b.knobs(lnDelay, {dlv(dl16), 70, 80, 120, 0, 0, 80});
    q.row(0, lnCrush, fxRep("..x.....x.....x.", 4)); q.cond(0, lnCrush, {2, 8, 14, 18, 24, 30, 34, 40, 46, 50, 56, 62}, c50);
    q.row(0, lnPerform, fxBars({"................", "..........x-....", "................", "......x-....x-x-"}));
    q.row(0, lnFlanger, fxRep(".....x-.........", 4)); q.cond(0, lnFlanger, {5, 21, 37, 53}, c25);
    q.row(0, lnDelay, fxRep(".......x.......x", 4)); q.cond(0, lnDelay, {7, 23, 39, 55}, c25);
    q.length(0, 64);
    q.copy(0, 1);
    q.row(1, lnCrush, fxRep("x.x...x.x.x...xx", 4));
    q.row(1, lnPerform, fxBars({"......x-......x-", "..........x-x-..", "......x-......x-", "......x-x-x-x-x-"}));
    q.cyc(1, lnPerform, 0, {50, 40, 30, 20, 60});
    q.chain("1 2 0:3 1:1");
}

// trimDb: OUTPUT trim so the preset plays about as loud as the dry signal (measured with a drum-like test loop)
struct PresetDef { const char* name; const char* desc; void (*build)(PB&); float trimDb = 0.f; };
static const PresetDef kPresets[] = {
    {"Init",            "Every knob at its base, every effect in step mode, no step lit", pInit},
    {"Warm Bus",        "Drive, glue compression and a little plate; bar 4 pushes harder", pWarmBus, 1.0f},
    {"Dub Throws",      "Dotted tape echo and hall throws by chance, a moving high pass",  pDubThrows},
    {"Filter Rise",     "A low pass opening over 4 bars, then an eighth-note LFO (CHAIN)", pFilterRise},
    {"Lo-Fi Tape",      "Crushed, dark, random wow and a quiet tape echo",                 pLofi, -7.0f},
    {"Jet Flanger",     "A flanger sweep in bars 2 and 4, a reverse gesture into bar 1",   pJetFlanger},
    {"Chorus Wash",     "Wide four-voice chorus into a breathing shimmer reverb",          pChorusWash, 3.0f},
    {"Pump",            "Hard compression, drive, a low pass that follows the level",      pPump, 5.1f},
    {"Crush Accents",   "Bit-crushed off-beats, bits cycling, a crushed fill",             pCrushAccents, -2.4f},
    {"Drive Stabs",     "Folded and tube drive on the backbeats, plate throws",            pDriveStabs, -0.8f},
    {"Space Echo",      "Wobbly BBD echo into a spring, a long throw in bar 4",            pSpaceEcho},
    {"Stutter Fills",   "Stutter gestures at the bar ends; B ends with a tape stop",       pStutterFills, -1.5f},
    {"Reverse Swells",  "Reverse gestures into the downbeats, hall tails",                 pReverseSwells},
    {"Breakdown",       "Low pass closing over 4 bars, echo and hall at the end, tape stop", pBreakdown},
    {"Poly FX",         "Filter, flanger, echo, crush, chorus on 13, 15, 12, 9, 7 steps",  pPolyFx, 1.1f},
    {"Wobble Filter",   "FILTER LFO in eighths, sixteenths in the last bar, driven",       pWobble, -10.6f},
    {"Envelope Wah",    "A band pass following the input level, drive and chorus",         pEnvWah, 6.8f},
    {"Glitch Chain",    "Crush, stutter, flanger and echo flicker by chance; A A A B",     pGlitchChain, -2.0f},
};
static constexpr int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

inline void buildPreset(int i, float* values, Bank& bank)
{
    PB b(values, bank);
    if (i >= 0 && i < kNumPresets) {
        kPresets[i].build(b);
        if (kPresets[i].trimDb != 0.f) {   // OUTPUT: 0.5 dB per step below 96, 0.26 above
            const float d = kPresets[i].trimDb;
            values[kGlobalBase + gOUTPUT] = clampf(std::round(96.f + (d < 0 ? d / 0.5f : d / 0.26f)), 1.f, 127.f);
        }
    }
}

} // namespace kn
