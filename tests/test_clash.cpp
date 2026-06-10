#include "doctest.h"
#include "sim/simulation.h"
#include "sim/chardata.h"
#include "sim/clash.h"
#include "sim/fighter.h"

using namespace neg;

namespace {

struct Fixture {
    CharacterData chars[2];
    SimulationState s;
    Fixture() {
        chars[0] = default_character(CharId::Breaker);
        chars[1] = default_character(CharId::Ballerina);
        sim::init_state(s, chars, default_tuning(), 42, /*skip_intro=*/true);
    }
    void place(int32_t x0, int32_t x1) {
        s.fighters[0].pos_x = Fixed::from_int(x0);
        s.fighters[1].pos_x = Fixed::from_int(x1);
        s.fighters[0].move_target_x = s.fighters[0].pos_x;
        s.fighters[1].move_target_x = s.fighters[1].pos_x;
    }
    void commit(int p, Input in, Tier tier) {
        s.fighters[p].commit = Commit{in, tier, 0, true};
    }
};

} // namespace

TEST_CASE("RPS table encodes A>B>C>D>A plus A>C, D>B exactly") {
    // design.md §5.1, all 16 cells
    CHECK_FALSE(input_beats(Input::A, Input::A));
    CHECK(input_beats(Input::A, Input::B));
    CHECK(input_beats(Input::A, Input::C));
    CHECK_FALSE(input_beats(Input::A, Input::D));
    CHECK_FALSE(input_beats(Input::B, Input::A));
    CHECK_FALSE(input_beats(Input::B, Input::B));
    CHECK(input_beats(Input::B, Input::C));
    CHECK_FALSE(input_beats(Input::B, Input::D));
    CHECK_FALSE(input_beats(Input::C, Input::A));
    CHECK_FALSE(input_beats(Input::C, Input::B));
    CHECK_FALSE(input_beats(Input::C, Input::C));
    CHECK(input_beats(Input::C, Input::D));
    CHECK(input_beats(Input::D, Input::A));
    CHECK(input_beats(Input::D, Input::B));
    CHECK_FALSE(input_beats(Input::D, Input::C));
    CHECK_FALSE(input_beats(Input::D, Input::D));
}

TEST_CASE("both silent -> BothMiss chip to both, stay Neutral") {
    Fixture f;
    f.place(400, 500);
    int32_t h0 = f.s.fighters[0].health, h1 = f.s.fighters[1].health;
    resolve_clash(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::BothMiss);
    CHECK(f.s.fighters[0].health == h0 - f.s.tune.chip_damage);
    CHECK(f.s.fighters[1].health == h1 - f.s.tune.chip_damage);
    CHECK(f.s.duel.macro == Macro::Neutral);
}

TEST_CASE("both out of range -> BothWhiff, no damage") {
    Fixture f;
    f.place(100, 1100); // gap 1000 > every range
    f.commit(0, Input::A, Tier::Normal);
    f.commit(1, Input::B, Tier::Normal);
    int32_t h0 = f.s.fighters[0].health, h1 = f.s.fighters[1].health;
    resolve_clash(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::BothWhiff);
    CHECK(f.s.fighters[0].health == h0);
    CHECK(f.s.fighters[1].health == h1);
    CHECK(f.s.duel.macro == Macro::Neutral);
}

TEST_CASE("one lands -> lander wins and enters Advantage") {
    Fixture f;
    f.place(450, 550); // gap 100: Breaker A range 150 reaches
    f.commit(0, Input::A, Tier::Normal);
    // P1 stays silent
    resolve_clash(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::OneLands);
    CHECK(f.s.last_result.winner == 0);
    CHECK(f.s.duel.macro == Macro::Advantage);
    CHECK(f.s.duel.attacker == 0);
    CHECK(f.s.duel.combo_count == 1);
    CHECK(f.s.fighters[1].health == f.chars[1].health - f.chars[0].damage_ground[1]);
}

TEST_CASE("a Miss-tier press cannot land: auto-loss to any landed input") {
    Fixture f;
    f.place(450, 550);
    f.commit(0, Input::A, Tier::Miss);   // pressed, but blew the rhythm
    f.commit(1, Input::B, Tier::Normal); // Ballerina B reaches across gap 100
    resolve_clash(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::OneLands);
    CHECK(f.s.last_result.winner == 1);
}

