# No Engine Game — Game Design

**Working title:** TBD
**Genre:** Rhythm Yomi Duel — beat-locked 1v1 dance combat
**Scope (M1):** 2 characters, 1 stage, local 2-player
**References:** *Hi-Fi Rush* (rhythm-modulated combat), *Yomi* / *Crypt of the Necrodancer* / *Divekick* (yomi duel)
**Companion docs:** `docs/technical.md` (systems/implementation design — how this maps to code), `docs/research.md` (infrastructure foundation — stack, build, sim loop, determinism), `docs/build-plan.md` (how to assemble/build/test with no engine)
**Date:** 2026-05-20

---

## 1. Concept

Two dancers face off in a beat-locked duel. Every beat, both players simultaneously commit one of four inputs. Each input is **both a spatial movement and an attack**. Clashes resolve through an asymmetric 4-way rock-paper-scissors layer, modulated by rhythm timing. Winning a clash puts you in *Advantage* and starts a combo; the opponent must predict your inputs to break free.

This is **not** a Street Fighter clone. It is a beat-locked yomi duel with two players sharing one metronome. Street Fighter is a vocabulary reference (round structure, 1v1, hit/health), not a mechanical one.

## 2. Design Pillars

1. **Shared-beat duel.** Both players input on the same metronome. Theatrical, primal, spectatable.
2. **Predict, don't react.** Simultaneous commits eliminate reactive play. All depth comes from yomi.
3. **Constraint forces depth.** Four inputs only. Variety comes from state-sensitive interpretation, RPS layer, character roster, and song structure — never from input count.
4. **Highlight-moment first.** Advantage swap, Perfect steal, clean read — these are the moments the design optimizes for.
5. **Spectator-readable.** A friend in the room can follow who's pressuring, who got read, who broke.

## 3. State Machine

Two explicit states + one implicit sub-state.

| State | Description |
|---|---|
| **Neutral** | Both players free to act. RPS clash decides each beat. |
| **Advantage** | One player attacks (Attacker role), one defends (Defender role). Combo state. |
| **Airborne** *(sub-state inside Advantage)* | Active after Attacker's C launch. Both characters airborne. |

P1-Advantage and P2-Advantage are the **same state with roles swapped** — build once, parameterize the role.

### Transitions

```
Neutral
  ├─→ Advantage           (clash winner becomes Attacker)
  └─→ stays Neutral       (clash tie or both whiff)

Advantage (Attacker A, Defender D)
  ├─→ Neutral             (D matches A's input, OR A misses, OR combo length cap reached)
  ├─→ Advantage (swapped) (D matches AND hits Perfect — "steal")
  └─→ Airborne sub-state  (A presses C launch)

Airborne (sub-state of Advantage)
  ├─→ stays Airborne      (A presses C)
  └─→ grounded Advantage  (A D-chain to ground both, OR gravity timeout at 4-5 beats)
```

## 4. Input System

### 4.1 The Four Inputs

Each input is a dual commitment: **spatial movement + attack**. Beat-locked. Shared metronome.

| Input | Spatial | Attack (Neutral) | In Combo (Attacker) | In Combo (Defender) |
|---|---|---|---|---|
| **A** | Close (forward) | Jab, short range | Continues combo, closes gap | Tries to match |
| **B** | Far (retreat) | Poke, long range | **Cross-up** — teleport behind opponent | Tries to match |
| **C** | Up (leap) | Leaping attack | **Launch** — both go airborne | Tries to match |
| **D** | Down (drop) | Low sweep | **Vertical equalizer** — grounds airborne opponent | Tries to match |

### 4.2 State-Sensitive Interpretation

Same input, different mechanical role per state. B is the clearest example: retreat-poke in Neutral, cross-up disorient in Combo. Across 3 contexts (Neutral / Attacker / Defender), the 4 inputs map to **12 effective moves** without expanding the input space.

### 4.3 Inputs Are Frame-Invariant

Inputs are close / far / up / down — *not* left / right. Cross-up never inverts a defender's input mapping; the player always presses the same buttons regardless of side.

## 5. RPS Layer (Neutral Only)

### 5.1 Structure

Asymmetric 4-way RPS. Math forces 2 strong + 2 weak — no balanced 4-way exists without ties.

```
A > B > C > D > A   (cycle)
A > C, D > B        (diagonals)
```

