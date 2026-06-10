# No Engine Game — Technical Design

**What this doc is:** the bridge between *how the game feels* (`docs/design.md`) and *what the code does*. It maps every mechanic in the design onto concrete data structures, a single deterministic tick pipeline, and a small set of resolver functions.

**Companion docs:**
- `docs/design.md` — gameplay design (the source of truth for *behavior*). Every system here cites a design section.
- `docs/research.md` — technical **infrastructure** foundation (SDL3 stack, build, fixed-timestep loop, Q16.16 determinism, render/collision, Aseprite pipeline). Still authoritative for §1–§3, §5, §7.
- `docs/build-plan.md` — how this gets assembled, compiled, and tested with no engine.

> **Supersession notice.** `research.md` was written before the genre pivot (SF1 fighter → rhythm yomi duel). Its **gameplay** sections — §4 (motion inputs / QCF / DP), §6 (frame-data move/cancel system), §8 (SF-style state machine), §9 (SF1 movesets), and throws — describe a game we no longer build. **This document replaces them.** Where research.md and this doc disagree on gameplay, this doc wins. Where they touch infrastructure (loop, determinism, render, fixed-point, collision math, Aseprite), research.md still holds and this doc inherits it by reference.

**Date:** 2026-06-02

---

## 0. The one-paragraph model

The game is a deterministic state machine advanced at a fixed **60 Hz**. Time is counted in **ticks**; the metronome groups ticks into **beats** (`TICKS_PER_BEAT = 30` at 120 BPM). Each beat, both players **commit exactly one of four inputs** (A/B/C/D) with a **timing tier** (Perfect / Normal / Miss). When a beat's input window closes, a single **resolver** compares both commits and mutates the world: in **Neutral** a 4-way RPS clash decides who takes **Advantage**; in **Advantage** a combo either continues, ends, or is **stolen**. All of this lives in a pure `neg_sim` library that touches no SDL and no floats — which is what makes replay, frame-step, the analyzer, and (later) rollback nearly free.

---

## 1. Temporal model — ticks, beats, and input commits

This is the heart of the rhythm layer and has no analogue in research.md, so it is specified in full.

### 1.1 Ticks and beats

- The sim advances at a fixed **60 Hz**. The fundamental unit is the **tick** (`uint64_t tick`).
- `TICKS_PER_BEAT (TPB) = 60 / (BPM / 60)`. At the default **120 BPM**, `TPB = 30`.
- `beat_index = tick / TPB`. The **beat instant** for beat *n* is the tick `n * TPB`.
- `beat_phase = tick % TPB` — where we are *within* the current beat (0 = on the instant). The renderer and metronome read this; the sim logic only cares about the resolution boundary (§1.4).

> **Constraint (locked, `no-temporal-speed`):** `TPB` is global. It is identical for both players and both characters, always. No character, passive, or song event may change one player's beat cadence relative to the other. Song-as-system tempo changes (design.md §16) must alter `BPM` for *both* players simultaneously.

### 1.2 The input-capture window

Each beat owns a capture window **centered on its instant**, so the windows tile the timeline with no gaps and no overlaps:

```
window(n) = ticks in [ n*TPB - TPB/2 , n*TPB + TPB/2 )      // at TPB=30: [30n-15, 30n+15)
```

Every key press falls inside exactly one beat's window and is attributed to that beat. A press near the boundary belongs to whichever instant it's closest to — which is exactly what a player intends.

### 1.3 Timing tier — concentric bands around the instant

The tier is a function of the press's tick-distance from the beat instant, `d = |press_tick - n*TPB|`:

| Tier | Band | Starting value (TPB=30) | ~% of beat |
|---|---|---|---|
| **Perfect** | `d ≤ PERFECT_TICKS` | `PERFECT_TICKS = 2` (5-tick window) | ~17% |
| **Normal**  | `PERFECT_TICKS < d ≤ NORMAL_TICKS` | `NORMAL_TICKS = 11` | ~60% |
| **Miss**    | `d > NORMAL_TICKS`, **or no press** | the sloppy outer band + silence | ~23% |

These three numbers are the **only** rhythm-feel tuning knobs and live in `CharacterData`/global config, not in code constants. (design.md §6 calls for ~15% Perfect / ~70% Normal; the starting values above are close and get dialed in playtesting.)

### 1.4 Commit rule and resolution boundary

