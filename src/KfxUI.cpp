// Kinetik KFX-8 — user interface (NanoVG)
#include "DistrhoUI.hpp"
#include "Bank.hpp"
#include "Presets.hpp"
#include "Skins.hpp"
#include "LogoMark.hpp"
#include "Fonts.hpp"
#include "License.hpp"
#include "Updater.hpp"
#include "FsUtf8.hpp"
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <cmath>
#include <string>
#include <memory>
#include <deque>
#include <chrono>

START_NAMESPACE_DISTRHO

using namespace kn;
using DGL_NAMESPACE::Color;

static constexpr float W = 1180.f, H = 720.f;
static constexpr int kFineSteps = 24;   // out_step is in 1/24 of a 16th

struct R {
    float x, y, w, h;
    bool in(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
};

enum DragKind { dkNone, dkKnob, dkGlobal, dkTempo, dkPatLen, dkSwing, dkLaneLen, dkPaint, dkHold, dkResize };
enum Overlay { ovNone, ovPresets, ovSkins, ovSave, ovLicense, ovUpdate };

class KfxUI : public UI
{
public:
    KfxUI()
        : UI(W, H)
    {
        loadSharedResources();
        const double s = getScaleFactor();
        if (s > 1.01) setSize((uint)(W * s), (uint)(H * s));
        setGeometryConstraints((uint)(W * 0.6 * (s > 1.01 ? s : 1.0)), (uint)(H * 0.6 * (s > 1.01 ? s : 1.0)), true, false);
        for (int i = 0; i < kTotalParams; ++i) values[i] = 0.f;
        for (int l = 0; l < kLanes; ++l) for (int k = 0; k < kLaneParams; ++k) values[lpIndex(l, k)] = laneParamDefault(l, k);
        for (int g = 0; g < kGlobalParams; ++g) values[kGlobalBase + g] = globalDefault(g);
        bank.reset(new Bank());
        bank->initDefault();
        fonts[fnSerif] = createFontFromMemory("kn-serif", kFontSerif, sizeof(kFontSerif), false);
        fonts[fnSerifItalic] = createFontFromMemory("kn-serif-i", kFontSerifItalic, sizeof(kFontSerifItalic), false);
        fonts[fnMono] = createFontFromMemory("kn-mono", kFontMono, sizeof(kFontMono), false);
        fonts[fnBlack] = createFontFromMemory("kn-black", kFontBlack, sizeof(kFontBlack), false);
        fonts[fnLogo] = createFontFromMemory("kn-logo", kFontUnbounded, sizeof(kFontUnbounded), false);
        int sk, zm;
        readDefaultView(sk, zm);
        setSkin(sk, false);
        if (zm != 100) setSize((uint)std::lround(W * baseScale() * zm / 100.0), (uint)std::lround(H * baseScale() * zm / 100.0));
        viewDirty = false;
        licInfo = lic::findInstalled("KFX-8");
        updater.reset(new upd::Checker("KFX-8", KN_VERSION, "https://github.com/Kozzzmo/kinetik-kfx8/releases/latest/download/update.txt"));
        updater->check(false);
    }

