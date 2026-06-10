#include "sim/simulation.h"
#include "sim/beatclock.h"
#include "sim/input_commit.h"
#include "sim/fighter.h"
#include "sim/clash.h"
#include "sim/combo.h"

namespace neg {

// ---- checksum: FNV-1a 64 over fields, never over raw struct bytes (padding) --

namespace {
struct Hasher {
    uint64_t h = 14695981039346656037ULL;
    void byte(uint8_t b) { h = (h ^ b) * 1099511628211ULL; }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) byte((uint8_t)(v >> (i * 8))); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) byte((uint8_t)(v >> (i * 8))); }
    void i32(int32_t v) { u32((uint32_t)v); }
    void u16(uint16_t v) { byte((uint8_t)v); byte((uint8_t)(v >> 8)); }
    void u8(uint8_t v) { byte(v); }
    void b(bool v) { byte(v ? 1 : 0); }
    void fx(Fixed f) { i32(f.v); }
};

void hash_fighter(Hasher& h, const Fighter& f) {
    h.u8((uint8_t)f.character);
    h.i32(f.health);
    h.fx(f.pos_x); h.fx(f.pos_y);
    h.fx(f.move_target_x); h.fx(f.move_target_y);
    h.b(f.facing_right); h.b(f.airborne);
    h.u8((uint8_t)f.role);
    h.u8((uint8_t)f.commit.input); h.u8((uint8_t)f.commit.tier);
    h.u8(f.commit.press_phase); h.b(f.commit.locked);
    h.u16(f.anim_tag); h.u16(f.anim_tick);
    h.u8((uint8_t)f.last_committed);
}
} // namespace

uint64_t SimulationState::checksum() const {
    Hasher h;
    h.u64(tick);
    h.u64(rng.state); h.u64(rng.inc);
    h.u16(clock.bpm); h.u16(clock.ticks_per_beat); h.u32(clock.beat_index);
    h.u8((uint8_t)match.phase); h.u8(match.round);
    h.u8(match.wins_p0); h.u8(match.wins_p1);
    h.i32(match.round_timer_ticks); h.i32(match.phase_timer_ticks);
    h.u8((uint8_t)match.round_winner); h.u8((uint8_t)match.end_reason);
    h.u8((uint8_t)match.match_winner);
    hash_fighter(h, fighters[0]);
    hash_fighter(h, fighters[1]);
    h.u8((uint8_t)duel.macro); h.u8(duel.attacker); h.u8(duel.defender);
    h.u8(duel.combo_count); h.u8(duel.air_beats_elapsed);
    h.u8((uint8_t)duel.last_hit_input); h.i32(duel.last_hit_damage);
    h.u8((uint8_t)last_result.outcome); h.u8((uint8_t)last_result.winner);
    h.i32(last_result.damage_p0); h.i32(last_result.damage_p1);
    h.u8((uint8_t)last_result.p0_input); h.u8((uint8_t)last_result.p1_input);
    h.u8((uint8_t)last_result.p0_tier); h.u8((uint8_t)last_result.p1_tier);
    h.b(last_result.air); h.u32(last_result.beat); h.u64(last_result.resolved_tick);
    return h.h;
}

