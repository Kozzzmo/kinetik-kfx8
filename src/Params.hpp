// Kinetik KFX-8 — constants, parameters and value texts
#pragma once
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include "Dsp.hpp"

namespace kn {

static constexpr int kLanes = 9;          // one effect per lane (processing order: Bank::order)
static constexpr int kSteps = 64;         // steps per lane
static constexpr int kPatterns = 16;
static constexpr int kKnobs = 7;          // effect knobs per lane (lockable per step); the 7th is MODEL on DELAY / REVERB, unused elsewhere
static constexpr int kLaneParams = 8;     // + MIX
static constexpr uint8_t kNoLock = 255;
static constexpr int kChainMax = 16;      // entries of the pattern chain

// processing order: tone shaping first, then dynamics, modulation, and the tails last
enum Lane { lnDrive, lnCrush, lnFilter, lnComp, lnChorus, lnFlanger, lnDelay, lnReverb, lnPerform };
static const char* const kLaneNames[kLanes] = {"DRIVE", "CRUSH", "FILTER", "COMP", "CHORUS", "FLANGER", "DELAY", "REVERB", "PERFORM"};
static const int kDefaultOrder[kLanes] = {lnDrive, lnCrush, lnFilter, lnComp, lnChorus, lnFlanger, lnPerform, lnDelay, lnReverb};

struct LaneDef { const char* knob[kKnobs]; uint8_t def[kLaneParams]; const char* help; };
static const LaneDef kLaneDefs[kLanes] = {
    {{"AMOUNT", "TYPE", "TONE", "LOW CUT", "BIAS", "LEVEL", nullptr},      {50, 0, 90, 0, 64, 64, 0, 127},   "Saturation: soft, hard, folded or tube-like"},
    {{"BITS", "RATE", "NOISE", "DRIVE", "TONE", "LEVEL", nullptr},         {70, 40, 0, 30, 110, 64, 0, 127}, "Bit and sample-rate reduction"},
    {{"CUTOFF", "RESO", "TYPE", "SWEEP", "ENV", "DRIVE", nullptr},         {50, 60, 0, 64, 64, 0, 0, 127},   "Resonant filter; SWEEP moves it across each step, ENV follows the level"},
    {{"THRESH", "RATIO", "ATTACK", "RELEASE", "MAKEUP", "KNEE", nullptr},  {70, 60, 30, 50, 0, 40, 0, 127},  "Compressor (MIX below 100% = parallel compression)"},
    {{"RATE", "DEPTH", "DELAY", "VOICES", "WIDTH", "TONE", nullptr},       {40, 60, 50, 32, 100, 110, 0, 64},"Chorus: one to four modulated voices"},
    {{"RATE", "SYNC", "DEPTH", "MANUAL", "FEEDBACK", "WIDTH", nullptr},    {30, 0, 80, 40, 90, 80, 0, 127},  "Flanger: a very short modulated delay with feedback"},
    {{"TIME", "FEEDBACK", "TONE", "WIDTH", "MOD", "DUCK", "MODEL"},        {88, 64, 80, 64, 0, 0, 0, 90},    "Echo in tempo: digital, tape, BBD, diffuse or multi-tap"},
    {{"DECAY", "PREDELAY", "TONE", "LOW CUT", "MOD", "WIDTH", "MODEL"},    {70, 20, 80, 20, 30, 127, 32, 70},"Reverb: room, plate, hall, spring or shimmer"},
    {{"SIZE", "DECAY", "PITCH", "GATE", nullptr, nullptr, "MODEL"},       {56, 127, 64, 127, 0, 0, 0, 127}, "Stutter, reverse or tape stop: lit steps next to each other make one gesture"},
};

static const double kDelTime[8] = {0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 0.75, 1.0};
static const char* const kDelTimeNames[8] = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/8D", "1/4"};
static const double kFlSync[8] = {0.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25};     // LFO period in quarter notes (0 = free RATE)
static const char* const kFlSyncNames[8] = {"FREE", "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/8", "1/16"};
static constexpr int kDelayModels = 5, kReverbModels = 5;
static const char* const kDelayModelNames[kDelayModels] = {"DIGITAL", "TAPE", "BBD", "DIFFUSE", "MULTI-TAP"};
static const char* const kReverbModelNames[kReverbModels] = {"ROOM", "PLATE", "HALL", "SPRING", "SHIMMER"};
inline int modelOf(float v, int n) { const int m = (int)(v / 128.f * n); return m < 0 ? 0 : (m >= n ? n - 1 : m); }
static constexpr int kPerformModels = 3;
static const char* const kPerformModelNames[kPerformModels] = {"STUTTER", "REVERSE", "TAPE STOP"};
static const char* const kPerformKnobs[kPerformModels][4] = {{"SIZE", "DECAY", "PITCH", "GATE"}, {"LENGTH", "SPEED", "FADE", "TONE"}, {"TIME", "CURVE", "TONE", "WOBBLE"}};
static const double kStutSize[8]  = {0.0625, 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 1.0, 2.0};
static const char* const kStutSizeNames[8] = {"1/64", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4", "1/2"};
static const double kRevLen[8]    = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0};
static const char* const kRevLenNames[8] = {"1/16", "1/8", "1/8D", "1/4", "1/4D", "1/2", "1/2D", "1 BAR"};
// step conditions (as the drum machine): chance, a:b = plays on loop a of every b, FILL, first loop
static constexpr int kConds = 19;
static const char* const kCondNames[kConds] = {"ALWAYS", "90%", "75%", "50%", "25%", "10%", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "FILL", "NOT FILL", "1ST", "NOT 1ST"};
// lane modes
enum LaneMode { lmSteps = 0, lmAlways = 1, lmOneShot = 2 };
static const char* const kModeNames[3] = {"STEPS", "ALWAYS", "1-SHOT"};
static const char* const kDriveTypes[4] = {"SOFT", "HARD", "FOLD", "TUBE"};
static const char* const kFilterTypes[4] = {"LOW PASS", "BAND PASS", "HIGH PASS", "NOTCH"};
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int bin8(float v) { int b = (int)(v / 16.f); return b < 0 ? 0 : (b > 7 ? 7 : b); }
inline int bin4(float v) { int b = (int)(v / 32.f); return b < 0 ? 0 : (b > 3 ? 3 : b); }

// lane speed (steps per 16th): index stored in the pattern
static constexpr int kSpeeds = 8;
static const double kSpeedMult[kSpeeds] = {1.0, 2.0, 4.0, 0.5, 0.25, 0.75, 1.5, 3.0};
static const char* const kSpeedNames[kSpeeds] = {"1x", "2x", "4x", "1/2x", "1/4x", "3/4x", "3/2x", "3x"};

// ---- host parameters: lanes first (lane l = indices l*7 .. l*7+6), then the globals
// Saved host parameters never move: new ones go just before gOUT_STEP. (0.2.0 replaced the whole effect set.)
static constexpr int kGlobalBase = kLanes * kLaneParams;
enum Global {
    gDRYWET = 0, gINPUT, gOUTPUT, gTEMPO, gPLAY, gPATTERN,
    gLFO1_WAVE, gLFO1_RATE, gLFO1_SYNC, gLFO1_DEPTH, gLFO1_DEST, gLFO1_PHASE,
    gLFO2_WAVE, gLFO2_RATE, gLFO2_SYNC, gLFO2_DEPTH, gLFO2_DEST, gLFO2_PHASE,
    gENV_SENS, gENV_ATTACK, gENV_RELEASE, gENV_DEPTH, gENV_DEST,            // envelope follower (the input level as a modulation)
    gOUT_STEP, gOUT_ACTIVE, gOUT_PATTERN, kGlobalParams
};
static constexpr int kLfos = 2, kLfoParams = 6, kMods = 3;   // mod source 2 = the envelope follower (5 knobs)
static const char* const kEnvKnobs[5] = {"SENS", "ATTACK", "RELEASE", "DEPTH", "DEST"};
inline int lfoParam(int i, int k) { return gLFO1_WAVE + i * kLfoParams + k; }
static const char* const kLfoKnobs[kLfoParams] = {"WAVE", "RATE", "SYNC", "DEPTH", "DEST", "PHASE"};
static constexpr int kLfoWaves = 6;
static const char* const kLfoWaveNames[kLfoWaves] = {"SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE", "RANDOM"};
inline int lfoWave(float v) { return clampi((int)(v / 128.f * kLfoWaves), 0, kLfoWaves - 1); }
inline float lfoRateHz(float v) { return 0.02f * std::pow(1000.f, v / 127.f); }      // 0.02 .. 20 Hz
// DEST: 0 = none, 1 + lpIndex(lane, knob) = that lane knob (knob 6 = MIX)
static constexpr int kLfoDests = kLanes * kLaneParams + 1;
static constexpr int kTotalParams = kGlobalBase + kGlobalParams;
inline int lpIndex(int lane, int k) { return lane * kLaneParams + k; }
inline float laneParamDefault(int lane, int k) { return kLaneDefs[lane].def[k]; }
inline float globalDefault(int g)
{
    switch (g) {
    case gDRYWET: return 127.f; case gINPUT: return 96.f; case gOUTPUT: return 96.f;
    case gTEMPO: return 120.f; case gOUT_STEP: return -1.f;
    case gLFO1_RATE: case gLFO2_RATE: return 50.f;
    case gLFO1_DEPTH: case gLFO2_DEPTH: case gENV_DEPTH: return 64.f;
    case gENV_SENS: return 64.f; case gENV_ATTACK: return 20.f; case gENV_RELEASE: return 60.f;
    default: return 0.f;
    }
}
// INPUT / OUTPUT knob -> gain (96 = 0 dB, 0 = -inf, 127 = +8 dB)
inline float trimDb(float v) { return v <= 0.5f ? -120.f : (v - 96.f) * (v < 96.f ? 0.5f : 0.26f); }
inline float trimGain(float v) { return v <= 0.5f ? 0.f : std::pow(10.f, trimDb(v) / 20.f); }

// ---- mappings (shared by the effects and the UI)
inline float levelDb(float v) { return (v - 64.f) / 64.f * 12.f; }                             // -12 .. +12 dB
inline float hzExp(float v, float lo, float hi) { return lo * std::pow(hi / lo, clampf(v / 127.f, 0.f, 1.f)); }
inline float compThreshDb(float v) { return -48.f + v / 127.f * 48.f; }
inline float compRatio(float v) { return 1.f + 19.f * std::pow(v / 127.f, 2.f); }
inline float compAttackMs(float v) { return 0.1f * std::pow(1000.f, v / 127.f); }
inline float compReleaseMs(float v) { return 10.f * std::pow(150.f, v / 127.f); }
inline float rateHz(float v, float lo, float hi) { return hzExp(v, lo, hi); }
inline float revDecaySec(float v) { return 0.2f * std::pow(100.f, v / 127.f); }               // 0.2 .. 20 s

inline std::string fmtHz(float hz) { char b[24]; if (hz >= 1000.f) std::snprintf(b, sizeof(b), "%.1fK", hz / 1000.f); else std::snprintf(b, sizeof(b), "%d HZ", (int)hz); return b; }
inline std::string fmtMs(float ms) { char b[24]; if (ms >= 1000.f) std::snprintf(b, sizeof(b), "%.1f S", ms / 1000.f); else if (ms < 10.f) std::snprintf(b, sizeof(b), "%.1f MS", ms); else std::snprintf(b, sizeof(b), "%d MS", (int)std::lround(ms)); return b; }
inline std::string fmtPct(float v) { char b[16]; std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(v / 127.f * 100.f)); return b; }
inline std::string fmtDb(float db) { char b[16]; std::snprintf(b, sizeof(b), "%+.1fdB", db); return b; }
inline std::string fmtBip(float v) { char b[16]; const int p = (int)std::lround((v - 64.f) / 64.f * 100.f); std::snprintf(b, sizeof(b), p ? "%+d%%" : "0", p); return b; }