- **First press wins.** The first A/B/C/D press inside a beat's window is the irrevocable **commit** for that beat; later presses in the same window are ignored. This rewards decisive reads, punishes mashing for a lucky Perfect, and matches the design pillar *predict, don't react* (design.md §2). (Alternative — "closest-to-instant press wins" — is noted as a tuning lever but rejected as the default because it invites mashing.)
- A beat's commits are **resolved when its window closes**, at tick `n*TPB + TPB/2`. At that tick, both players' commits for beat *n* are final and the resolver (§4) runs exactly once. Resolving at window-close (a half-beat after the instant) lets even a late-but-Normal press land before the beat is judged.
- If a player never pressed in the window, their commit is `(input = none, tier = Miss)`.

### 1.5 Why this is the right shape for the analyzer

Because the entire game state advances by discrete, fully-determined beats, a **beat log** (one row per beat: each player's input + tier + the resolved outcome) is a complete, replayable record of the match. The analyzer (§8) is mostly a view onto this log.

---

## 2. Movement model — deterministic on-beat slide

design.md §10.2: "both players move simultaneously each beat." The technical question is *how* a discrete per-beat displacement becomes smooth on screen without breaking the "renderer never interpolates" rule (research.md §2.2).

**Decision:** movement is resolved per beat as a **target displacement**, then the fighter **slides toward that target in fixed-point across the following beat's ticks, inside the sim**. The motion is therefore deterministic (it's just more sim state), and the renderer still draws the latest sim position with zero interpolation.

```
at resolution of beat n:   fighter.move_target = clamp_to_walls(pos + beat_displacement(input, char))
each tick until next res.:  fighter.pos += (move_target - pos) / ticks_remaining   // Fixed; exact arrival
```

- **Displacement per input** is `CharacterData` (close/far/up/down distances). This is the *spatial speed* differentiation axis (`no-temporal-speed`): Breaker advances farther on A, Ballerina retreats farther on B, etc.
- **Walls** (design.md §10.2) clamp the target, never the slide, so corner pressure is a natural consequence.
- **Cross-up** (B in Advantage, design.md §10.3) is an **instantaneous teleport** at the resolution tick — the attacker swaps to the far side of the defender and `facing` flips. It does *not* slide.
- **Airborne** vertical motion is the same mechanism on the Y axis; a `C` launch sets a high `move_target.y` and gravity-out sets it back to floor over the configured air-beats.

---

## 3. Data model (POD, lives in `neg_sim`)

All gameplay state is plain-old-data: trivially copyable, no pointers into the heap, no virtuals. This is what makes `memcpy`-style snapshots, checksums, replay, and rollback cheap (research.md §3). `Fixed` is the Q16.16 type from research.md §3.4.

### 3.1 Enumerations

```cpp
enum class Input  : uint8_t { None = 0, A, B, C, D };          // the four dual inputs
enum class Tier   : uint8_t { Miss = 0, Normal, Perfect };
enum class Macro  : uint8_t { Neutral, Advantage };            // P1/P2-Advantage share this; roles disambiguate
enum class Role   : uint8_t { Free, Attacker, Defender };
enum class CharId : uint8_t { Breaker = 0, Ballerina = 1 };
enum class Phase  : uint8_t { Intro, Fighting, RoundEnd, MatchEnd };
```

### 3.2 The per-beat commit

```cpp
struct Commit {
    Input  input      = Input::None;
    Tier   tier       = Tier::Miss;
    uint8_t press_tick = 0;     // beat-phase tick of the committed press (for analyzer; 0 if none)
    bool   locked     = false;  // first-press-wins flag for this beat
};
```

### 3.3 The fighter

```cpp
struct Fighter {
    CharId  character;
    int32_t health;

    Fixed   pos_x, pos_y;       // feet anchor; world space
    Fixed   move_target_x, move_target_y;
    bool    facing_right;
    bool    airborne;
    uint8_t airborne_beats;     // beats elapsed in air sub-state (gravity timeout)

    Role    role;               // Free in Neutral; Attacker/Defender in Advantage
    Commit  commit;             // this beat's committed input+tier

    uint16_t anim_tag;          // current animation id (drives combo-position cycling, §5.3 research)
    uint16_t anim_tick;

    // minimal passive state:
    Input    last_committed;    // Breaker "Stick the Landing" needs to know if combo ends on D
};
```

