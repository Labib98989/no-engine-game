# No Engine Game — Build & Verification Plan

**What this doc is:** how to actually *assemble, compile, and verify* the game when there is no engine and no "play" button you can trust. It turns `docs/research.md` §0/§1.3 (the build recipe), `docs/technical.md` (the systems), and `docs/design.md` §15 (the milestones) into a single executable, **gated** construction sequence.

**Core principle (read this first):**
> **Build the determinism boundary first. Test the pure sim headlessly. Bolt on rendering last.**
> Because `neg_sim` is a pure, deterministic library (no SDL, no float — `technical.md` §9), most of the *game* can be verified before a single pixel is drawn. With no engine, that pure core *is* your test bench. The window is just one of three front-ends that link it.

**Date:** 2026-06-02

---

## 1. Toolchain prerequisites (you install these)

> **Language constraint (hard rule).** The project is **strictly C / C++**. Third-party **libraries and frameworks are fine** (SDL3, SDL3_image, miniaudio, nlohmann_json, doctest) — but **no game engine**, and **no scripting language anywhere in the toolchain, build, or asset pipeline** (no Python, Node, etc.). Build orchestration is CMake; any code generation or asset processing is C++ or a CMake custom command. The Aseprite step (M1) only *invokes the Aseprite binary* to export PNG+JSON; all parsing happens in C++ via nlohmann_json — there is no offline scripting step. Python may exist on the machine but is deliberately unused.

This machine, as of 2026-06-02, has **VS 2022 Community (without the C++ workload)** and `git` — but **no MSVC C++ compiler, no CMake, no vcpkg**. Install, in order:

| Tool | Get it | Notes |
|---|---|---|
| **VS 2022 — "Desktop development with C++"** | VS Installer → Modify → check that workload | Brings MSVC v143 (`cl.exe`), the Windows SDK, and the **Developer Command Prompt**. This is the one big install. |
| **CMake ≥ 3.25** | bundled with the C++ workload, *or* cmake.org installer | `cmake --version` must be ≥ 3.25 (presets v6). |
| **vcpkg** (manifest mode) | `git clone https://github.com/microsoft/vcpkg`, run `bootstrap-vcpkg.bat`, set `VCPKG_ROOT` | Manifest mode = deps pinned per-project via `vcpkg.json`; no global installs. |
| **Git** | already present (2.51) | for `git init` + committing golden replays. |
| **Aseprite** | **deferred to M1** | Not needed until the sprite pipeline. M0 draws rectangles. |

### Toolchain smoke check
Open the **x64 Native Tools Command Prompt for VS 2022** (not plain PowerShell — it sets the MSVC environment) and confirm all four:
```
cl                       :: prints "Microsoft (R) C/C++ ... x64"
cmake --version          :: >= 3.25
echo %VCPKG_ROOT%        :: points at your vcpkg clone
git --version
```
If `cl` is missing, the C++ workload didn't install — fix that before anything else. **This is S0's entry gate.**

---

## 2. Repo scaffold

Lay down once, then `git init`:

```
No Engine Game/
  CMakeLists.txt           ← from research.md §1.3, source list swapped for the rhythm modules
  CMakePresets.json        ← from research.md §1.3 (msvc-x64, msvc-x64-asan)
  vcpkg.json               ← { sdl3, sdl3-image, nlohmann-json }   (research.md §1.3)
  .gitignore               ← build/, .vs/, *.user, vcpkg_installed/
  .clang-format            ← house style (LLVM-ish, 4-space, 100 col)
  third_party/
    miniaudio/             ← vendored miniaudio.{h,c}  (deferred until audio in S2)
    doctest/               ← vendored doctest.h (single header) for neg_tests
  src/                     ← module layout per technical.md §9
    sim/  data/  platform/  render/  audio/  analyzer/  main.cpp
  tests/
    test_main.cpp          ← doctest entry
    test_fixed.cpp  test_clash.cpp  test_combo.cpp  ...
  tools/
    headless.cpp           ← neg_headless console runner (§3)
  assets/                  ← (M1) atlases, JSON moves, audio
  replays/                 ← committed golden input logs (§5)
  docs/                    ← design.md, research.md, technical.md, build-plan.md
```

### CMake targets (four front-ends onto one core)
```cmake
neg_sim       STATIC   # src/sim/* — zero SDL, zero float. The deterministic core.
neg           WIN32    # the game: links neg_sim + SDL3 + SDL3_image + nlohmann_json + miniaudio
neg_tests     CONSOLE  # links neg_sim + doctest only. No SDL.
neg_headless  CONSOLE  # links neg_sim only. Feeds scripted inputs, dumps beat log + checksum.
```
The fact that `neg_tests` and `neg_headless` **link only `neg_sim`** is what physically proves the determinism boundary: if a `#include <SDL3/...>` or a `float` sneaks into `src/sim/`, those targets fail to build. The boundary is a build error, not a code-review note.

