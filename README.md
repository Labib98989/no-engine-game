# No Engine Game — rhythm yomi duel

A beat-locked 1v1 dance duel built from scratch in C++17 + SDL3. No engine, no
scripting languages anywhere in the toolchain. **All art and audio are
placeholders** (rectangles, an embedded 5x7 pixel font, synthesized clicks) —
the real pipeline (Aseprite atlases) arrives in M1.

Both players share one metronome. Every beat, each commits one of four inputs
(A close / B far / C up / D down) — each is both a movement and an attack.
Neutral resolves through an asymmetric 4-way RPS; winning puts you in
Advantage and starts a combo the defender can break (or steal) only by
predicting your inputs. Full design in [docs/design.md](docs/design.md),
systems mapping in [docs/technical.md](docs/technical.md), construction plan
in [docs/build-plan.md](docs/build-plan.md).

## Build (Windows / MSVC)

Prereqs: VS 2022 with the *Desktop development with C++* workload, vcpkg
(manifest mode), `VCPKG_ROOT` set.

```bat
cmake --preset msvc-x64
cmake --build --preset debug
ctest --preset debug
```

Run `build\msvc-x64\bin\Debug\neg.exe` (SDL3.dll and assets are staged next to
the exe automatically).

## Targets — three front-ends onto one deterministic core

| Target | What it is |
|---|---|
| `neg_sim` | Pure simulation library. Zero SDL, zero float (Q16.16 fixed-point). |
| `neg` | The game window. |
| `neg_tests` | doctest unit suite (links `neg_sim` only). |
| `neg_headless` | Scripted runner: beat-script in, beat log + checksum out. |

`neg_tests` and `neg_headless` linking *only* `neg_sim` is what enforces the
determinism boundary — an SDL include or a float in `src/sim/` is a build
error, not a code-review note.

## Controls

Inputs are **close/far/up/down**, never left/right — they stay valid on either
side (frame-invariant, design.md §4.3).

| Action | P1 | P2 |
|---|---|---|
| A — close | `D` | `Left` |
| B — far | `A` | `Right` |
| C — up | `W` | `Up` |
| D — down | `S` | `Down` |

Debug: `F1` overlay · `F5` pause · `F6` step one tick · `F7` resume ·
`F9` record (restarts the match from a fixed seed) · `F10` replay + verify
checksums · `Enter` rematch at match end · `Esc` quit.

## Headless verification

```bat
neg_headless replays\script_example.txt            # beat log + final checksum
neg_headless replays\script_combo_cap.txt --verify # runs twice, diffs checksums
neg_headless --ticklog session_ticks.txt           # replay an F9 recording
```

`session_ticks.txt` is written next to `neg.exe` when you stop a recording
with F9. The headless cross-check uses the built-in placeholder character
data; it matches the game as long as `assets/*.json` and `src/sim/chardata.cpp`
agree (they do, by construction, until tuning starts).

## Tuning

Character kits live in `assets/characters/*.json`, global rules in
`assets/config.json`. All numbers are placeholders to be dialed during M1
playtests. Hard rule: **no temporal differentiation** — both characters always
act on the same shared beat; only ranges, distances, damage, and structural
passives may differ.
