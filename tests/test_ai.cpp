#include "doctest.h"
#include "sim/simulation.h"
#include "sim/chardata.h"
#include "sim/ai.h"

#include <vector>

using namespace neg;

namespace {

// Full-tick rig with the CPU driving P2 — the exact call pattern main.cpp uses.
struct BotRig {
    CharacterData chars[2];
    Tuning tune;
    SimulationState s;
    AiState bot;
    AiConfig cfg;
    BotRig(AiPreset p = AiPreset::Normal, uint64_t seed = 7, bool skip_intro = true) {
        chars[0] = default_character(CharId::Breaker);
        chars[1] = default_character(CharId::Ballerina);
        tune = default_tuning();
        sim::init_state(s, chars, tune, seed, skip_intro);
        ai_init(bot, seed, 1);
        cfg = default_ai_config(p);
    }
    uint8_t step(uint8_t p0bits = 0) {
        FrameInput in{};
        in.pressed[0] = p0bits;
        AiView v = ai_make_view(s, 1);
        in.pressed[1] = ai_update(bot, v, cfg, chars);
        sim::tick(s, in, chars);
        return in.pressed[1];
    }
};

// P0 keeps the match moving: a press on every beat instant, input cycling.
uint8_t scripted_p0(uint64_t tick) {
    if (tick % 30 != 0) return 0;
    return (uint8_t)(1u << (unsigned)((tick / 30) % 4));
}

} // namespace

TEST_CASE("bot emissions are legal: one valid bit, at most one press per window") {
    BotRig r;
    const uint64_t half = 15, ticks = 9000;
    std::vector<int> per_window(ticks / 30 + 3, 0);
    for (uint64_t t = 1; t <= ticks; ++t) {
        uint8_t b = r.step(scripted_p0(t));
        if (!b) continue;
        CHECK((b & ~0x0Fu) == 0);       // bits 0..3 only
        CHECK((b & (b - 1u)) == 0);     // exactly one bit
        per_window[(size_t)((t + half) / 30)]++;
    }
    for (int c : per_window) CHECK(c <= 1);
}

TEST_CASE("bot is silent outside Fighting") {
    BotRig r(AiPreset::Hard, 7, /*skip_intro=*/false);
    for (uint64_t t = 1; t <= (uint64_t)r.tune.intro_beats * 30 + 60; ++t) {
        bool was_fighting = r.s.match.phase == Phase::Fighting;
        uint8_t b = r.step();
        if (!was_fighting) CHECK(b == 0);
    }
}

TEST_CASE("bot records resolved history") {
    BotRig r;
    for (uint64_t t = 1; t <= 300; ++t) r.step(scripted_p0(t));
    CHECK(r.bot.history_len > 0);
    CHECK(r.bot.history[0].outcome != Outcome::None);
}

TEST_CASE("determinism: same seed, same emission stream") {
    BotRig a, b;
    std::vector<uint8_t> ea, eb;
    for (uint64_t t = 1; t <= 2700; ++t) ea.push_back(a.step(scripted_p0(t)));
    for (uint64_t t = 1; t <= 2700; ++t) eb.push_back(b.step(scripted_p0(t)));
    CHECK(ea == eb);
}

TEST_CASE("the bot lives outside the sim: replaying its bits reproduces checksums") {
    // If the bot touched s.rng (or any sim state), feeding its recorded output
    // back through a bot-free run would diverge. This is the checksum-invariance
    // guarantee: human-vs-human replays and vs-CPU replays are the same thing.
    BotRig a;
    std::vector<uint8_t> bits;
    std::vector<uint64_t> ca, cb;
    for (uint64_t t = 1; t <= 2700; ++t) {
        bits.push_back(a.step(scripted_p0(t)));
        ca.push_back(a.s.checksum());
    }
    CharacterData chars[2] = {default_character(CharId::Breaker),
                              default_character(CharId::Ballerina)};
    SimulationState s{};
    sim::init_state(s, chars, default_tuning(), 7, true);
    for (uint64_t t = 1; t <= 2700; ++t) {
        FrameInput in{};
        in.pressed[0] = scripted_p0(t);
        in.pressed[1] = bits[(size_t)(t - 1)];
        sim::tick(s, in, chars);
        cb.push_back(s.checksum());
    }
    CHECK(ca == cb);
}