> Note what is **absent** vs research.md's `Fighter`: no `hitstun`, no `blockstun`, no `hitstop`, no `InputBuffer` ring, no `current_move` pointer. The rhythm duel has none of those concepts. Hitstop-style impact freeze, if desired, is a *cosmetic* render/audio effect that must **not** stall the shared beat.

### 3.4 The duel state (combo bookkeeping)

```cpp
struct DuelState {
    Macro  macro = Macro::Neutral;
    uint8_t attacker = 0;       // fighter index 0/1 when macro == Advantage
    uint8_t defender = 1;
    uint8_t combo_count = 0;    // hits so far this Advantage (cap 5, design.md §8.2)
};
```

### 3.5 The resolution result (read-only output for render / audio / analyzer)

```cpp
enum class Outcome : uint8_t {
    None, BothWhiff, OneLands, RpsDecided, SameTypeTie, BothMiss,   // Neutral outcomes
    ComboContinue, ComboBreak, ComboSteal, ComboCapEnd, ComboMissEnd,
    Launch, GroundOut                                               // Advantage outcomes
};

struct ResolutionResult {
    Outcome outcome = Outcome::None;
    int8_t  winner  = -1;       // fighter index, or -1 for none/both
    int32_t damage_p0 = 0, damage_p1 = 0;
    Input   p0_input, p1_input;
    Tier    p0_tier,  p1_tier;
};
```

### 3.6 Match + beat clock + top-level state

```cpp
struct BeatClock {
    uint16_t bpm          = 120;
    uint16_t ticks_per_beat = 30;   // derived from bpm at round start; identical for both players
    uint32_t beat_index   = 0;
};

struct MatchState {
    Phase    phase = Phase::Intro;
    uint8_t  round = 1;
    uint8_t  wins_p0 = 0, wins_p1 = 0;     // best-of-N (design.md §16 open: N TBD)
    int32_t  round_timer_ticks = 0;        // counts down; 0 => time-over
};

struct SimulationState {
    uint64_t         tick = 0;
    Rng              rng;                  // PCG32, seeded, in-state (research.md §3.2)
    BeatClock        clock;
    MatchState       match;
    Fighter          fighters[2];
    DuelState        duel;
    ResolutionResult last_result;          // last beat's outcome (cleared each beat)
    // CharacterData is NOT here — it is immutable config loaded once (§3.7).
};
```

### 3.7 Character data (immutable config, not sim state)

Loaded once from JSON into POD, passed *by const reference* to the resolvers. Keeping it out of `SimulationState` keeps snapshots tiny and makes "same engine, different game" (design.md §11.3) a pure data swap.

```cpp
struct CharacterData {
    int32_t health;
    Fixed   range[5];           // indexed by Input; strike reach per input
    Fixed   move_dist[5];       // per-input beat displacement (spatial-speed axis)
    int32_t damage_ground[5];   // base damage per input, Neutral/ground
    int32_t combo_scale_ground[5];  // 100/80/60/40/20 (design.md §8.2)
    int32_t combo_scale_air[5];     // Ballerina: 100/90/70/50/30 (design.md §11.2)
    Fixed   launch_height;
    uint8_t air_beats;          // gravity timeout (Breaker low, Ballerina +1)
    uint8_t passive_id;         // 0 = none, 1 = Stick the Landing, 2 = Sustain
};
```

Concrete numbers are **deliberately placeholder** — design.md §11 specifies only the *direction* of each delta; values are tuned in M1 playtesting.

---

## 4. The tick pipeline — `sim::tick(state, inputs, chars)`

A **single pure function** is the only way the world changes. No render, input, or audio code ever mutates `SimulationState` directly (research.md §11). Per tick, in order:

```
sim::tick(SimulationState& s, const FrameInput& in, const CharacterData chars[2]):

  1. s.tick += 1
     beat   = s.tick / TPB ;  phase = s.tick % TPB

  2. CAPTURE: for each player, if not yet locked this beat and a fresh A/B/C/D press
              arrived this tick, set fighter.commit = (input, tier_from_phase(phase), phase, locked=true)
              (first-press-wins, §1.4)

  3. SLIDE:   advance each fighter.pos toward move_target by one tick's fixed-point step (§2)
              advance anim_tick

  4. RESOLVE (only when phase == TPB/2, the window-close boundary, §1.4):
        if duel.macro == Neutral:    resolve_clash(s, chars)      // §5
        else:                         resolve_combo(s, chars)      // §6
        -> writes s.last_result, applies damage, sets move_targets,
           updates duel.macro/roles/combo_count, sets anim_tags

  5. ROUND CHECK: KO (health<=0), time-over (round_timer<=0), draw -> MatchState.phase

  6. BEAT ROLLOVER (only at phase == TPB/2): clock.beat_index++ ;
        clear both fighters' commit (input=None, tier=Miss, locked=false)

  7. round_timer_ticks-- (while Fighting)

  8. (dev builds) s.checksum() logged for the determinism harness (§7)
```

