#include "doctest.h"
#include "sim/simulation.h"
#include "sim/chardata.h"
#include "sim/combo.h"

using namespace neg;

namespace {

struct Fixture {
    CharacterData chars[2];
    SimulationState s;
    Fixture(int attacker = 0) {
        chars[0] = default_character(CharId::Breaker);
        chars[1] = default_character(CharId::Ballerina);
        sim::init_state(s, chars, default_tuning(), 42, /*skip_intro=*/true);
        s.fighters[0].pos_x = Fixed::from_int(450);
        s.fighters[1].pos_x = Fixed::from_int(550);
        s.fighters[0].move_target_x = s.fighters[0].pos_x;
        s.fighters[1].move_target_x = s.fighters[1].pos_x;
        enter_advantage(attacker);
    }
    void enter_advantage(int att) {
        s.duel.macro = Macro::Advantage;
        s.duel.attacker = (uint8_t)att;
        s.duel.defender = (uint8_t)(1 - att);
        s.duel.combo_count = 1; // hit #1 already applied by the clash
        s.duel.last_hit_input = Input::A;
        s.duel.last_hit_damage = 50;
        s.fighters[att].role = Role::Attacker;
        s.fighters[1 - att].role = Role::Defender;
    }
    void commit(int p, Input in, Tier tier) {
        s.fighters[p].commit = Commit{in, tier, 0, true};
    }
    Fighter& att() { return s.fighters[s.duel.attacker]; }
    Fighter& def() { return s.fighters[s.duel.defender]; }
    const CharacterData& att_cd() { return chars[(int)att().character]; }
};

} // namespace

TEST_CASE("combo continues on unmatched hit with scaling") {
    Fixture f;
    int32_t hp = f.def().health;
    f.commit(0, Input::A, Tier::Normal);
    f.commit(1, Input::C, Tier::Normal); // wrong guess
    resolve_combo(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::ComboContinue);
    int32_t expect = f.chars[0].damage_ground[(int)Input::A] *
                     f.chars[0].combo_scale_ground[1] / 100; // hit #2 at 80%
    CHECK(f.def().health == hp - expect);
    CHECK(f.s.duel.combo_count == 2);
}

TEST_CASE("silent defender still gets hit") {
    Fixture f;
    f.commit(0, Input::D, Tier::Normal);
    resolve_combo(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::ComboContinue);
    CHECK(f.s.duel.combo_count == 2);
}

TEST_CASE("exact match breaks the combo") {
    Fixture f;
    int32_t hp = f.def().health;
    f.commit(0, Input::C, Tier::Normal);
    f.commit(1, Input::C, Tier::Normal);
    resolve_combo(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::ComboBreak);
    CHECK(f.s.duel.macro == Macro::Neutral);
    CHECK(f.s.fighters[0].role == Role::Free);
    CHECK(f.def().health == hp); // matched hit deals nothing (no D-end bonus here)
}

TEST_CASE("match plus Perfect steals the advantage") {
    Fixture f;
    f.commit(0, Input::B, Tier::Normal);
    f.commit(1, Input::B, Tier::Perfect);
    resolve_combo(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::ComboSteal);
    CHECK(f.s.duel.macro == Macro::Advantage);
    CHECK(f.s.duel.attacker == 1);
    CHECK(f.s.duel.combo_count == 1);
    CHECK(f.s.fighters[1].role == Role::Attacker);
    CHECK(f.s.fighters[0].role == Role::Defender);
}

TEST_CASE("attacker dropping the beat ends the combo") {
    Fixture f;
    f.commit(1, Input::A, Tier::Normal);
    resolve_combo(f.s, f.chars); // attacker never committed
    CHECK(f.s.last_result.outcome == Outcome::ComboMissEnd);
    CHECK(f.s.duel.macro == Macro::Neutral);
}

TEST_CASE("combo caps at 5 hits") {
    Fixture f;
    f.s.duel.combo_count = 4;
    int32_t hp = f.def().health;
    f.commit(0, Input::A, Tier::Normal);
    f.commit(1, Input::D, Tier::Normal);
    resolve_combo(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::ComboCapEnd);
    int32_t expect = f.chars[0].damage_ground[(int)Input::A] *
                     f.chars[0].combo_scale_ground[4] / 100; // hit #5 at 20%
    CHECK(f.def().health == hp - expect);
    CHECK(f.s.duel.macro == Macro::Neutral);
}