| Input | Beats | Loses to | Role |
|---|---|---|---|
| **A** | B, C | D | Strong / safe poke |
| **B** | C | A, D | Weak / committed read |
| **C** | D | A, B | Weak / committed read |
| **D** | A, B | C | Strong / safe poke |

### 5.2 Balancing Axis

To prevent B and C from being trash picks:

- **Strong (A, D)** → lower damage. Safe pokes, likely to win clash.
- **Weak (B, C)** → higher damage. Committed reads, huge payoff when they land.

Yomi pyramid emerges: opponent expects safe → you commit to a hard-hitter → opponent counter-reads with the safe attack that beats your commit. Mind games, not blind dice.

### 5.3 RPS Applies Only In Neutral

In Combo, RPS is dropped. Defender breaks combo by **exact-match only**. This prevents degenerate forced-choice combo continuations (asymmetric RPS would force B→C and C→D dead-ends).

## 6. Timing Layer

Three tiers per input:

- **Perfect** — tight window (~15% of beat)
- **Normal** — wider window (~70% of beat)
- **Miss** — outside the window, or no input

### Role of Timing

| Context | Effect |
|---|---|
| Neutral, different types | RPS decides winner; timing modulates damage |
| Neutral, same type | Timing breaks the tie (Perfect > Normal > Miss) |
| Neutral, attacker Miss | Auto-loss to any landed input |
| Neutral, both Miss | Small chip damage to both (anti-stall) |
| Combo, attacker Perfect | Bonus damage that hit |
| Combo, defender match + Perfect | **Steal advantage** (defender becomes attacker) |
| Combo, attacker Miss | Combo ends |

## 7. Clash Resolution (Neutral)

**Range gatekeeps RPS.** Only landed attacks enter the comparison.

```
Each beat in Neutral:
  1. Both players commit input + timing
  2. Check range for each attack
  3. Both whiff       → nothing happens, stay in Neutral
  4. One whiff, one land → lander wins
  5. Both land, different → RPS decides; timing modulates damage
  6. Both land, same     → timing breaks tie
  7. Both Miss        → small chip damage to both
  8. Winner enters Advantage; loser enters Disadvantage
```

Practical effect: spacing dictates whether you even get to play the RPS. Footsies emerges through input choice alone.

## 8. Combo (Advantage State)

### 8.1 Loop

```
While in Advantage:
  Attacker presses any of 4         (any input continues combo)
  Defender presses any of 4         (trying to match)

  if Attacker Miss                  → combo ends, return Neutral
  if Defender matches               → combo ends, return Neutral
  if Defender matches AND Perfect   → roles swap, Defender steals advantage
  if Attacker Perfect               → bonus damage this hit
  if combo length cap (5) reached   → combo ends, return Neutral
```

### 8.2 Damage Scaling

| Hit # | Damage % |
|---|---|
| 1 | 100% |
| 2 | 80% |
| 3 | 60% |
| 4 | 40% |
| 5 | 20% |

Hard cap at 5 hits. Prevents combo snowball; keeps exchanges frequent.

### 8.3 Combo-Position Cycling (Cosmetic)

Each successive beat in a combo plays a different animation for the same input. Pure visual variety. No mechanical effect. Stops combos from looking samey.

### 8.4 Defender Escape — Probability Math

- Match chance per beat: 25% (4 inputs, blind guess)
- Perfect chance per beat: ~15% (timing window)
- Steal chance per beat: 25% × 15% = **~3-4%**
- Over a 4-beat combo: ~13-15% any steal occurs

Rare enough to be hype, common enough to threaten. Acceptable RNG-as-texture given short capped combos.

## 9. Airborne Sub-State

### 9.1 Entry

Only via Attacker's **C** during ground combo. Launches opponent; Attacker jumps with them. Both airborne.

### 9.2 Inputs While Airborne (Attacker)

| Input | Behavior |
|---|---|
| **A** | Close + attack, horizontal toward opponent |
| **B** | Cross-up — teleport behind opponent (still works) |
| **C** | Keep both airborne, extend air combo |
| **D** | Grounds opponent first; if opponent already grounded, grounds self |

### 9.3 Exit

