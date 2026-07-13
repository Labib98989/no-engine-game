#include "sim/clash.h"
#include "sim/fighter.h"

namespace neg {

// Single source of truth for the RPS layer (technical.md §5.1).
//                          A      B      C      D
const bool rps_beats[4][4] = {
    /* A */ {false, true, true, false}, // A > B, C ; loses to D
    /* B */ {false, false, true, false}, // B > C    ; loses to A, D
    /* C */ {false, false, false, true}, // C > D    ; loses to A, B
    /* D */ {true, true, false, false},  // D > A, B ; loses to C
};

int32_t base_damage(const CharacterData& cd, Input in, Tier tier, const Tuning& t) {
    int32_t d = cd.damage_ground[(int)in];
    if (tier == Tier::Perfect) d = d * t.perfect_damage_pct / 100;
    return d;
}

static void enter_advantage(SimulationState& s, int winner, Input hit_input, int32_t dmg) {
    int loser = 1 - winner;
    s.duel.macro = Macro::Advantage;
    s.duel.attacker = (uint8_t)winner;
    s.duel.defender = (uint8_t)loser;
    s.duel.combo_count = 1; // the clash-winning hit is hit #1 (technical.md §5.2)
    s.duel.air_beats_elapsed = 0;
    s.duel.last_hit_input = hit_input;
    s.duel.last_hit_damage = dmg;
    s.fighters[winner].role = Role::Attacker;
    s.fighters[loser].role = Role::Defender;
    s.fighters[winner].anim_tag = 1;
    s.fighters[loser].health -= dmg;
    if (s.fighters[loser].health < 0) s.fighters[loser].health = 0;
    if (winner == 0) s.last_result.damage_p1 = dmg; else s.last_result.damage_p0 = dmg;
    s.last_result.winner = (int8_t)winner;
}

void resolve_clash(SimulationState& s, const CharacterData chars[2]) {
    const Tuning& t = s.tune;
    Fighter& f0 = s.fighters[0];
    Fighter& f1 = s.fighters[1];
    const Commit c0 = f0.commit;
    const Commit c1 = f1.commit;
    const CharacterData& cd0 = chars[(int)f0.character];
    const CharacterData& cd1 = chars[(int)f1.character];

    update_facing(s);

    ResolutionResult r{};
    r.p0_input = c0.input; r.p0_tier = c0.tier;
    r.p1_input = c1.input; r.p1_tier = c1.tier;
    r.beat = s.clock.beat_index;
    r.resolved_tick = s.tick;
    s.last_result = r;

    f0.last_committed = c0.input;
    f1.last_committed = c1.input;

    // Range gatekeeps the RPS (design.md §7): only landed attacks compare.
    // A Miss-tier press blew the rhythm — it cannot land.
    // The gate reads the gap between the window-open anchors, not the live
    // positions: fighters may already be sliding this beat (movement starts
    // when the pose commits), so anchoring keeps the clash exactly where it
    // stood when the window opened. A direct caller with no window captures
    // the anchors from the current positions here.
    if (!s.duel.anchor_ready) {
        s.duel.anchor_x[0] = f0.pos_x;
        s.duel.anchor_x[1] = f1.pos_x;
        s.duel.anchor_ready = true;
    }
    Fixed gap = Fixed::abs(s.duel.anchor_x[0] - s.duel.anchor_x[1]);
    bool ok0 = c0.input != Input::None && c0.tier != Tier::Miss;
    bool ok1 = c1.input != Input::None && c1.tier != Tier::Miss;
    bool land0 = ok0 && cd0.range[(int)c0.input] >= gap;
    bool land1 = ok1 && cd1.range[(int)c1.input] >= gap;

    if (c0.tier == Tier::Miss && c1.tier == Tier::Miss) {
        // Anti-stall: both blew the beat (silence or sloppy press).
        s.last_result.outcome = Outcome::BothMiss;
        f0.health -= t.chip_damage;
        f1.health -= t.chip_damage;
        if (f0.health < 0) f0.health = 0;
        if (f1.health < 0) f1.health = 0;
        s.last_result.damage_p0 = t.chip_damage;
        s.last_result.damage_p1 = t.chip_damage;
    } else if (!land0 && !land1) {
        s.last_result.outcome = Outcome::BothWhiff; // no damage, no penalty
    } else if (land0 != land1) {
        int w = land0 ? 0 : 1;
        const Commit& wc = land0 ? c0 : c1;
        const CharacterData& wcd = land0 ? cd0 : cd1;
        s.last_result.outcome = Outcome::OneLands;
        enter_advantage(s, w, wc.input, base_damage(wcd, wc.input, wc.tier, t));
    } else if (c0.input != c1.input) {
        int w = input_beats(c0.input, c1.input) ? 0 : 1;
        const Commit& wc = w == 0 ? c0 : c1;
        const CharacterData& wcd = w == 0 ? cd0 : cd1;
        s.last_result.outcome = Outcome::RpsDecided;
        enter_advantage(s, w, wc.input, base_damage(wcd, wc.input, wc.tier, t));
    } else {
        // Same input, both land: timing breaks the tie (design.md §6).
        s.last_result.outcome = Outcome::SameTypeTie;
        if (c0.tier != c1.tier) {
            int w = c0.tier > c1.tier ? 0 : 1;
            const Commit& wc = w == 0 ? c0 : c1;
            const CharacterData& wcd = w == 0 ? cd0 : cd1;
            enter_advantage(s, w, wc.input, base_damage(wcd, wc.input, wc.tier, t));
        } else if (c0.tier == Tier::Perfect) {
            // Perfect-Perfect tie: re-clash, unless exactly one side has
            // Sustain — then she wins (design.md §11.2, structural not temporal).
            bool sus0 = cd0.passive == PassiveId::Sustain;
            bool sus1 = cd1.passive == PassiveId::Sustain;
            if (sus0 != sus1) {
                int w = sus0 ? 0 : 1;
                const Commit& wc = w == 0 ? c0 : c1;
                const CharacterData& wcd = w == 0 ? cd0 : cd1;
                enter_advantage(s, w, wc.input, base_damage(wcd, wc.input, wc.tier, t));
            }
            // else: stay Neutral, re-clash next beat
        }
        // Normal-Normal: re-clash next beat
    }

    // Both players always take their beat displacement (design.md §10.2),
    // whatever the clash outcome.
    apply_neutral_movement(s, chars);
}

} // namespace neg