// readable value of lane knob k (0..127)
inline std::string laneValueText(int lane, int k, float v)
{
    char b[32];
    if (k == kKnobs) return fmtPct(v);
    switch (lane) {
    case lnDrive:
        if (k == 0) return fmtPct(v);
        if (k == 1) return kDriveTypes[bin4(v)];
        if (k == 2) return fmtHz(hzExp(v, 1000.f, 20000.f));
        if (k == 3) return v < 0.5f ? "OFF" : fmtHz(hzExp(v, 20.f, 800.f));
        if (k == 4) return fmtBip(v);
        return fmtDb(levelDb(v));
    case lnCrush:
        if (k == 0) { std::snprintf(b, sizeof(b), "%d BIT", (int)std::lround(16.f - v / 127.f * 15.f)); return b; }
        if (k == 1) { std::snprintf(b, sizeof(b), "1/%d", (int)std::lround(std::pow(64.f, v / 127.f))); return b; }
        if (k == 4) return fmtHz(hzExp(v, 500.f, 20000.f));
        if (k == 5) return fmtDb(levelDb(v));
        return fmtPct(v);
    case lnFilter:
        if (k == 0) return fmtHz(hzExp(v, 20.f, 20000.f));
        if (k == 2) return kFilterTypes[bin4(v)];
        if (k == 3 || k == 4) { std::snprintf(b, sizeof(b), "%+.1f OCT", (v - 64.f) / 64.f * 4.f); return b; }
        return fmtPct(v);
    case lnComp:
        if (k == 0) { std::snprintf(b, sizeof(b), "%.1f DB", compThreshDb(v)); return b; }
        if (k == 1) { std::snprintf(b, sizeof(b), "%.1f:1", compRatio(v)); return b; }
        if (k == 2) return fmtMs(compAttackMs(v));
        if (k == 3) return fmtMs(compReleaseMs(v));
        if (k == 4) return fmtDb(v / 127.f * 24.f);
        std::snprintf(b, sizeof(b), "%.1f DB", v / 127.f * 12.f); return b;
    case lnChorus:
        if (k == 0) { std::snprintf(b, sizeof(b), "%.2f HZ", rateHz(v, 0.05f, 5.f)); return b; }
        if (k == 2) return fmtMs(5.f + v / 127.f * 25.f);
        if (k == 3) { std::snprintf(b, sizeof(b), "%d", bin4(v) + 1); return b; }
        if (k == 5) return fmtHz(hzExp(v, 1000.f, 20000.f));
        return fmtPct(v);
    case lnFlanger:
        if (k == 0) { std::snprintf(b, sizeof(b), "%.2f HZ", rateHz(v, 0.02f, 10.f)); return b; }
        if (k == 1) return kFlSyncNames[bin8(v)];
        if (k == 3) return fmtMs(0.1f + v / 127.f * 9.9f);
        if (k == 4) return fmtBip(v);
        return fmtPct(v);
    case lnDelay:
        if (k == 0) return kDelTimeNames[bin8(v)];
        if (k == 1) { std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(v / 127.f * 95.f)); return b; }
        if (k == 2) return fmtHz(hzExp(v, 800.f, 18000.f));
        if (k == 6) return kDelayModelNames[modelOf(v, kDelayModels)];
        return fmtPct(v);
    case lnPerform:
        return std::string();     // see performValueText (depends on the model)
    case lnReverb:
        if (k == 0) { std::snprintf(b, sizeof(b), "%.1f S", revDecaySec(v)); return b; }
        if (k == 1) return fmtMs(v / 127.f * 200.f);
        if (k == 2) return fmtHz(hzExp(v, 1500.f, 16000.f));
        if (k == 3) return v < 0.5f ? "OFF" : fmtHz(hzExp(v, 20.f, 1000.f));
        if (k == 6) return kReverbModelNames[modelOf(v, kReverbModels)];
        return fmtPct(v);
    }
    std::snprintf(b, sizeof(b), "%d", (int)std::lround(v)); return b;
}

