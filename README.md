# No Engine Game — a rhythm yomi duel

A beat-locked 1v1 dance duel built from scratch in **C++17 + SDL3**. No game
engine, no scripting language anywhere in the toolchain — just a small
deterministic simulation core with a thin SDL presentation layer on top.

Two dancers (a b-boy **Breaker** and a **Ballerina**) share **one metronome**.
Every beat, each player commits one of four inputs — **A close · B far · C up ·
D down** — and each input is *both* a movement and an attack. Neutral resolves
through an asymmetric 4-way rock-paper-scissors gated by range; winning a clash
puts you in **Advantage** and starts a combo the defender can break — or steal —
only by reading your next inputs.

> **All art and audio are placeholders** — rectangles, an embedded 5×7 pixel
> font, and synthesized clicks. Character PNG atlases render when present and
> silently fall back to rectangles when they don't. The real art pipeline
> arrives in a later milestone.

Design details: [docs/design.md](docs/design.md) ·
systems mapping: [docs/technical.md](docs/technical.md) ·
construction plan: [docs/build-plan.md](docs/build-plan.md).

---

## Contents

- [Play now (no build required)](#play-now-no-build-required)
- [How to play](#how-to-play)
- [Controls](#controls)
- [Build from source](#build-from-source)
- [Project layout](#project-layout)
- [The four front-ends](#the-four-front-ends-onto-one-deterministic-core)
- [Headless verification & replays](#headless-verification--replays)
- [Tuning](#tuning)
- [Troubleshooting](#troubleshooting)

---

## Play now (no build required)

If you just want to play on a Windows 10/11 machine and don't want to install a
compiler:

1. Download **`no-engine-game-win64.zip`** from the
   [latest release](https://github.com/Labib98989/no-engine-game/releases/latest).
2. Extract it anywhere (keep the files together — `neg.exe` needs `SDL3.dll`
   and the `assets/` folder next to it).
3. Double-click **`neg.exe`**.

That's the whole install. The game finds its assets relative to the executable,
so the folder is fully self-contained and portable — copy it to a USB stick and
it runs on any other Windows machine. Player settings are written to a
`settings.json` next to the exe on first save.

> No macOS/Linux build is published yet. The simulation core is portable C++,
> but the presentation layer and packaging are Windows-only for now.

---

## How to play

- **One shared beat.** A metronome ticks at a fixed BPM. On each beat both
  fighters *commit* one input. There is no "faster" character — both always act
  on the same beat; characters differ only in **range, movement distance,
  damage, and structural passives**.
- **Four inputs, each a move + an attack:**
  | Input | Movement | Attack |
  |---|---|---|
  | **A — close** | step toward the opponent | short-to-mid poke |
  | **B — far** | step away | spacing tool |
  | **C — up** | hop | anti-air / rising hit |
  | **D — down** | hold ground | low / sweep |
- **Range gates the RPS.** An attack only enters the rock-paper-scissors
  comparison if the opponent is within its range at the beat. Out of range =
  it whiffs, even if the input would have won.
- **The RPS layer:** `A > B`, `A > C`, `B > C`, `C > D`, `D > A`, `D > B`.
  Win the clash and you enter **Advantage**.
- **Advantage & combos.** The winner starts a combo; the defender can **break**
  it (or **steal** advantage) only by correctly predicting the attacker's
  inputs — this is the "yomi" (mind-reading) layer. Combos are capped so a
  single read can't end the round.
- **Timing tiers.** Pressing exactly on the beat (**Perfect**) hits harder than
  a loose press (**Normal**); blowing the window entirely (**Miss**) can't land
  at all and takes chip damage if both players miss.
- **Rounds.** First to the configured number of round wins takes the match.

Not sure what to press? The title screen has a **How to Play** entry, and an
attract-mode CPU-vs-CPU duel plays behind the menus so you can watch the rhythm
before jumping in.

---

## Controls

Inputs are **close / far / up / down**, never left/right — they stay valid on
either side of the opponent (frame-invariant, design.md §4.3).

| Action | Player 1 | Player 2 |
|---|---|---|
| **A — close** | `D` | `←` |
| **B — far** | `A` | `→` |
| **C — up** | `W` | `↑` |
| **D — down** | `S` | `↓` |

**Menus:** `W/S` or arrows move · `A/D` or left/right change a value · `Enter`
select · `Esc` back (during a match this opens the pause menu).

The title screen offers **VS Player**, **VS CPU**, **Watch** (CPU vs CPU),
**How to Play**, and **Options** (volume, screen shake, fullscreen, debug HUD —
persisted to `settings.json`).

**Debug / developer keys:**

| Key | Action |
|---|---|
| `F1` | toggle debug overlay |
| `F5` | freeze the simulation |
| `F6` | step one tick (while frozen) |
| `F7` | resume |
| `F8` | cycle the P2 seat in-match: Human → CPU Easy → Normal → Hard |
| `F9` | start/stop recording (restarts from a fixed seed and the standard cast) |
| `F10` | replay the last recording and verify per-tick checksums |

---

## Build from source

The game targets **Windows + MSVC**. Dependencies (SDL3, SDL3_image, nlohmann-json)
are pulled by **vcpkg in manifest mode**, so you don't install them by hand.

### Prerequisites

1. **Visual Studio 2022** with the **"Desktop development with C++"** workload.
   This provides the MSVC compiler, the Windows SDK, and a bundled CMake
   (≥ 3.25) + Ninja. The free *Community* edition is fine.
2. **Git**.
3. **vcpkg**. Clone and bootstrap it once, anywhere:
   ```bat
   git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
   C:\dev\vcpkg\bootstrap-vcpkg.bat
   ```
4. Set the **`VCPKG_ROOT`** environment variable to that folder so the CMake
   preset can find the vcpkg toolchain. To set it permanently for your user:
   ```powershell
   setx VCPKG_ROOT C:\dev\vcpkg
   ```
   Open a **new** terminal afterward so the variable is picked up. The exact
   dependency versions are pinned by `builtin-baseline` in `vcpkg.json`, so any
   machine resolves the same SDL3/JSON versions.

> A convenient way to get a shell with `cmake`, `cl.exe`, and the Windows SDK
> already on `PATH` is the **"x64 Native Tools Command Prompt for VS 2022"**
> from the Start menu, or the **CMake / Terminal** integration inside Visual
> Studio itself ("Open a local folder" → pick this repo).

### Configure, build, test

From the repository root:

```bat
cmake --preset msvc-x64      :: configure (vcpkg installs SDL3 on first run — can take a while)
cmake --build --preset debug :: build the Debug configuration
ctest --preset debug         :: run the unit + headless test suite
```

For an optimized build:

```bat
cmake --build --preset release
```

There is also an AddressSanitizer configure preset, `msvc-x64-asan`.

### Run

```bat
build\msvc-x64\bin\Debug\neg.exe
```

`SDL3.dll` (and friends) plus a copy of `assets/` are staged next to the
executable automatically as a post-build step, so the exe is runnable in place
and the whole `bin\Debug` (or `bin\Release`) folder is a portable package.

---

## Project layout

```
src/
  sim/        Deterministic core — Q16.16 fixed-point, zero SDL, zero float.
              beat clock, RPS/clash, combos, characters, CPU AI, RNG.
  shell/      Menu/title/options/results state machine.
  platform/   SDL window, event loop plumbing, input mapping.
  render/     Drawing, 5×7 pixel font, sprite atlases, screen-shake effects.
  data/       JSON loading (character kits, tuning, AI) and settings persistence.
  audio/      miniaudio click/metronome synthesis.
  main.cpp    Fixed-timestep 60 Hz loop wiring it all together.
tests/        doctest unit suite (links the sim core only).
tools/        headless.cpp — scripted/AI-driven runner for CI and replays.
assets/       Character kits, AI presets, global config (all JSON) + placeholder art.
replays/      Beat scripts used by the headless tests.
third_party/  Vendored single-header libs: doctest, miniaudio.
docs/         design.md, technical.md, build-plan.md, research.md.
```

## The four front-ends onto one deterministic core

| Target | What it is |
|---|---|
| `neg_sim` | Pure simulation **static library**. Zero SDL, zero float (Q16.16 fixed-point). |
| `neg` | The game window (SDL3 + SDL3_image + nlohmann-json + miniaudio). |
| `neg_tests` | doctest unit suite — links **`neg_sim` only**. |
| `neg_headless` | Scripted runner: beat-script in, beat log + checksum out — links **`neg_sim` only**. |

`neg_tests` and `neg_headless` linking *only* `neg_sim` is what physically
enforces the determinism boundary: an accidental SDL include or a `float` in
`src/sim/` becomes a **build error**, not a code-review note.

## Headless verification & replays

The headless runner drives the sim with no window — used by CI and for
reproducing sessions:

```bat
neg_headless replays\script_example.txt             :: beat log + final checksum
neg_headless replays\script_combo_cap.txt --verify  :: run twice, diff checksums
neg_headless --ticklog session_ticks.txt            :: replay an in-game F9 recording
neg_headless --bot0 hard --bot1 easy --beats 300 --verify  :: CPU vs CPU
```

`session_ticks.txt` is written next to `neg.exe` when you stop a recording with
`F9`. The headless cross-check uses the built-in placeholder character data; it
matches the game as long as `assets/*.json` and `src/sim/chardata.cpp` agree.

## Tuning

All balance numbers are data, not code:

- Character kits: `assets/characters/*.json`
- Global rules (BPM, damage, combo cap, stage): `assets/config.json`
- CPU difficulty presets: `assets/ai/{easy,normal,hard}.json`

Anything missing or malformed falls back to the built-in defaults in
`src/sim/chardata.cpp`, so the game always launches.

**Hard design rule — no temporal differentiation:** both characters always act
on the same shared beat. Only ranges, movement distances, damage, and
structural passives may differ between them.

## Troubleshooting

| Symptom | Fix |
|---|---|
| CMake configure fails with *"VCPKG_ROOT … does not exist"* or can't find the toolchain | `VCPKG_ROOT` isn't set (or the terminal predates `setx`). Set it to your vcpkg clone and open a new terminal. |
| `cmake` / `ctest` "not recognized" | Use the *x64 Native Tools Command Prompt for VS 2022*, or build from inside Visual Studio, so the bundled CMake is on `PATH`. |
| First configure is very slow | Expected — vcpkg compiles SDL3 from source once, then caches it under `build/`. Subsequent configures are fast. |
| `neg.exe` won't start / `SDL3.dll not found` | Run the exe from its own `bin\<Config>` folder, or copy the whole folder together. The DLLs and `assets/` are staged next to the exe by the build. |
| Fighters render as plain rectangles | That's the intended placeholder fallback when a character PNG atlas is missing or invalid. Gameplay is unaffected. |
| Settings don't persist | `settings.json` is saved next to the exe; make sure that folder is writable. |

---

Built with SDL3, SDL3_image, nlohmann-json, miniaudio, and doctest. Placeholder
art and audio are original and stand in for the real pipeline.