MSVC flags (research.md §1.3): `/W4 /permissive- /Zc:preprocessor /utf-8 /fp:strict`, `/Od /Zi` debug, `/O2 /GL` + `/LTCG` release. Post-build, copy `$<TARGET_RUNTIME_DLLS:neg>` next to the exe so SDL3.dll is staged.

---

## 3. The "no-engine" verification strategy

Three front-ends replace the missing "play button". Use the cheapest one that can catch the bug.

1. **`neg_tests` — unit tests (doctest, single header).** Pure-logic correctness with zero graphics:
   - `Fixed` arithmetic (round-trip, multiply/divide precision, overflow guards).
   - The RPS table `beats[][]` matches design.md §5 exactly (all 16 cells).
   - `resolve_clash`: every branch — both-whiff, one-lands, RPS-decided, same-type-tie, both-miss, Sustain tie-break.
   - `resolve_combo`: continue / break / steal / cap-end / miss-end, damage scaling, cross-up facing flip, Stick-the-Landing bonus.
   - `tier_from_phase` banding; `beat_index`/`phase` arithmetic.
   These run in milliseconds and are where ~80% of gameplay bugs get caught.

2. **`neg_headless` — scripted deterministic runner.** A C++ console exe that reads a per-beat input **data file** (a plain `.txt` scenario, *not* a scripting language) and drives `sim::tick` with no window:
   ```
   # replays/script_example.txt  — one line per beat: <P0 input,tier> <P1 input,tier>
   A,Perfect  B,Normal
   D,Normal   D,Perfect
   ...
   ```
   It prints a **beat log** (the analyzer's table, §technical.md 8.3) and the **final state checksum**. This verifies *gameplay and balance* — "does a 5-hit combo really cap?", "does Ballerina's Sustain fire?" — before any rendering exists. It's also the fastest way to reproduce a reported bug.

3. **Determinism harness.** Run the same input log through `neg_headless` **twice** (and, later, through the windowed `neg` via F9/F10) and diff the per-tick checksums. Identical ⇒ deterministic. First divergence ⇒ exact tick of the bug (technical.md §7). This is what guarantees replays, the analyzer scrubber, and future rollback actually work.

**Visual / feel** verification (does it *look* on-beat, does input feel responsive) is the only thing that needs the window — and it rides on the in-engine analyzer (technical.md §8), not on guesswork.

---

## 4. Construction sequence — dependency-ordered, gated

Each slice ends with a concrete **verify gate**. Do not start a slice until the previous gate passes. M0 slices are fully specified; M1 is outlined at lower resolution and detailed when reached.

### S0 — Toolchain + blank window
- **Entry gate:** the §1 smoke check passes (`cl`, `cmake`, `vcpkg`, `git` all present).
- Scaffold §2; `cmake --preset msvc-x64`; build `neg`; it opens a 1280×720 SDL3 window, clears to a color, closes cleanly on quit. SDL3.dll staged next to the exe.
- **Gate:** window opens and closes with no debug-heap leak report (MSVC `_CrtDumpMemoryLeaks` or run under the debugger). vcpkg resolved SDL3 in manifest mode.

### S1 — `neg_sim` skeleton + test bench
- `Fixed`, `Rng`, empty `SimulationState`, a `sim::tick` stub, `checksum()`. Stand up `neg_tests` (doctest) and `neg_headless`.
- **Gate:** `neg_tests` runs green on `Fixed` + checksum tests; `neg_sim` compiles with **zero** SDL/float (proven by `neg_tests`/`neg_headless` linking only `neg_sim`); `neg_headless` runs an empty script and prints a stable checksum.

### S2 — Fixed-timestep loop + metronome
- The research.md §2.1 loop in `main.cpp`: accumulator, `MAX_FRAME_NS` clamp, `MAX_STEPS_PER_FRAME`. `BeatClock` + `tick→beat→phase`. On-screen frame counter and beat counter + phase bar. miniaudio plays an audible **click on each beat** (reads `beat_index`, one-way — technical.md §9).
- **Gate:** frame counter advances ~60/sec; beat counter advances at BPM/60 (2/sec at 120 BPM); click lands on the beat; pausing the debugger does not fast-forward beats (clamp works).

### S3 — Input → per-beat commit + timing tier
- Platform layer drains SDL events → `FrameInput` (scancodes; WASD = P1, arrows = P2 — research.md §4.2). `input_commit` applies first-press-wins + tier banding (technical.md §1). On-screen readout: `P1: A (Perfect)`.
- **Gate:** in the inspector, each press maps to the right input and the tier matches its distance from the beat instant; a second press in the same beat is ignored (first-press-wins).

### S4 — Two rectangles + on-beat movement
- `render/` draws floor line + two colored rectangles at fighter positions. Per-beat **slide** toward `move_target` (technical.md §2), close/far/up/down displacement, walls clamp, frame-invariant mapping.
- **Gate:** rectangles slide smoothly on the beat, stay inside the walls, and the input→direction mapping does **not** invert when a fighter is on the other side (frame-invariance, design.md §4.3).

### S5 — Frame-step + record/replay + determinism
- **F5/F6/F7** pause/step/resume (no special code beyond the loop — research.md §2.4); **F9/F10** record/replay an input log; per-tick checksum logged.
- **Gate:** record a ~10-second session, replay it, per-tick checksums match exactly — verified both in-engine (F9→F10) **and** by feeding the same log to `neg_headless`. This is the M0 determinism guarantee.

### — M0 GATE COMPLETE — (== design.md §15 M0)
Window + floor + two rectangles, shared-beat 4-input local 2P, fixed 60 Hz, frame + beat counters, analyzer stub (forward step + replay + checksum), `neg_sim` with zero SDL/float. **Stop, commit, tag `m0`.**

### M1 — Playable duel (outlined; detail when reached)
Each gets its own verify gate (unit tests + headless script first, then the window):
1. **Clash resolver** wired into the live loop — Neutral RPS produces Advantage. *Gate: headless script reproduces every design.md §5/§7 branch.*
2. **Advantage / combo** — continue/break/steal/cap, scaling, cross-up. *Gate: headless 5-hit-cap + steal scripts; visual cross-up reads correctly.*
3. **Airborne sub-state** — launch, air inputs, gravity-out, D-chain.
4. **Health / rounds / timer / KO / time-over / draw** (design.md §16 match economy — pick N for best-of-N here).
5. **Analyzer panel + F1 box overlay + beat log** (technical.md §8.2–8.3).
6. **Aseprite → atlas pipeline** (research.md §5; install Aseprite now) with placeholder pixel art; ticks-not-ms timings.
7. **Second character + `CharacterData` tuning** — Breaker/Ballerina numbers dialed via the beat log (technical.md §3.7).

---

## 5. Verification harness reference

- **doctest** — vendored single header in `third_party/doctest/`; `tests/test_main.cpp` defines `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`. One `test_*.cpp` per sim module. Run via `ctest` or just execute `neg_tests`.
- **Headless script format** — one line per beat, `<P0 input,tier> <P1 input,tier>`, `#` comments. `neg_headless <script>` prints the beat log and final checksum; `--checksums` dumps per-tick.
- **Golden replays** — commit known-good input logs + their expected final checksum to `replays/`. A regression run replays each and asserts the checksum; any drift means a sim change altered behavior (intended or not — both worth knowing).
- **Run cadence** — `neg_tests` on every change to `src/sim/`; the golden-replay determinism check before every commit that touches the sim.

---

## 6. Engineering-discipline checklist (research.md §11, rhythm-adapted)

- [ ] `src/sim/` (`neg_sim`) has zero SDL deps and zero floats — enforced by `neg_tests`/`neg_headless` linking only `neg_sim`.
- [ ] Every state change goes through `sim::tick`. Render, input, audio, analyzer **read** `SimulationState`; none mutate it.
- [ ] All randomness routes through `SimulationState::rng` (PCG32). No `rand()`/`time()`.
- [ ] `SimulationState::checksum()` logged each tick in dev builds.
- [ ] Record/replay works from M0 (S5). Diverged checksums report the exact tick.
- [ ] `TICKS_PER_BEAT` is global and identical for both players — no per-character temporal delta (`no-temporal-speed`).
- [ ] Frame timings stored as **ticks**, never milliseconds, after load (M1 Aseprite import).
- [ ] All boxes authored canonical right-facing; mirroring is one function applied uniformly (M1, research.md §5.5).
- [ ] Analyzer phasing respected: M0 = forward-step + replay + counters; M1 = inspector + overlay + beat log; M2 = backward scrub.

---

## 7. What this plan deliberately does **not** do
- No toolchain *installation* (you do that — §1 lists what's needed).
- No code this session — this is the plan; S0 scaffolding is the next, separate session.
- No resolution of design.md §16 open questions (song-as-system, match economy, meter) — they surface as M1 decision points, not blockers.
- No rollback netcode — but every choice here (pure tick, POD state, input-log replay, checksum) is chosen so M3 rollback is a drop-in, per design.md §15 / research.md §10.
