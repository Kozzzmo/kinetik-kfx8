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
| `Presets.hpp` | (0.5: 17 presets, see below) factory presets (`PB`: `knobs` = 6 knobs + MIX, `model`, `always`, `row`, `lock`) |
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

## 0.5.0 — new factory presets

- `Presets.hpp`: `PB2` helpers (`row` with "-" = tied step, `cyc` locks per lit step, `cond`, `length`, `copy`, `chain`,
  `lfo(i, wave, sync, depth, lane, knob)`, `env(...)`, `filterLfo`, `order`), `PresetDef::trimDb` = OUTPUT trim so the
  preset is as loud as the dry signal (measured with a drum-like loop, BS.1770).

## 0.5.2 — SHIMMER reverb rebuilt (owner: "le son est pauvre et souvent le même", "craque quand on passe midi")

- The old SHIMMER added an octave-up copy of the tail back into the tank input on top of the normal feedback: the loop gain
  passed 1 around the middle of MOD (saturation, crackle) and the regenerating octave covered the source. Now (idea from the
  MIT-licensed Clouds / Parasites reverb, no code copied) three of the eight lines are also read by two Hann-windowed taps
  moving at twice the speed; the feedback is plain x keep + shifted x amount with keep^2 + 0.75 amount^2 = 1, so the energy
  never grows; a 30 Hz high-pass and a soft ceiling sit in the loop; the shifted taps run a few cents apart per line.
  Test (KDSP-16): `tests/shimmer.cpp` (stable at every MOD, MOD swept with a held tone stays bounded, tail dies, octave added).

## 0.6.0 — family layout (owner: "les boutons aux mêmes endroits, même ordre, même taille"; KDYN-10 excluded)

- `src/Frame.hpp` (shared, identical in KDSP-16 / KMSP-6 / KBAS-1 / KFX-8 / KGRN-4, in `scripts/shared.sha256`): canvas
  `kW x kH` = 1180 x 800, header rects (`patBox playBtn tempoBox skinBtn`; the plug-in's own boxes packed right up to
  `kHeaderBoxesEnd` 692), bottom bar at y 728 (`recBtn clickBtn randomBtn fillBtn undoBtn`, extras `extraBtn(x, w)` between
  `kExtrasX` 618 and `kExtrasEnd` 834, `presetsBtn saveBtn`), RANDOM menu opening upwards (`randomItem`, `randomMenu`),
  `Bar` (what to show), `hit()`, drawing `Draw<R, UI>::drawBar / drawRandomMenu / barButton` through the UI's own primitives
  (each UI declares `template <class, class> friend struct kframe::Draw;`, `fr()` converts a `kframe::Rc`).
- Each UI: `barInfo()` fills the Bar, the bottom bar is drawn after the panels, the RANDOM menu last; with the menu open the
  first click goes to `kframe::hit` (`hRandomItem + i`). Sequencer toolbars in the family's order: pages | LEN | SPEED | < > |
  COPY PASTE CLEAR | -LOCKS | the plug-in's extras on the right.
- Next steps agreed: the KDSP-16 preset browser everywhere, PATTERNS block, knob pages / LFO order / names, then KJNG-2.
- KFX-8: no REC (`hasRec` false); RANDOM = LANE / PATTERN (`randomize(false/true)`), `rndOpen`; FILL (HOLD) = `fillPerfBtn` in the bar; PATTERNS row CHAIN COPY PASTE CLEAR at y 220, A01-A16 4 x 4 from y 258; LFO tabs y 516, LANE EDITOR y 552.