namespace sim {

static void reset_round(SimulationState& s, const CharacterData chars[2], bool skip_intro) {
    const Tuning& t = s.tune;
    int32_t center = t.stage_width / 2;
    for (int p = 0; p < 2; ++p) {
        Fighter& f = s.fighters[p];
        const CharacterData& cd = chars[(int)f.character];
        f.health = cd.health;
        f.pos_x = Fixed::from_int(p == 0 ? center - t.start_offset : center + t.start_offset);
        f.pos_y = Fixed::zero();
        f.move_target_x = f.pos_x;
        f.move_target_y = f.pos_y;
        f.facing_right = (p == 0);
        f.airborne = false;
        f.role = Role::Free;
        f.commit = Commit{};
        f.anim_tag = 0;
        f.anim_tick = 0;
        f.last_committed = Input::None;
    }
    s.duel = DuelState{};
    s.last_result = ResolutionResult{};
    s.match.round_timer_ticks = (int32_t)t.round_seconds * 60;
    s.match.round_winner = -1;
    s.match.end_reason = RoundEndReason::None;
    if (skip_intro) {
        s.match.phase = Phase::Fighting;
        s.match.phase_timer_ticks = 0;
    } else {
        s.match.phase = Phase::Intro;
        s.match.phase_timer_ticks = (int32_t)t.intro_beats * s.clock.ticks_per_beat;
    }
}

void init_state(SimulationState& s, const CharacterData chars[2], const Tuning& tune,
                uint64_t seed, bool skip_intro) {
    s = SimulationState{};
    s.tune = tune;
    s.rng.seed(seed, 0x6e6f656e67ULL); // "noeng"
    s.clock.bpm = tune.bpm;
    s.clock.ticks_per_beat = ticks_per_beat_for(tune.bpm);
    s.clock.beat_index = 0;
    s.match.round = 1;
    s.fighters[0].character = CharId::Breaker;
    s.fighters[1].character = CharId::Ballerina;
    reset_round(s, chars, skip_intro);
}

static void check_round_over(SimulationState& s) {
    if (s.match.phase != Phase::Fighting) return;
    int32_t h0 = s.fighters[0].health;
    int32_t h1 = s.fighters[1].health;
    bool ko0 = h0 <= 0, ko1 = h1 <= 0;
    bool timeover = s.match.round_timer_ticks <= 0;
    if (!ko0 && !ko1 && !timeover) return;

    if (ko0 && ko1) {
        s.match.end_reason = RoundEndReason::DoubleKO;
        s.match.round_winner = -1; // draw: no win awarded, round replays
    } else if (ko0 || ko1) {
        s.match.end_reason = RoundEndReason::KO;
        s.match.round_winner = ko0 ? 1 : 0;
    } else if (h0 != h1) {
        s.match.end_reason = RoundEndReason::TimeOver;
        s.match.round_winner = h0 > h1 ? 0 : 1;
    } else {
        s.match.end_reason = RoundEndReason::TimeOverDraw;
        s.match.round_winner = -1;
    }
    if (s.match.round_winner == 0) s.match.wins_p0++;
    if (s.match.round_winner == 1) s.match.wins_p1++;
    s.match.phase = Phase::RoundEnd;
    s.match.phase_timer_ticks = s.tune.round_end_ticks;
}

void tick(SimulationState& s, const FrameInput& in, const CharacterData chars[2]) {
    s.tick += 1;
    uint16_t tpb = s.clock.ticks_per_beat;
    uint32_t phase = (uint32_t)(s.tick % tpb);
    uint32_t half = tpb / 2u;

    // Window-close boundary: resolve beat (tick/TPB), then open the next
    // window. Resolution runs before capture so a press on the exact boundary
    // tick attributes to the just-opened window — window(n) is
    // [n*TPB - TPB/2, n*TPB + TPB/2), technical.md §1.2.
    if (phase == half) {
        if (s.match.phase == Phase::Fighting) {
            if (s.duel.macro == Macro::Neutral) resolve_clash(s, chars);
            else resolve_combo(s, chars);
            check_round_over(s);
        }
        s.clock.beat_index = (uint32_t)(s.tick / tpb) + 1;
        s.fighters[0].commit = Commit{};
        s.fighters[1].commit = Commit{};
    }

    capture_commits(s, in);

    // The cosmetic Neutral hop turns around at the beat instant.
    if (phase == 0) {
        for (int p = 0; p < 2; ++p) {
            Fighter& f = s.fighters[p];
            if (!f.airborne && f.move_target_y > Fixed::zero()) f.move_target_y = Fixed::zero();
        }
    }

    slide_fighters(s);

    switch (s.match.phase) {
    case Phase::Intro:
        if (--s.match.phase_timer_ticks <= 0) s.match.phase = Phase::Fighting;
        break;
    case Phase::Fighting:
        s.match.round_timer_ticks--;
        check_round_over(s); // time-over triggers between resolutions too
        break;
    case Phase::RoundEnd:
        if (--s.match.phase_timer_ticks <= 0) {
            if (s.match.wins_p0 >= s.tune.rounds_to_win || s.match.wins_p1 >= s.tune.rounds_to_win) {
                s.match.phase = Phase::MatchEnd;
                s.match.match_winner = s.match.wins_p0 >= s.tune.rounds_to_win ? 0 : 1;
            } else {
                s.match.round++;
                reset_round(s, chars, false);
            }
        }
        break;
    case Phase::MatchEnd:
        break; // restart is a fresh init_state, owned by the front-end
    }
}

} // namespace sim
} // namespace neg