- **Attacker D-chain:** beat 1 grounds opponent, beat 2 grounds Attacker.
- **Gravity timeout:** auto-grounded after 4-5 beats.
- **Combo ends** (defender matches, attacker misses, length cap) — both land back, return to Neutral.

### 9.4 Defender In Airborne

Same rules as grounded combo: match → break, match + Perfect → steal. *Edge cases TBD.*

## 10. Spatial System

### 10.1 Range

Each attack has a strike range:
- **In range:** animation closes gap, attack lands.
- **Out of range:** whiff — character hits air. No damage, no penalty.

### 10.2 Movement Resolution

Both players move simultaneously each beat. **Walls bound the arena** — corner pressure is a free dynamic, prevents perma-retreat strategies.

### 10.3 Cross-Up

B in combo teleports Attacker behind Defender for disorient. Inputs are frame-invariant (close/far/up/down) so cross-up never inverts defender mappings — purely positional confusion.

## 11. Characters

Two dancers ship in M1, each named for its dance style. Differentiation follows the *no-temporal-speed* principle: same shared beat, different spatial ranges, movement distances, damage values, and one signature passive per character. No character ever acts off-beat — only off-distance.

### 11.1 Breaker

**Dance vocabulary:** b-boy / floorwork — windmills, freezes, sweeps, headspins. Grounded, percussive, hits the beat hard.

**Axis:** horizontal + ground. Lives on A (close brawl) and D (sweep).

| Kit delta | Direction |
|---|---|
| A range | Extended (gap-closer is strong) |
| D-sweep arc | Wide ground coverage |
| B retreat distance | Shorter (commits harder when retreating) |
| A advance distance | Longer (closes faster) |
| C launch height | Low ceiling; fewer airborne beats before gravity-out |
| Health | Above average |
| Damage curve | Boosted on A/D (strong/safe class) |

**Signature passive — Stick the Landing.** Combos that end on **D** deal **+25%** damage on the final hit. Telegraphs the rhythm; opponent must read whether Breaker will fake the D or commit.

### 11.2 Ballerina

**Dance vocabulary:** ballet / contemporary — pirouettes, grand jetés, lift-work. Vertical, flowing, plays the space between hits.

**Axis:** vertical + air. Lives on B (graceful retreat-poke) and C (launch).

| Kit delta | Direction |
|---|---|
| B range | Extended (poke across stage) |
| A range | Short (gets bullied point-blank) |
| B retreat distance | Longer (light footwork) |
| A advance distance | Shorter |
| C launch | Higher; airborne sub-state lasts +1 beat |
| Air combo scaling | Slower decay (100/90/70/50/30 air vs 100/80/60/40/20 ground) |
| Health | Below average |
| Damage curve | Boosted on B/C (weak/committed class) |

**Signature passive — Sustain.** On a **Perfect-input clash tie** (both pressed same input, both Perfect), Ballerina wins instead of going to re-clash. Rewards rhythm precision; forces Breaker to avoid mirroring Ballerina on key beats.

### 11.3 Matchup Tension

The **C launch** is the contested move. Breaker's air game is weak; Ballerina's is best in the game. So:

- Breaker chooses *when to refuse C* and *how to punish Ballerina's C bait*.
- Ballerina chooses *when to force C* and *how to bait Breaker into it*.

Same engine. Different game.

## 12. Audio-Sim Coupling

**The simulation tick is the source of truth for "what beat we're on."** Audio playback chases the sim, never the reverse.

Rationale: deterministic replays, future rollback netcode, audio drift cannot corrupt gameplay timing.

Concrete rule: `beat_index = tick / TICKS_PER_BEAT`, where `TICKS_PER_BEAT = 60 / (BPM / 60)`. At 120 BPM and 60Hz sim, that's 30 ticks per beat.

## 13. Technical Architecture

*(Summary only. **Full detail in `docs/technical.md`** — the authoritative systems/implementation design that maps this gameplay onto data structures and the tick pipeline. Infrastructure foundation lives in `docs/research.md`; build/test path in `docs/build-plan.md`.)*

### 13.1 Stack

