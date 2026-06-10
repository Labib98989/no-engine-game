// The complete POD data model of the rhythm duel — technical.md §3.
// Everything here is trivially copyable: snapshots, replay, checksum and
// (later) rollback are memcpy-cheap.
#pragma once
#include <cstdint>
#include "sim/fixed.h"
#include "sim/rng.h"

namespace neg {

// ---- enumerations (technical.md §3.1) --------------------------------------

enum class Input : uint8_t { None = 0, A, B, C, D }; // the four dual inputs
enum class Tier : uint8_t { Miss = 0, Normal, Perfect };
enum class Macro : uint8_t { Neutral = 0, Advantage };
enum class Role : uint8_t { Free = 0, Attacker, Defender };
enum class CharId : uint8_t { Breaker = 0, Ballerina = 1 };
enum class Phase : uint8_t { Intro = 0, Fighting, RoundEnd, MatchEnd };

enum class PassiveId : uint8_t { None = 0, StickTheLanding = 1, Sustain = 2 };

enum class RoundEndReason : uint8_t { None = 0, KO, TimeOver, DoubleKO, TimeOverDraw };

// ---- per-beat commit (technical.md §3.2) -----------------------------------

struct Commit {
    Input input = Input::None;
    Tier tier = Tier::Miss;
    uint8_t press_phase = 0; // beat-phase tick of the committed press (analyzer)
    bool locked = false;     // first-press-wins flag for this beat
};

// ---- fighter (technical.md §3.3) -------------------------------------------

struct Fighter {
    CharId character = CharId::Breaker;
    int32_t health = 0;

    Fixed pos_x, pos_y; // feet anchor; world space. y >= 0, 0 = floor.
    Fixed move_target_x, move_target_y;
    bool facing_right = true;
    bool airborne = false; // combo air sub-state only (the Neutral C hop is cosmetic)

    Role role = Role::Free;
    Commit commit;

    uint16_t anim_tag = 0;  // combo-position cycling is cosmetic (design.md §8.3)
    uint16_t anim_tick = 0;

    Input last_committed = Input::None;
};

// ---- duel / combo bookkeeping (technical.md §3.4) ---------------------------

struct DuelState {
    Macro macro = Macro::Neutral;
    uint8_t attacker = 0;    // fighter index when macro == Advantage
    uint8_t defender = 1;
    uint8_t combo_count = 0; // hits applied so far this Advantage (cap 5)
    uint8_t air_beats_elapsed = 0;
    Input last_hit_input = Input::None; // Stick the Landing needs the final hit
    int32_t last_hit_damage = 0;
};

// ---- resolution result (technical.md §3.5) ----------------------------------

enum class Outcome : uint8_t {
    None = 0,
    BothWhiff, OneLands, RpsDecided, SameTypeTie, BothMiss, // Neutral
    ComboContinue, ComboBreak, ComboSteal, ComboCapEnd, ComboMissEnd, // Advantage
    Launch, GroundOut
};

struct ResolutionResult {
    Outcome outcome = Outcome::None;
    int8_t winner = -1; // fighter index, or -1
    int32_t damage_p0 = 0, damage_p1 = 0;
    Input p0_input = Input::None, p1_input = Input::None;
    Tier p0_tier = Tier::Miss, p1_tier = Tier::Miss;
    bool air = false;          // resolved inside the airborne sub-state
    uint32_t beat = 0;         // which beat this resolved
    uint64_t resolved_tick = 0; // tick the resolution ran (render/audio edge-detect)
};

// ---- clocks and match (technical.md §3.6) -----------------------------------

struct BeatClock {
    uint16_t bpm = 120;
    uint16_t ticks_per_beat = 30; // derived from bpm at init; global, identical for both players
    uint32_t beat_index = 0;      // the beat whose capture window is currently open
};

struct MatchState {
    Phase phase = Phase::Intro;
    uint8_t round = 1;
    uint8_t wins_p0 = 0, wins_p1 = 0;
    int32_t round_timer_ticks = 0; // counts down while Fighting; 0 => time-over
    int32_t phase_timer_ticks = 0; // Intro / RoundEnd countdowns
    int8_t round_winner = -1;      // -1 none/draw
    RoundEndReason end_reason = RoundEndReason::None;
    int8_t match_winner = -1;
};

// ---- tuning (global config; placeholder numbers, dialed in playtests) -------

struct Tuning {
    uint16_t bpm = 120;
    uint8_t perfect_ticks = 2; // d <= 2          -> Perfect (technical.md §1.3)
    uint8_t normal_ticks = 11; // 2 < d <= 11     -> Normal; beyond -> Miss

    int32_t chip_damage = 4;            // both-Miss anti-stall chip (design.md §6)
    int32_t perfect_damage_pct = 125;   // Perfect-timing damage multiplier
    int32_t stick_landing_bonus_pct = 25;

    uint8_t combo_cap = 5;

    uint16_t round_seconds = 99; // placeholder match economy (design.md §16 open)
    uint8_t rounds_to_win = 2;   // best-of-3
    uint8_t intro_beats = 4;
    uint16_t round_end_ticks = 180; // 3 s of KO/TIME banner before next round

    int32_t stage_width = 1200; // world units
    int32_t wall_margin = 60;
    int32_t min_gap = 70;       // fighters never overlap closer than this
    int32_t start_offset = 250; // from stage center
    int32_t crossup_gap = 90;   // landing distance behind the defender
};

// ---- character data (immutable config, NOT sim state — technical.md §3.7) ---

struct CharacterData {
    char name[16] = {0};
    int32_t health = 1000;
    Fixed range[5];              // indexed by Input (None unused); strike reach
    Fixed move_dist[5];          // per-input beat displacement (C = hop/launch rise)
    int32_t damage_ground[5];    // base damage per input
    int32_t combo_scale_ground[5]; // 100/80/60/40/20 (design.md §8.2)
    int32_t combo_scale_air[5];    // Ballerina decays slower (design.md §11.2)
    Fixed launch_height;
    uint8_t air_beats = 3; // gravity timeout (Breaker low, Ballerina +1)
    PassiveId passive = PassiveId::None;
};

// ---- per-tick input snapshot (platform -> sim; never SDL) --------------------

struct FrameInput {
    // bit 0..3 = A..D, set on the tick the key edge arrived
    uint8_t pressed[2] = {0, 0};
};

// ---- top-level state ---------------------------------------------------------

struct SimulationState {
    uint64_t tick = 0;
    Rng rng;
    Tuning tune;
    BeatClock clock;
    MatchState match;
    Fighter fighters[2];
    DuelState duel;
    ResolutionResult last_result;

    uint64_t checksum() const; // FNV-1a over fields (simulation.cpp)
};

} // namespace neg
