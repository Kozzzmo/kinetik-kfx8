// Kinetik KFX-8 — patterns (8 lanes of steps with per-step locks), text serialization and edit commands
#pragma once
#include "Params.hpp"
#include "Chain.hpp"
#include <cstring>
#include <cstdlib>
#include <string>

namespace kn {

struct Pattern {
    uint8_t length = 16;                  // master length: a queued pattern starts when it ends
    uint8_t swing = 50;                   // 50..80 (% of the step pair given to the first step)
    uint8_t len[kLanes];                  // lane lengths 1..64 (each lane loops on its own)
    uint8_t speed[kLanes];                // index into kSpeedMult
    uint8_t on[kLanes][kSteps];           // 1 = the lane's effect is active during this step
    uint8_t tie[kLanes][kSteps];          // 1 = this step continues the previous one (no cut, the first step's locks)
    uint8_t cond[kLanes][kSteps];         // step condition (kCondNames; 0 = always)
    uint8_t locks[kLanes][kSteps][kKnobs];// per-step knob values (kNoLock = the knob)

    Pattern() { clear(); }
    void clear()
    {
        length = 16; swing = 50;
        for (int l = 0; l < kLanes; ++l) { len[l] = 16; speed[l] = 0; }
        std::memset(on, 0, sizeof(on)); std::memset(tie, 0, sizeof(tie)); std::memset(cond, 0, sizeof(cond));
        std::memset(locks, kNoLock, sizeof(locks));
    }
    void clearLane(int l)
    {
        std::memset(on[l], 0, sizeof(on[l])); std::memset(tie[l], 0, sizeof(tie[l])); std::memset(cond[l], 0, sizeof(cond[l]));
        std::memset(locks[l], kNoLock, sizeof(locks[l]));
    }
    int laneStep(int l, int64_t k) const { const int n = len[l] ? len[l] : 16; return (int)(((k % n) + n) % n); }
    bool hasLocks(int l, int s) const { for (int k = 0; k < kKnobs; ++k) if (locks[l][s][k] != kNoLock) return true; return false; }
    bool laneEmpty(int l) const { for (int s = 0; s < kSteps; ++s) if (on[l][s] || tie[l][s] || cond[l][s]) return false; return true; }
    bool lit(int l, int s) const { return on[l][s] || tie[l][s]; }
    // the step whose locks apply at step s (the start of a tied run), and how far s is into the run
    int runStart(int l, int s, int& back) const
    {
        const int L = len[l] ? len[l] : 16; back = 0;
        int j = s;
        while (tie[l][j] && back < L - 1) { const int pj = (j - 1 + L) % L; if (!lit(l, pj)) break; j = pj; ++back; }
        return j;
    }
    int runLength(int l, int start) const
    {
        const int L = len[l] ? len[l] : 16; int n = 1;
        while (n < L && tie[l][(start + n) % L]) ++n;
        return n;
    }
    bool isDefault() const
    {
        if (length != 16 || swing != 50) return false;
        for (int l = 0; l < kLanes; ++l) {
            if (len[l] != 16 || speed[l] != 0) return false;
            for (int s = 0; s < kSteps; ++s) { if (on[l][s] || hasLocks(l, s)) return false; }
        }
        return true;
    }
    // rotate lane l by n steps (within its length), locks follow
    void rotateLane(int l, int n)
    {
        const int L = len[l];
        uint8_t o[kSteps], tt[kSteps], cc[kSteps]; uint8_t lk[kSteps][kKnobs];
        std::memcpy(o, on[l], sizeof(o)); std::memcpy(tt, tie[l], sizeof(tt)); std::memcpy(cc, cond[l], sizeof(cc)); std::memcpy(lk, locks[l], sizeof(lk));
        for (int s = 0; s < L; ++s) {
            const int d = ((s + n) % L + L) % L;
            on[l][d] = o[s]; tie[l][d] = tt[s]; cond[l][d] = cc[s]; std::memcpy(locks[l][d], lk[s], kKnobs);
        }
    }
};

struct Bank {
    Pattern patterns[kPatterns];
    uint8_t mute[kLanes] = {};            // lane bypassed (all patterns)
    uint8_t always[kLanes] = {};          // LaneMode: 0 = on during lit steps, 1 = on all the time (lit steps only carry locks), 2 = ONE-SHOT (a MIDI note / PUSH plays the lane once)
    uint8_t order[kLanes];                // processing order (lane ids)
    Chain chain;
    void initDefault()
    {
        for (auto& p : patterns) p.clear();
        std::memset(mute, 0, sizeof(mute)); std::memset(always, 0, sizeof(always)); chain = Chain();
        for (int i = 0; i < kLanes; ++i) order[i] = (uint8_t)kDefaultOrder[i];
    }
    bool defaultOrder() const { for (int i = 0; i < kLanes; ++i) if (order[i] != kDefaultOrder[i]) return false; return true; }
    int position(int lane) const { for (int i = 0; i < kLanes; ++i) if (order[i] == lane) return i; return lane; }
};

static constexpr int kMuteTouched = 1000;   // applyCommand: lane mutes / modes / chain changed

inline std::string serializeBank(const Bank& b)
{
    std::string s = "KFX83\n";
    char buf[160];
    for (int l = 0; l < kLanes; ++l) if (b.mute[l]) { std::snprintf(buf, sizeof(buf), "U %d 1\n", l); s += buf; }
    for (int l = 0; l < kLanes; ++l) if (b.always[l]) { std::snprintf(buf, sizeof(buf), "A %d %d\n", l, b.always[l]); s += buf; }
    if (!b.defaultOrder()) { s += "O"; for (int i = 0; i < kLanes; ++i) s += " " + std::to_string(b.order[i]); s += "\n"; }
    if (!b.chain.isDefault()) s += b.chain.line() + "\n";
    for (int p = 0; p < kPatterns; ++p) {
        const Pattern& P = b.patterns[p];
        if (P.isDefault()) continue;
        std::snprintf(buf, sizeof(buf), "P %d %d %d\n", p, P.length, P.swing); s += buf;
        for (int l = 0; l < kLanes; ++l) {
            if (P.len[l] != 16 || P.speed[l] != 0) { std::snprintf(buf, sizeof(buf), "L %d %d %d %d\n", p, l, P.len[l], P.speed[l]); s += buf; }
            if (!P.laneEmpty(l)) {
                std::string row(kSteps, '0');
                for (int st = 0; st < kSteps; ++st) if (P.on[l][st]) row[st] = '1';
                std::snprintf(buf, sizeof(buf), "S %d %d ", p, l); s += buf; s += row; s += "\n";
            }
            bool anyTie = false; for (int st = 0; st < kSteps; ++st) anyTie = anyTie || P.tie[l][st];
            if (anyTie) {
                std::string row(kSteps, '0');
                for (int st = 0; st < kSteps; ++st) if (P.tie[l][st]) row[st] = '1';
                std::snprintf(buf, sizeof(buf), "T %d %d ", p, l); s += buf; s += row; s += "\n";
            }
            for (int st = 0; st < kSteps; ++st)
                if (P.cond[l][st]) { std::snprintf(buf, sizeof(buf), "c %d %d %d %d\n", p, l, st, P.cond[l][st]); s += buf; }
            for (int st = 0; st < kSteps; ++st)
                for (int k = 0; k < kKnobs; ++k)
                    if (P.locks[l][st][k] != kNoLock) { std::snprintf(buf, sizeof(buf), "k %d %d %d %d %d\n", p, l, st, k, P.locks[l][st][k]); s += buf; }
        }
    }
    return s;
}

// One edit line. Returns the pattern it touched, kMuteTouched, or -1 (nothing / not understood).
//   P p length swing      L p lane len speed      s p lane step 0|1      S p lane <0/1 string>
//   c p lane step condition   O <order of the lanes>   T p lane <0/1 string> / t p lane step 0|1 (ties)   k p lane step knob v  (v 255 = no lock)        kc p lane step (clear the step's locks)
//   U lane 0|1 (bypass)   A lane 0|1 (always on)   C on n p:r ... (chain)   cl p (clear pattern)     cll p lane            cp src dst      rot p lane n
inline int applyCommand(Bank& b, const char* line)
{
    char op[8] = {0};
    if (std::sscanf(line, "%7s", op) != 1) return -1;
    const char* a = line + std::strlen(op);
    int v[6] = {0};
    auto okP = [](int p) { return p >= 0 && p < kPatterns; };
    auto okL = [](int l) { return l >= 0 && l < kLanes; };
    auto okS = [](int s) { return s >= 0 && s < kSteps; };
    if (!std::strcmp(op, "P") && std::sscanf(a, "%d %d %d", &v[0], &v[1], &v[2]) == 3 && okP(v[0])) {
        b.patterns[v[0]].length = (uint8_t)clampi(v[1], 1, kSteps); b.patterns[v[0]].swing = (uint8_t)clampi(v[2], 50, 80); return v[0];
    }
    if (!std::strcmp(op, "L") && std::sscanf(a, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]) == 4 && okP(v[0]) && okL(v[1])) {
        b.patterns[v[0]].len[v[1]] = (uint8_t)clampi(v[2], 1, kSteps); b.patterns[v[0]].speed[v[1]] = (uint8_t)clampi(v[3], 0, kSpeeds - 1); return v[0];
    }
    if (!std::strcmp(op, "s") && std::sscanf(a, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]) == 4 && okP(v[0]) && okL(v[1]) && okS(v[2])) {
        b.patterns[v[0]].on[v[1]][v[2]] = v[3] ? 1 : 0; return v[0];
    }
    if (!std::strcmp(op, "S") && std::sscanf(a, "%d %d", &v[0], &v[1]) == 2 && okP(v[0]) && okL(v[1])) {
        const char* r = std::strrchr(a, ' ');
        if (!r) return -1;
        ++r;
        for (int st = 0; st < kSteps; ++st) b.patterns[v[0]].on[v[1]][st] = (r[0] && *r++ == '1') ? 1 : 0;
        return v[0];
    }
    if (!std::strcmp(op, "T") && std::sscanf(a, "%d %d", &v[0], &v[1]) == 2 && okP(v[0]) && okL(v[1])) {
        const char* r = std::strrchr(a, ' ');
        if (!r) return -1;
        ++r;
        for (int st = 0; st < kSteps; ++st) b.patterns[v[0]].tie[v[1]][st] = (r[0] && *r++ == '1') ? 1 : 0;
        return v[0];
    }
    if (!std::strcmp(op, "t") && std::sscanf(a, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]) == 4 && okP(v[0]) && okL(v[1]) && okS(v[2])) {
        b.patterns[v[0]].tie[v[1]][v[2]] = v[3] ? 1 : 0; return v[0];
    }
    if (!std::strcmp(op, "k") && std::sscanf(a, "%d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5 && okP(v[0]) && okL(v[1]) && okS(v[2]) && v[3] >= 0 && v[3] < kKnobs) {
        b.patterns[v[0]].locks[v[1]][v[2]][v[3]] = (uint8_t)(v[4] == kNoLock ? kNoLock : clampi(v[4], 0, 127)); return v[0];
    }
    if (!std::strcmp(op, "kc") && std::sscanf(a, "%d %d %d", &v[0], &v[1], &v[2]) == 3 && okP(v[0]) && okL(v[1]) && okS(v[2])) {
        std::memset(b.patterns[v[0]].locks[v[1]][v[2]], kNoLock, kKnobs); return v[0];
    }
    if (!std::strcmp(op, "U") && std::sscanf(a, "%d %d", &v[0], &v[1]) == 2 && okL(v[0])) { b.mute[v[0]] = v[1] ? 1 : 0; return kMuteTouched; }
    if (!std::strcmp(op, "A") && std::sscanf(a, "%d %d", &v[0], &v[1]) == 2 && okL(v[0])) { b.always[v[0]] = (uint8_t)clampi(v[1], 0, 2); return kMuteTouched; }
    if (!std::strcmp(op, "O")) {            // a permutation of the lanes
        int o[kLanes]; const char* c = a; bool seen[kLanes] = {};
        for (int i = 0; i < kLanes; ++i) { char* e = nullptr; const long x = std::strtol(c, &e, 10); if (e == c || x < 0 || x >= kLanes || seen[x]) return -1; seen[x] = true; o[i] = (int)x; c = e; }
        for (int i = 0; i < kLanes; ++i) b.order[i] = (uint8_t)o[i];
        return kMuteTouched;
    }
    if (!std::strcmp(op, "c") && std::sscanf(a, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]) == 4 && okP(v[0]) && okL(v[1]) && okS(v[2])) {
        b.patterns[v[0]].cond[v[1]][v[2]] = (uint8_t)clampi(v[3], 0, kConds - 1); return v[0];
    }
    if (!std::strcmp(op, "C")) return b.chain.parse(a, kPatterns) ? kMuteTouched : -1;
    if (!std::strcmp(op, "cl") && std::sscanf(a, "%d", &v[0]) == 1 && okP(v[0])) { b.patterns[v[0]].clear(); return v[0]; }
    if (!std::strcmp(op, "cll") && std::sscanf(a, "%d %d", &v[0], &v[1]) == 2 && okP(v[0]) && okL(v[1])) { b.patterns[v[0]].clearLane(v[1]); return v[0]; }
    if (!std::strcmp(op, "cp") && std::sscanf(a, "%d %d", &v[0], &v[1]) == 2 && okP(v[0]) && okP(v[1])) { b.patterns[v[1]] = b.patterns[v[0]]; return v[1]; }
    if (!std::strcmp(op, "rot") && std::sscanf(a, "%d %d %d", &v[0], &v[1], &v[2]) == 3 && okP(v[0]) && okL(v[1])) { b.patterns[v[0]].rotateLane(v[1], v[2]); return v[0]; }
    return -1;
}

inline bool deserializeBank(Bank& b, const char* text)
{
    if (!text || std::strncmp(text, "KFX8", 4) != 0) return false;   // KFX81 = 0.1 (other effects: its locks are kept as they are)
    b.initDefault();
    if (std::strncmp(text, "KFX83", 5) != 0)          // before 0.4 the compressor came 4th by default: those banks keep it
        for (int i = 0; i < kLanes; ++i) b.order[i] = (uint8_t)kDefaultOrder03[i];
    const char* s = text;
    std::string line;
    while (*s) {
        const char* e = std::strchr(s, '\n');
        const size_t n = e ? (size_t)(e - s) : std::strlen(s);
        line.assign(s, n);
        if (!line.empty() && line.rfind("KFX", 0) != 0) applyCommand(b, line.c_str());
        s += n; if (*s == '\n') ++s;
    }
    return true;
}

} // namespace kn