inline std::string lfoDestText(int d)
{
    if (d <= 0 || d >= kLfoDests) return "NONE";
    const int i = d - 1, l = i / kLaneParams, k = i % kLaneParams;
    if (k < kKnobs && !kLaneDefs[l].knob[k]) return "NONE";
    return std::string(kLaneNames[l]) + " " + (k < kKnobs ? kLaneDefs[l].knob[k] : "MIX");
}
inline std::string lfoValueText(int k, float v)
{
    char b[32];
    switch (k) {
    case 0: return kLfoWaveNames[lfoWave(v)];
    case 1: std::snprintf(b, sizeof(b), "%.2f HZ", lfoRateHz(v)); return b;
    case 2: return kFlSyncNames[bin8(v)];
    case 3: return fmtBip(v);
    case 4: return lfoDestText((int)std::lround(v));
    default: std::snprintf(b, sizeof(b), "%d DEG", (int)std::lround(v / 127.f * 360.f)); return b;
    }
}


// PERFORM lane: knob names and values depend on its model (knob 6)
inline const char* laneKnobName(int l, int k, float modelVal)
{
    if (k >= kKnobs) return "MIX";
    if (l == lnPerform && k < 4) return kPerformKnobs[modelOf(modelVal, kPerformModels)][k];
    return kLaneDefs[l].knob[k];
}
inline std::string performValueText(int k, float v, int model)
{
    char b[32];
    if (k == 6) return kPerformModelNames[model];
    if (k == kKnobs) return fmtPct(v);
    switch (model) {
    case 0:
        if (k == 0) return kStutSizeNames[bin8(v)];
        if (k == 1) { std::snprintf(b, sizeof(b), v >= 126.5f ? "NONE" : "%d%%", (int)std::lround((1.f - v / 127.f) * 100.f)); return b; }
        if (k == 2) { std::snprintf(b, sizeof(b), "%+d ST", (int)std::lround((v - 64.f) / 64.f * 12.f)); return b; }
        std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(10.f + v / 127.f * 90.f)); return b;
    case 1:
        if (k == 0) return kRevLenNames[bin8(v)];
        if (k == 1) { std::snprintf(b, sizeof(b), "%.2fx", std::pow(2.f, (v - 64.f) / 64.f)); return b; }
        if (k == 2) { std::snprintf(b, sizeof(b), "%d MS", (int)std::lround(1.f + v / 127.f * 59.f)); return b; }
        std::snprintf(b, sizeof(b), v >= 126.5f ? "OPEN" : "%d", (int)std::lround(v)); return b;
    default:
        if (k == 0) { std::snprintf(b, sizeof(b), "%.2f BT", 0.0625 * std::pow(64.0, v / 127.0)); return b; }
        if (k == 1) return v < 43.f ? "SOFT" : (v < 86.f ? "LINEAR" : "HARD");
        std::snprintf(b, sizeof(b), "%d", (int)std::lround(v)); return b;
    }
}
inline std::string knobText(int l, int k, float v, float modelVal)
{
    if (l == lnPerform) return performValueText(k, v, modelOf(modelVal, kPerformModels));
    return laneValueText(l, k, v);
}
inline std::string envValueText(int k, float v)
{
    char b[32];
    switch (k) {
    case 0: std::snprintf(b, sizeof(b), "%+.0fdB", (v - 64.f) / 64.f * 24.f); return b;
    case 1: return fmtMs(1.f + v / 127.f * 199.f);
    case 2: return fmtMs(10.f + v / 127.f * 1990.f);
    case 3: return fmtBip(v);
    default: return lfoDestText((int)std::lround(v));
    }
}

} // namespace kn
