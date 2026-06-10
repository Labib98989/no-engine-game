#include "sim/input_commit.h"
#include "sim/beatclock.h"

namespace neg {

void capture_commits(SimulationState& s, const FrameInput& in) {
    for (int p = 0; p < 2; ++p) {
        Fighter& f = s.fighters[p];
        if (f.commit.locked) continue;
        uint8_t bits = in.pressed[p];
        if (!bits) continue;
        // Two buttons on the same tick: lowest index wins, deterministically.
        Input input = Input::None;
        for (int b = 0; b < 4; ++b) {
            if (bits & (1u << b)) { input = (Input)(b + 1); break; }
        }
        uint32_t d = press_distance(s.tick, s.clock.beat_index, s.clock.ticks_per_beat);
        f.commit.input = input;
        f.commit.tier = tier_from_distance(d, s.tune);
        f.commit.press_phase = (uint8_t)(s.tick % s.clock.ticks_per_beat);
        f.commit.locked = true;
    }
}

} // namespace neg
