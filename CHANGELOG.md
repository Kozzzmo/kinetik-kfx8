# Changelog

## 0.5.0

- **New factory presets**: 17 effect sequences over 4 bars (64 steps) using lane modes, tied gestures, conditions,
  per-step locks, lane lengths and speeds, both LFOs, the envelope follower, the FILTER LFO, STUTTER / REVERSE /
  TAPE STOP gestures and chains (Stutter Fills, Reverse Swells, Envelope Wah and Glitch Chain are new).
- Every preset plays about as loud as the dry signal (OUTPUT trimmed per preset).

## 0.4.0

- The lanes are listed in the order they process the sound. Drag a lane's name up or down to move it in the chain
  (the list shows where it lands while you drag); < EARLIER / LATER > still work.
- COMP is now last in the chain by default (projects and presets saved before keep their order).
- FILTER LFO: the FILTER lane's 7th knob LFO = its depth (up to +/- 4 octaves, lockable per step); RATE (4 bars to
  1/32, triplets, dotted) and WAVE (triangle, sine, square, saws, random) in the FILTER editor, in tempo. The filter
  display shows its reach and follows it while playing.
- UNDO in red, as the other Kinetik plug-ins.

## 0.3.0

- Side-chain input (VST3 / CLAP; a second stereo input): COMP SOURCE = SIDECHAIN ducks the sound to it (a kick on the
  side-chain pumps the loop), ENV follower SOURCE = SIDECHAIN moves any knob with it. The LCD shows whether the
  side-chain receives anything. AU stays plain stereo.

## 0.2.0

- New effects: DRIVE, CRUSH, FILTER, COMP, CHORUS, FLANGER, DELAY (DIGITAL / TAPE / BBD / DIFFUSE / MULTI-TAP),
  REVERB (ROOM / PLATE / HALL / SPRING / SHIMMER), and PERFORM (STUTTER / REVERSE / TAPE STOP).
  Projects and presets made with 0.1 open with different effects (the whole set changed).
- Each effect: STEPS (on during lit steps), ALWAYS (on all the time, lit steps bring their locks) or 1-SHOT (a MIDI note /
  SHOT plays the lane once).
- TIE (Shift+click): a step continues the previous one, no cut, the first step's locks.
- Two LFOs and an envelope follower, assignable to any knob of any effect.
- Step conditions (chance, 1:2 ... 4:4, FILL, first loop); FILL button and MIDI A2.
- Effect order: move an effect earlier / later in the chain.
- CHAIN of patterns; opening a page past the end makes the lane (and the pattern) that long.

## 0.1.0

- First version: 8 sequenced effects (STUTTER, REVERSE, TAPE STOP, PITCH, CRUSH, FILTER, GATE, DELAY) on 8 lanes of up
  to 64 steps with per-step locks, lane length and speed, 16 patterns, swing, MIDI lane hold and pattern select,
  DRY/WET / INPUT / OUTPUT, 14 factory presets, user presets, undo, random lane / pattern, four skins, updates and licence.