- **Language:** C++17, C-like style. RAII for SDL handles; no inheritance trees.
- **Compiler:** MSVC 2022, `/fp:strict`, `/W4`, `/permissive-`.
- **Build:** CMake 3.25+ with `CMakePresets.json`, vcpkg manifest mode.
- **Windowing / input / render:** SDL3 3.4.x.
- **Image loading:** SDL3_image.
- **Audio:** miniaudio (vendored single-header).
- **Loop:** classic `int main()`, fixed-timestep 60Hz simulation, no render interpolation, VSync on.
- **Numerics:** Q16.16 fixed-point in `/sim`. No floats in simulation core.

### 13.2 Module Layout

```
src/
  sim/         deterministic. No SDL, no float (Q16.16 only).
  data/        JSON → POD loaders
  render/      SDL3-dependent. Reads SimulationState; never mutates.
  platform/    SDL3 init, event pump, window
  audio/       miniaudio glue
  main.cpp
```

`neg_sim` is a static library with zero SDL/system deps — physically enforces the determinism boundary.

### 13.3 Sim Loop

Fixed-timestep 60Hz with accumulator pattern. Render reads latest sim state directly. Spiral-of-death clamps: 250ms max frame, 5 sim steps per render frame.

### 13.4 Day-One Debug Features

- **F1:** hitbox / hurtbox / pushbox / pivot overlay
- **F5 / F6 / F7:** pause / step one tick / resume
- **F9 / F10:** input recording / replay
- On-screen frame counter
- Beat counter HUD
- Sim state checksum per tick (dev builds)

## 14. Authoring Pipeline

*(Inherited from research doc.)*

- **Aseprite** as single source: sprites, frame durations, animation tags, pivots, hitbox slices.
- **JSON per move**: damage, hitstun, RPS-type, range, cancel rules; references Aseprite frame tags.
- **Per-character atlas** + shared FX/HUD/stage atlases (~5 textures total).
- Frame timings stored as ticks at load. Game never speaks in milliseconds.
- All boxes authored canonical right-facing; mirror at runtime.

## 15. Milestone Roadmap

### M0 — Foundation Gate (1-2 weeks)

- CMake + vcpkg + SDL3 + SDL3_image + miniaudio building on Windows/MSVC.
- Window opens, floor line, two colored rectangles.
- P1 (WASD) and P2 (arrows) can input the 4 actions on a shared metronome.
- Fixed 60Hz loop, on-screen frame counter, beat counter.
- Debug overlay stub (toggleable empty layer).
- `neg_sim` static library: zero SDL/float deps verified.

### M1 — Playable Duel (6-10 weeks)

- Two dancers with full 4-input movesets across all states.
- Asymmetric RPS clash resolution + timing tiers.
- Combo system with damage scaling, length cap, cross-up, airborne sub-state.
- Range/whiff system, walls.
- Health bars, rounds, timer, KO / time-over / draw.
- Aseprite → atlas pipeline functional with placeholder pixel art.
- F1 hitbox overlay, F5-F7 frame-step, F9-F10 record/replay.

### M2 — Depth + Polish

- Character differentiation passes (dance styles diverge mechanically).
- Song-as-system: verse / chorus / drop rule modulation.
- Audio polish: hit SFX pool, voice clips, BGM sync.
- AI training-mode dummy.

### M3 — Optional Rollback Netcode

- Determinism boundary already paid for in M0 → drop-in GGPO integration.

## 16. Open Design Questions

To be decided in future sessions:

1. **Song-as-system.** Does song structure (verse / chorus / drop) modulate rules mid-fight? Damage spikes on chorus? Tighter Perfect window on drops? Tempo changes? *Constraint: any tempo change must affect both players equally — no one-sided temporal asymmetry.*
2. **Match economy.** Health per round. Best-of-3 vs single round. Round timer length. KO / time-over / draw rules.
3. **Visual presentation.** UI for state (Neutral / Attacker / Defender), beat indicator, damage/health bars, combo counter, audiovisual feedback for clash outcomes.
4. **Audio design.** Music tracks per stage vs per character. SFX pool sizing. Mixing rules.
5. **Onboarding / tutorial.** First-time player flow. Practice mode design.
6. **Meter / super system.** Optional power-spike layer. Currently no meter — should there be?
7. **Airborne edge cases.** Defender escape behavior specifics. Gravity transition handling. Vertical state mismatches.
8. **Character numeric values.** Section 11 specifies *direction* of each kit delta (extended / shorter / boosted / etc.). Concrete numbers (tile ranges, HP totals, damage values, beat counts) get tuned during M1 playtesting.
