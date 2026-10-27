// Kinetik KFX-8 — constants, parameters and value texts
#pragma once
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include "Dsp.hpp"

namespace kn {

static constexpr int kLanes = 8;          // one effect per lane, processed in this order
static constexpr int kSteps = 64;         // steps per lane
static constexpr int kPatterns = 16;
static constexpr int kKnobs = 4;          // effect knobs per lane (lockable per step)
static constexpr int kLaneParams = 5;     // + MIX
static constexpr uint8_t kNoLock = 255;

enum Lane { lnStutter, lnReverse, lnTape, lnPitch, lnCrush, lnFilter, lnGate, lnDelay };
static const char* const kLaneNames[kLanes] = {"STUTTER", "REVERSE", "TAPE STOP", "PITCH", "CRUSH", "FILTER", "GATE", "DELAY"};
static const char* const kLaneShort[kLanes] = {"STUT", "REV", "TAPE", "PITCH", "CRUSH", "FILT", "GATE", "DLY"};

struct LaneDef { const char* knob[kKnobs]; uint8_t def[kLaneParams]; const char* help; };
static const LaneDef kLaneDefs[kLanes] = {
    {{"SIZE", "DECAY", "PITCH", "GATE"},     {56, 127, 64, 127, 127}, "Repeats a slice of the sound: the first pass plays, then it loops"},
    {{"LENGTH", "SPEED", "FADE", "TONE"},    {56, 64, 30, 127, 127},  "Plays the last beats backwards"},
    {{"TIME", "CURVE", "TONE", "WOBBLE"},    {60, 64, 80, 0, 127},    "Slows the sound down to a stop, like a tape machine switched off"},
    {{"SHIFT", "FINE", "WINDOW", "FEEDBACK"},{88, 64, 50, 0, 127},    "Shifts the pitch up or down (feedback = rising / falling spiral)"},
    {{"BITS", "RATE", "DRIVE", "TONE"},      {70, 40, 30, 110, 127},  "Bit reduction, sample-rate reduction and drive"},
    {{"CUTOFF", "RESO", "TYPE", "SWEEP"},    {50, 60, 0, 64, 127},    "Resonant filter; SWEEP moves the cutoff across each step"},
    {{"RATE", "DUTY", "SMOOTH", "DEPTH"},    {40, 50, 20, 127, 127},  "Chops the sound in rhythm"},
    {{"TIME", "FEEDBACK", "TONE", "WIDTH"},  {88, 64, 80, 64, 90},    "Echo throw: only the sound of the lit steps goes into the echo, the tail rings on"},
};

// beat-synced choices (in quarter notes); knobs pick one of 8 with bins of 16 values
static const double kStutSize[8]  = {0.0625, 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 1.0, 2.0};
static const char* const kStutSizeNames[8] = {"1/64", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4", "1/2"};
static const double kRevLen[8]    = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0};
static const char* const kRevLenNames[8] = {"1/16", "1/8", "1/8D", "1/4", "1/4D", "1/2", "1/2D", "1 BAR"};
static const double kGateRate[8]  = {0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625, 1.0 / 24.0};
static const char* const kGateRateNames[8] = {"1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64", "1/64T"};
static const double kDelTime[8]   = {0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 0.75, 1.0};
static const char* const kDelTimeNames[8] = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/8D", "1/4"};
static const char* const kFilterTypes[3] = {"LOW PASS", "BAND PASS", "HIGH PASS"};
inline int bin8(float v) { int b = (int)(v / 16.f); return b < 0 ? 0 : (b > 7 ? 7 : b); }
inline int bin3(float v) { int b = (int)(v / 42.67f); return b < 0 ? 0 : (b > 2 ? 2 : b); }

// lane speed (steps per 16th): index stored in the pattern
static constexpr int kSpeeds = 8;
static const double kSpeedMult[kSpeeds] = {1.0, 2.0, 4.0, 0.5, 0.25, 0.75, 1.5, 3.0};
static const char* const kSpeedNames[kSpeeds] = {"1x", "2x", "4x", "1/2x", "1/4x", "3/4x", "3/2x", "3x"};

// ---- host parameters: lanes first (lane l = indices l*5 .. l*5+4), then the globals
// Saved host parameters never move: new ones go just before gOUT_STEP.
static constexpr int kGlobalBase = kLanes * kLaneParams;
enum Global {
    gDRYWET = 0, gINPUT, gOUTPUT, gTEMPO, gPLAY, gPATTERN,
    gOUT_STEP, gOUT_ACTIVE, gOUT_PATTERN, kGlobalParams
};
static constexpr int kTotalParams = kGlobalBase + kGlobalParams;
inline int lpIndex(int lane, int k) { return lane * kLaneParams + k; }
inline float laneParamDefault(int lane, int k) { return kLaneDefs[lane].def[k]; }
inline float globalDefault(int g)
{
    switch (g) {
    case gDRYWET: return 127.f; case gINPUT: return 96.f; case gOUTPUT: return 96.f;
    case gTEMPO: return 120.f; case gOUT_STEP: return -1.f;
    default: return 0.f;
    }
}
// INPUT / OUTPUT knob -> gain (96 = 0 dB, 0 = -inf, 127 = +8 dB)
inline float trimDb(float v) { return v <= 0.5f ? -120.f : (v - 96.f) * (v < 96.f ? 0.5f : 0.26f); }
inline float trimGain(float v) { return v <= 0.5f ? 0.f : std::pow(10.f, trimDb(v) / 20.f); }

// readable value of lane knob k (0..127)
inline std::string laneValueText(int lane, int k, float v)
{
    char b[32];
    if (k == 4) { std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(v / 127.f * 100.f)); return b; }
    switch (lane) {
    case lnStutter:
        if (k == 0) return kStutSizeNames[bin8(v)];
        if (k == 1) { std::snprintf(b, sizeof(b), v >= 126.5f ? "NONE" : "%d%%", (int)std::lround((1.f - v / 127.f) * 100.f)); return b; }
        if (k == 2) { std::snprintf(b, sizeof(b), "%+d ST", (int)std::lround((v - 64.f) / 64.f * 12.f)); return b; }
        std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(10.f + v / 127.f * 90.f)); return b;
    case lnReverse:
        if (k == 0) return kRevLenNames[bin8(v)];
        if (k == 1) { std::snprintf(b, sizeof(b), "%.2fx", std::pow(2.f, (v - 64.f) / 64.f)); return b; }
        if (k == 2) { std::snprintf(b, sizeof(b), "%d MS", (int)std::lround(1.f + v / 127.f * 59.f)); return b; }
        std::snprintf(b, sizeof(b), v >= 126.5f ? "OPEN" : "%d", (int)std::lround(v)); return b;
    case lnTape:
        if (k == 0) { std::snprintf(b, sizeof(b), "%.2f BT", 0.0625 * std::pow(64.0, v / 127.0)); return b; }
        if (k == 1) return v < 43.f ? "SOFT" : (v < 86.f ? "LINEAR" : "HARD");
        std::snprintf(b, sizeof(b), "%d", (int)std::lround(v)); return b;
    case lnPitch:
        if (k == 0) { std::snprintf(b, sizeof(b), "%+d ST", (int)std::lround((v - 64.f) / 64.f * 24.f)); return b; }
        if (k == 1) { std::snprintf(b, sizeof(b), "%+d CT", (int)std::lround((v - 64.f) / 64.f * 50.f)); return b; }
        if (k == 2) { std::snprintf(b, sizeof(b), "%d MS", (int)std::lround(15.f + v / 127.f * 105.f)); return b; }
        std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(v / 127.f * 90.f)); return b;
    case lnCrush:
        if (k == 0) { std::snprintf(b, sizeof(b), "%d BIT", (int)std::lround(16.f - v / 127.f * 15.f)); return b; }
        if (k == 1) { std::snprintf(b, sizeof(b), "1/%d", (int)std::lround(std::pow(64.f, v / 127.f))); return b; }
        std::snprintf(b, sizeof(b), "%d", (int)std::lround(v)); return b;
    case lnFilter:
        if (k == 0) { const float hz = 20.f * std::pow(1000.f, v / 127.f);
                      if (hz >= 1000.f) std::snprintf(b, sizeof(b), "%.1fK", hz / 1000.f); else std::snprintf(b, sizeof(b), "%d HZ", (int)hz); return b; }
        if (k == 2) return kFilterTypes[bin3(v)];
        if (k == 3) { std::snprintf(b, sizeof(b), "%+.1f OCT", (v - 64.f) / 64.f * 4.f); return b; }
        std::snprintf(b, sizeof(b), "%d", (int)std::lround(v)); return b;
    case lnGate:
        if (k == 0) return kGateRateNames[bin8(v)];
        if (k == 1) { std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(5.f + v / 127.f * 90.f)); return b; }
        if (k == 2) { std::snprintf(b, sizeof(b), "%.1f MS", 0.5 + v / 127.0 * 19.5); return b; }
        std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(v / 127.f * 100.f)); return b;
    case lnDelay:
        if (k == 0) return kDelTimeNames[bin8(v)];
        if (k == 1) { std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(v / 127.f * 95.f)); return b; }
        std::snprintf(b, sizeof(b), "%d", (int)std::lround(v)); return b;
    }
    std::snprintf(b, sizeof(b), "%d", (int)std::lround(v)); return b;
}

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace kn
