# Kinetik KFX-8 — project memory for Claude

Kinetik KFX-8 is a sequenced multi-effect plug-in (VST3 / AU / CLAP, built with DPF and NanoVG): 8 effects on 8
parallel lanes of steps (owner's choice: "8 effets, 8 lignes en parallèle"). All DSP is original code. Family:
KDSP-16 (`Kozzzmo/kinetik-kdsp16`), KMSP-6, KJNG-2, KDYN-10; next products KBAS-1 (acid bass) and KGRN-4 (granular).
Repository: `Kozzzmo/kinetik-kfx8` (public, created 2026-10-27; first built locally in `/home/claude/kinetik-kfx8`, with `dpf` symlinked to the KDSP-16 checkout).

## Source map (`src/`)

| File | Role |
|---|---|
| `Params.hpp` | 8 lanes (`Lane`: STUTTER REVERSE TAPE STOP PITCH CRUSH FILTER GATE DELAY, processed in this order), 64 steps, 16 patterns; per lane 4 knobs (`kLaneDefs`: names, defaults, help) + MIX = host params `lpIndex(l,k)` = l*5+k; globals after `kGlobalBase` = 40: DRYWET INPUT OUTPUT TEMPO PLAY PATTERN, outputs OUT_STEP (1/24 16ths) OUT_ACTIVE (lane bits) OUT_PATTERN; beat-synced tables (`kStutSize`, `kRevLen`, `kGateRate`, `kDelTime`, 8 bins of 16 = `bin8`), lane speeds `kSpeedMult`; `laneValueText` |
| `Bank.hpp` | `Pattern` (master length, swing, per lane `len` / `speed`, `on[l][s]`, `locks[l][s][4]`), `Bank::mute` (lane bypass); text format header `KFX81`; commands `P L s S k kc U cl cll cp rot` (`applyCommand` returns the pattern touched or `kMuteTouched`) |
| `Fx.hpp` | the effects: `History` (8 s stereo ring indexed by absolute sample time), `Stutter` (first pass live, then the slice repeats with DECAY / PITCH per repeat / GATE), `Reverse` (the LENGTH before the trigger backwards, looped, FADE windows, TONE), `TapeStop` (rate (1-u)^curve, darker as it slows, WOBBLE, amplitude follows the rate), `Pitch` (two-tap delay shifter, sin² crossfade, FEEDBACK), `Crush`, `Filter` (Svf, SWEEP = octaves across the step), `Gate` (chop locked to the song position), `Delay` (synced, ping-pong WIDTH, TONE + 80 Hz HP in the loop, fed only while active, sleeps) |
| `Engine.hpp` | transport like KDSP-16 (host or internal PLAY), 32-sample segments: per lane step from the lane grid (speed, swing), `on` = lit step (or MIDI / PUSH hold `midiHold`), run start = off -> on (stutter / reverse / tape `start()`), knob = step lock or knob; 4 ms on/off envelope; chain in series, `x += (fx(x) - x) * env * mix` (delay: wet added, input x env); DRY/WET, INPUT / OUTPUT trims smoothed; pattern change at the end of the master length; `midiPattern` |
| `KfxPlugin.cpp` | DPF glue (stereo in/out, MIDI in, AU type `aumf`, unique id `KFX8`): params, states `bank` / `cmd` / `skin`, `pv` UI edits (stale host values ignored 1.5 s, as KDSP-16), cmd `hold <mask>` (PUSH pads), MIDI C2-G2 hold lanes, C3-D#4 patterns, CC 120/123 release holds; demo gate = dry for 3 s every 45 s |
| `KfxUI.cpp` | 1180x720: header (logo, LENGTH, SWING, PAT, PLAY, BPM, SKIN), lanes panel (step pages 1-16..49-64, lane LEN / SPEED / rotate / FILL / CLEAR / COPY / PASTE; per lane: name + length + LED, ON/OFF bypass, PUSH hold; 16 step cells, playhead, lock dots), right panel (DRY/WET INPUT OUTPUT, patterns A01-A16 + copy/paste/clear, RANDOM LANE / ALL, UNDO), editor (LCD: name, help, `drawLaneGraphic` picture of the knobs; 5 knobs; right-click a step = lock mode, CLEAR LOCKS), presets window (factory + user `.kfx8`), save, skins, about/update, licence, tooltips |
| `Presets.hpp` | 14 factory presets (`PB` builder: `knobs`, `row` = string sets the lane length, `lock`, `speed`) |
| shared (identical in the family, `scripts/check-shared.sh`) | `Skins.hpp`, `Fonts.hpp`, `Dsp.hpp`, `FsUtf8.hpp`, `Updater.*`, `License.hpp`, `LogoMark.hpp`, monocypher, `fonts/OFL-Unbounded.txt` |

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
