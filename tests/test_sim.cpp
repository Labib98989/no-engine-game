#include "doctest.h"
#include "sim/simulation.h"
#include "sim/chardata.h"

#include <vector>

using namespace neg;

namespace {

struct Rig {
    CharacterData chars[2];
    Tuning tune;
    SimulationState s;
    Rig(bool skip_intro = true, uint64_t seed = 7) {
        chars[0] = default_character(CharId::Breaker);
        chars[1] = default_character(CharId::Ballerina);
        tune = default_tuning();
        sim::init_state(s, chars, tune, seed, skip_intro);
    }
    void step(FrameInput in = FrameInput{}) { sim::tick(s, in, chars); }
    void step_to(uint64_t tick, FrameInput in = FrameInput{}) {
        while (s.tick < tick) step(in);
    }
    static FrameInput press(int player, Input i) {
        FrameInput f{};
        f.pressed[player] = (uint8_t)(1u << ((int)i - 1));
        return f;
    }
};

// A deterministic pseudo-script: scattered presses for both players.
FrameInput scripted(uint64_t tick) {
    FrameInput in{};
    if (tick % 30 == 0) in.pressed[0] |= 1u << ((tick / 30) % 4);       // P0 on instants
    if (tick % 30 == 7 && tick % 60 == 7) in.pressed[1] |= 1u << ((tick / 60) % 4);
    return in;
}

} // namespace

TEST_CASE("first press wins; later presses in the window are ignored") {
    Rig r;
    r.step_to(29);
    r.step(Rig::press(0, Input::A)); // tick 30 = beat 1 instant, d=0
    CHECK(r.s.fighters[0].commit.locked);
    CHECK(r.s.fighters[0].commit.input == Input::A);
    CHECK(r.s.fighters[0].commit.tier == Tier::Perfect);
    r.step(Rig::press(0, Input::B)); // tick 31: ignored
    CHECK(r.s.fighters[0].commit.input == Input::A);
}

TEST_CASE("the resolution runs at window close and the commit clears") {
    Rig r;
    r.step_to(30, FrameInput{});
    r.step(Rig::press(0, Input::C)); // tick 31, d=1 -> Perfect
    r.step_to(44);
    CHECK(r.s.fighters[0].commit.locked);
    r.step(); // tick 45: beat 1 resolves, window 2 opens
    CHECK(r.s.last_result.resolved_tick == 45);
    CHECK(r.s.last_result.p0_input == Input::C);
    CHECK(r.s.last_result.p0_tier == Tier::Perfect);
    CHECK_FALSE(r.s.fighters[0].commit.locked);
    CHECK(r.s.clock.beat_index == 2);
}

TEST_CASE("a press on the boundary tick lands in the just-opened window") {
    Rig r;
    r.step_to(44);
    r.step(Rig::press(0, Input::D)); // tick 45: belongs to beat 2, d=15 -> Miss band
    CHECK(r.s.fighters[0].commit.locked);
    CHECK(r.s.fighters[0].commit.input == Input::D);
    CHECK(r.s.fighters[0].commit.tier == Tier::Miss);
}

TEST_CASE("intro counts down in beats, then the fight starts") {
    Rig r(/*skip_intro=*/false);
    CHECK(r.s.match.phase == Phase::Intro);
    r.step_to((uint64_t)r.tune.intro_beats * 30 + 1);
    CHECK(r.s.match.phase == Phase::Fighting);
}

TEST_CASE("determinism: identical input streams give identical checksums") {
    Rig a, b;
    std::vector<uint64_t> ca, cb;
    for (uint64_t t = 1; t <= 900; ++t) {
        a.step(scripted(t));
        ca.push_back(a.s.checksum());
    }
    for (uint64_t t = 1; t <= 900; ++t) {
        b.step(scripted(t));
        cb.push_back(b.s.checksum());
    }
    CHECK(ca == cb);
}

TEST_CASE("determinism: a POD snapshot resumes bit-exact") {
    Rig a;
    for (uint64_t t = 1; t <= 300; ++t) a.step(scripted(t));
    SimulationState snap = a.s; // plain copy is a full snapshot
    Rig b;
    b.s = snap;
    for (uint64_t t = 301; t <= 600; ++t) {
        a.step(scripted(t));
        b.step(scripted(t));
    }
    CHECK(a.s.checksum() == b.s.checksum());
}

TEST_CASE("KO ends the round, then the next round resets health") {
    Rig r;
    r.s.fighters[1].health = 1;
    // Put them in range and land P0's A on beat 1.
    r.s.fighters[0].pos_x = Fixed::from_int(500);
    r.s.fighters[1].pos_x = Fixed::from_int(580);
    r.s.fighters[0].move_target_x = r.s.fighters[0].pos_x;
    r.s.fighters[1].move_target_x = r.s.fighters[1].pos_x;
    r.step_to(29);
    r.step(Rig::press(0, Input::A));
    r.step_to(45); // resolution
    CHECK(r.s.match.phase == Phase::RoundEnd);
    CHECK(r.s.match.end_reason == RoundEndReason::KO);
    CHECK(r.s.match.wins_p0 == 1);
    r.step_to(45 + r.tune.round_end_ticks + 1);
    CHECK(r.s.match.round == 2);
    CHECK(r.s.match.phase == Phase::Intro);
    CHECK(r.s.fighters[1].health == r.chars[1].health);
}

TEST_CASE("time-over awards the round to the healthier fighter") {
    Rig r;
    r.s.match.round_timer_ticks = 10;
    r.s.fighters[1].health -= 100;
    r.step_to(r.s.tick + 11);
    CHECK(r.s.match.phase == Phase::RoundEnd);
    CHECK(r.s.match.end_reason == RoundEndReason::TimeOver);
    CHECK(r.s.match.round_winner == 0);
}

TEST_CASE("winning rounds_to_win rounds ends the match") {
    Rig r;
    r.s.match.wins_p0 = 1; // one more win takes it
    r.s.match.round_timer_ticks = 5;
    r.s.fighters[1].health -= 50;
    r.step_to(r.s.tick + 6);
    CHECK(r.s.match.phase == Phase::RoundEnd);
    r.step_to(r.s.tick + r.tune.round_end_ticks + 1);
    CHECK(r.s.match.phase == Phase::MatchEnd);
    CHECK(r.s.match.match_winner == 0);
}

TEST_CASE("character-select init supports mirror matches") {
    Rig r;
    sim::init_state(r.s, r.chars, r.tune, 7, CharId::Ballerina, CharId::Ballerina, true);
    CHECK(r.s.fighters[0].character == CharId::Ballerina);
    CHECK(r.s.fighters[1].character == CharId::Ballerina);
    CHECK(r.s.fighters[0].health == r.chars[1].health); // both read chars[Ballerina]
    CHECK(r.s.fighters[1].health == r.chars[1].health);
}

TEST_CASE("fighters slide to their targets exactly by the next resolution") {
    Rig r;
    r.s.fighters[0].pos_x = Fixed::from_int(400);
    r.s.fighters[0].move_target_x = Fixed::from_int(460);
    r.step_to(44); // one tick before resolution at 45
    CHECK(r.s.fighters[0].pos_x == Fixed::from_int(460));
}