TEST_CASE("no-cheat: the opponent's current-window commit cannot influence the bot") {
    // Two identical worlds. In one, P0 commits on the window-open boundary tick
    // (the earliest a commit can exist in a fresh window) — so it is already
    // visible in SimulationState when the bot makes its decision for that
    // window. The bot's emissions across the whole window must be identical.
    BotRig a, b; // same seed
    for (uint64_t t = 1; t <= 44; ++t) {
        a.step();
        b.step();
    }
    std::vector<uint8_t> ea, eb;
    ea.push_back(a.step(0x01)); // tick 45 = boundary: P0 commits A in world a
    eb.push_back(b.step(0x00)); //                     P0 stays silent in world b
    for (uint64_t t = 46; t <= 74; ++t) { // stop before the resolution at 75
        ea.push_back(a.step());
        eb.push_back(b.step());
    }
    // The worlds really do differ in the forbidden field...
    CHECK(a.s.fighters[0].commit.locked);
    CHECK_FALSE(b.s.fighters[0].commit.locked);
    // ...but the legal view agrees on everything, and so does the behavior.
    AiView va = ai_make_view(a.s, 1), vb = ai_make_view(b.s, 1);
    CHECK(va.anchor_gap == vb.anchor_gap);
    CHECK(va.beat_index == vb.beat_index);
    CHECK(va.self_commit.locked == vb.self_commit.locked);
    CHECK(ea == eb);
}

TEST_CASE("solver: symmetric RPS converges near uniform") {
    int32_t m[5][5] = {};
    m[0][1] = 100; m[1][0] = -100; // A > B
    m[1][2] = 100; m[2][1] = -100; // B > C
    m[2][0] = 100; m[0][2] = -100; // C > A
    uint16_t rc[5], cc[5];
    ai_solve_zero_sum(m, 3, 3, 1024, rc, cc);
    for (int i = 0; i < 3; ++i) {
        CHECK(rc[i] > 1024 / 3 - 100);
        CHECK(rc[i] < 1024 / 3 + 100);
    }
}

TEST_CASE("solver: a strictly dominant row takes the mixture") {
    int32_t m[5][5] = {};
    for (int j = 0; j < 4; ++j) {
        m[0][j] = 10;
        m[1][j] = -10;
        m[2][j] = -10;
        m[3][j] = -10;
    }
    uint16_t rc[5], cc[5];
    ai_solve_zero_sum(m, 4, 4, 256, rc, cc);
    CHECK(rc[0] >= 256 * 9 / 10);
}

TEST_CASE("solver: low exploitability on asymmetric 4x4 games") {
    static const int32_t M[2][4][4] = {
        {{0, 60, -30, 10}, {-60, 0, 45, -20}, {30, -45, 0, 80}, {-10, 20, -80, 0}},
        {{5, -40, 70, -15}, {40, -5, -55, 25}, {-70, 55, 10, -35}, {15, -25, 35, -10}},
    };
    const uint16_t iters = 4096;
    for (int k = 0; k < 2; ++k) {
        int32_t m[5][5] = {};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) m[i][j] = M[k][i][j];
        uint16_t rc[5], cc[5];
        ai_solve_zero_sum(m, 4, 4, iters, rc, cc);
        // Best pure-row response against the column mixture, minus the value of
        // the row mixture itself, must be small: nothing the human settles into
        // can beat the anchor by more than the error model's noise floor.
        int64_t v_mix = 0;
        int64_t best_row = 0;
        for (int i = 0; i < 4; ++i) {
            int64_t vi = 0;
            for (int j = 0; j < 4; ++j) vi += (int64_t)m[i][j] * cc[j];
            if (i == 0 || vi > best_row) best_row = vi;
            v_mix += vi * rc[i];
        }
        CHECK(best_row - v_mix / iters <= (int64_t)15 * iters);
    }
}

