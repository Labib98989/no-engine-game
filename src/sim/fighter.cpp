#include "sim/fighter.h"
#include "sim/beatclock.h"

namespace neg {

Fixed clamp_to_walls(Fixed x, const Tuning& t) {
    return Fixed::clamp(x, Fixed::from_int(t.wall_margin),
                        Fixed::from_int(t.stage_width - t.wall_margin));
}

Fixed fighter_gap(const SimulationState& s) {
    return Fixed::abs(s.fighters[0].pos_x - s.fighters[1].pos_x);
}

void update_facing(SimulationState& s) {
    Fighter& a = s.fighters[0];
    Fighter& b = s.fighters[1];
    if (a.pos_x != b.pos_x) {
        a.facing_right = a.pos_x < b.pos_x;
        b.facing_right = b.pos_x < a.pos_x;
    }
}

void slide_fighters(SimulationState& s) {
    uint16_t tpb = s.clock.ticks_per_beat;
    uint32_t phase = (uint32_t)(s.tick % tpb);
    uint32_t half = tpb / 2u;

    // Ticks remaining until the next resolution boundary (phase == half),
    // counting this tick; exact fixed-point arrival when rem hits 1.
    uint32_t rem_x = (phase < half) ? (half - phase) : (tpb + half - phase);
    // Y legs run instant -> resolution -> instant (half-beat motion).
    uint32_t rem_y = half - (phase % half);
    if (rem_y == 0) rem_y = half;

    for (int p = 0; p < 2; ++p) {
        Fighter& f = s.fighters[p];
        f.pos_x += Fixed::div_int(f.move_target_x - f.pos_x, (int32_t)rem_x);
        f.pos_y += Fixed::div_int(f.move_target_y - f.pos_y, (int32_t)rem_y);
        f.anim_tick++;
    }
}

static Fixed toward(const Fighter& self, const Fighter& opp) {
    if (self.pos_x == opp.pos_x)
        return self.facing_right ? Fixed::from_int(1) : Fixed::from_int(-1);
    return self.pos_x < opp.pos_x ? Fixed::from_int(1) : Fixed::from_int(-1);
}

void apply_neutral_movement(SimulationState& s, const CharacterData chars[2]) {
    const Tuning& t = s.tune;
    Fixed tx[2];

    for (int p = 0; p < 2; ++p) {
        Fighter& f = s.fighters[p];
        const Fighter& opp = s.fighters[1 - p];
        const CharacterData& cd = chars[(int)f.character];
        Fixed dir = toward(f, opp);
        Input in = f.commit.input;
        tx[p] = f.pos_x;

        switch (in) {
        case Input::A: tx[p] = f.pos_x + dir * cd.move_dist[(int)Input::A]; break;
        case Input::B: tx[p] = f.pos_x - dir * cd.move_dist[(int)Input::B]; break;
        case Input::C:
            // Cosmetic hop: rises to the instant, lands by the next resolution.
            f.move_target_y = cd.move_dist[(int)Input::C];
            break;
        case Input::D:
        case Input::None:
        default: break;
        }
        tx[p] = clamp_to_walls(tx[p], t);
    }

    // Simultaneous movement: preserve left/right order and min_gap so Neutral
    // never tunnels (crossing sides is the combo cross-up's job, not Neutral's).
    Fixed gap = Fixed::from_int(t.min_gap);
    int left = s.fighters[0].pos_x <= s.fighters[1].pos_x ? 0 : 1;
    int right = 1 - left;
    if (tx[right] - tx[left] < gap) {
        Fixed mid = Fixed::div_int(tx[left] + tx[right], 2);
        Fixed half_gap = Fixed::div_int(gap, 2);
        Fixed lo = Fixed::from_int(t.wall_margin) + half_gap;
        Fixed hi = Fixed::from_int(t.stage_width - t.wall_margin) - half_gap;
        mid = Fixed::clamp(mid, lo, hi);
        tx[left] = mid - half_gap;
        tx[right] = mid + half_gap;
    }

    s.fighters[0].move_target_x = tx[0];
    s.fighters[1].move_target_x = tx[1];
}

} // namespace neg