`FrameInput` is the per-frame snapshot built by the platform layer (research.md §4.1): `pressed[]`, `held[]`, `released[]` indexed by logical button, identical interface for keyboard, gamepad, and the AI/training dummy. The sim never sees SDL.

> **Catch-up note:** the fixed-timestep loop (research.md §2.1) may call `sim::tick` up to `MAX_STEPS_PER_FRAME` times in one render frame, passing the *same* `FrameInput` snapshot to each. Because step 2 is first-press-wins and idempotent for a held key, this is safe.

---

## 5. Neutral resolver — the RPS clash (`resolve_clash`)

Implements design.md §5 (RPS) + §7 (clash resolution) + §6 (timing).

### 5.1 The RPS table (single source of truth)

design.md §5.1: cycle `A>B>C>D>A` plus diagonals `A>C`, `D>B`. Encoded once as a 4×4:

```cpp
// beats[x][y] == true  iff input x beats input y   (indices: A=0,B=1,C=2,D=3)
static constexpr bool beats[4][4] = {
//        A      B      C      D
/* A */ {false, true,  true,  false},   // A > B, C   ;  loses to D
/* B */ {false, false, true,  false},   // B > C      ;  loses to A, D
/* C */ {false, false, false, true },   // C > D      ;  loses to A, B
/* D */ {true,  true,  false, false},   // D > A, B   ;  loses to C
};
```
A and D are the **strong/safe** class (each beats two, low damage); B and C are the **weak/committed** class (each beats one, high damage) — design.md §5.2. The damage class lives in `CharacterData.damage_ground[]`, not here.

### 5.2 Resolution order (design.md §7)

```
resolve_clash(s, chars):
  c0, c1 = fighters' commits
  land0 = (c0.input != None) && in_range(attacker=0, c0.input, chars)   // range gate, §5.3 / design §10.1
  land1 = (c1.input != None) && in_range(attacker=1, c1.input, chars)

  if  c0 Miss and c1 Miss:                  -> BothMiss   : small chip to both (anti-stall, design §6)
  elif not land0 and not land1:             -> BothWhiff  : nothing; stay Neutral
  elif land0 xor land1:                     -> OneLands   : lander wins -> Advantage
  elif c0.input != c1.input:                -> RpsDecided : beats[][] picks winner;
                                                            timing tier modulates winner's damage
                                                            (design §6: Perfect>Normal>Miss multiplier)
  else  (same input, both land):            -> SameTypeTie: timing breaks tie (Perfect>Normal>Miss);
                                                            if exact Perfect-Perfect tie -> re-clash next beat,
                                                            UNLESS a fighter has Sustain (Ballerina) -> she wins
  winner -> enter_advantage(s, winner)
```

- **`in_range`** compares the attacker's reach (`CharacterData.range[input]`) against the gap between fighters. Out of range = whiff = no damage, no penalty (design.md §10.1).
- **Damage** = `chars[winner].damage_ground[input]` × timing multiplier. Strong A/D land often for less; weak B/C land rarely for more — the yomi pyramid (design.md §5.2).
- **`enter_advantage`** sets `duel.macro = Advantage`, `attacker = winner`, `defender = loser`, `combo_count = 1`, applies the first hit's damage, and sets the attacker's anim tag.

### 5.3 Ballerina's *Sustain* passive

design.md §11.2: on a Perfect-Perfect same-input tie, Ballerina wins instead of going to re-clash. Implemented as the single branch in the `SameTypeTie` case above, keyed off `passive_id == 2`. It is **structural**, not temporal — it bends a tie-break rule, never the beat (`no-temporal-speed`).

---

## 6. Advantage resolver — combo loop (`resolve_combo`)

Implements design.md §8 (combo) + §9 (airborne) + §10.3 (cross-up). RPS is **dropped** in Advantage; the defender breaks by **exact match only** (design.md §5.3).

