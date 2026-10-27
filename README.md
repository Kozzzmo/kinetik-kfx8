# Kinetik KFX-8

Sequenced multi-effect plug-in (VST3 / AU / CLAP, Windows and macOS): nine effects, each switched on by its own lane of
steps, on all the time, or played once from a MIDI note, with per-step locks, two LFOs and an envelope follower.

- DRIVE, CRUSH, FILTER, COMP, CHORUS, FLANGER, DELAY (digital, tape, BBD, diffuse, multi-tap), REVERB (room, plate, hall,
  spring, shimmer) and PERFORM (stutter, reverse, tape stop); the order of the chain can be changed.
- Each lane: STEPS / ALWAYS / 1-SHOT, its own length (1-64 steps) and speed, TIE (Shift+click) for long gestures, step
  conditions (chance, 1:2 ... 4:4, FILL, first loop), right-click a step to lock knobs on it.
- Two LFOs (free or synced) and an envelope follower, each to any knob of any effect.
- Side-chain input (VST3 / CLAP): the COMP and the envelope follower can listen to it (duck a loop to a kick).
- 16 patterns, CHAIN, swing; follows the host transport (or plays on its own).
- MIDI: C2-G#2 hold lanes / start 1-SHOT lanes, A2 = FILL, C3-D#4 choose patterns A01-A16.
- DRY/WET, INPUT and OUTPUT; factory presets and your own; undo; four skins; resizable window.

Build: `make setup` once (fetches DPF), then `make` (or `make universal` on macOS, or
`make WINDOWS=true CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++` from Linux).

Proprietary software — see `LICENSE` and `EULA.txt`. Third-party code: `THIRD_PARTY_NOTICES.txt`.