TEST_CASE("RPS decides when both land different inputs") {
    Fixture f;
    f.place(460, 540); // gap 80: Breaker A 150 ok, Ballerina B 260 ok
    f.commit(0, Input::A, Tier::Normal);
    f.commit(1, Input::B, Tier::Normal); // A beats B
    resolve_clash(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::RpsDecided);
    CHECK(f.s.last_result.winner == 0);
    CHECK(f.s.last_result.damage_p1 == f.chars[0].damage_ground[1]);

    Fixture g;
    g.place(460, 540);
    g.commit(0, Input::C, Tier::Normal); // gap 80: Breaker C 120 ok
    g.commit(1, Input::B, Tier::Normal); // B beats C
    resolve_clash(g.s, g.chars);
    CHECK(g.s.last_result.outcome == Outcome::RpsDecided);
    CHECK(g.s.last_result.winner == 1);
}

TEST_CASE("Perfect timing multiplies clash damage") {
    Fixture f;
    f.place(460, 540);
    f.commit(0, Input::A, Tier::Perfect);
    f.commit(1, Input::B, Tier::Normal);
    resolve_clash(f.s, f.chars);
    CHECK(f.s.last_result.winner == 0);
    CHECK(f.s.last_result.damage_p1 ==
          f.chars[0].damage_ground[1] * f.s.tune.perfect_damage_pct / 100);
}

TEST_CASE("same input: higher tier wins; equal Normal re-clashes") {
    Fixture f;
    f.place(460, 540);
    f.commit(0, Input::D, Tier::Perfect);
    f.commit(1, Input::D, Tier::Normal);
    resolve_clash(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::SameTypeTie);
    CHECK(f.s.last_result.winner == 0);
    CHECK(f.s.duel.macro == Macro::Advantage);

    Fixture g;
    g.place(460, 540);
    g.commit(0, Input::D, Tier::Normal);
    g.commit(1, Input::D, Tier::Normal);
    resolve_clash(g.s, g.chars);
    CHECK(g.s.last_result.outcome == Outcome::SameTypeTie);
    CHECK(g.s.last_result.winner == -1);
    CHECK(g.s.duel.macro == Macro::Neutral);
}

TEST_CASE("Sustain: Ballerina wins the Perfect-Perfect same-input tie") {
    Fixture f;
    f.place(460, 540);
    f.commit(0, Input::D, Tier::Perfect);
    f.commit(1, Input::D, Tier::Perfect);
    resolve_clash(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::SameTypeTie);
    CHECK(f.s.last_result.winner == 1); // Ballerina has Sustain
    CHECK(f.s.duel.macro == Macro::Advantage);
    CHECK(f.s.duel.attacker == 1);
}

TEST_CASE("range gates the RPS: the in-range player simply lands") {
    Fixture f;
    f.place(400, 640); // gap 240: only Ballerina B (260) reaches
    f.commit(0, Input::A, Tier::Perfect); // would beat B if it landed
    f.commit(1, Input::B, Tier::Normal);
    resolve_clash(f.s, f.chars);
    CHECK(f.s.last_result.outcome == Outcome::OneLands);
    CHECK(f.s.last_result.winner == 1);
}

TEST_CASE("neutral movement: A closes, B retreats, walls clamp, no tunneling") {
    Fixture f;
    f.place(400, 500);
    f.commit(0, Input::A, Tier::Normal); // toward opponent +115
    f.commit(1, Input::B, Tier::Normal); // away +135
    resolve_clash(f.s, f.chars);
    CHECK(f.s.fighters[0].move_target_x == Fixed::from_int(515));
    CHECK(f.s.fighters[1].move_target_x == Fixed::from_int(635));

    Fixture g; // both advance into each other: min_gap preserved
    g.place(500, 580);
    g.commit(0, Input::A, Tier::Miss); // movement still applies on sloppy press
    g.commit(1, Input::A, Tier::Miss);
    resolve_clash(g.s, g.chars);
    Fixed gap = g.s.fighters[1].move_target_x - g.s.fighters[0].move_target_x;
    CHECK(gap >= Fixed::from_int(g.s.tune.min_gap));

    Fixture w; // retreat into the wall clamps
    w.place(100, 900);
    w.commit(0, Input::B, Tier::Normal);
    resolve_clash(w.s, w.chars);
    CHECK(w.s.fighters[0].move_target_x >= Fixed::from_int(w.s.tune.wall_margin));
}