```
resolve_combo(s, chars):
  A = attacker commit ; D = defender commit
  if A.input == None / Miss:                     -> ComboMissEnd  : end -> Neutral
  elif D.input == A.input and D.tier == Perfect: -> ComboSteal    : swap roles, combo_count=1, defender now attacks
  elif D.input == A.input:                        -> ComboBreak    : end -> Neutral
  elif combo_count >= 5:                          -> ComboCapEnd   : apply final hit, end -> Neutral
  else:                                           -> ComboContinue : apply hit, combo_count++
```

### 6.1 Per-input behavior of the attacker's commit (design.md §4.1, §8, §9)

| Input | Grounded Advantage | Airborne Advantage |
|---|---|---|
| **A** | continue, close gap | close + attack toward opponent |
| **B** | **cross-up** teleport behind defender (§2), `facing` flips | cross-up (still works) |
| **C** | **launch** → enter airborne sub-state (both go airborne) | extend air combo, stay airborne |
| **D** | low sweep / **vertical equalizer**; if defender airborne, grounds them | grounds opponent first, then self (D-chain) |

### 6.2 Damage scaling

`damage = base × scale[combo_count] / 100`, using `combo_scale_ground[]` (100/80/60/40/20) or `combo_scale_air[]` (Ballerina 100/90/70/50/30, design.md §11.2). Attacker **Perfect** adds the configured bonus to *that* hit (design.md §6).

### 6.3 Breaker's *Stick the Landing* passive

design.md §11.1: a combo whose **final** hit is **D** deals **+25%** on that hit. Detected at `ComboCapEnd`/`ComboMissEnd`/`ComboBreak` when the attacker is Breaker (`passive_id == 1`) and the last applied input was D. Structural, not temporal.

### 6.4 Airborne sub-state (design.md §9)

`Fighter.airborne` + `airborne_beats` track it. Entry: attacker `C` in ground combo launches both. Exit: D-chain (beat 1 grounds opponent, beat 2 grounds attacker), **gravity timeout** at `CharacterData.air_beats` (Breaker low, Ballerina +1), or any combo-end (both land, return to Neutral). Defender rules are identical to grounded combo (match → break, match+Perfect → steal). *Edge cases (vertical mismatches) flagged TBD in design.md §16 — typed placeholders only here.*

---

## 7. Determinism, RNG, checksum (inherited from research.md §3)

Restated, not re-derived. The `neg_sim` library physically enforces these by having **zero SDL and zero float** dependencies (build-plan.md):

- **Fixed-point only** in sim: `Fixed` (Q16.16), `int64_t` intermediate for multiply/divide (research.md §3.4).
- **One seeded PRNG in `SimulationState`** (`Rng`, PCG32). The only randomness in the game is combo-steal texture and any tie jitter; it all routes through `s.rng`. No `rand()`, no `time()`.
- **`SimulationState::checksum()`** — FNV-1a over the POD state each tick, logged in dev builds. Record an input stream, replay it, and per-tick checksums must match exactly; first divergence localizes the bug (research.md §3.5). This is the backbone of both the test harness and the analyzer.
- **Replay** = initial `SimulationState` + per-tick `FrameInput` log (~tiny). Frame-step, the analyzer's scrubber, and future rollback all consume this same representation.

---

## 8. Frame / Beat Analyzer (phased)

A real frame-by-frame analyzer like engines ship — and for a beat-locked game it's the single highest-leverage tool, because it doubles as the **balance/tuning** instrument. It is cheap precisely because §7 already pays for determinism: POD state + input-log replay + per-tick checksum ⇒ scrubbing and inspection are mostly *views*, not new systems.

### 8.1 M0 — data plumbing + forward step (small)
- On-screen **frame counter** and **beat counter + phase bar** (reads `tick`, `beat_index`, `beat_phase`).
- **F5/F6/F7** pause / step-one-tick-forward / resume — already free from the loop design (research.md §2.4): in `Paused`, run a tick only on `step_request`, do not consume the accumulator.
- **F9/F10** input recording / replay; per-tick **checksum** logged.

### 8.2 M1 — inspection
- **F1** box overlay (hurt/hit/push/pivot) once art and boxes exist (research.md §5.7).
- **Inspector panel**: per fighter — `Macro`/`Role`/`health`/`pos`; this beat's committed `Input` + `Tier`; `last_result` (outcome, winner, damage). The whole `ResolutionResult` struct, rendered.
- **Active-frame flash** on the resolution tick.

