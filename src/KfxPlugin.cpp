// Kinetik KFX-8 — DPF plugin glue
#include "DistrhoPlugin.hpp"
#include "Engine.hpp"
#include "License.hpp"
#include <atomic>
#include <cmath>
#include <mutex>
#include <memory>

START_NAMESPACE_DISTRHO

using namespace kn;

static constexpr uint32_t kMuteBit = 1u << 16;

class KfxPlugin : public Plugin
{
public:
    KfxPlugin()
        : Plugin(kTotalParams, 0, 3)
    {
        engine.reset(new Engine());
        master.reset(new Bank());
        master->initDefault();
        engine->bank = *master;
        engine->init(getSampleRate());
        for (int i = 0; i < kTotalParams; ++i) { uiVal[i].store(0.f); uiHold[i].store(0); }
        licensed.store(lic::findInstalled(kProductCode).valid);
    }

protected:
    const char* getLabel() const override { return "KinetikKFX8"; }
    const char* getDescription() const override { return "Sequenced multi-effect: 8 effects, each always on or switched by its own lane of steps, with per-step locks"; }
    const char* getMaker() const override { return "Kinetik"; }
    const char* getLicense() const override { return "Proprietary"; }
    uint32_t getVersion() const override { return d_version(KN_VER_MAJOR, KN_VER_MINOR, KN_VER_MICRO); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        if (input && index >= 2) {             // inputs 3 / 4: the side-chain (COMP and ENV follower SOURCE)
            port.hints = kAudioPortIsSidechain;
            port.name = index == 2 ? "Sidechain Left" : "Sidechain Right";
            port.symbol = index == 2 ? "sc_in_left" : "sc_in_right";
            port.groupId = kPortGroupNone;
            return;
        }
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }
    // AU also offers plain stereo (no side-chain): then only 2 inputs come
    void ioChanged(uint16_t numInputs, uint16_t) override { numIn.store(numInputs); }