    // ------------------------------------------------------------------ skins / view
    static std::string viewFile()
    {
       #ifdef _WIN32
        const std::string base = fs::env("APPDATA");
        return !base.empty() ? base + "\\KinetikKFX8-view.txt" : std::string();
       #else
        const std::string base = fs::env("HOME");
       #ifdef __APPLE__
        return !base.empty() ? base + "/Library/Preferences/KinetikKFX8-view.txt" : std::string();
       #else
        return !base.empty() ? base + "/.config/KinetikKFX8-view.txt" : std::string();
       #endif
       #endif
    }
    static void readDefaultView(int& skinOut, int& zoomOut)
    {
        skinOut = kDefaultSkin; zoomOut = 100;
        const std::string f = viewFile();
        if (f.empty()) return;
        if (FILE* fp = fs::open(f, "r")) {
            int a = 0, b = 100; const int n = std::fscanf(fp, "%d %d", &a, &b); std::fclose(fp);
            if (n >= 1 && a >= 0 && a < kNumThemes) skinOut = a;
            if (n >= 2 && b >= 60 && b <= 300) zoomOut = b;
        }
    }
    static void writeDefaultView(int s, int z)
    {
        const std::string f = viewFile();
        if (f.empty()) return;
        if (FILE* fp = fs::open(f, "w")) { std::fprintf(fp, "%d %d\n", s, z); std::fclose(fp); }
    }
    void setSkin(int i, bool save) { skin = clampi(i, 0, kNumThemes - 1); th = &kThemes[skin]; if (save) saveView(); repaint(); }
    double baseScale() const { const double s = getScaleFactor(); return s > 0.1 ? s : 1.0; }
    int zoomPercent() const { return (int)std::lround(getWidth() / (W * baseScale()) * 100.0); }
    void setZoom(double z)
    {
        z = std::max(0.6, std::min(3.0, z));
        const uint w = (uint)std::lround(W * baseScale() * z), h = (uint)std::lround(H * baseScale() * z);
        if (std::abs((int)w - (int)getWidth()) > 1) setSize(w, h);
        viewDirty = true; repaint();
    }
    void saveView()
    {
        char b[32]; std::snprintf(b, sizeof(b), "%d %d", skin, zoomPercent());
        setState("skin", b);
        writeDefaultView(skin, zoomPercent());
        viewDirty = false;
    }

protected:
    // ------------------------------------------------------------------ DSP -> UI
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kTotalParams || values[index] == value) return;
        values[index] = value;
        repaint();
    }
    void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, "bank") == 0) { deserializeBank(*bank, value); repaint(); }
        if (std::strcmp(key, "skin") == 0 && value && *value) {
            int sk = kDefaultSkin, zm = 0;
            const int n = std::sscanf(value, "%d %d", &sk, &zm);
            if (n >= 1) setSkin(sk, false);
            if (n >= 2 && zm >= 60 && zm <= 300 && std::abs(zm - zoomPercent()) > 1) setZoom(zm / 100.0);
            viewDirty = false;
        }
    }
    void onResize(const ResizeEvent& ev) override { UI::onResize(ev); viewDirty = true; viewDirtyTicks = 0; }

    // ------------------------------------------------------------------ helpers
    float gp(int g) const { return values[kGlobalBase + g]; }
    float lp(int l, int k) const { return values[lpIndex(l, k)]; }
    int editPattern() const { return clampi((int)std::lround(gp(gPATTERN)), 0, kPatterns - 1); }
    Pattern& pat() { return bank->patterns[editPattern()]; }
    int64_t fineStep() const { return (int64_t)std::lround(gp(gOUT_STEP)); }
    bool playingThis() const { return gp(gOUT_STEP) >= 0.f && (int)std::lround(gp(gOUT_PATTERN)) == editPattern(); }
    int laneStepNow(int l)
    {
        if (!playingThis()) return -1;
        const int64_t k = (int64_t)std::floor((double)fineStep() / kFineSteps * kSpeedMult[pat().speed[l] % kSpeeds] + 1e-9);
        return pat().laneStep(l, k);
    }
    bool lockMode() const { return lockStep >= 0; }

    void setParam(uint32_t idx, float v)
    {
        if (values[idx] != v && idx != (uint32_t)(kGlobalBase + gPLAY) && idx != (uint32_t)(kGlobalBase + gPATTERN)) autoSnap("sound", 1.2);
        values[idx] = v;
        setParameterValue(idx, v);
        char b[40]; std::snprintf(b, sizeof(b), "pv %u %.4f\n", (unsigned)idx, v); pvQueue += b;
    }
    std::string pvQueue;
    void flushPv() { if (pvQueue.empty()) return; pvQueue.pop_back(); setState("cmd", pvQueue.c_str()); pvQueue.clear(); }
    void setParamGesture(uint32_t idx, float v) { editParameter(idx, true); setParam(idx, v); editParameter(idx, false); }
    void sendCmd(const std::string& c)
    {
        const bool live = c.rfind("hold ", 0) == 0;
        if (!live) { autoSnap("edit", 0.5); applyCommand(*bank, c.c_str()); }
        setState("cmd", c.c_str());
    }
    void sendLines(const std::string& c)
    {
        autoSnap("edit", 0.5);
        size_t st = 0;
        while (st < c.size()) {
            size_t e = c.find('\n', st); if (e == std::string::npos) e = c.size();
            applyCommand(*bank, c.substr(st, e - st).c_str());
            st = e + 1;
        }
        setState("cmd", c.c_str());
    }
    void sendPatternSettings() { char b[48]; std::snprintf(b, sizeof(b), "P %d %d %d", editPattern(), pat().length, pat().swing); sendCmd(b); }
    void sendLaneSettings(int l) { char b[48]; std::snprintf(b, sizeof(b), "L %d %d %d %d", editPattern(), l, pat().len[l], pat().speed[l]); sendCmd(b); }

    // ------------------------------------------------------------------ layout
    R logoArea() const { return {12.f, 4.f, 372.f, 50.f}; }
    R patLenBox() const { return {536.f, 9.f, 76.f, 40.f}; }
    R swingBox() const { return {618.f, 9.f, 76.f, 40.f}; }
    R patBox() const { return {700.f, 9.f, 130.f, 40.f}; }
    R playBtn() const { return {840.f, 9.f, 84.f, 40.f}; }
    R tempoBox() const { return {932.f, 9.f, 110.f, 40.f}; }
    R skinBtn() const { return {1050.f, 9.f, 116.f, 40.f}; }
    // lanes
    R stepPageBtn(int i) const { return {24.f + i * 58.f, 66.f, 54.f, 26.f}; }
    R lenMinus() const { return {300.f, 66.f, 26.f, 26.f}; }
    R lenBox() const { return {328.f, 66.f, 70.f, 26.f}; }
    R lenPlus() const { return {400.f, 66.f, 26.f, 26.f}; }
    R speedBtn() const { return {434.f, 66.f, 92.f, 26.f}; }
    R rotL() const { return {534.f, 66.f, 28.f, 26.f}; }
    R rotR() const { return {564.f, 66.f, 28.f, 26.f}; }
    R fillBtn() const { return {600.f, 66.f, 58.f, 26.f}; }
    R clearLaneBtn() const { return {662.f, 66.f, 62.f, 26.f}; }
    R copyLaneBtn() const { return {728.f, 66.f, 60.f, 26.f}; }
    R pasteLaneBtn() const { return {792.f, 66.f, 60.f, 26.f}; }
    float rowY(int l) const { return 100.f + l * 46.f; }
    R laneBtn(int l) const { return {24.f, rowY(l), 104.f, 42.f}; }
    R bypassBtn(int l) const { return {130.f, rowY(l), 32.f, 20.f}; }
    R holdBtn(int l) const { return {130.f, rowY(l) + 22.f, 32.f, 20.f}; }
    R stepCell(int l, int i) const { return {168.f + i * 42.6f, rowY(l), 39.f, 42.f}; }
    // right panel
    R gKnob(int i) const { return {874.f + i * 98.f, 64.f, 94.f, 120.f}; }
    R patBtn(int i) const { return {880.f + (i % 4) * 70.f, 214.f + (i / 4) * 34.f, 64.f, 30.f}; }
    R patCopy() const { return {880.f, 356.f, 88.f, 28.f}; }
    R patPaste() const { return {974.f, 356.f, 88.f, 28.f}; }
    R patClear() const { return {1068.f, 356.f, 88.f, 28.f}; }
    R rndLaneBtn() const { return {880.f, 398.f, 134.f, 34.f}; }
    R rndAllBtn() const { return {1022.f, 398.f, 134.f, 34.f}; }
    R undoBtn() const { return {880.f, 440.f, 276.f, 34.f}; }
    // bottom: selected lane
    R lcdR() const { return {24.f, 506.f, 330.f, 192.f}; }
    R knobR(int k) const { return {366.f + k * 100.f, 524.f, 96.f, 120.f}; }
    R lockClearBtn() const { return {246.f, 514.f, 98.f, 24.f}; }
    R presetName() const { return {870.f, 516.f, 134.f, 80.f}; }
    R presetBtn() const { return {1010.f, 516.f, 150.f, 36.f}; }
    R saveBtn() const { return {1010.f, 558.f, 150.f, 36.f}; }
    R gripR() const { return {W - 22.f, H - 22.f, 22.f, 22.f}; }
    // overlays
    R viewBox() const { return {240.f, 96.f, 700.f, 520.f}; }
    R skinRow(int i) const { return {264.f + (i % 2) * 330.f, 150.f + (i / 2) * 148.f, 316.f, 136.f}; }
    R zoomBtn(int i) const { return {264.f + i * 109.f, 486.f, 101.f, 36.f}; }
    R presetsBox() const { return {80.f, 60.f, 1020.f, 600.f}; }
    R presetItem(int col, int row) const { return {100.f + col * 500.f, 130.f + row * 30.f, 480.f, 27.f}; }
    R presetsClose() const { return {980.f, 606.f, 100.f, 36.f}; }
    R saveBox() const { return {290.f, 200.f, 600.f, 270.f}; }
    R saveField() const { return {320.f, 290.f, 540.f, 52.f}; }
    R saveOk() const { return {620.f, 400.f, 240.f, 40.f}; }
    R saveCancel() const { return {320.f, 400.f, 140.f, 40.f}; }
    R licBox() const { return {290.f, 170.f, 600.f, 330.f}; }
    R licLoad() const { return {320.f, 420.f, 190.f, 40.f}; }
    R licPaste() const { return {520.f, 420.f, 190.f, 40.f}; }
    R licClose() const { return {720.f, 420.f, 140.f, 40.f}; }
    R updBox() const { return {290.f, 150.f, 600.f, 380.f}; }
    R updAuto() const { return {320.f, 400.f, 170.f, 34.f}; }
    R updAutoInst() const { return {496.f, 400.f, 190.f, 34.f}; }
    R updLic() const { return {692.f, 400.f, 168.f, 34.f}; }
    R updMain() const { return {320.f, 456.f, 250.f, 40.f}; }
    R updSkip() const { return {580.f, 456.f, 150.f, 40.f}; }
    R updClose() const { return {740.f, 456.f, 120.f, 40.f}; }

    // ------------------------------------------------------------------ drawing primitives (same as the other Kinetik plug-ins)
    void useFont(int f)
    {
        if (f >= fnSerif && f < kFontCount) fontFaceId(fonts[f]); else fontFace(NANOVG_DEJAVU_SANS_TTF);
    }
    void box(const R& r, const Color& c, float rad = -1.f) { beginPath(); roundedRect(r.x, r.y, r.w, r.h, rad < 0 ? th->radius : rad); fillColor(c); fill(); }
    void frame(const R& r, const Color& c, float rad = -1.f, float wdt = 1.f)
    {
        beginPath(); roundedRect(r.x + 0.5f, r.y + 0.5f, r.w - 1, r.h - 1, rad < 0 ? th->radius : rad); strokeColor(c); strokeWidth(wdt); stroke();
    }
    static float textSize(float size, int font) { const float s = std::max(size, 9.5f); return font == fnMono ? s * 1.13f * 1.08f : s * 1.08f; }
    void label(float x, float y, const char* s, float size, const Color& c, int align = ALIGN_CENTER | ALIGN_MIDDLE, int font = -1)
    {
        if (s == nullptr || *s == '\0') return;
        const int f = font < 0 ? th->fontLabel : font;
        useFont(f); fontSize(textSize(size, f));
        textLetterSpacing(f == fnMono ? th->labelSpacing * (size / 12.f) : 0.f);
        fillColor(c); textAlign(align); text(x, y, s, nullptr);
        textLetterSpacing(0.f);
    }
    void number(float x, float y, const char* s, float size, const Color& c, int align = ALIGN_CENTER | ALIGN_MIDDLE, int font = -1)
    {
        if (s == nullptr || *s == '\0') return;
        const int f = font < 0 ? th->fontNum : font;
        useFont(f);
        float sz = f == fnSerif || f == fnSerifItalic ? size * 1.25f : (f == fnBlack ? size * 0.95f : size);
        if (size <= 16.f) sz *= 1.15f;
        fontSize(sz); textAlign(align);
        if (th->glow) { fontBlur(sz * 0.18f); fillColor(Color(c, Color(0, 0, 0, 0.f), 0.35f)); text(x, y, s, nullptr); fontBlur(0.f); }
        fillColor(c); text(x, y, s, nullptr);
    }
    enum Icon { icPlay, icStop };
    void iconLabel(float cx, float cy, Icon ic, const char* s, float size, const Color& c)
    {
        const int f = th->fontButton;
        useFont(f); fontSize(textSize(size, f));
        textLetterSpacing(f == fnMono ? th->labelSpacing * (size / 12.f) : 0.f);
        DGL_NAMESPACE::Rectangle<float> bnd; textBounds(0, 0, s, nullptr, bnd);
        textLetterSpacing(0.f);
        const float isz = size * 0.62f, gap = size * 0.55f, total = isz + gap + bnd.getWidth();
        const float x = cx - total * 0.5f;
        beginPath();
        if (ic == icPlay) { moveTo(x, cy - isz * 0.55f); lineTo(x + isz * 0.9f, cy); lineTo(x, cy + isz * 0.55f); closePath(); }
        else rect(x, cy - isz * 0.45f, isz * 0.9f, isz * 0.9f);
        fillColor(c); fill();
        label(x + isz + gap, cy, s, size, c, ALIGN_LEFT | ALIGN_MIDDLE, f);
    }
    void panelBox(const R& r) { box(r, th->panel, th->radius + 2.f); frame(r, th->panelEdge, th->radius + 2.f, th->panelEdgeWidth); }
    void button(const R& r, const char* s, bool on, float size = 11.5f)
    {
        if (on) box(r, th->selFill); else { box(r, th->btnFill); frame(r, th->btnEdge, -1.f, 1.f); }
        label(r.cx(), r.cy() + 1, s, size, on ? th->selText : th->btnText, ALIGN_CENTER | ALIGN_MIDDLE, th->fontButton);
    }
    void overlayBox(const R& r)
    {
        beginPath(); rect(0, 0, W, H); fillColor(th->overlay); fill();
        box(r, th->panel, th->radius + 3.f); frame(r, th->panelEdge, th->radius + 3.f, th->panelEdgeWidth + 0.5f);
    }
    Color accentInk() const { return th->logo == lgRiso ? th->text : th->accent; }
    Color onColor() const { return th->logo == lgNeon ? Color(236, 36, 50) : th->accent; }   // lit steps (NEON red, as KDSP-16)
    void drawKnob(const R& r, float v01, const char* name, const std::string& val, bool locked, bool highlight)
    {
        const float cx = r.cx(), cy = r.y + 44.f, rad = 26.f;
        const float a0 = kPi * 0.75f, a1 = kPi * 2.25f;
        v01 = clampf(v01, 0.f, 1.f);
        const Color hi = locked ? th->lock : th->knobArc;
        const float a = a0 + (a1 - a0) * v01;
        switch (th->knob) {
        case ksTicks: {
            for (int i = 0; i < 25; ++i) {
                const float t = i / 24.f, ang = a0 + (a1 - a0) * t; const bool lit = t <= v01 + 1e-4f;
                beginPath(); circle(cx + std::cos(ang) * (rad + 3), cy + std::sin(ang) * (rad + 3), lit ? 1.6f : 1.1f); fillColor(lit ? hi : th->knobTrack); fill();
            }
            if (th->glow) { beginPath(); circle(cx, cy, rad - 3); fillPaint(radialGradient(cx, cy, rad - 8, rad + 8, Color(hi, Color(0, 0, 0, 0.f), 0.75f), Color(0, 0, 0, 0.f))); fill(); }
            beginPath(); circle(cx, cy, rad - 5); fillColor(th->knobBody); fill();
            strokeColor(locked ? th->lock : th->knobRing); strokeWidth(locked ? 2.f : 1.2f); stroke();
            beginPath(); moveTo(cx + std::cos(a) * 4, cy + std::sin(a) * 4); lineTo(cx + std::cos(a) * (rad - 9), cy + std::sin(a) * (rad - 9));
            strokeColor(locked ? th->lock : th->knobPointer); strokeWidth(2.4f); lineCap(ROUND); stroke();
            break;
        }
        case ksArcThick: {
            beginPath(); arc(cx, cy, rad, a0, a1, CW); strokeColor(th->knobTrack); strokeWidth(6.f); lineCap(BUTT); stroke();
            beginPath(); arc(cx, cy, rad, a0, a0 + (a1 - a0) * std::max(0.001f, v01), CW); strokeColor(hi); strokeWidth(6.f); stroke();
            beginPath(); circle(cx, cy, rad - 6); fillColor(locked ? th->lock : th->knobBody); fill();
            strokeColor(th->knobRing); strokeWidth(1.2f); stroke();
            beginPath(); moveTo(cx + std::cos(a) * 6, cy + std::sin(a) * 6); lineTo(cx + std::cos(a) * (rad - 9), cy + std::sin(a) * (rad - 9));
            strokeColor(th->knobPointer); strokeWidth(2.4f); lineCap(ROUND); stroke();
            break;
        }
        default: {
            beginPath(); circle(cx, cy, rad + 4); fillColor(th->inner); fill();
            beginPath(); arc(cx, cy, rad, a0, a1, CW); strokeColor(th->knobTrack); strokeWidth(5.f); stroke();
            beginPath(); arc(cx, cy, rad, a0, a0 + (a1 - a0) * std::max(0.001f, v01), CW); strokeColor(hi); strokeWidth(5.f); stroke();
            beginPath(); circle(cx, cy, rad - 7); fillColor(highlight ? Color(th->knobBody, Color(255, 255, 255), 0.1f) : th->knobBody); fill();
            beginPath(); moveTo(cx + std::cos(a) * (rad - 18), cy + std::sin(a) * (rad - 18)); lineTo(cx + std::cos(a) * (rad - 8), cy + std::sin(a) * (rad - 8));
            strokeColor(th->knobPointer); strokeWidth(3.f); stroke();
            break;
        }
        }
        label(cx, r.y + 88.f, name, 12.f, locked ? th->lock : th->text);
        number(cx, r.y + 106.f, val.c_str(), 14.f, locked ? th->lock : (th->fontNum == fnSans ? th->dim : th->text));
    }
    void drawLogo()
    {
        drawOrbitMark(*this, 14.f, 7.f, 46.f, th->logo);
        const char* nm = "KFX-8";
        useFont(fnLogo); fontSize(30.f); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        const float nx = 68.f, ny = 23.f;
        if (th->glow) { fontBlur(10.f); fillColor(Color(th->accent, Color(0, 0, 0, 0.f), 0.45f)); text(nx, ny, nm, nullptr); fontBlur(0.f); }
        fillColor(th->glow ? Color(232, 236, 255) : th->text); text(nx, ny, nm, nullptr);
        const int ust = updater ? (int)updater->getState() : -1;
        if (ust == upd::Checker::stAvailable || ust == upd::Checker::stInstalling || ust == upd::Checker::stInstalled) {
            const std::string v = updater->manifest().version;
            const std::string t = ust == upd::Checker::stAvailable ? "UPDATE " + v + " AVAILABLE - CLICK"
                                : ust == upd::Checker::stInstalling ? "INSTALLING UPDATE " + v + "..." : "UPDATED TO " + v + " - REOPEN TO USE IT";
            label(nx + 1, 45.f, t.c_str(), 9.5f, accentInk(), ALIGN_LEFT | ALIGN_MIDDLE, fnMono);
        } else if (licInfo.valid || !lic::kDemoModeEnabled) label(nx + 1, 45.f, "KINETIK  SEQUENCED MULTI-EFFECT", 9.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE, fnMono);
        else label(nx + 1, 45.f, "DEMO - CLICK TO REGISTER", 9.5f, accentInk(), ALIGN_LEFT | ALIGN_MIDDLE, fnMono);
    }

    // ------------------------------------------------------------------ main draw
    void onNanoDisplay() override
    {
        sx = getWidth() / W;
        scale(sx, sx);
        if (!haloImage.isValid()) makeTextures();
        beginPath(); rect(0, 0, W, H); fillColor(th->bg); fill();
        if (th->halftone && haloImage.isValid()) {
            Paint p = imagePattern(0, 0, 6.f, 6.f, 0.f, haloImage, 1.f); p.innerColor = th->dots; p.outerColor = th->dots;
            beginPath(); rect(0, 0, W, H); fillPaint(p); fill();
        }
        drawHeader();
        drawLanes();
        drawRight();
        drawEditor();
        if (overlay == ovPresets) drawPresets();
        if (overlay == ovSkins) drawSkins();
        if (overlay == ovSave) drawSave();
        if (overlay == ovLicense) drawLicense();
        if (overlay == ovUpdate) drawUpdate();
        if (tipShown && overlay == ovNone) drawTip();
        drawGrip();
    }
    void makeTextures()
    {
        uint8_t h[6 * 6 * 4] = {};
        for (int i : {7, 8, 13, 14}) { h[i * 4] = 255; h[i * 4 + 1] = 255; h[i * 4 + 2] = 255; h[i * 4 + 3] = 140; }
        haloImage = createImageFromRGBA(6, 6, h, IMAGE_REPEAT_X | IMAGE_REPEAT_Y | (1 << 5));
    }

    void lcdCell(const R& r, const char* top, const char* val)
    {
        box(r, th->lcdBg); frame(r, th->panelEdge, -1.f, th->panelEdgeWidth);
        label(r.cx(), r.y + 11, top, 9.f, th->lcdDim);
        number(r.cx(), r.y + 28, val, 16.f, th->lcdText);
    }
    void drawHeader()
    {
        drawLogo();
        char b[48];
        std::snprintf(b, sizeof(b), "%d", pat().length); lcdCell(patLenBox(), "LENGTH", b);
        std::snprintf(b, sizeof(b), "%d%%", pat().swing); lcdCell(swingBox(), "SWING", b);
        const int ep = editPattern(), pp = (int)std::lround(gp(gOUT_PATTERN));
        const bool running = gp(gOUT_STEP) >= 0.f;
        const R pr = patBox();
        box(pr, th->lcdBg); frame(pr, th->panelEdge, -1.f, th->panelEdgeWidth);
        label(pr.x + 10, pr.cy() + 1, "PAT", 10.f, th->lcdDim, ALIGN_LEFT | ALIGN_MIDDLE);
        if (running && pp != ep) std::snprintf(b, sizeof(b), "A%02d>A%02d", pp + 1, ep + 1); else std::snprintf(b, sizeof(b), "A%02d", ep + 1);
        number(pr.x + pr.w - 10, pr.cy() + 1, b, running && pp != ep ? 16.f : 20.f, th->lcdText, ALIGN_RIGHT | ALIGN_MIDDLE);
        const bool play = gp(gPLAY) > 0.5f;
        const R pb = playBtn();
        if (play) box(pb, th->selFill); else { box(pb, th->btnFill); frame(pb, th->btnEdge); }
        iconLabel(pb.cx(), pb.cy() + 1, play ? icStop : icPlay, play ? "STOP" : "PLAY", 12.f, play ? th->selText : th->btnText);
        const R tb = tempoBox();
        box(tb, th->lcdBg); frame(tb, th->panelEdge, -1.f, th->panelEdgeWidth);
        std::snprintf(b, sizeof(b), "%.1f", gp(gTEMPO));
        number(tb.x + 70, tb.cy() + 1, b, 20.f, th->lcdText, ALIGN_RIGHT | ALIGN_MIDDLE);
        label(tb.x + 75, tb.cy() + 2, "BPM", 10.f, th->lcdDim, ALIGN_LEFT | ALIGN_MIDDLE);
        const R sk = skinBtn();
        box(sk, th->btnFill); frame(sk, th->btnEdge);
        label(sk.cx(), sk.y + 12, "SKIN / SIZE", 9.f, th->dim);
        std::snprintf(b, sizeof(b), "%s %d%%", th->name, zoomPercent());
        label(sk.cx(), sk.y + 28, b, 10.5f, th->text);
    }

    void drawLanes()
    {
        panelBox({14.f, 58.f, 846.f, 430.f});
        Pattern& p = pat();
        char b[32];
        // toolbar: step pages, then the selected lane's settings
        for (int i = 0; i < 4; ++i) {
            std::snprintf(b, sizeof(b), "%d-%d", i * 16 + 1, i * 16 + 16);
            bool used = false; for (int l = 0; l < kLanes; ++l) used = used || p.len[l] > i * 16;
            const R r = stepPageBtn(i);
            if (stepPg == i) button(r, b, true, 10.f);
            else { box(r, th->btnFill); frame(r, th->btnEdge); label(r.cx(), r.cy() + 1, b, 10.f, used ? th->btnText : th->faint); }
        }
        button(lenMinus(), "-", false, 12.f); button(lenPlus(), "+", false, 12.f);
        box(lenBox(), th->lcdBg); std::snprintf(b, sizeof(b), "LEN %d", p.len[selLane]);
        label(lenBox().cx(), lenBox().cy() + 1, b, 10.f, th->lcdText);
        std::snprintf(b, sizeof(b), "SPEED %s", kSpeedNames[p.speed[selLane] % kSpeeds]);
        button(speedBtn(), b, p.speed[selLane] != 0, 10.f);
        button(rotL(), "<", false, 11.f); button(rotR(), ">", false, 11.f);
        button(fillBtn(), "FILL", false, 10.f);
        button(clearLaneBtn(), "CLEAR", false, 10.f);
        button(copyLaneBtn(), "COPY", false, 10.f);
        button(pasteLaneBtn(), "PASTE", false, 10.f);
        const uint32_t act = (uint32_t)std::lround(gp(gOUT_ACTIVE));
        for (int l = 0; l < kLanes; ++l) {
            const bool sel = l == selLane, byp = bank->mute[l] != 0;
            const R lb = laneBtn(l);
            const Color fg = sel ? th->selText : (byp ? th->faint : th->text);
            if (sel) box(lb, th->selFill); else { box(lb, th->btnFill); frame(lb, th->btnEdge); }
            label(lb.x + 9, lb.y + 15, kLaneNames[l], 10.5f, fg, ALIGN_LEFT | ALIGN_MIDDLE);
            std::snprintf(b, sizeof(b), "%d  %s", p.len[l], kSpeedNames[p.speed[l] % kSpeeds]);
            label(lb.x + 9, lb.y + 32, b, 9.f, sel ? th->selText : th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
            const bool lit = (act >> l) & 1;
            beginPath(); circle(lb.x + lb.w - 11, lb.y + 13, 4.5f);
            fillColor(lit ? (th->logo == lgNeon ? Color(125, 255, 90) : th->accent) : Color(fg, Color(0, 0, 0, 0.f), 0.25f)); fill();
            // bypass + momentary push
            const R bb = bypassBtn(l);
            if (byp) box(bb, th->lock, 3.f); else { box(bb, th->btnFill, 3.f); frame(bb, th->btnEdge, 3.f); }
            label(bb.cx(), bb.cy() + 1, byp ? "OFF" : "ON", 8.5f, byp ? th->selText : th->btnText);
            const R hb = holdBtn(l);
            const bool held = (holdMask >> l) & 1;
            if (held) box(hb, th->accent, 3.f); else { box(hb, th->btnFill, 3.f); frame(hb, th->btnEdge, 3.f); }
            label(hb.cx(), hb.cy() + 1, "PUSH", 8.5f, held ? th->accentText : th->btnText);
            // steps
            const int now = laneStepNow(l);
            for (int i = 0; i < 16; ++i) {
                const int s = stepPg * 16 + i;
                const R r = stepCell(l, i);
                const bool inLen = s < p.len[l];
                const bool on = p.on[l][s] != 0;
                const Color cellCol = inLen ? ((i / 4) % 2 ? th->stepAlt : th->stepFill) : Color(th->stepOff, th->bg, 0.4f);
                box(r, cellCol, 3.f);
                if (on && inLen) {
                    const Color lc = onColor();
                    if (th->glow) { beginPath(); rect(r.x - 3, r.y - 3, r.w + 6, r.h + 6); fillPaint(boxGradient(r.x, r.y, r.w, r.h, 3, 8, Color(lc, Color(0, 0, 0, 0.f), 0.45f), Color(0, 0, 0, 0.f))); fill(); }
                    box({r.x + 3, r.y + 3, r.w - 6, r.h - 6}, lc, 2.f);
                } else if (inLen) box({r.x + 4, r.y + 4, r.w - 8, 5}, th->ledOff, 1.f);
                if (inLen && p.hasLocks(l, s)) { beginPath(); circle(r.x + r.w - 8, r.y + r.h - 8, 2.8f); fillColor(on ? th->selText : th->lock); fill(); }
                if (s == now && inLen) frame(r, th->text, 3.f, 2.f);
                if (lockStep == s && lockLane == l) frame({r.x - 2, r.y - 2, r.w + 4, r.h + 4}, th->lock, 4.f, 2.f);
                if (l == 0) {   // step numbers above the first lane are in the toolbar row; beat marks on the first lane
                    (void)0;
                }
            }
        }
        // beat numbers under the grid
        for (int i = 0; i < 16; ++i) {
            const R r = stepCell(kLanes - 1, i);
            std::snprintf(b, sizeof(b), "%d", stepPg * 16 + i + 1);
            label(r.cx(), r.y + r.h + 11, b, 8.5f, i % 4 == 0 ? th->dim : th->faint);
        }
    }

    void drawRight()
    {
        panelBox({868.f, 58.f, 298.f, 430.f});
        static const char* names[3] = {"DRY/WET", "INPUT", "OUTPUT"};
        static const int gs[3] = {gDRYWET, gINPUT, gOUTPUT};
        for (int i = 0; i < 3; ++i) {
            const float v = gp(gs[i]);
            char b[24];
            if (i == 0) std::snprintf(b, sizeof(b), "%d%%", (int)std::lround(v / 127.f * 100.f));
            else { const float db = trimDb(v); if (db < -100.f) std::snprintf(b, sizeof(b), "-INF"); else std::snprintf(b, sizeof(b), "%+.1fdB", db); }
            drawKnob(gKnob(i), v / 127.f, names[i], b, false, drag == dkGlobal && dragIndex == i);
        }
        label(884, 200, "PATTERNS", 10.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        if (!patMsg.empty()) label(1156, 200, patMsg.c_str(), 9.5f, th->lock, ALIGN_RIGHT | ALIGN_MIDDLE);
        const int ep = editPattern(), pp = (int)std::lround(gp(gOUT_PATTERN));
        const bool running = gp(gOUT_STEP) >= 0.f;
        for (int i = 0; i < kPatterns; ++i) {
            char b[8]; std::snprintf(b, sizeof(b), "A%02d", i + 1);
            const R r = patBtn(i);
            if (i == ep) box(r, th->selFill); else { box(r, th->btnFill); frame(r, th->btnEdge); }
            bool any = false; for (int l = 0; l < kLanes && !any; ++l) any = !bank->patterns[i].laneEmpty(l);
            number(r.cx(), r.cy() + 1, b, 14.f, i == ep ? th->selText : (any ? th->text : th->faint));
            if (running && i == pp && pp != ep) frame(r, th->accent, -1.f, 2.f);
        }
        button(patCopy(), "COPY", false, 10.f); button(patPaste(), "PASTE", false, 10.f); button(patClear(), "CLEAR", false, 10.f);
        button(rndLaneBtn(), "RANDOM LANE", false, 10.f);
        button(rndAllBtn(), "RANDOM ALL", false, 10.f);
        const bool can = !undoStack.empty();
        if (can) button(undoBtn(), ("UNDO " + undoStack.back().what).c_str(), false, 10.5f);
        else { box(undoBtn(), th->btnFill); frame(undoBtn(), Color(th->btnEdge, th->bg, 0.5f)); label(undoBtn().cx(), undoBtn().cy() + 1, "UNDO", 10.5f, th->faint); }
    }

    // the selected lane: LCD (name, what it does, a picture of its knobs) + its 5 knobs
    void drawEditor()
    {
        panelBox({14.f, 496.f, 1152.f, 212.f});
        const int l = selLane;
        const R lc = lcdR();
        box(lc, th->lcdBg); frame(lc, th->panelEdge, -1.f, th->panelEdgeWidth);
        number(lc.x + 12, lc.y + 22, kLaneNames[l], 20.f, th->lcdText, ALIGN_LEFT | ALIGN_MIDDLE);
        if (lockMode() && lockLane == l) {
            char b[32]; std::snprintf(b, sizeof(b), "LOCK STEP %d", lockStep + 1);
            label(lc.x + 12, lc.y + 46, b, 10.5f, th->lock, ALIGN_LEFT | ALIGN_MIDDLE);
            frame(lockClearBtn(), th->lock); label(lockClearBtn().cx(), lockClearBtn().cy() + 1, "CLEAR LOCKS", 9.5f, th->lock);
        } else {
            fontFace(NANOVG_DEJAVU_SANS_TTF); fontSize(11.5f); fillColor(th->lcdDim); textAlign(ALIGN_LEFT | ALIGN_TOP); textLineHeight(1.2f);
            textBox(lc.x + 12, lc.y + 38, lc.w - 24, kLaneDefs[l].help, nullptr);
        }
        drawLaneGraphic(l, {lc.x + 12, lc.y + 84, lc.w - 24, lc.h - 96});
        for (int k = 0; k < kLaneParams; ++k) {
            const float v = knobValue(k);
            const bool locked = k < kKnobs && lockMode() && lockLane == l && pat().locks[l][lockStep][k] != kNoLock;
            drawKnob(knobR(k), v / 127.f, k < kKnobs ? kLaneDefs[l].knob[k] : "MIX", laneValueText(l, k, v), locked,
                     drag == dkKnob && dragIndex == k);
        }
        // preset name + buttons
        const R pn = presetName();
        label(pn.x + pn.w - 4, pn.y + 10, "PRESET", 9.f, th->dim, ALIGN_RIGHT | ALIGN_MIDDLE);
        number(pn.x + pn.w - 4, pn.y + 34, presetNameStr.c_str(), 15.f, th->text, ALIGN_RIGHT | ALIGN_MIDDLE);
        box(presetBtn(), th->btnFill); frame(presetBtn(), th->text, -1.f, 1.5f);
        label(presetBtn().cx(), presetBtn().cy() + 1, "PRESETS", 11.5f, th->text);
        button(saveBtn(), "SAVE AS PRESET", false, 10.5f);
    }
    float knobValue(int k)
    {
        const int l = selLane;
        if (k < kKnobs && lockMode() && lockLane == l) { const uint8_t v = pat().locks[l][lockStep][k]; if (v != kNoLock) return v; }
        return lp(l, k);
    }

    // a small picture of what the lane does with its current knobs, over a quarter-note grid
    void drawLaneGraphic(int l, const R& g)
    {
        float p[kKnobs]; for (int k = 0; k < kKnobs; ++k) p[k] = knobValue(k);
        const Color ink = th->lcdText, dim = withAlpha(th->lcdDim, 0.6f);
        beginPath(); rect(g.x, g.y + g.h - 1, g.w, 1); fillColor(dim); fill();
        for (int i = 1; i < 4; ++i) { beginPath(); rect(g.x + g.w * i / 4.f, g.y, 1, g.h); fillColor(withAlpha(dim, 0.5f)); fill(); }
        const float bw = g.w / 4.f;   // one beat
        auto bar = [&](float x0, float w, float hh, const Color& c) { beginPath(); rect(g.x + x0, g.y + g.h - hh, std::max(1.f, w), hh); fillColor(c); fill(); };
        switch (l) {
        case lnStutter: {
            const float len = (float)kStutSize[bin8(p[0])] * bw, gate = 0.1f + p[3] / 127.f * 0.9f;
            float a = 1.f; int n = 0;
            for (float x = 0; x < g.w - 1 && n < 64; x += len, ++n) {
                bar(x, std::min(len * gate, g.w - x) - 1.f, (g.h - 6) * a, n == 0 ? dim : ink);
                if (n > 0) a *= p[1] / 127.f;
            }
            break;
        }
        case lnReverse: {
            const float len = std::min((float)kRevLen[bin8(p[0])] * bw, g.w);
            for (float x0 = 0; x0 < g.w - 1; x0 += len) {
                beginPath(); moveTo(g.x + x0, g.y + 4); lineTo(g.x + std::min(g.w, x0 + len), g.y + g.h - 2); lineTo(g.x + x0, g.y + g.h - 2); closePath();
                fillColor(withAlpha(ink, 0.8f)); fill();
            }
            break;
        }
        case lnTape: {
            const double T = 0.0625 * std::pow(64.0, p[0] / 127.0) * bw;
            const float curve = std::pow(2.5f, (p[1] - 64.f) / 64.f * 1.3f);
            beginPath();
            for (int i = 0; i <= 60; ++i) {
                const float x = g.w * i / 60.f, u = (float)(x / T);
                const float r = u >= 1.f ? 0.f : std::pow(1.f - u, curve);
                const float y = g.y + g.h - 2 - (g.h - 8) * r;
                if (i == 0) moveTo(g.x + x, y); else lineTo(g.x + x, y);
            }
            strokeColor(ink); strokeWidth(2.f); stroke();
            break;
        }
        case lnPitch: {
            const float st = (p[0] - 64.f) / 64.f * 24.f;
            const float y0 = g.cy(), y1 = g.cy() - st / 24.f * (g.h * 0.45f);
            beginPath(); rect(g.x, y0, g.w * 0.3f, 1.5f); fillColor(dim); fill();
            beginPath(); moveTo(g.x + g.w * 0.3f, y0); lineTo(g.x + g.w * 0.4f, y1); lineTo(g.x + g.w, y1); strokeColor(ink); strokeWidth(2.f); stroke();
            char b[16]; std::snprintf(b, sizeof(b), "%+d", (int)std::lround(st));
            label(g.x + g.w - 4, y1 + (st >= 0 ? 10.f : -10.f), b, 10.f, ink, ALIGN_RIGHT | ALIGN_MIDDLE);
            break;
        }
        case lnCrush: {
            const int hold = std::max(1, (int)std::lround(std::pow(64.f, p[1] / 127.f)));
            const float levels = std::max(1.f, std::pow(2.f, 15.f - p[0] / 127.f * 15.f) / 4096.f * 8.f);
            const int n = 120;
            beginPath();
            float held = 0.f;
            for (int i = 0; i < n; ++i) {
                if (i % std::max(1, hold / 2) == 0) held = std::round(std::sin(i / (float)n * kTwoPi * 2.f) * levels) / levels;
                const float x = g.x + g.w * i / (n - 1), y = g.cy() - held * (g.h * 0.42f);
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            strokeColor(ink); strokeWidth(1.8f); stroke();
            break;
        }
        case lnFilter: {
            const int type = bin3(p[2]);
            const float fc = p[0] / 127.f, q = resoQ(p[1], 0.6f, 18.f);
            beginPath();
            for (int i = 0; i <= 80; ++i) {
                const float x = i / 80.f, d = (x - fc) * 6.f;
                float mag;
                if (type == 0) mag = 1.f / std::sqrt(1.f + std::pow(std::exp(d), 4.f));
                else if (type == 2) mag = 1.f / std::sqrt(1.f + std::pow(std::exp(-d), 4.f));
                else mag = std::exp(-d * d * 0.8f);
                mag *= 1.f + (q - 0.7f) * 0.08f * std::exp(-d * d * 4.f);
                const float y = g.y + g.h - 2 - (g.h - 6) * clampf(mag * 0.8f, 0.f, 1.f);
                if (i == 0) moveTo(g.x + g.w * x, y); else lineTo(g.x + g.w * x, y);
            }
            strokeColor(ink); strokeWidth(2.f); stroke();
            break;
        }
        case lnGate: {
            const float per = (float)kGateRate[bin8(p[0])] * bw, duty = 0.05f + p[1] / 127.f * 0.9f, depth = p[3] / 127.f;
            for (float x = 0; x < g.w - 1; x += per) {
                bar(x, per * duty, g.h - 6, ink);
                if (depth < 0.99f) bar(x + per * duty, per * (1.f - duty), (g.h - 6) * (1.f - depth), dim);
            }
            break;
        }
        case lnDelay: {
            const float t = (float)kDelTime[bin8(p[0])] * bw, fb = p[1] / 127.f * 0.95f;
            float a = 1.f; int n = 0;
            for (float x = 0; x < g.w - 1 && n < 40; x += t, ++n) { bar(x, 4.f, (g.h - 6) * a, n == 0 ? dim : ink); a *= n == 0 ? 1.f : fb; if (n > 0 && a < 0.02f) break; }
            break;
        }
        }
    }
    static Color withAlpha(Color c, float a) { c.alpha *= a; return c; }

    // ------------------------------------------------------------------ presets (factory + user files)
    static std::string userDir()
    {
       #ifdef _WIN32
        const std::string base = fs::env("APPDATA");
        if (base.empty()) return std::string();
        std::string d = base + "\\Kinetik"; fs::makeDir(d); d += "\\KFX-8"; fs::makeDir(d); d += "\\Presets"; fs::makeDir(d);
        return d + "\\";
       #else
        const std::string base = fs::env("HOME");
        if (base.empty()) return std::string();
       #ifdef __APPLE__
        std::string d = base + "/Library/Application Support";
       #else
        std::string d = base + "/.config";
       #endif
        fs::makeDir(d); d += "/Kinetik"; fs::makeDir(d); d += "/KFX-8"; fs::makeDir(d); d += "/Presets"; fs::makeDir(d);
        return d + "/";
       #endif
    }
    void scanUser()
    {
        userPresets.clear();
        const std::string d = userDir();
        if (d.empty()) return;
        for (const std::string& f : fs::list(d, ".kfx8")) userPresets.push_back(f.substr(0, f.size() - 5));
        std::sort(userPresets.begin(), userPresets.end());
    }
    void drawPresets()
    {
        const R bx = presetsBox();
        overlayBox(bx);
        number(100, 92, "Presets", 22.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        label(100, 120, "FACTORY", 10.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        label(600, 120, "YOURS", 10.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        for (int i = 0; i < kNumPresets && i < 15; ++i) {
            const R r = presetItem(0, i);
            const bool sel = presetNameStr == kPresets[i].name;
            if (sel) box(r, th->selFill); else box(r, th->btnFill);
            number(r.x + 10, r.cy() + 1, kPresets[i].name, 13.f, sel ? th->selText : th->text, ALIGN_LEFT | ALIGN_MIDDLE);
            label(r.x + r.w - 10, r.cy() + 1, kPresets[i].desc, 8.5f, sel ? th->selText : th->dim, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        if (userPresets.empty()) label(600, 145, "none yet: SAVE AS PRESET stores the knobs and the 16 patterns", 9.5f, th->faint, ALIGN_LEFT | ALIGN_MIDDLE);
        for (int i = 0; i < (int)userPresets.size() && i < 15; ++i) {
            const R r = presetItem(1, i);
            const bool sel = presetNameStr == userPresets[i];
            if (sel) box(r, th->selFill); else box(r, th->btnFill);
            number(r.x + 10, r.cy() + 1, userPresets[i].c_str(), 13.f, sel ? th->selText : th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        if (!presetMsg.empty()) label(100, 624, presetMsg.c_str(), 10.f, th->lock, ALIGN_LEFT | ALIGN_MIDDLE);
        button(presetsClose(), "CLOSE", true, 11.f);
    }
    void presetsClick(float x, float y)
    {
        for (int i = 0; i < kNumPresets && i < 15; ++i) if (presetItem(0, i).in(x, y)) { loadFactory(i); return; }
        for (int i = 0; i < (int)userPresets.size() && i < 15; ++i) if (presetItem(1, i).in(x, y)) { loadUser(userPresets[i]); return; }
        if (presetsClose().in(x, y) || !presetsBox().in(x, y)) { overlay = ovNone; presetMsg.clear(); }
        repaint();
    }
    void loadFactory(int i)
    {
        snapshot("preset");
        std::vector<float> nv(values, values + kTotalParams);
        std::unique_ptr<Bank> nb(new Bank());
        buildPreset(i, nv.data(), *nb);
        applyState(nv.data(), nb.get());
        setParamGesture(kGlobalBase + gPATTERN, 0.f);
        presetNameStr = kPresets[i].name; lockStep = -1; stepPg = 0;
        repaint();
    }
    void loadUser(const std::string& name)
    {
        FILE* fp = fs::open(userDir() + name + ".kfx8", "rb");
        if (!fp) { presetMsg = "Could not open " + name; repaint(); return; }
        std::string all; char buf[4096]; size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) all.append(buf, n);
        std::fclose(fp);
        if (all.rfind("KFX8-PRESET", 0) != 0) { presetMsg = "Not a KFX-8 preset: " + name; repaint(); return; }
        snapshot("preset");
        std::vector<float> nv(values, values + kTotalParams);
        const size_t bankPos = all.find("\nBANK\n");
        const std::string head = all.substr(0, bankPos == std::string::npos ? all.size() : bankPos);
        size_t st = 0;
        while (st < head.size()) {
            size_t en = head.find('\n', st); if (en == std::string::npos) en = head.size();
            int idx; float v;
            if (std::sscanf(head.c_str() + st, "V %d %f", &idx, &v) == 2 && idx >= 0 && idx < kGlobalBase + gOUT_STEP && std::isfinite(v))
                nv[idx] = idx == kGlobalBase + gTEMPO ? clampf(v, 40.f, 240.f) : clampf(v, 0.f, 127.f);
            st = en + 1;
        }
        std::unique_ptr<Bank> nb(new Bank());
        if (bankPos == std::string::npos || !deserializeBank(*nb, all.c_str() + bankPos + 6)) nb.reset(new Bank(*bank));
        applyState(nv.data(), nb.get());
        presetNameStr = name; lockStep = -1;
        repaint();
    }
    void openSave() { saveName = presetNameStr == "Init" ? "" : presetNameStr; saveConfirm = false; overlay = ovSave; repaint(); }
    bool saveExists() const { return std::find(userPresets.begin(), userPresets.end(), saveName) != userPresets.end(); }
    void commitSave()
    {
        while (!saveName.empty() && saveName.back() == ' ') saveName.pop_back();
        if (saveName.empty()) return;
        if (saveExists() && !saveConfirm) { saveConfirm = true; repaint(); return; }
        FILE* fp = fs::open(userDir() + saveName + ".kfx8", "wb");
        if (!fp) { presetMsg = "Could not write the preset."; overlay = ovNone; repaint(); return; }
        std::fprintf(fp, "KFX8-PRESET 1\nNAME %s\n", saveName.c_str());
        for (int i = 0; i < kGlobalBase + gOUT_STEP; ++i) {
            if (i == kGlobalBase + gPLAY || i == kGlobalBase + gPATTERN) continue;
            std::fprintf(fp, "V %d %.4f\n", i, values[i]);
        }
        std::fprintf(fp, "BANK\n%s", serializeBank(*bank).c_str());
        std::fclose(fp);
        presetNameStr = saveName; scanUser();
        overlay = ovNone; repaint();
    }
    void drawSave()
    {
        const R bx = saveBox();
        overlayBox(bx);
        number(320, 236, "Save as preset", 22.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        label(320, 270, "Name (type on the keyboard, Enter = save, Esc = cancel)", 10.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        const R f = saveField();
        box(f, th->lcdBg); frame(f, th->lcdText, -1.f, 1.5f);
        number(f.x + 16, f.cy() + 1, saveName.c_str(), 20.f, th->lcdText, ALIGN_LEFT | ALIGN_MIDDLE);
        if (saveExists()) label(320, 366, saveConfirm ? "Click SAVE again to replace it." : "A preset with this name exists: it will be replaced.", 10.5f, th->lock, ALIGN_LEFT | ALIGN_MIDDLE);
        else label(320, 366, "Saves every knob and the 16 patterns.", 10.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        button(saveCancel(), "CANCEL", false, 11.f);
        button(saveOk(), saveConfirm ? "REPLACE" : "SAVE", true, 12.f);
    }
    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (overlay != ovSave) return false;
        const uint c = ev.character;
        if (c >= 32 && c < 127 && saveName.size() < 28) {
            const char ch = (char)c;
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' || ch == '_') { saveName.push_back(ch); saveConfirm = false; repaint(); }
        }
        return true;
    }

    // ------------------------------------------------------------------ skins window
    void drawSkins()
    {
        overlayBox(viewBox());
        number(264, 124, "Skin and size", 22.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        label(916, 124, "saved with the project and for new instances", 10.f, th->dim, ALIGN_RIGHT | ALIGN_MIDDLE);
        for (int i = 0; i < kNumThemes; ++i) {
            const R r = skinRow(i);
            const Theme& t = kThemes[i];
            box(r, t.bg, 4.f); frame(r, i == skin ? th->accent : th->btnEdge, 4.f, i == skin ? 3.f : 1.f);
            const Theme* keep = th; th = &t;
            number(r.x + 14, r.y + 22, t.name, 16.f, t.text, ALIGN_LEFT | ALIGN_MIDDLE);
            box({r.x + 14, r.y + 40, 120, 52}, t.lcdBg);
            label(r.x + 22, r.y + 52, "MIX", 8.f, t.lcdDim, ALIGN_LEFT | ALIGN_MIDDLE);
            number(r.x + 22, r.y + 74, "75", 20.f, t.lcdText, ALIGN_LEFT | ALIGN_MIDDLE);
            drawKnob({r.x + 140, r.y + 26, 80, 110}, 0.6f, "", "", false, false);
            for (int k = 0; k < 4; ++k) {
                const R st{r.x + 224 + (k % 2) * 40.f, r.y + 40 + (k / 2) * 44.f, 36, 40};
                box(st, t.stepFill, 3.f);
                if (k == 0 || k == 3) box({st.x + 3, st.y + 3, st.w - 6, st.h - 6}, onColor(), 2.f);
            }
            th = keep;
        }
        label(264, 468, "WINDOW SIZE", 11.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        static const int zooms[6] = {80, 100, 120, 140, 160, 200};
        for (int i = 0; i < 6; ++i) { char b[16]; std::snprintf(b, sizeof(b), "%d%%", zooms[i]); button(zoomBtn(i), b, std::abs(zoomPercent() - zooms[i]) <= 2, 12.f); }
        label(264, 546, "You can also drag the corner at the bottom right of the window to any size.", 10.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
    }
    void drawGrip()
    {
        const Color c = drag == dkResize ? th->accent : Color(th->dim, th->bg, 0.2f);
        for (int i = 0; i < 3; ++i) {
            const float o = 6.f + i * 5.f;
            beginPath(); moveTo(W - 3, H - o); lineTo(W - o, H - 3); strokeColor(c); strokeWidth(1.6f); lineCap(ROUND); stroke();
        }
    }

    // ------------------------------------------------------------------ About / updates and licence windows
    void drawUpdate()
    {
        const R bx = updBox();
        overlayBox(bx);
        using C = upd::Checker;
        const C::State st = updater->getState();
        const upd::Manifest m = updater->manifest();
        number(320, bx.y + 36, "Kinetik KFX-8", 22.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        label(860, bx.y + 36, (std::string("version ") + KN_VERSION).c_str(), 11.f, th->dim, ALIGN_RIGHT | ALIGN_MIDDLE);
        std::string l1, l2;
        switch (st) {
        case C::stChecking: l1 = "Checking for updates..."; break;
        case C::stAvailable: l1 = "Version " + m.version + " is available."; l2 = m.notes; break;
        case C::stUpToDate: l1 = "You have the latest version."; break;
        case C::stDownloading: l1 = "Downloading and checking the installer..."; break;
        case C::stInstalling: l1 = "Installing version " + m.version + "..."; l2 = "Accept the system prompt. Keep working: the music does not stop."; break;
        case C::stInstalled: l1 = "Version " + m.version + " is installed."; l2 = "It is used the next time the plug-in loads."; break;
        case C::stOpened: l1 = "The installer is open."; l2 = "Close your music software before you finish the installation, then open it again."; break;
        case C::stFailed: l1 = updater->message(); l2 = "Check your internet connection and try again."; break;
        default: l1 = "Click CHECK NOW to look for a new version."; break;
        }
        label(320, bx.y + 86, l1.c_str(), 13.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        if (!l2.empty()) { fontFace(NANOVG_DEJAVU_SANS_TTF); fontSize(12.5f); fillColor(th->dim); textAlign(ALIGN_LEFT | ALIGN_TOP); textBox(320, bx.y + 104, 540, l2.c_str(), nullptr); }
        button(updAuto(), updater->autoCheck() ? "AUTO CHECK: ON" : "AUTO CHECK: OFF", updater->autoCheck(), 10.5f);
        button(updAutoInst(), updater->autoInstall() ? "AUTO INSTALL: ON" : "AUTO INSTALL: OFF", updater->autoInstall(), 10.5f);
        button(updLic(), licInfo.valid ? "LICENCE: REGISTERED" : "ENTER A LICENCE", false, 10.5f);
        const bool avail = st == C::stAvailable, busy = st == C::stChecking || st == C::stDownloading || st == C::stInstalling;
        button(updMain(), busy ? "PLEASE WAIT..." : (avail ? (m.url.empty() ? "OPEN DOWNLOAD PAGE" : "DOWNLOAD & INSTALL") : "CHECK NOW"), avail, 11.f);
        if (avail) button(updSkip(), "SKIP THIS ONE", false, 10.5f);
        button(updClose(), "CLOSE", false, 11.f);
    }
    bool updateClick(float x, float y)
    {
        using C = upd::Checker;
        const C::State st = updater->getState();
        if (updMain().in(x, y)) { if (st == C::stAvailable) updater->install(); else if (st != C::stChecking && st != C::stDownloading && st != C::stInstalling) updater->check(true); }
        else if (st == C::stAvailable && updSkip().in(x, y)) updater->skipThisVersion();
        else if (updAuto().in(x, y)) updater->setAutoCheck(!updater->autoCheck());
        else if (updAutoInst().in(x, y)) updater->setAutoInstall(!updater->autoInstall());
        else if (updLic().in(x, y)) { overlay = ovLicense; licMsg.clear(); }
        else if (updClose().in(x, y) || !updBox().in(x, y)) overlay = ovNone;
        repaint(); return true;
    }
    void drawLicense()
    {
        const R bx = licBox();
        overlayBox(bx);
        number(320, bx.y + 36, licInfo.valid ? "Registered" : "Register KFX-8", 22.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
        if (licInfo.valid) {
            label(320, bx.y + 82, ("Licensed to " + licInfo.name).c_str(), 13.f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
            label(320, bx.y + 108, licInfo.email.c_str(), 11.f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
            label(320, bx.y + 134, ("Licence " + licInfo.product + "  -  order " + licInfo.order + "  -  " + licInfo.issued).c_str(), 10.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        } else {
            label(320, bx.y + 80, "This copy runs in demo mode: the effect drops out for 3 seconds every 45 seconds.", 10.5f, th->text, ALIGN_LEFT | ALIGN_MIDDLE);
            label(320, bx.y + 106, "After your purchase you receive a licence file by e-mail.", 10.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
            label(320, bx.y + 130, "Load that file, or copy its text and press PASTE LICENSE (or Ctrl/Cmd+V).", 10.5f, th->dim, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        label(320, bx.y + 210, licMsg.c_str(), 10.5f, th->lock, ALIGN_LEFT | ALIGN_MIDDLE);
        button(licLoad(), "LOAD LICENSE FILE", false, 10.5f);
        button(licPaste(), "PASTE LICENSE", false, 10.5f);
        button(licClose(), "CLOSE", true, 11.f);
    }
    void applyLicenseText(const std::string& t)
    {
        if (t.empty()) { licMsg = "Nothing to read."; repaint(); return; }
        const lic::Info i = lic::install(t, "KFX-8");
        if (i.valid) { licInfo = i; licMsg = "Licence installed. The demo limits are removed."; setState("cmd", "lic"); }
        else licMsg = i.error;
        repaint();
    }
    void pasteLicense()
    {
        size_t n = 0;
        const void* d = getWindow().getClipboard(n);
        if (d == nullptr || n == 0) { licMsg = "The clipboard is empty. Copy the licence text first."; repaint(); return; }
        std::string t((const char*)d, n);
        while (!t.empty() && t.back() == '\0') t.pop_back();
        applyLicenseText(t);
    }
    void uiFileBrowserSelected(const char* filename) override
    {
        if (filename == nullptr) return;
        std::string text;
        if (!lic::readFile(filename, text)) { licMsg = "Could not read that file."; repaint(); return; }
        applyLicenseText(text);
    }
    bool licenseClick(float x, float y)
    {
        if (licLoad().in(x, y)) {
            FileBrowserOptions o; o.title = "Load your Kinetik licence";
            if (!openFileBrowser(o)) licMsg = "The file browser is not available here: use PASTE LICENSE.";
            repaint(); return true;
        }
        if (licPaste().in(x, y)) { pasteLicense(); return true; }
        if (licClose().in(x, y) || !licBox().in(x, y)) { overlay = ovNone; licMsg.clear(); repaint(); }
        return true;
    }

    // ------------------------------------------------------------------ whole-state helpers (presets, random, undo)
    void applyState(const float* nv, const Bank* nb)
    {
        const bool was = restoring; restoring = true;
        for (int i = 0; i < kGlobalBase + gOUT_STEP; ++i) {
            if (i == kGlobalBase + gPLAY || i == kGlobalBase + gPATTERN) continue;
            if (std::fabs(values[i] - nv[i]) > 1e-4f) setParamGesture((uint32_t)i, nv[i]);
        }
        if (nb) { *bank = *nb; setState("bank", serializeBank(*bank).c_str()); }
        restoring = was;
        repaint();
    }
    struct Snap { std::vector<float> values; std::string bank; std::string what; };
    static double nowSec() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    Snap capture(const char* what) const { Snap s; s.values.assign(values, values + kTotalParams); s.bank = serializeBank(*bank); s.what = what; return s; }
    void snapshot(const char* what)
    {
        if (restoring) return;
        undoStack.push_back(capture(what));
        if (undoStack.size() > 40) undoStack.pop_front();
        redoStack.clear();
        lastSnapAt = nowSec(); lastSnapWhat = what;
    }
    void autoSnap(const char* what, double window)
    {
        if (restoring) return;
        const double t = nowSec();
        if (t - lastSnapAt < 0.15 || (lastSnapWhat == what && t - lastSnapAt < window)) { if (lastSnapWhat == what) lastSnapAt = t; return; }
        snapshot(what);
    }
    void restoreSnap(const Snap& s)
    {
        std::unique_ptr<Bank> b(new Bank()); deserializeBank(*b, s.bank.c_str());
        std::vector<float> v = s.values; applyState(v.data(), b.get());
        lastSnapAt = -10.0; lastSnapWhat.clear();
    }
    void undo() { if (undoStack.empty()) return; Snap s = std::move(undoStack.back()); undoStack.pop_back(); redoStack.push_back(capture(s.what.c_str())); restoreSnap(s); repaint(); }
    void redo() { if (redoStack.empty()) return; Snap s = std::move(redoStack.back()); redoStack.pop_back(); undoStack.push_back(capture(s.what.c_str())); restoreSnap(s); repaint(); }

    // random lane: a sparse rhythm in the lane's length + knob values in a musical range + a few locks
    uint32_t rnd = 0x1234567u;
    float urand() { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; return (rnd & 0xFFFFFF) / 16777216.f; }
    void randomLane(std::vector<float>& nv, Pattern& P, int l)
    {
        static const float lo[kLanes][kKnobs] = {{8, 60, 40, 60}, {16, 40, 10, 70}, {30, 20, 40, 0}, {40, 50, 20, 0}, {30, 10, 0, 50}, {20, 20, 0, 30}, {16, 30, 0, 80}, {32, 30, 30, 40}};
        static const float hi[kKnobs] = {120, 127, 110, 127};
        P.clearLane(l);
        const float density = 0.15f + urand() * 0.3f;
        const int n = P.len[l];
        for (int s = 0; s < n; ++s) if (urand() < density) {
            P.on[l][s] = 1;
            if (urand() < 0.4f) { const int k = (int)(urand() * kKnobs) % kKnobs; P.locks[l][s][k] = (uint8_t)(lo[l][k] + urand() * (hi[k] - lo[l][k])); }
        }
        for (int k = 0; k < kKnobs; ++k) nv[lpIndex(l, k)] = std::round(lo[l][k] + urand() * (hi[k] - lo[l][k]));
        if (l == lnFilter) nv[lpIndex(l, 2)] = (float)((int)(urand() * 3) * 48);
        nv[lpIndex(l, 4)] = l == lnDelay ? 60.f + urand() * 50.f : 90.f + urand() * 37.f;
    }
    void randomize(bool all)
    {
        rnd ^= (uint32_t)(nowSec() * 1000.0) * 2654435761u; if (!rnd) rnd = 1;
        snapshot(all ? "random all" : "random lane");
        std::vector<float> nv(values, values + kTotalParams);
        std::unique_ptr<Bank> nb(new Bank(*bank));
        Pattern& P = nb->patterns[editPattern()];
        if (all) {
            for (int l = 0; l < kLanes; ++l) P.clearLane(l);
            const int count = 3 + (int)(urand() * 3);
            for (int c = 0; c < count; ++c) randomLane(nv, P, (int)(urand() * kLanes) % kLanes);
        } else randomLane(nv, P, selLane);
        applyState(nv.data(), nb.get());
        presetNameStr = all ? "(random)" : presetNameStr;
    }

    // ------------------------------------------------------------------ tooltips
    std::string tipAt(float x, float y)
    {
        for (int l = 0; l < kLanes; ++l) {
            if (laneBtn(l).in(x, y)) return std::string(kLaneNames[l]) + ": " + kLaneDefs[l].help + ". Click to edit its knobs; the number is its length and speed.";
            if (bypassBtn(l).in(x, y)) return "ON / OFF: switch the whole lane off (bypass) without losing its steps.";
            if (holdBtn(l).in(x, y)) return "PUSH: the effect plays while you hold the mouse button (MIDI notes C2 to G2 do the same for lanes 1 to 8).";
            for (int i = 0; i < 16; ++i) if (stepCell(l, i).in(x, y))
                return "Click: the effect is on during this step (drag to paint). Right-click: lock knobs on this step. Lit steps next to each other make one continuous gesture.";
        }
        for (int i = 0; i < 4; ++i) if (stepPageBtn(i).in(x, y)) return "Show steps 1-16, 17-32, 33-48 or 49-64 (lanes can be up to 64 steps).";
        if (lenBox().in(x, y) || lenMinus().in(x, y) || lenPlus().in(x, y)) return "Length of the selected lane (1-64 steps): lanes of different lengths drift against each other.";
        if (speedBtn().in(x, y)) return "Speed of the selected lane's steps (1x = 16ths). Click = next, right-click = previous.";
        if (rotL().in(x, y) || rotR().in(x, y)) return "Shift the selected lane's steps (and their locks) one step left or right.";
        if (fillBtn().in(x, y)) return "Light every step of the selected lane.";
        if (clearLaneBtn().in(x, y)) return "Clear the selected lane's steps and locks.";
        if (copyLaneBtn().in(x, y) || pasteLaneBtn().in(x, y)) return "Copy the selected lane's steps, locks, length and speed, then paste them on another lane or pattern.";
        if (patLenBox().in(x, y)) return "Pattern length (16ths): a pattern chosen while playing starts when this length ends. Drag or scroll.";
        if (swingBox().in(x, y)) return "Swing: delays every second 16th (50% = straight). Drag or scroll.";
        if (playBtn().in(x, y)) return "Internal play when your music software is stopped (KFX-8 follows its transport when it plays).";
        if (tempoBox().in(x, y)) return "Internal tempo (your music software's tempo is used when it plays). Drag or scroll.";
        for (int i = 0; i < 3; ++i) if (gKnob(i).in(x, y))
            return i == 0 ? "DRY/WET: how much of the effects you hear (100% = only the effect chain)." : (i == 1 ? "INPUT: level into the effects." : "OUTPUT: level out of KFX-8.");
        for (int i = 0; i < kPatterns; ++i) if (patBtn(i).in(x, y)) return "Choose the pattern (while playing it starts at the end of the current one). MIDI notes C3 to D#4 choose A01 to A16.";
        if (rndLaneBtn().in(x, y)) return "Random steps, locks and knobs for the selected lane (UNDO restores).";
        if (rndAllBtn().in(x, y)) return "A random pattern on three to five lanes (UNDO restores).";
        if (undoBtn().in(x, y)) return "Undo the last change (Ctrl/Cmd+Z). Right-click = redo.";
        for (int k = 0; k < kLaneParams; ++k) if (knobR(k).in(x, y))
            return k == kKnobs ? "MIX: how much of this effect replaces the sound while it is on." : "Drag to set, double-click to reset. With a step locked (right-click a step) it sets that step only.";
        if (presetBtn().in(x, y)) return "Factory presets and your own.";
        if (saveBtn().in(x, y)) return "Save the knobs and the 16 patterns as a preset of your own.";
        if (skinBtn().in(x, y)) return "Skin and window size. Right-click = next skin.";
        if (logoArea().in(x, y)) return "About KFX-8, updates and licence.";
        return std::string();
    }
    void drawTip()
    {
        if (tipText.empty()) return;
        const float wMax = 300.f;
        fontFace(NANOVG_DEJAVU_SANS_TTF); fontSize(12.f); textAlign(ALIGN_LEFT | ALIGN_TOP); textLineHeight(1.25f);
        float b[4]; textBoxBounds(0, 0, wMax, tipText.c_str(), nullptr, b);
        const float w = b[2] - b[0] + 20.f, h = b[3] - b[1] + 14.f;
        float x = hovX + 14.f, y = hovY + 18.f;
        if (x + w > W - 6) x = W - 6 - w;
        if (y + h > H - 6) y = hovY - h - 10.f;
        beginPath(); roundedRect(x + 2, y + 3, w, h, 6); fillColor(Color(0, 0, 0, 0.25f)); fill();
        box({x, y, w, h}, th->panel, 6.f); frame({x, y, w, h}, th->text, 6.f);
        fillColor(th->text); textBox(x + 10, y + 7, wMax, tipText.c_str(), nullptr);
    }

    // ------------------------------------------------------------------ editing
    void setStep(int l, int s, bool on)
    {
        if ((pat().on[l][s] != 0) == on) return;
        char b[48]; std::snprintf(b, sizeof(b), "s %d %d %d %d", editPattern(), l, s, on ? 1 : 0); sendCmd(b);
    }
    void setKnob(int k, float v)
    {
        v = clampf(v, 0.f, 127.f);
        if (k < kKnobs && lockMode() && lockLane == selLane) {
            const int iv = (int)std::lround(v);
            if (pat().locks[selLane][lockStep][k] == iv) return;
            char b[48]; std::snprintf(b, sizeof(b), "k %d %d %d %d %d", editPattern(), selLane, lockStep, k, iv); sendCmd(b);
        } else setParam(lpIndex(selLane, k), v);
        repaint();
    }
    bool knobIsHostParam(int k) const { return !(k < kKnobs && lockMode() && lockLane == selLane); }
    void setLaneLen(int n) { pat().len[selLane] = (uint8_t)clampi(n, 1, kSteps); sendLaneSettings(selLane); stepPg = std::min(stepPg, (pat().len[selLane] - 1) / 16); repaint(); }
    void setHold(uint32_t m) { holdMask = m; char b[32]; std::snprintf(b, sizeof(b), "hold %u", m); setState("cmd", b); repaint(); }
    void showPatMsg(const std::string& m) { patMsg = m; patMsgUntil = nowSec() + 2.0; repaint(); }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (overlay == ovLicense) {
            if (ev.press && (ev.key == 'v' || ev.key == 'V') && (ev.mod & (kModifierControl | kModifierSuper))) pasteLicense();
            else if (ev.press && ev.key == kKeyEscape) { overlay = ovNone; licMsg.clear(); repaint(); }
            return true;
        }
        if (overlay == ovSave) {
            if (ev.press) {
                if (ev.key == kKeyEscape) { overlay = ovNone; repaint(); }
                else if (ev.key == kKeyEnter || ev.key == '\n') commitSave();
                else if (ev.key == kKeyBackspace || ev.key == kKeyDelete) { if (!saveName.empty()) { saveName.pop_back(); saveConfirm = false; repaint(); } }
            }
            return true;
        }
        if (ev.press && (ev.mod & (kModifierControl | kModifierSuper)) && overlay == ovNone) {
            if (ev.key == 'z' || ev.key == 'Z') { if (ev.mod & kModifierShift) redo(); else undo(); return true; }
            if (ev.key == 'y' || ev.key == 'Y') { redo(); return true; }
        }
        if (ev.press && ev.key == kKeyEscape) { if (overlay != ovNone) overlay = ovNone; else lockStep = -1; repaint(); return true; }
        return false;
    }

    bool onMouse(const MouseEvent& ev) override
    {
        const float x = ev.pos.getX() / sx, y = ev.pos.getY() / sx;
        if (tipShown) { tipShown = false; repaint(); }
        tipBlockUntil = nowSec() + 1.0;
        if (!ev.press) {
            if (drag == dkKnob && knobIsHostParam(dragIndex)) editParameter(lpIndex(selLane, dragIndex), false);
            if (drag == dkGlobal) { static const int gs[3] = {gDRYWET, gINPUT, gOUTPUT}; editParameter(kGlobalBase + gs[dragIndex], false); }
            if (drag == dkTempo) editParameter(kGlobalBase + gTEMPO, false);
            if (drag == dkPatLen || drag == dkSwing) sendPatternSettings();
            if (drag == dkLaneLen) sendLaneSettings(selLane);
            if (drag == dkHold) setHold(0);
            if (drag == dkResize) saveView();
            if (drag != dkNone) { drag = dkNone; repaint(); }
            return true;
        }
        const bool dbl = (ev.time - lastClickTime) < 350 && std::fabs(x - lastX) < 4 && std::fabs(y - lastY) < 4;
        lastClickTime = ev.time; lastX = x; lastY = y;
        const bool right = ev.button == kMouseButtonRight;

        if (gripR().in(x, y) && overlay == ovNone) { drag = dkResize; resizeStartW = getWidth(); grabX = ev.pos.getX(); grabY = ev.pos.getY(); return true; }
        if (overlay == ovPresets) { presetsClick(x, y); return true; }
        if (overlay == ovLicense) return licenseClick(x, y);
        if (overlay == ovUpdate) return updateClick(x, y);
        if (overlay == ovSave) {
            if (saveOk().in(x, y)) commitSave(); else if (saveCancel().in(x, y) || !saveBox().in(x, y)) { overlay = ovNone; repaint(); }
            return true;
        }
        if (overlay == ovSkins) {
            static const int zooms[6] = {80, 100, 120, 140, 160, 200};
            for (int i = 0; i < kNumThemes; ++i) if (skinRow(i).in(x, y)) { setSkin(i, true); return true; }
            for (int i = 0; i < 6; ++i) if (zoomBtn(i).in(x, y)) { setZoom(zooms[i] / 100.0); saveView(); return true; }
            if (!viewBox().in(x, y)) { overlay = ovNone; repaint(); }
            return true;
        }
        if (logoArea().in(x, y)) { overlay = ovUpdate; repaint(); return true; }
        if (skinBtn().in(x, y)) { if (right) setSkin((skin + 1) % kNumThemes, true); else overlay = ovSkins; repaint(); return true; }
        if (playBtn().in(x, y)) { setParamGesture(kGlobalBase + gPLAY, gp(gPLAY) > 0.5f ? 0.f : 1.f); repaint(); return true; }
        if (tempoBox().in(x, y)) { drag = dkTempo; dragY = y; dragStart = gp(gTEMPO); editParameter(kGlobalBase + gTEMPO, true); return true; }
        if (patLenBox().in(x, y)) { drag = dkPatLen; dragY = y; dragStart = pat().length; return true; }
        if (swingBox().in(x, y)) { drag = dkSwing; dragY = y; dragStart = pat().swing; return true; }

        // lanes
        for (int l = 0; l < kLanes; ++l) {
            if (laneBtn(l).in(x, y)) { selLane = l; if (lockLane != l) lockStep = -1; repaint(); return true; }
            if (bypassBtn(l).in(x, y)) { char b[24]; std::snprintf(b, sizeof(b), "U %d %d", l, bank->mute[l] ? 0 : 1); sendCmd(b); repaint(); return true; }
            if (holdBtn(l).in(x, y)) { selLane = l; drag = dkHold; setHold(1u << l); return true; }
            for (int i = 0; i < 16; ++i) {
                if (!stepCell(l, i).in(x, y)) continue;
                const int s = stepPg * 16 + i;
                if (s >= pat().len[l]) return true;
                if (right) {
                    if (lockStep == s && lockLane == l) lockStep = -1; else { lockStep = s; lockLane = l; selLane = l; }
                    repaint(); return true;
                }
                selLane = l;
                if (lockLane != l) lockStep = -1;
                paintVal = pat().on[l][s] == 0;
                setStep(l, s, paintVal);
                drag = dkPaint; dragIndex = l;
                repaint(); return true;
            }
        }
        for (int i = 0; i < 4; ++i) if (stepPageBtn(i).in(x, y)) { stepPg = i; repaint(); return true; }
        if (lenMinus().in(x, y)) { setLaneLen(pat().len[selLane] - 1); return true; }
        if (lenPlus().in(x, y)) { setLaneLen(pat().len[selLane] + 1); return true; }
        if (lenBox().in(x, y)) { drag = dkLaneLen; dragY = y; dragStart = pat().len[selLane]; return true; }
        if (speedBtn().in(x, y)) { pat().speed[selLane] = (uint8_t)((pat().speed[selLane] + (right ? kSpeeds - 1 : 1)) % kSpeeds); sendLaneSettings(selLane); repaint(); return true; }
        if (rotL().in(x, y) || rotR().in(x, y)) { char b[48]; std::snprintf(b, sizeof(b), "rot %d %d %d", editPattern(), selLane, rotL().in(x, y) ? -1 : 1); sendCmd(b); repaint(); return true; }
        if (fillBtn().in(x, y)) {
            std::string row(kSteps, '0'); for (int s = 0; s < pat().len[selLane]; ++s) row[s] = '1';
            sendCmd("S " + std::to_string(editPattern()) + " " + std::to_string(selLane) + " " + row); repaint(); return true;
        }
        if (clearLaneBtn().in(x, y)) { char b[32]; std::snprintf(b, sizeof(b), "cll %d %d", editPattern(), selLane); sendCmd(b); lockStep = -1; repaint(); return true; }
        if (copyLaneBtn().in(x, y)) { laneClip.reset(new Pattern(pat())); laneClipLane = selLane; showPatMsg(std::string(kLaneNames[selLane]) + " copied"); return true; }
        if (pasteLaneBtn().in(x, y) && laneClip) {
            const Pattern& c = *laneClip; const int p = editPattern(), l = selLane;
            std::string cmd, row(kSteps, '0');
            for (int s = 0; s < kSteps; ++s) row[s] = c.on[laneClipLane][s] ? '1' : '0';
            cmd += "cll " + std::to_string(p) + " " + std::to_string(l) + "\n";
            cmd += "S " + std::to_string(p) + " " + std::to_string(l) + " " + row + "\n";
            cmd += "L " + std::to_string(p) + " " + std::to_string(l) + " " + std::to_string(c.len[laneClipLane]) + " " + std::to_string(c.speed[laneClipLane]) + "\n";
            if (laneClipLane == l)   // locks only make sense on the same effect
                for (int s = 0; s < kSteps; ++s) for (int k = 0; k < kKnobs; ++k)
                    if (c.locks[l][s][k] != kNoLock) cmd += "k " + std::to_string(p) + " " + std::to_string(l) + " " + std::to_string(s) + " " + std::to_string(k) + " " + std::to_string(c.locks[l][s][k]) + "\n";
            cmd.pop_back(); sendLines(cmd); repaint(); return true;
        }
        // right panel
        for (int i = 0; i < 3; ++i) if (gKnob(i).in(x, y)) {
            static const int gs[3] = {gDRYWET, gINPUT, gOUTPUT};
            if (dbl) { setParamGesture(kGlobalBase + gs[i], globalDefault(gs[i])); repaint(); return true; }
            drag = dkGlobal; dragIndex = i; dragY = y; dragStart = gp(gs[i]); editParameter(kGlobalBase + gs[i], true); return true;
        }
        for (int i = 0; i < kPatterns; ++i) if (patBtn(i).in(x, y)) { setParamGesture(kGlobalBase + gPATTERN, (float)i); lockStep = -1; repaint(); return true; }
        if (patCopy().in(x, y)) { patClip = editPattern(); showPatMsg("A" + std::to_string(patClip + 1) + " copied"); return true; }
        if (patPaste().in(x, y) && patClip >= 0) { char b[32]; std::snprintf(b, sizeof(b), "cp %d %d", patClip, editPattern()); sendCmd(b); showPatMsg("pasted"); return true; }
        if (patClear().in(x, y)) { char b[24]; std::snprintf(b, sizeof(b), "cl %d", editPattern()); sendCmd(b); lockStep = -1; repaint(); return true; }
        if (rndLaneBtn().in(x, y)) { randomize(false); return true; }
        if (rndAllBtn().in(x, y)) { randomize(true); return true; }
        if (undoBtn().in(x, y)) { if (right) redo(); else undo(); return true; }
        // editor
        if (lockMode() && lockLane == selLane && lockClearBtn().in(x, y)) { char b[40]; std::snprintf(b, sizeof(b), "kc %d %d %d", editPattern(), selLane, lockStep); sendCmd(b); repaint(); return true; }
        for (int k = 0; k < kLaneParams; ++k) if (knobR(k).in(x, y)) {
            if (dbl) {
                if (!knobIsHostParam(k)) { char b[48]; std::snprintf(b, sizeof(b), "k %d %d %d %d %d", editPattern(), selLane, lockStep, k, (int)kNoLock); sendCmd(b); }
                else setParamGesture(lpIndex(selLane, k), laneParamDefault(selLane, k));
                repaint(); return true;
            }
            drag = dkKnob; dragIndex = k; dragY = y; dragStart = knobValue(k);
            if (knobIsHostParam(k)) editParameter(lpIndex(selLane, k), true);
            else if (pat().locks[selLane][lockStep][k] == kNoLock) setKnob(k, dragStart);
            repaint(); return true;
        }
        if (presetBtn().in(x, y)) { scanUser(); overlay = ovPresets; repaint(); return true; }
        if (saveBtn().in(x, y)) { scanUser(); openSave(); return true; }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        {
            const float hx = ev.pos.getX() / sx, hy = ev.pos.getY() / sx;
            if (std::fabs(hx - hovX) > 3.f || std::fabs(hy - hovY) > 3.f) { hovX = hx; hovY = hy; hovSince = nowSec(); tipNone = false; if (tipShown) { tipShown = false; repaint(); } }
        }
        if (drag == dkNone) return false;
        if (drag == dkResize) {
            const double dx = ev.pos.getX() - grabX, dy = (ev.pos.getY() - grabY) * (double)W / H;
            const double nw = resizeStartW + (dx + dy) * 0.5;
            const double z = std::max(0.6, std::min(3.0, nw / (W * baseScale())));
            const uint w = (uint)std::lround(W * baseScale() * z), h = (uint)std::lround(H * baseScale() * z);
            if (std::abs((int)w - (int)getWidth()) >= 3) setSize(w, h);
            return true;
        }
        const float x = ev.pos.getX() / sx, y = ev.pos.getY() / sx;
        const float sens = (ev.mod & kModifierShift) ? 0.12f : 0.6f;
        const float v = dragStart + (dragY - y) * sens;
        switch (drag) {
        case dkKnob: setKnob(dragIndex, v); break;
        case dkGlobal: { static const int gs[3] = {gDRYWET, gINPUT, gOUTPUT}; setParam(kGlobalBase + gs[dragIndex], clampf(v, 0.f, 127.f)); repaint(); break; }
        case dkTempo: setParam(kGlobalBase + gTEMPO, clampf(std::round((dragStart + (dragY - y) * ((ev.mod & kModifierShift) ? 0.05f : 0.5f)) * 10.f) / 10.f, 40.f, 240.f)); repaint(); break;
        case dkPatLen: pat().length = (uint8_t)clampi((int)std::lround(dragStart + (dragY - y) * 0.15f), 1, kSteps); repaint(); break;
        case dkSwing: pat().swing = (uint8_t)clampi((int)std::lround(dragStart + (dragY - y) * 0.15f), 50, 80); repaint(); break;
        case dkLaneLen: pat().len[selLane] = (uint8_t)clampi((int)std::lround(dragStart + (dragY - y) * 0.15f), 1, kSteps); repaint(); break;
        case dkPaint: {
            const int l = dragIndex;
            for (int i = 0; i < 16; ++i) {
                const R r = stepCell(l, i);
                if (x >= r.x && x < r.x + r.w + 3.f) { const int s = stepPg * 16 + i; if (s < pat().len[l]) setStep(l, s, paintVal); break; }
            }
            repaint(); break;
        }
        default: break;
        }
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = ev.pos.getX() / sx, y = ev.pos.getY() / sx;
        const float d = ev.delta.getY() > 0 ? 1.f : (ev.delta.getY() < 0 ? -1.f : 0.f);
        if (d == 0.f || overlay != ovNone) return false;
        for (int k = 0; k < kLaneParams; ++k) if (knobR(k).in(x, y)) {
            if (knobIsHostParam(k)) { editParameter(lpIndex(selLane, k), true); setKnob(k, knobValue(k) + d); editParameter(lpIndex(selLane, k), false); }
            else setKnob(k, knobValue(k) + d);
            return true;
        }
        for (int i = 0; i < 3; ++i) if (gKnob(i).in(x, y)) { static const int gs[3] = {gDRYWET, gINPUT, gOUTPUT}; setParamGesture(kGlobalBase + gs[i], clampf(gp(gs[i]) + d, 0.f, 127.f)); repaint(); return true; }
        if (tempoBox().in(x, y)) { setParamGesture(kGlobalBase + gTEMPO, clampf(gp(gTEMPO) + d, 40.f, 240.f)); repaint(); return true; }
        if (patLenBox().in(x, y)) { pat().length = (uint8_t)clampi(pat().length + (int)d, 1, kSteps); sendPatternSettings(); repaint(); return true; }
        if (swingBox().in(x, y)) { pat().swing = (uint8_t)clampi(pat().swing + (int)d, 50, 80); sendPatternSettings(); repaint(); return true; }
        if (lenBox().in(x, y)) { setLaneLen(pat().len[selLane] + (int)d); return true; }
        return false;
    }

    void uiIdle() override
    {
        if (!tipShown && !tipNone && drag == dkNone && overlay == ovNone && hovX >= 0.f && nowSec() - hovSince > 0.8 && nowSec() > tipBlockUntil) {
            tipText = tipAt(hovX, hovY);
            if (!tipText.empty()) { tipShown = true; repaint(); } else tipNone = true;
        }
        flushPv();
        if (updater && (int)updater->getState() != updLastState) { updLastState = (int)updater->getState(); repaint(); }
        if (!patMsg.empty() && nowSec() > patMsgUntil) { patMsg.clear(); repaint(); }
        if (viewDirty && drag != dkResize && ++viewDirtyTicks > 30) saveView();
    }

private:
    float values[kTotalParams];
    std::unique_ptr<Bank> bank;
    float sx = 1.f;
    int skin = kDefaultSkin;
    const Theme* th = &kThemes[kDefaultSkin];
    FontId fonts[kFontCount] = {};
    NanoImage haloImage;
    int selLane = 0, stepPg = 0, lockLane = 0, lockStep = -1;
    Overlay overlay = ovNone;
    DragKind drag = dkNone; int dragIndex = 0; float dragY = 0, dragStart = 0; bool paintVal = true;
    uint lastClickTime = 0; float lastX = -100, lastY = -100;
    uint32_t holdMask = 0;
    int patClip = -1;
    std::unique_ptr<Pattern> laneClip; int laneClipLane = 0;
    std::string patMsg; double patMsgUntil = 0;
    std::string presetNameStr = "Init", presetMsg, saveName; bool saveConfirm = false;
    std::vector<std::string> userPresets;
    std::deque<Snap> undoStack, redoStack; bool restoring = false; double lastSnapAt = -10.0; std::string lastSnapWhat;
    std::unique_ptr<upd::Checker> updater; int updLastState = -1;
    lic::Info licInfo; std::string licMsg;
    bool viewDirty = false; int viewDirtyTicks = 0;
    double resizeStartW = 0, grabX = 0, grabY = 0;
    float hovX = -1.f, hovY = -1.f; double hovSince = 0, tipBlockUntil = 0; bool tipShown = false, tipNone = false; std::string tipText;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KfxUI)
};

UI* createUI() { return new KfxUI(); }

END_NAMESPACE_DISTRHO
