// CPU opponent: per-beat maximin play + a difficulty error model — design.md §17,
// technical.md §12. Lives in neg_sim (fixed-point/integer only) but OUTSIDE
// SimulationState: the bot owns its own PCG32 and never touches s.rng, so sim
// checksums are identical to human-vs-human play. Its output enters the sim as
// ordinary FrameInput bits — the same contract as the keyboard (research.md §11)
// — and is therefore ticklog-recorded and replay-transparent.
#pragma once
#include "sim/types.h"

namespace neg {

// ---- difficulty knobs (POD; mirrors assets/ai/*.json like CharacterData) ----
// Difficulty only degrades the maximin anchor. It never reads the opponent's
// hidden commit and never touches the shared clock (design.md §11: no temporal
// differentiation — the bot's "speed" is timing accuracy, not a shorter beat).
struct AiConfig {
    char name[16] = {0};
    uint16_t solver_iters = 256;      // fictitious-play iterations per decision
    uint8_t policy_noise_pct = 25;    // 0..100: swap the maximin sample for a
                                      // character-flavored habit pick
    uint8_t drop_pct = 3;             // 0..100: skip the beat entirely (silence)
    uint8_t aim_min_ticks = 0;        // |press offset| from the beat instant,
    uint8_t aim_max_ticks = 9;        //   uniform in [min, max]; sign random
    int32_t v_advantage = 120;        // damage-equivalent value of entering Advantage
    int32_t whiff_approach_bonus = 6; // Neutral shaping: prefer A when nothing lands
};

enum class AiPreset : uint8_t { Easy = 0, Normal = 1, Hard = 2 };

// In-code mirror of assets/ai/*.json (chardata.cpp pattern): tests and the
// headless runner get the presets with zero JSON.
AiConfig default_ai_config(AiPreset p);

// ---- no-cheat observation contract ------------------------------------------
// Everything the bot may legally see: exactly what a human opponent sees.
// Built ONLY by ai_make_view; deliberately excludes the opponent's
// current-window commit (simultaneous reveal is the yomi core, design.md §2)
// and s.rng. Past beats are public once resolved — they live in last_result.
struct AiView {
    uint64_t tick = 0;             // observed sim tick; our output is for tick+1
    uint16_t ticks_per_beat = 30;
    uint32_t beat_index = 0;       // the beat whose capture window is open
    bool fighting = false;         // match.phase == Fighting
    Macro macro = Macro::Neutral;
    bool self_is_attacker = false; // valid while macro == Advantage
    uint8_t combo_count = 0;
    uint8_t air_beats_elapsed = 0;
    bool self_airborne = false;
    bool opp_airborne = false;
    Fixed anchor_gap;              // what the clash range gate will actually read
    int32_t self_health = 0, opp_health = 0;
    Commit self_commit;            // own commit only — never the opponent's
    CharId self_char = CharId::Ballerina;
    CharId opp_char = CharId::Breaker;
    Tuning tune;                   // public immutable config (no hidden info)
    ResolutionResult last_result;  // PAST beat only; public once resolved
    uint8_t self_index = 1;
};

AiView ai_make_view(const SimulationState& s, int player);

// ---- bot state ----------------------------------------------------------------
// What the bot legitimately remembers across beats: the resolved history a
// human would also remember. Unused by the v1 policy (no opponent modeling);
// it exists so a future adaptive knob is a pure addition (design.md §17).
struct AiHistoryEntry {
    uint32_t beat = 0;
    Input self_input = Input::None, opp_input = Input::None;
    Tier self_tier = Tier::Miss, opp_tier = Tier::Miss;
    Outcome outcome = Outcome::None;
};

struct AiState {
    Rng rng;                     // own PCG32, seeded from the match seed + player
    uint8_t player = 1;
    uint32_t decided_beat = 0xFFFFFFFFu; // window we already planned
    Input planned_input = Input::None;   // None == deliberate silence (drop)
    uint64_t planned_tick = 0;
    uint64_t last_recorded_tick = 0;     // history-ring dedupe
    AiHistoryEntry history[32];
    uint8_t history_len = 0;
    uint8_t history_head = 0;
};

void ai_init(AiState& a, uint64_t seed, int player);

// Per-tick entry point. Returns the pressed bits (bit i = Input(i+1)) for the
// assigned player's NEXT tick; 0 on idle ticks. Decides once per window
// (action + target press tick), emits on the target tick only — first-press-
// wins in capture_commits makes any accidental double harmless anyway.
uint8_t ai_update(AiState& a, const AiView& v, const AiConfig& cfg,
                  const CharacterData chars[2]);

// Exposed for tests: fictitious play on an n<=5 zero-sum matrix (row player
// maximizes). Deterministic: fixed iteration count, integer payoffs, ties
// break to the lowest index. Both players' empirical mixtures are returned as
// counts summing to iters; sample by walking counts with rng.next_below(iters).
void ai_solve_zero_sum(const int32_t payoff[5][5], int n_rows, int n_cols,
                       uint16_t iters, uint16_t out_row_counts[5],
                       uint16_t out_col_counts[5]);

} // namespace neg
