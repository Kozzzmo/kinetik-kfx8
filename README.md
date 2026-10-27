# Kinetik KFX-8

Sequenced multi-effect plug-in (VST3 / AU / CLAP, Windows and macOS): eight effects — STUTTER, REVERSE, TAPE STOP,
PITCH, CRUSH, FILTER, GATE and DELAY — each switched on and off by its own lane of steps, with per-step knob locks.

- 8 lanes in series, each with 4 knobs + MIX, its own length (1-64 steps) and speed (1/4x to 4x).
- Lit steps next to each other make one gesture: the stutter, reverse and tape stop capture the sound at the first
  step and carry on until the last.
- Right-click a step to lock knobs on it (the knob values return after the step).
- 16 patterns, swing, pattern length; follows the host transport (or plays on its own when the host is stopped).
- MIDI: C2-G2 hold lanes 1-8 on while pressed, C3-D#4 choose patterns A01-A16.
- DRY/WET, INPUT and OUTPUT; factory presets and your own; undo; four skins; resizable window.

Build: `make setup` once (fetches DPF), then `make` (or `make universal` on macOS, or
`make WINDOWS=true CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++` from Linux).

Proprietary software — see `LICENSE` and `EULA.txt`. Third-party code: `THIRD_PARTY_NOTICES.txt`.
