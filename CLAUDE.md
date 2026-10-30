# Kinetik KFX-8 — project memory for Claude

Kinetik KFX-8 is a sequenced multi-effect plug-in (VST3 / AU / CLAP, built with DPF and NanoVG): 9 effects on 9
parallel lanes of steps (0.1: "8 effets, 8 lignes en parallèle"; 0.2: new effect set + PERFORM lane, owner's requests). All DSP is original code. Family:
KDSP-16 (`Kozzzmo/kinetik-kdsp16`), KMSP-6, KJNG-2, KDYN-10; next products KBAS-1 (acid bass) and KGRN-4 (granular).
Repository: `Kozzzmo/kinetik-kfx8` (public, created 2026-10-27; first built locally in `/home/claude/kinetik-kfx8`, with `dpf` symlinked to the KDSP-16 checkout).

## Source map (`src/`) — 0.2.0 (the effect set of 0.1 was replaced at the owner's request)

| File | Role |
|---|---|
| `Params.hpp` | 9 lanes (`Lane`: DRIVE CRUSH FILTER COMP CHORUS FLANGER DELAY REVERB PERFORM; processing order = `Bank::order`, default `kDefaultOrder`), 64 steps, 16 patterns; per lane 7 knobs (`kLaneDefs`; knob 6 = MODEL on DELAY / REVERB / PERFORM, unused elsewhere = nullptr) + MIX = host params `lpIndex(l,k)` = l*8+k; globals after `kGlobalBase` = 72: DRYWET INPUT OUTPUT TEMPO PLAY PATTERN, LFO 1/2 (`lfoParam(i,k)`: WAVE RATE SYNC DEPTH DEST PHASE), ENV follower (SENS ATTACK RELEASE DEPTH DEST), outputs; DEST = 0 none / 1 + lpIndex; models (`kDelayModelNames`, `kReverbModelNames`, `kPerformModelNames`, `modelOf`), conditions `kCondNames`, lane modes `LaneMode` (STEPS / ALWAYS / 1-SHOT); value texts `laneValueText`, `knobText` (PERFORM depends on its model), `laneKnobName` |
| `Bank.hpp` | `Pattern` (master length, swing, per lane `len` / `speed`, `on`, `tie` (TIE: continues the previous step, `runStart` / `runLength`), `cond`, `locks[l][s][7]`), `Bank::mute`, `always` (= LaneMode), `order`, `chain`; header `KFX82` (KFX81 still read); commands `P L s S T t c k kc U A O C cl cll cp rot` |
| `Fx.hpp` | Drive (4 types), Crush, Filter (LP/BP/HP/notch, SWEEP across the step or the tied run, ENV follower), Comp, Chorus, Flanger (free or synced), Delay (5 models), Reverb (8-line FDN, 5 models: scale, plate diffusers, spring dispersion, shimmer octave-up loop), PERFORM: shared `History` + Stutter / Reverse / TapeStop from 0.1 |
| `Engine.hpp` | transport, pattern change at the master length (or `ChainPlayer`), per lane: step on its grid, condition decided at the step start (`condPasses`, loop count since `patStartBeat`, FILL), ties, 1-SHOT (`shotPending` on a hold press, plays `len` steps from the next step), ALWAYS; `applyLfos` (2 LFOs + envelope follower `envVal`) adds to the knob or lock of the destination per 32-sample segment; lanes processed in `bank.order`; tails (DELAY, REVERB) fed only while on |
| `KfxPlugin.cpp` | DPF glue; MIDI C2-G#2 hold lanes / start 1-SHOT lanes, A2 = FILL, C3-D#4 patterns; cmds `hold <mask>`, `fill 0/1` |
| `KfxUI.cpp` | 1180x720: lanes (mode STEPS / ALWAYS / 1-SHOT, ON/OFF, PUSH / SHOT, `#n` = place in the chain, TIE drawn as a joined bar, condition text), page buttons extend the lane (`showPage`), right panel (patterns + CHAIN, RANDOM LANE / ALL, FILL (HOLD), UNDO), editor: LCD + knobs (PERFORM names by model), `< EARLIER / LATER >` (order) or CONDITION in lock mode, MODE; tabs LFO1 / LFO2 / ENV / LANE EDITOR (`edLfo`, `modParam`), destination window `ovDest`, CHAIN window `ovChain` |
| `Presets.hpp` | 14 factory presets (`PB`: `knobs` = 6 knobs + MIX, `model`, `always`, `row`, `lock`) |
| shared (identical in the family, `scripts/check-shared.sh`) | `Skins.hpp`, `Fonts.hpp`, `Dsp.hpp`, `FsUtf8.hpp`, `Updater.*`, `License.hpp`, `LogoMark.hpp`, `Chain.hpp`, monocypher, `fonts/OFL-Unbounded.txt` |

User presets: `%APPDATA%\Kinetik\KFX-8\Presets\*.kfx8` (`KFX8-PRESET 1`, `NAME`, `V <param> <value>`, `BANK` + bank).
View file `KinetikKFX8-view.txt` ("skin zoom").

## Working with the owner (read first)

- The owner (GitHub **Kozzzmo**) writes in **French**: answer in French. UI, code, comments and README in **English**.
- Git: commit as `Kozzzmo <Kozzzmo@users.noreply.github.com>`. **No `Co-Authored-By` or any Claude attribution**.
- Proprietary code (`LICENSE`); permissive third-party code only, listed in `THIRD_PARTY_NOTICES.txt`.
- No other makers' names, graphics or trade dress in product text (Elektron, Torso...), no artist-named presets.
- Family look: Orbit logo + product name in Unbounded, skins NEON (default) / MIDNIGHT / RISO / CLASSIC, large text
  (`textSize` x1.13 x1.08 mono SemiBold, `number` x1.15 <= 16 px), NEON lit steps red, activity LEDs green.
- Licence product string `KFX-8` (or BUNDLE); `tools/make_license.py` knows KFX-8 / KBAS-1 / KGRN-4.

## Build, test

- `make` (dpf in `dpf/`; `./scripts/setup.sh` clones it at the family's pinned revision), Windows:
  `make WINDOWS=true CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++`.
- Tests: `tests/engine.cpp` (each lane changes the sound only on its steps, locks, bypass, DRY/WET, pattern change at
  the bar, MIDI hold when stopped), `tests/bank.cpp` (text round trip, commands, presets), `tests/license.cpp`,
  `tests/update.cpp`. VST3 validator: 47/47.
- CI workflow (`.github/workflows/build.yml`) adapted from KDSP-16 (no samples; auval `aumf KFX8 Kntk`); every push to `main`
  builds and publishes the release `v$(cat VERSION)`.

## Added in 0.3.0

- Side-chain: `DISTRHO_PLUGIN_NUM_INPUTS 4` (inputs 3 / 4 = `kAudioPortIsSidechain`, no port group), `DISTRHO_PLUGIN_EXTRA_IO
  { 2, 2 }` so AU also offers plain stereo; `ioChanged` stores the input count, `Engine::scL / scR` = nullptr when there is
  none. COMP knob 6 = SOURCE (`sideSource`, >= 64 = side-chain; nothing connected = no gain reduction), global `gENV_SRC`
  (added before the outputs), output `gOUT_SC` (side-chain peak, `drawScMeter` in the COMP and ENV LCDs).

## Added in 0.4.0

- Lanes are drawn in processing order: `rowOf(l)` (= `bank->position(l)`, or the preview while a lane is dragged: `dkLaneMove`,
  `laneMoved`, `moveTarget`), `rowY(l)` uses it; drop sends `O ...`. Beat numbers under the last listed row.
- `kDefaultOrder` puts COMP last; bank header `KFX83`; older banks (no O line) get `kDefaultOrder03`.
- FILTER knob 6 = LFO (depth); globals `gFLFO_RATE gFLFO_WAVE` (appended before the outputs; `kFltLfoBeats`, `fltLfoShape`,
  synced to the song position); `FxCtx::fltLfo`, `Engine::fltLfoNow`. UI: in the FILTER editor the EARLIER / LATER row shows
  the LFO RATE / WAVE buttons; the filter graphic shows its reach. Red UNDO.