### 8.3 M1 → M2 — scrubbing + beat log (the killer view)
- **Step backward** + a **timeline scrubber**, via either a ring buffer of recent `SimulationState`s (cheap — it's POD) or re-sim from a keyframe + the input log. Both are trivial because state is POD and `tick` is pure.
- **Beat log**: a scrollable table, one row per beat — `[beat# | P1 input/tier | P2 input/tier | outcome | dmg P1/P2 | combo#]`. This is the complete, replayable history of a match and the primary lens for debugging *and* tuning the RPS/combo numbers.

---

## 9. Module layout (`neg_sim` boundary enforced by the build)

Adjusted from research.md §1.4 for the rhythm systems (no motion-input/cancel modules):

```
src/
  sim/            ← deterministic. No SDL, no float (Q16.16 only). == neg_sim static lib.
    fixed.{h,cpp}        Q16.16 type + ops
    rng.{h,cpp}          PCG32 in-state PRNG
    beatclock.{h,cpp}    tick<->beat, phase, tier_from_phase
    input_commit.{h,cpp} FrameInput -> per-beat Commit (first-press-wins, tier banding)
    fighter.{h,cpp}      Fighter struct + per-tick slide
    clash.{h,cpp}        resolve_clash (RPS table, range gate, timing)
    combo.{h,cpp}        resolve_combo (continue/break/steal/cap, cross-up, scaling, passives)
    airborne.{h,cpp}     air sub-state entry/exit
    simulation.{h,cpp}   SimulationState + sim::tick pipeline + checksum
  data/           ← JSON -> CharacterData / config POD loaders
  platform/       ← SDL3 init, event pump -> FrameInput, window
  render/         ← reads SimulationState; never mutates. rectangles (M0) -> sprites (M1)
  audio/          ← miniaudio glue; reads beat_index, fires metronome/click/music (one-way, design §12)
  analyzer/       ← overlay, inspector panel, beat log, scrubber (reads state + checksum log)
  main.cpp        ← classic int main(); fixed-timestep loop (research.md §2.1)
```

`neg_sim` links into the game exe, the unit tests, and the headless runner alike — the same pure core, three front-ends (build-plan.md §3).

---

## 10. Traceability — design.md → this doc

| design.md | Realized in |
|---|---|
| §3 State machine (Neutral/Advantage/Airborne) | §3.4 `DuelState`, §4 pipeline step 4, §5–§6 resolvers |
| §4 Four inputs / state-sensitive / frame-invariant | §3.1 `Input`, §6.1 table, §2 (facing-derived direction) |
| §5 RPS layer + balancing axis | §5.1 `beats[][]`, `CharacterData.damage_ground[]` |
| §6 Timing layer (Perfect/Normal/Miss) | §1.3 tier bands, §5.2/§6.2 modulation |
| §7 Clash resolution (range gate) | §5.2 `resolve_clash`, §5.3 range |
| §8 Combo (scaling, cap, steal) | §6 `resolve_combo`, §6.2 scaling |
| §9 Airborne sub-state | §6.4, `Fighter.airborne`/`airborne_beats` |
| §10 Spatial (range, movement, cross-up, walls) | §2 movement, §5.3 range, §6.1 cross-up |
| §11 Characters (Breaker/Ballerina, passives) | §3.7 `CharacterData`, §5.3 Sustain, §6.3 Stick the Landing |
| §12 Audio-sim coupling | §9 `audio/` one-way, §1.1 sim owns the beat |
| §13 Technical architecture (summary) | this whole doc |
| §15 Milestones | build-plan.md §4 |
| §16 Open questions | typed placeholders; not resolved here |

---

## 11. Open technical questions (deferred, not blocking M0)
- **Resolution-boundary feel** — resolve at window-close (`+TPB/2`) vs. at the next instant. Starting with window-close; revisit if the half-beat delay feels laggy.
- **Commit rule** — first-press-wins vs. closest-to-instant. Starting first-press-wins; the alternative is a one-line change in §4 step 2.
- **Cosmetic impact freeze** — whether to add a render/audio-only "hit pop" that must not stall the shared beat. Pure presentation; decide in M1.
- **Airborne vertical mismatches** (design.md §9.4) — exact behavior when both inputs disagree on grounding. Placeholder until M1 playtest.
- All of design.md §16 (song-as-system, match economy, meter) — parked.