TEST_CASE("timing: preset spreads land the advertised tier rates") {
    // silent counts both deliberate drops and forced waits (a round transition
    // can resume Fighting inside a window with no emittable tick left);
    // pressed_miss isolates the timing model itself.
    auto measure = [](AiPreset p, int& perfect, int& normal, int& pressed_miss, int& silent) {
        BotRig r(p, 99);
        perfect = normal = pressed_miss = silent = 0;
        uint64_t last_seen = 0;
        uint64_t reseed = 100; // matches KO out; restart until the sample is big
        for (uint64_t t = 1; t <= 90000 && perfect + normal + pressed_miss + silent < 400; ++t) {
            if (r.s.match.phase == Phase::MatchEnd) {
                sim::init_state(r.s, r.chars, r.tune, reseed++, true);
                last_seen = 0;
            }
            r.step(scripted_p0(r.s.tick + 1));
            const ResolutionResult& res = r.s.last_result;
            if (res.resolved_tick == 0 || res.resolved_tick == last_seen ||
                res.outcome == Outcome::None)
                continue;
            last_seen = res.resolved_tick;
            if (res.p1_input == Input::None) silent++;
            else if (res.p1_tier == Tier::Miss) pressed_miss++;
            else if (res.p1_tier == Tier::Perfect) perfect++;
            else normal++;
        }
    };
    int perfect = 0, normal = 0, pressed_miss = 0, silent = 0;

    measure(AiPreset::Hard, perfect, normal, pressed_miss, silent);
    int total = perfect + normal + pressed_miss + silent;
    REQUIRE(total > 200);
    CHECK(pressed_miss == 0);           // aim band 0..4 never leaves Normal
    CHECK(silent * 100 <= total * 3);   // drop 0: only forced waits remain
    CHECK(perfect * 100 >= total * 50);
    CHECK(perfect * 100 <= total * 70);

    measure(AiPreset::Easy, perfect, normal, pressed_miss, silent);
    total = perfect + normal + pressed_miss + silent;
    REQUIRE(total > 200);
    int miss = pressed_miss + silent; // drops + the sloppy outer band
    CHECK(miss * 100 >= total * 15);
    CHECK(miss * 100 <= total * 35);
}

TEST_CASE("outranged Neutral shades toward A so the gap closes") {
    // Round-start gap (500) exceeds every range: the matrix is flat except the
    // approach bonus on row A, so the anchor is pure A; only policy noise and
    // drops pick anything else.
    int a_picks = 0;
    const int runs = 60;
    for (int k = 0; k < runs; ++k) {
        BotRig r(AiPreset::Normal, 1000 + (uint64_t)k);
        while (r.bot.decided_beat == 0xFFFFFFFFu) r.step();
        if (r.bot.planned_input == Input::A) a_picks++;
    }
    CHECK(a_picks >= runs / 2); // expectation ~76%
}

TEST_CASE("as defender the bot aims inside the difficulty band around the instant") {
    BotRig r(AiPreset::Hard, 5);
    for (uint64_t t = 1; t <= 45; ++t) r.step(); // fresh window 2 just opened
    // Impose Advantage with the bot defending (fixture style, test_clash.cpp).
    r.s.duel.macro = Macro::Advantage;
    r.s.duel.attacker = 0;
    r.s.duel.defender = 1;
    r.s.duel.combo_count = 1;
    r.s.fighters[0].role = Role::Attacker;
    r.s.fighters[1].role = Role::Defender;
    r.step(); // the beat-2 decision fires here, on the defender path
    CHECK(r.bot.planned_input != Input::None); // Hard never drops
    uint64_t instant = 60;
    uint64_t d = r.bot.planned_tick > instant ? r.bot.planned_tick - instant
                                              : instant - r.bot.planned_tick;
    CHECK(d <= r.cfg.aim_max_ticks); // Perfect-seeking: the spread alone sets steal rate
}
