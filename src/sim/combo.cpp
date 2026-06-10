#include "sim/combo.h"
#include "sim/clash.h"
#include "sim/fighter.h"

namespace neg {

// Combo over -> back to Neutral. Applies Breaker's Stick the Landing:
// a combo whose final applied hit was D deals +25% on that hit
// (design.md §11.1; fires on break/cap/miss ends, not on steals).
static void end_combo(SimulationState& s, const CharacterData chars[2]) {
    const CharacterData& acd = chars[(int)s.fighters[s.duel.attacker].character];
    if (acd.passive == PassiveId::StickTheLanding && s.duel.last_hit_input == Input::D &&
        s.duel.last_hit_damage > 0) {
        int32_t bonus = s.duel.last_hit_damage * s.tune.stick_landing_bonus_pct / 100;
        Fighter& def = s.fighters[s.duel.defender];
        def.health -= bonus;
        if (def.health < 0) def.health = 0;
        if (s.duel.defender == 0) s.last_result.damage_p0 += bonus;
        else s.last_result.damage_p1 += bonus;
    }

    s.duel.macro = Macro::Neutral;
    s.duel.combo_count = 0;
    s.duel.air_beats_elapsed = 0;
    s.duel.last_hit_input = Input::None;
    s.duel.last_hit_damage = 0;
    for (int p = 0; p < 2; ++p) {
        Fighter& f = s.fighters[p];
        f.role = Role::Free;
        f.airborne = false;
        f.move_target_y = Fixed::zero(); // both land back (design.md §9.3)
        f.anim_tag = 0;
    }
}

void resolve_combo(SimulationState& s, const CharacterData chars[2]) {
    const Tuning& t = s.tune;
    Fighter& att = s.fighters[s.duel.attacker];
    Fighter& def = s.fighters[s.duel.defender];
    const CharacterData& acd = chars[(int)att.character];
    const Commit ac = att.commit;
    const Commit dc = def.commit;

    update_facing(s);

    ResolutionResult r{};
    r.p0_input = s.fighters[0].commit.input;
    r.p0_tier = s.fighters[0].commit.tier;
    r.p1_input = s.fighters[1].commit.input;
    r.p1_tier = s.fighters[1].commit.tier;
    r.beat = s.clock.beat_index;
    r.resolved_tick = s.tick;
    r.air = att.airborne || def.airborne;
    s.last_result = r;

    att.last_committed = ac.input;
    def.last_committed = dc.input;

    bool was_air = att.airborne || def.airborne;

    if (ac.input == Input::None || ac.tier == Tier::Miss) {
        // Attacker dropped the rhythm: combo ends (design.md §8.1).
        s.last_result.outcome = Outcome::ComboMissEnd;
        s.last_result.winner = (int8_t)s.duel.defender;
        end_combo(s, chars);
        return;
    }

    if (dc.input == ac.input && dc.tier == Tier::Perfect) {
        // Match + Perfect: the steal (design.md §8.1). Roles swap, combo
        // bookkeeping continues at count 1 (technical.md §6).
        s.last_result.outcome = Outcome::ComboSteal;
        s.last_result.winner = (int8_t)s.duel.defender;
        uint8_t old_att = s.duel.attacker;
        s.duel.attacker = s.duel.defender;
        s.duel.defender = old_att;
        s.duel.combo_count = 1;
        s.duel.last_hit_input = Input::None; // new attacker hasn't landed yet
        s.duel.last_hit_damage = 0;
        s.fighters[s.duel.attacker].role = Role::Attacker;
        s.fighters[s.duel.defender].role = Role::Defender;
        s.fighters[s.duel.attacker].anim_tag = 1;
        return;
    }

    if (dc.input == ac.input) {
        // Plain match: combo broken, back to Neutral.
        s.last_result.outcome = Outcome::ComboBreak;
        s.last_result.winner = (int8_t)s.duel.defender;
        end_combo(s, chars);
        return;
    }

    // The hit lands. Scaling table picked by air state at the moment of the
    // hit; the attacker's own table drives both (design.md §11.2).
    const int32_t* scale = was_air ? acd.combo_scale_air : acd.combo_scale_ground;
    int hit_index = s.duel.combo_count; // 0-based; enter_advantage applied hit #1
    if (hit_index > 4) hit_index = 4;
    int32_t dmg = base_damage(acd, ac.input, ac.tier, t) * scale[hit_index] / 100;

    Outcome out = Outcome::ComboContinue;
    Fixed dir = att.pos_x <= def.pos_x ? Fixed::from_int(1) : Fixed::from_int(-1);

    switch (ac.input) {
    case Input::A:
        // Close the gap, stopping min_gap short of the defender.
        att.move_target_x = clamp_to_walls(def.pos_x - dir * Fixed::from_int(t.min_gap), t);
        break;
    case Input::B: {
        // Cross-up: instantaneous teleport behind the defender; no slide
        // (technical.md §2). Inputs stay frame-invariant so nothing inverts.
        Fixed nx = clamp_to_walls(def.pos_x + dir * Fixed::from_int(t.crossup_gap), t);
        att.pos_x = nx;
        att.move_target_x = nx;
        update_facing(s);
        break;
    }
    case Input::C:
        if (!att.airborne) {
            // Launch: both go airborne (design.md §9.1).
            att.airborne = true;
            def.airborne = true;
            s.duel.air_beats_elapsed = 0;
            att.move_target_y = acd.launch_height;
            def.move_target_y = acd.launch_height;
            out = Outcome::Launch;
        }
        // already airborne: extend the air combo, stay up
        break;
    case Input::D:
        if (def.airborne) {
            // Vertical equalizer: grounds the airborne opponent (design.md §9.2).
            def.airborne = false;
            def.move_target_y = Fixed::zero();
            out = Outcome::GroundOut;
        } else if (att.airborne) {
            // D-chain beat 2: opponent already grounded -> ground self.
            att.airborne = false;
            att.move_target_y = Fixed::zero();
            out = Outcome::GroundOut;
        }
        break;
    default: break;
    }

    def.health -= dmg;
    if (def.health < 0) def.health = 0;
    if (s.duel.defender == 0) s.last_result.damage_p0 += dmg;
    else s.last_result.damage_p1 += dmg;
    s.last_result.winner = (int8_t)s.duel.attacker;
    s.duel.last_hit_input = ac.input;
    s.duel.last_hit_damage = dmg;
    s.duel.combo_count++;
    att.anim_tag = s.duel.combo_count; // cosmetic combo-position cycling

    // Gravity timeout: the air sub-state can't outlast the attacker's
    // air_beats (Breaker low ceiling, Ballerina +1 — design.md §11).
    if (att.airborne || def.airborne) {
        s.duel.air_beats_elapsed++;
        if (s.duel.air_beats_elapsed >= acd.air_beats) {
            att.airborne = false;
            def.airborne = false;
            att.move_target_y = Fixed::zero();
            def.move_target_y = Fixed::zero();
            if (out == Outcome::ComboContinue) out = Outcome::GroundOut;
        }
    }

    if (s.duel.combo_count >= t.combo_cap) {
        s.last_result.outcome = Outcome::ComboCapEnd; // hit #5 applied, then end
        end_combo(s, chars);
        return;
    }

    s.last_result.outcome = out;
}

} // namespace neg
