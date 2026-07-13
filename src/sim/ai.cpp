#include "sim/ai.h"
#include "sim/clash.h"

namespace neg {

// ---- presets (in-code mirror of assets/ai/*.json, chardata.cpp pattern) -----

static void set_ai_name(AiConfig& c, const char* n) {
    int i = 0;
    for (; n[i] && i < 15; ++i) c.name[i] = n[i];
    c.name[i] = 0;
}

AiConfig default_ai_config(AiPreset p) {
    AiConfig c{};
    c.v_advantage = 120;       // ~ expected combo damage past hit #1 before a break
    c.whiff_approach_bonus = 6;
    switch (p) {
    case AiPreset::Easy:
        // Sloppy hands, strong habits: never aims the instant, drops beats.
        set_ai_name(c, "EASY");
        c.solver_iters = 128;
        c.policy_noise_pct = 55;
        c.drop_pct = 12;
        c.aim_min_ticks = 2;
        c.aim_max_ticks = 13; // ~8% Perfect / 75% Normal / 17% Miss band
        break;
    case AiPreset::Normal:
        set_ai_name(c, "NORMAL");
        c.solver_iters = 256;
        c.policy_noise_pct = 25;
        c.drop_pct = 3;
        c.aim_min_ticks = 0;
        c.aim_max_ticks = 9; // 30% Perfect / 70% Normal
        break;
    case AiPreset::Hard:
        set_ai_name(c, "HARD");
        c.solver_iters = 512;
        c.policy_noise_pct = 8;
        c.drop_pct = 0;
        c.aim_min_ticks = 0;
        c.aim_max_ticks = 4; // 60% Perfect / 40% Normal, never Miss
        break;
    }
    return c;
}

// ---- observation window -------------------------------------------------------

AiView ai_make_view(const SimulationState& s, int player) {
    const Fighter& self = s.fighters[player];
    const Fighter& opp = s.fighters[1 - player];
    AiView v{};
    v.tick = s.tick;
    v.ticks_per_beat = s.clock.ticks_per_beat;
    v.beat_index = s.clock.beat_index;
    v.fighting = s.match.phase == Phase::Fighting;
    v.macro = s.duel.macro;
    v.self_is_attacker = s.duel.macro == Macro::Advantage && s.duel.attacker == player;
    v.combo_count = s.duel.combo_count;
    v.air_beats_elapsed = s.duel.air_beats_elapsed;
    v.self_airborne = self.airborne;
    v.opp_airborne = opp.airborne;
    // The gap the coming resolution's range gate will read (clash.cpp): the
    // window-open anchors, not the live mid-slide positions.
    v.anchor_gap = s.duel.anchor_ready
                       ? Fixed::abs(s.duel.anchor_x[0] - s.duel.anchor_x[1])
                       : Fixed::abs(s.fighters[0].pos_x - s.fighters[1].pos_x);
    v.self_health = self.health;
    v.opp_health = opp.health;
    v.self_commit = self.commit; // own only — the opponent's stays hidden
    v.self_char = self.character;
    v.opp_char = opp.character;
    v.tune = s.tune;
    v.last_result = s.last_result;
    v.self_index = (uint8_t)player;
    return v;
}

// ---- fictitious play ------------------------------------------------------------
// Alternating best responses against the opponent's empirical mixture converge
// on zero-sum games (exploitability ~ 1/sqrt(iters) — plenty under the error
// model's noise floor at 256+ iterations on a 4x4). Pure integer arithmetic,
// lowest-index tie-breaks: bit-identical on every platform.

void ai_solve_zero_sum(const int32_t payoff[5][5], int n_rows, int n_cols,
                       uint16_t iters, uint16_t out_row_counts[5],
                       uint16_t out_col_counts[5]) {
    if (n_rows < 1) n_rows = 1;
    if (n_rows > 5) n_rows = 5;
    if (n_cols < 1) n_cols = 1;
    if (n_cols > 5) n_cols = 5;
    if (iters == 0) iters = 1;
    for (int k = 0; k < 5; ++k) {
        out_row_counts[k] = 0;
        out_col_counts[k] = 0;
    }
    for (uint16_t it = 0; it < iters; ++it) {
        // Row (maximizer) best-responds to the column player's play so far;
        // on the first iteration that is the uniform mixture.
        int64_t best = 0;
        int bi = 0;
        for (int i = 0; i < n_rows; ++i) {
            int64_t val = 0;
            for (int j = 0; j < n_cols; ++j)
                val += (int64_t)payoff[i][j] * (it == 0 ? 1 : (int64_t)out_col_counts[j]);
            if (i == 0 || val > best) {
                best = val;
                bi = i;
            }
        }
        out_row_counts[bi]++;
        // Column (minimizer) best-responds to the rows including this update.
        int64_t worst = 0;
        int bj = 0;
        for (int j = 0; j < n_cols; ++j) {
            int64_t val = 0;
            for (int i = 0; i < n_rows; ++i)
                val += (int64_t)payoff[i][j] * (int64_t)out_row_counts[i];
            if (j == 0 || val < worst) {
                worst = val;
                bj = j;
            }
        }
        out_col_counts[bj]++;
    }
}

// ---- payoff matrices ------------------------------------------------------------
// Entries are expected damage delta to the bot, both sides assumed Normal tier:
// timing quality lives entirely in the error model, never in the matrix.
// Perfect-tier terms (Sustain, the x1.25, tie-breaks) are documented deferrals
// (technical.md §12) — they only sharpen values the solver already ranks.

// Neutral mirrors resolve_clash (clash.cpp): range gates the RPS against the
// window-open anchor gap; landed-vs-whiffed decides before the table does.
static void build_neutral(int32_t m[5][5], const AiView& v, const AiConfig& cfg,
                          const CharacterData& self, const CharacterData& opp) {
    Fixed gap = v.anchor_gap;
    bool any_lands = false;
    for (int i = 1; i <= 4; ++i)
        if (self.range[i] >= gap) any_lands = true;
    for (int i = 1; i <= 4; ++i) {
        bool land_s = self.range[i] >= gap;
        int32_t val_s = base_damage(self, (Input)i, Tier::Normal, v.tune) + cfg.v_advantage;
        for (int j = 1; j <= 4; ++j) {
            bool land_o = opp.range[j] >= gap;
            int32_t val_o = base_damage(opp, (Input)j, Tier::Normal, v.tune) + cfg.v_advantage;
            int32_t val = 0;
            if (land_s && !land_o) {
                val = val_s;
            } else if (!land_s && land_o) {
                val = -val_o;
            } else if (land_s && land_o) {
                // Same input, both land: Normal-Normal ties re-clash (0).
                if (i != j) val = input_beats((Input)i, (Input)j) ? val_s : -val_o;
            } else if (!any_lands && (Input)i == Input::A) {
                // Fully outranged: shade toward A so the gap closes and a game
                // exists next beat. Richer positional value (walls, retreat at
                // a health lead) is a documented deferral.
                val = cfg.whiff_approach_bonus;
            }
            m[i - 1][j - 1] = val;
        }
    }
}

// Advantage from the ATTACKER's perspective: rows = attacker input, cols =
// defender guess. RPS is off (design.md §8) — a match breaks (0), a mismatch
// lands the scaled hit. Steal pricing on the diagonal is a v1 deferral: the
// defender's Perfect rate is a timing-model quantity, not a matrix one.
static void build_advantage(int32_t m[5][5], const AiView& v, const CharacterData& acd) {
    bool air = v.self_airborne || v.opp_airborne;
    const int32_t* scale = air ? acd.combo_scale_air : acd.combo_scale_ground;
    int hit = v.combo_count > 4 ? 4 : (int)v.combo_count;
    // The cap-final hit is guaranteed to end the combo, so a D that lands there
    // collects Stick the Landing for sure (end_combo, combo.cpp). Mid-combo D
    // bonuses are contingent on a later non-steal end and stay unpriced.
    bool final_hit = (int)v.combo_count == (int)v.tune.combo_cap - 1;
    for (int i = 1; i <= 4; ++i) {
        int32_t dmg = base_damage(acd, (Input)i, Tier::Normal, v.tune) * scale[hit] / 100;
        if (final_hit && acd.passive == PassiveId::StickTheLanding && (Input)i == Input::D)
            dmg += dmg * v.tune.stick_landing_bonus_pct / 100;
        for (int j = 1; j <= 4; ++j)
            m[i - 1][j - 1] = i == j ? 0 : dmg;
    }
}

// ---- decision -------------------------------------------------------------------

static Input sample_counts16(const uint16_t counts[5], int n, uint32_t r) {
    for (int k = 0; k < n; ++k) {
        if (r < counts[k]) return (Input)(k + 1);
        r -= counts[k];
    }
    return Input::A; // counts sum to the sample bound; not reached
}

// Low-difficulty mistakes lean into the character (design.md §11): Breaker errs
// toward his strong/safe A/D pair, Ballerina toward her committed B/C pair.
// Weights sum to 100.
static Input flavored_pick(CharId c, uint32_t r) {
    static const uint8_t breaker[4] = {35, 15, 15, 35};
    static const uint8_t ballerina[4] = {15, 35, 35, 15};
    const uint8_t* w = c == CharId::Breaker ? breaker : ballerina;
    for (int k = 0; k < 4; ++k) {
        if (r < w[k]) return (Input)(k + 1);
        r -= w[k];
    }
    return Input::A;
}

// A human would remember how past beats resolved; so may the bot. Unused by the
// v1 policy — it exists so a future adaptive knob is a pure addition.
static void record_history(AiState& a, const AiView& v) {
    const ResolutionResult& r = v.last_result;
    if (r.resolved_tick == 0 || r.resolved_tick == a.last_recorded_tick ||
        r.outcome == Outcome::None)
        return;
    AiHistoryEntry e{};
    e.beat = r.beat;
    bool self0 = v.self_index == 0;
    e.self_input = self0 ? r.p0_input : r.p1_input;
    e.self_tier = self0 ? r.p0_tier : r.p1_tier;
    e.opp_input = self0 ? r.p1_input : r.p0_input;
    e.opp_tier = self0 ? r.p1_tier : r.p0_tier;
    e.outcome = r.outcome;
    a.history[a.history_head] = e;
    a.history_head = (uint8_t)((a.history_head + 1u) % 32u);
    if (a.history_len < 32) a.history_len++;
    a.last_recorded_tick = r.resolved_tick;
}

static void decide(AiState& a, const AiView& v, const AiConfig& cfg,
                   const CharacterData chars[2]) {
    a.decided_beat = v.beat_index;
    uint16_t iters = cfg.solver_iters ? cfg.solver_iters : 1;

    // Fixed draw schedule: every decision consumes the same rng call sequence
    // whatever branch it takes, so the stream stays trivially auditable.
    uint32_t r_drop = a.rng.next_below(100);
    uint32_t r_noise = a.rng.next_below(100);
    uint32_t r_mix = a.rng.next_below(iters);
    uint32_t r_flavor = a.rng.next_below(100);
    uint32_t span = cfg.aim_max_ticks >= cfg.aim_min_ticks
                        ? (uint32_t)(cfg.aim_max_ticks - cfg.aim_min_ticks) + 1u
                        : 1u;
    uint32_t r_mag = a.rng.next_below(span);
    uint32_t r_sign = a.rng.next_below(2);

    // Perfect-play anchor: solve the current beat's zero-sum game and sample
    // the maximin mixture — unexploitable, so no habit the human develops can
    // beat it long-term. Everything after this line only degrades it.
    const CharacterData& self_cd = chars[(int)v.self_char];
    const CharacterData& opp_cd = chars[(int)v.opp_char];
    int32_t m[5][5] = {};
    uint16_t rc[5] = {};
    uint16_t cc[5] = {};
    Input pick;
    if (v.macro == Macro::Neutral) {
        build_neutral(m, v, cfg, self_cd, opp_cd);
        ai_solve_zero_sum(m, 4, 4, iters, rc, cc);
        pick = sample_counts16(rc, 4, r_mix);
    } else if (v.self_is_attacker) {
        build_advantage(m, v, self_cd);
        ai_solve_zero_sum(m, 4, 4, iters, rc, cc);
        pick = sample_counts16(rc, 4, r_mix);
    } else {
        // Defender: the same game seen from the attacker's side; our maximin
        // guess mix is the minimizing COLUMN mixture of the attacker's matrix.
        build_advantage(m, v, opp_cd);
        ai_solve_zero_sum(m, 4, 4, iters, rc, cc);
        pick = sample_counts16(cc, 4, r_mix);
    }

    // Error model, difficulty's whole surface: what it picks, then whether it
    // shows up at all. When (the timing spread) is below.
    if (r_noise < cfg.policy_noise_pct) pick = flavored_pick(v.self_char, r_flavor);
    if (r_drop < cfg.drop_pct) pick = Input::None;

    // Timing: aim an offset around the beat instant; the tier bands
    // (beatclock.h) turn the spread directly into Perfect/Normal/Miss rates.
    // As defender this doubles as steal-seeking — a match breaks at any tier
    // and only match+Perfect steals (combo.cpp), so the instant is always the
    // right target and the spread alone sets the steal rate.
    uint64_t instant = (uint64_t)v.beat_index * v.ticks_per_beat;
    int64_t offset = (int64_t)cfg.aim_min_ticks + (int64_t)r_mag;
    if (r_sign) offset = -offset;
    int64_t t = (int64_t)instant + offset;
    int64_t lo = (int64_t)v.tick + 1;                                  // emittable
    int64_t hi = (int64_t)instant + (int64_t)(v.ticks_per_beat / 2u) - 1; // window end
    if (lo > hi) {
        // Dying window: Fighting resumed with no emittable tick left in it
        // (round transitions can land a tick shy of a boundary). Any press now
        // would fall in the NEXT window at d = TPB/2 - 1 — a guaranteed Miss —
        // so wait; the fresh window's decision is at most a tick away.
        a.planned_input = Input::None;
        a.planned_tick = 0;
        return;
    }
    if (t < lo) t = lo;
    if (t > hi) t = hi;
    a.planned_input = pick;
    a.planned_tick = (uint64_t)t;
}

void ai_init(AiState& a, uint64_t seed, int player) {
    a = AiState{};
    a.player = (uint8_t)player;
    // Stream disjoint from the sim's (init_state uses initseq "noeng") and
    // between the two seats, so two bots on one seed stay decorrelated.
    a.rng.seed(seed, 0xb07ULL + (uint64_t)player);
}

uint8_t ai_update(AiState& a, const AiView& v, const AiConfig& cfg,
                  const CharacterData chars[2]) {
    record_history(a, v);

    // One decision per window. beat_index flips exactly on the boundary tick,
    // whose state already includes that tick's resolution — the decision always
    // sees the post-resolution macro state and fresh anchors (simulation.cpp).
    if (v.fighting && v.beat_index != a.decided_beat) decide(a, v, cfg, chars);

    if (v.fighting && a.planned_input != Input::None && v.tick + 1 == a.planned_tick)
        return (uint8_t)(1u << ((int)a.planned_input - 1));
    return 0;
}

} // namespace neg