    void initParameter(uint32_t index, Parameter& p) override
    {
        p.hints = kParameterIsAutomatable;
        if ((int)index < kGlobalBase) {
            const int l = (int)index / kLaneParams, k = (int)index % kLaneParams;
            static char names[kGlobalBase][40], syms[kGlobalBase][24];
            const char* kn = k < kKnobs ? (kLaneDefs[l].knob[k] ? kLaneDefs[l].knob[k] : "UNUSED") : "MIX";
            std::snprintf(names[index], sizeof(names[index]), "%s %s", kLaneNames[l], kn);
            std::snprintf(syms[index], sizeof(syms[index]), "l%d_%d", l + 1, k + 1);
            for (char* c = names[index] + std::strlen(kLaneNames[l]) + 1; *c; ++c) *c = (char)std::tolower((unsigned char)*c);   // "STUTTER size"
            p.name = names[index]; p.shortName = names[index]; p.symbol = syms[index];
            p.ranges.min = 0; p.ranges.max = 127; p.ranges.def = laneParamDefault(l, k);
            return;
        }
        const int g = (int)index - kGlobalBase;
        switch (g) {
        case gDRYWET: p.name = "Dry/Wet"; p.symbol = "drywet"; p.ranges.min = 0; p.ranges.max = 127; p.ranges.def = 127; return;
        case gINPUT: p.name = "Input"; p.symbol = "input"; p.ranges.min = 0; p.ranges.max = 127; p.ranges.def = 96; return;
        case gOUTPUT: p.name = "Output"; p.symbol = "output"; p.ranges.min = 0; p.ranges.max = 127; p.ranges.def = 96; return;
        case gTEMPO: p.name = "Internal Tempo"; p.symbol = "tempo"; p.ranges.min = 40; p.ranges.max = 240; p.ranges.def = 120; p.unit = "BPM"; return;
        case gPLAY: p.name = "Internal Play"; p.symbol = "play"; p.ranges.min = 0; p.ranges.max = 1; p.ranges.def = 0;
            p.hints |= kParameterIsBoolean | kParameterIsInteger; return;
        case gPATTERN: p.name = "Pattern"; p.symbol = "pattern"; p.ranges.min = 0; p.ranges.max = kPatterns - 1; p.ranges.def = 0;
            p.hints |= kParameterIsInteger; return;
        case gLFO1_WAVE: case gLFO1_RATE: case gLFO1_SYNC: case gLFO1_DEPTH: case gLFO1_DEST: case gLFO1_PHASE:
        case gLFO2_WAVE: case gLFO2_RATE: case gLFO2_SYNC: case gLFO2_DEPTH: case gLFO2_DEST: case gLFO2_PHASE: {
            static char nm[12][24], sy[12][16];
            const int i = (g - gLFO1_WAVE) / kLfoParams, k = (g - gLFO1_WAVE) % kLfoParams, x = g - gLFO1_WAVE;
            static const char* kn[kLfoParams] = {"Wave", "Rate", "Sync", "Depth", "Destination", "Phase"};
            std::snprintf(nm[x], sizeof(nm[x]), "LFO %d %s", i + 1, kn[k]); std::snprintf(sy[x], sizeof(sy[x]), "lfo%d_%d", i + 1, k + 1);
            p.name = nm[x]; p.shortName = nm[x]; p.symbol = sy[x];
            p.ranges.min = 0; p.ranges.max = k == 4 ? (float)(kLfoDests - 1) : 127.f; p.ranges.def = globalDefault(g);
            if (k == 4) p.hints |= kParameterIsInteger;
            return;
        }
        case gENV_SENS: p.name = "Envelope Sensitivity"; p.symbol = "env_sens"; p.ranges.min = 0; p.ranges.max = 127; p.ranges.def = globalDefault(g); return;
        case gENV_ATTACK: p.name = "Envelope Attack"; p.symbol = "env_attack"; p.ranges.min = 0; p.ranges.max = 127; p.ranges.def = globalDefault(g); return;
        case gENV_RELEASE: p.name = "Envelope Release"; p.symbol = "env_release"; p.ranges.min = 0; p.ranges.max = 127; p.ranges.def = globalDefault(g); return;
        case gENV_DEPTH: p.name = "Envelope Depth"; p.symbol = "env_depth"; p.ranges.min = 0; p.ranges.max = 127; p.ranges.def = globalDefault(g); return;
        case gENV_DEST: p.name = "Envelope Destination"; p.symbol = "env_dest"; p.ranges.min = 0; p.ranges.max = (float)(kLfoDests - 1); p.ranges.def = 0; p.hints |= kParameterIsInteger; return;
        case gOUT_STEP: p.name = "Play Step"; p.symbol = "out_step"; p.hints = kParameterIsOutput | kParameterIsInteger;
            p.ranges.min = -1; p.ranges.max = 32768 * Engine::kFine; p.ranges.def = -1; return;
        case gOUT_ACTIVE: p.name = "Active Lanes"; p.symbol = "out_active"; p.hints = kParameterIsOutput | kParameterIsInteger;
            p.ranges.min = 0; p.ranges.max = 255; p.ranges.def = 0; return;
        case gOUT_PATTERN: p.name = "Playing Pattern"; p.symbol = "out_pattern"; p.hints = kParameterIsOutput | kParameterIsInteger;
            p.ranges.min = 0; p.ranges.max = kPatterns - 1; p.ranges.def = 0; return;
        case gENV_SRC: p.name = "Envelope Source"; p.symbol = "env_source"; p.ranges.min = 0; p.ranges.max = 127; p.ranges.def = 0; return;
        case gOUT_SC: p.name = "Side-chain Level"; p.symbol = "out_sc"; p.hints = kParameterIsOutput;
            p.ranges.min = 0; p.ranges.max = 1; p.ranges.def = 0; return;
        default: break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        if (index == 0) { state.key = "bank"; state.defaultValue = String(serializeBank(*master).c_str()); state.label = "Patterns"; }
        else if (index == 1) { state.key = "cmd"; state.defaultValue = ""; state.label = "Edit command"; }
        else { state.key = "skin"; state.defaultValue = ""; state.label = "Interface skin"; }
    }

    float getParameterValue(uint32_t index) const override
    {
        if ((int)index < kGlobalBase) return engine->laneParam[index / kLaneParams][index % kLaneParams];
        const int g = (int)index - kGlobalBase;
        switch (g) {
        case gOUT_STEP: return (float)engine->outStep;
        case gOUT_ACTIVE: return (float)engine->outActive;
        case gOUT_PATTERN: return (float)engine->outPattern;
        case gOUT_SC: return std::min(1.f, engine->scPeak);
        default: return g < kGlobalParams ? engine->global[g] : 0.f;
        }
    }

    // UI edits win over stale host values for 1.5 s (same scheme as KDSP-16 / KMSP-6)
    std::atomic<float> uiVal[kTotalParams];
    std::atomic<int32_t> uiHold[kTotalParams];
    void uiEdit(uint32_t index, float value)
    {
        if (index >= (uint32_t)kTotalParams) return;
        uiVal[index].store(value);
        uiHold[index].store((int32_t)(getSampleRate() * 1.5));
        applyParameter(index, value);
    }
    void setParameterValue(uint32_t index, float value) override
    {
        if (index < (uint32_t)kTotalParams && uiHold[index].load(std::memory_order_relaxed) > 0) {
            if (std::fabs(value - uiVal[index].load(std::memory_order_relaxed)) > 0.01f) return;
            uiHold[index].store(0);
        }
        applyParameter(index, value);
    }
    void applyParameter(uint32_t index, float value)
    {
        if ((int)index < kGlobalBase) { engine->laneParam[index / kLaneParams][index % kLaneParams] = value; return; }
        const int g = (int)index - kGlobalBase;
        if (g < 0 || g >= gOUT_STEP) return;
        engine->global[g] = value;
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "bank") == 0) { std::lock_guard<std::mutex> lk(masterMutex); return String(serializeBank(*master).c_str()); }
        if (std::strcmp(key, "skin") == 0) return skinState;
        return String("");
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "skin") == 0) { skinState = value ? value : ""; return; }
        if (std::strcmp(key, "bank") == 0) {
            std::lock_guard<std::mutex> lk(masterMutex);
            if (deserializeBank(*master, value)) dirty.store(0xFFFFu | kMuteBit);
            return;
        }
        if (std::strcmp(key, "cmd") == 0 && value && *value) {
            std::lock_guard<std::mutex> lk(masterMutex);
            uint32_t d = 0;
            const char* s = value;
            std::string line;
            while (*s) {
                const char* e = std::strchr(s, '\n');
                const size_t len = e ? (size_t)(e - s) : std::strlen(s);
                line.assign(s, len);
                if (line.rfind("pv ", 0) == 0) {
                    unsigned i = 0; float v = 0.f;
                    if (std::sscanf(line.c_str() + 3, "%u %f", &i, &v) == 2) uiEdit(i, v);
                } else if (line == "lic") {
                    licensed.store(lic::findInstalled(kProductCode).valid);
                } else if (line.rfind("fill ", 0) == 0) {          // FILL button
                    uiFill.store(std::atoi(line.c_str() + 5) != 0);
                } else if (line.rfind("hold ", 0) == 0) {          // on-screen lane pad held (mask)
                    uiHoldMask.store((uint32_t)std::strtoul(line.c_str() + 5, nullptr, 10));
                } else {
                    const int p = applyCommand(*master, line.c_str());
                    if (p == kMuteTouched) d |= kMuteBit;
                    else if (p >= 0) d |= 1u << p;
                }
                s += len; if (*s == '\n') ++s;
            }
            if (d) dirty.fetch_or(d);
        }
    }

    void activate() override { engine->reset(); }
    void sampleRateChanged(double sr) override { engine->init(sr); }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midi, uint32_t midiCount) override
    {
        for (int i = 0; i < kTotalParams; ++i) {
            const int32_t h = uiHold[i].load(std::memory_order_relaxed);
            if (h > 0) uiHold[i].store(h > (int32_t)frames ? h - (int32_t)frames : 0, std::memory_order_relaxed);
        }
        if (dirty.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lk(masterMutex, std::try_to_lock);
            if (lk.owns_lock()) {
                const uint32_t d = dirty.exchange(0);
                for (int p = 0; p < kPatterns; ++p) if (d & (1u << p)) engine->bank.patterns[p] = master->patterns[p];
                if (d & kMuteBit) { std::memcpy(engine->bank.mute, master->mute, sizeof(master->mute)); std::memcpy(engine->bank.always, master->always, sizeof(master->always)); engine->bank.chain = master->chain; std::memcpy(engine->bank.order, master->order, sizeof(master->order)); }
            }
        }
        // MIDI: C2..G#2 (48..56) hold lanes 1-9 on while pressed (ONE-SHOT lanes start), A2 = FILL, C3..D#4 (60..75) choose patterns A01..A16
        for (uint32_t i = 0; i < midiCount; ++i) {
            const MidiEvent& ev = midi[i];
            if (ev.size < 3) continue;
            const uint8_t st = ev.data[0] & 0xF0; const int note = ev.data[1];
            const bool on = st == 0x90 && ev.data[2] > 0, off = st == 0x80 || (st == 0x90 && ev.data[2] == 0);
            if (note >= 48 && note < 48 + kLanes) {
                if (on) midiHoldMask |= 1u << (note - 48);
                else if (off) midiHoldMask &= ~(1u << (note - 48));
            } else if (on && note >= 60 && note < 60 + kPatterns) {
                engine->midiPattern = note - 60;
            }
            if (note == 48 + kLanes) { if (on) midiFill = true; else if (off) midiFill = false; }   // A2 = FILL
            if (st == 0xB0 && (ev.data[1] == 120 || ev.data[1] == 123)) { midiHoldMask = 0; midiFill = false; }
        }
        engine->midiHold = midiHoldMask | uiHoldMask.load(std::memory_order_relaxed);
        engine->fill = midiFill || uiFill.load(std::memory_order_relaxed);
        if (engine->midiPattern >= 0 && (int)std::lround(engine->global[gPATTERN]) == engine->midiPattern) engine->midiPattern = -1;

        Transport tp;
        const TimePosition& pos = getTimePosition();
        if (pos.bbt.valid) {
            tp.hostValid = true;
            tp.hostPlaying = pos.playing;
            const double bt = pos.bbt.beatType > 0 ? pos.bbt.beatType : 4.0;
            const double beats = (pos.bbt.bar - 1) * (double)pos.bbt.beatsPerBar + (pos.bbt.beat - 1)
                               + (pos.bbt.ticksPerBeat > 0 ? pos.bbt.tick / pos.bbt.ticksPerBeat : 0.0);
            tp.beat = beats * (4.0 / bt);
            tp.bpm = pos.bbt.beatsPerMinute > 0 ? pos.bbt.beatsPerMinute : 120.0;
        }
        const bool sc = numIn.load(std::memory_order_relaxed) >= 4;
        engine->scL = sc && inputs[2] && inputs[3] ? inputs[2] : nullptr; engine->scR = engine->scL ? inputs[3] : nullptr;
        engine->process(inputs[0], inputs[1], outputs[0], outputs[1], frames, tp);
        if (lic::kDemoModeEnabled && !licensed.load(std::memory_order_relaxed)) demoGate(inputs, outputs, frames);
    }

    // Demo mode (no licence): the effects drop out (dry sound) for 3 seconds every 45 seconds.
    void demoGate(const float** inputs, float** outputs, uint32_t frames)
    {
        const double sr = getSampleRate();
        const uint64_t period = (uint64_t)(45.0 * sr), mute = (uint64_t)(3.0 * sr);
        const float coef = 1.f - std::exp(-1.f / (0.01f * (float)sr));
        for (uint32_t i = 0; i < frames; ++i) {
            const float target = (demoPos % period) >= period - mute ? 0.f : 1.f;
            demoGain += (target - demoGain) * coef;
            ++demoPos;
            if (demoGain < 0.9999f)
                for (uint32_t c = 0; c < 2; ++c) outputs[c][i] = inputs[c][i] + (outputs[c][i] - inputs[c][i]) * demoGain;
        }
    }

private:
    std::unique_ptr<Engine> engine;
    std::unique_ptr<Bank> master;
    mutable std::mutex masterMutex;
    std::atomic<uint32_t> dirty{0};
    std::atomic<int> numIn{DISTRHO_PLUGIN_NUM_INPUTS};
    std::atomic<bool> licensed{false};
    std::atomic<uint32_t> uiHoldMask{0};
    uint32_t midiHoldMask = 0; bool midiFill = false;
    std::atomic<bool> uiFill{false};
    uint64_t demoPos = 0; float demoGain = 1.f;
    static constexpr const char* kProductCode = "KFX-8";
    String skinState;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KfxPlugin)
};

Plugin* createPlugin() { return new KfxPlugin(); }

END_NAMESPACE_DISTRHO
