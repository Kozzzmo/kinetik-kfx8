// KFX-8 pattern bank: text round trip, edit commands, rotation, factory presets.
//   g++ -O2 -std=c++17 tests/bank.cpp -o /tmp/t && /tmp/t
#include "../src/Presets.hpp"
#include <cstdio>
#include <memory>
#include <vector>
using namespace kn;

static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; }

int main()
{
    std::unique_ptr<Bank> a(new Bank()), b(new Bank());
    a->initDefault();
    applyCommand(*a, "P 3 32 62");
    applyCommand(*a, "L 3 2 13 4");
    applyCommand(*a, "s 3 2 5 1");
    applyCommand(*a, "S 3 7 1001");
    applyCommand(*a, "k 3 2 5 1 99");
    applyCommand(*a, "U 6 1");
    check(deserializeBank(*b, serializeBank(*a).c_str()), "bank text reads back");
    const Pattern& p = b->patterns[3];
    check(p.length == 32 && p.swing == 62 && p.len[2] == 13 && p.speed[2] == 4 && p.on[2][5] && p.locks[2][5][1] == 99, "pattern settings, steps and locks survive");
    check(p.on[7][0] && !p.on[7][1] && p.on[7][3] && b->mute[6] == 1, "step strings and bypass survive");
    check(serializeBank(*a) == serializeBank(*b), "serialization is stable");
    applyCommand(*a, "rot 3 2 2");
    check(a->patterns[3].on[2][7] && a->patterns[3].locks[2][7][1] == 99 && !a->patterns[3].on[2][5], "rotation moves steps and locks within the lane length");
    applyCommand(*a, "k 3 2 7 1 255");
    check(a->patterns[3].locks[2][7][1] == kNoLock, "lock 255 = cleared");
    applyCommand(*a, "cp 3 9");
    check(a->patterns[9].len[2] == 13 && a->patterns[9].on[2][7], "copy pattern");
    applyCommand(*a, "cll 9 2");
    check(a->patterns[9].laneEmpty(2), "clear lane");
    check(applyCommand(*a, "s 99 0 0 1") < 0 && applyCommand(*a, "k 0 9 0 0 1") < 0 && applyCommand(*a, "zz") < 0, "out-of-range / unknown commands refused");
    check(!deserializeBank(*b, "KNTK4\n"), "another product's bank refused");
    // presets
    bool ok = true;
    for (int i = 0; i < kNumPresets; ++i) {
        std::vector<float> v(kTotalParams); std::unique_ptr<Bank> pb(new Bank());
        buildPreset(i, v.data(), *pb);
        bool any = false;
        for (int l = 0; l < kLanes; ++l) any = any || !pb->patterns[0].laneEmpty(l);
        for (int x = 0; x < kGlobalBase + gOUT_STEP; ++x) if (!(v[x] >= 0.f && v[x] <= 240.f)) ok = false;
        if (i > 0 && !any) { std::printf("     %s has no lit step\n", kPresets[i].name); ok = false; }
        std::unique_ptr<Bank> rb(new Bank());
        if (!deserializeBank(*rb, serializeBank(*pb).c_str()) || serializeBank(*rb) != serializeBank(*pb)) ok = false;
    }
    check(ok, "every factory preset builds, has steps and round-trips");
    std::printf(fails ? "%d failures\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