TEST_CASE("B in combo is the cross-up teleport and flips facing") {
    Fixture f;
    f.commit(0, Input::B, Tier::Normal);
    f.commit(1, Input::A, Tier::Normal);
    resolve_combo(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::ComboContinue);
    // attacker was left of defender (450 < 550): lands behind at 550+90
    CHECK(f.att().pos_x == Fixed::from_int(640));
    CHECK(f.att().move_target_x == Fixed::from_int(640)); // teleport, no slide
    CHECK_FALSE(f.att().facing_right);
    CHECK(f.def().facing_right);
}

TEST_CASE("C launches both into the air sub-state") {
    Fixture f;
    f.commit(0, Input::C, Tier::Normal);
    resolve_combo(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::Launch);
    CHECK(f.att().airborne);
    CHECK(f.def().airborne);
    CHECK(f.def().move_target_y == f.att_cd().launch_height);
    CHECK(f.s.duel.air_beats_elapsed == 1);
}

TEST_CASE("D is the vertical equalizer, then the D-chain grounds self") {
    Fixture f;
    f.commit(0, Input::C, Tier::Normal);
    resolve_combo(f.s, f.chars); // launch
    f.commit(0, Input::D, Tier::Normal);
    f.commit(1, Input::A, Tier::Normal);
    resolve_combo(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::GroundOut);
    CHECK_FALSE(f.def().airborne);
    CHECK(f.att().airborne); // beat 1 grounds the opponent only
    f.commit(0, Input::D, Tier::Normal);
    f.commit(1, Input::B, Tier::Normal);
    resolve_combo(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::GroundOut);
    CHECK_FALSE(f.att().airborne); // beat 2 grounds self
}

TEST_CASE("gravity timeout grounds both at the attacker's air_beats") {
    Fixture f; // Breaker attacks: air_beats = 3
    f.commit(0, Input::C, Tier::Normal);
    resolve_combo(f.s, f.chars); // elapsed 1
    CHECK(f.att().airborne);
    f.commit(0, Input::C, Tier::Normal);
    resolve_combo(f.s, f.chars); // elapsed 2
    CHECK(f.att().airborne);
    f.commit(0, Input::C, Tier::Normal);
    resolve_combo(f.s, f.chars); // elapsed 3 -> timeout
    CHECK_FALSE(f.att().airborne);
    CHECK_FALSE(f.def().airborne);
    CHECK(f.s.duel.macro == Macro::Advantage); // sub-state ends, combo continues
}

TEST_CASE("Stick the Landing: +25% on the final hit when the combo ends on D") {
    Fixture f; // attacker 0 is Breaker
    f.commit(0, Input::D, Tier::Normal);
    f.commit(1, Input::B, Tier::Normal);
    resolve_combo(f.s, f.chars); // D hit applied (hit #2, 80%)
    int32_t d_dmg = f.chars[0].damage_ground[(int)Input::D] * 80 / 100;
    int32_t hp = f.def().health;
    f.commit(0, Input::A, Tier::Normal);
    f.commit(1, Input::A, Tier::Normal); // break -> combo ended, last hit was D
    resolve_combo(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::ComboBreak);
    CHECK(f.def().health == hp - d_dmg * 25 / 100);
}

TEST_CASE("no Stick the Landing when the combo ends on another input") {
    Fixture f;
    f.commit(0, Input::A, Tier::Normal);
    f.commit(1, Input::B, Tier::Normal);
    resolve_combo(f.s, f.chars);
    int32_t hp = f.def().health;
    f.commit(0, Input::C, Tier::Normal);
    f.commit(1, Input::C, Tier::Normal); // break; last hit was A
    resolve_combo(f.s, f.chars);
    CHECK(f.def().health == hp);
}

TEST_CASE("Ballerina uses her slower air scaling while airborne") {
    Fixture f(/*attacker=*/1);
    f.commit(1, Input::C, Tier::Normal);
    resolve_combo(f.s, f.chars); // launch; hit #2 ground scale applied
    int32_t hp = f.def().health;
    f.commit(1, Input::A, Tier::Normal);
    f.commit(0, Input::D, Tier::Normal);
    resolve_combo(f.s, f.chars); // air hit #3: air scale 70%
    int32_t expect = f.chars[1].damage_ground[(int)Input::A] *
                     f.chars[1].combo_scale_air[2] / 100;
    CHECK(f.def().health == hp - expect);
}

TEST_CASE("health clamps at zero") {
    Fixture f;
    f.def().health = 5;
    f.commit(0, Input::B, Tier::Perfect);
    resolve_combo(f.s, f.chars);
    CHECK(f.def().health == 0);
}
