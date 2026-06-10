// tick <-> beat arithmetic and timing-tier banding — technical.md §1.
#pragma once
#include <cstdint>
#include "sim/types.h"

namespace neg {

// TPB = 60 ticks/s * 60 s/min / bpm. 120 BPM @ 60 Hz -> 30.
inline uint16_t ticks_per_beat_for(uint16_t bpm) { return (uint16_t)(3600 / bpm); }

// Resolution boundary: a beat's capture window closes at phase == TPB/2
// (technical.md §1.4); beat n resolves at tick n*TPB + TPB/2.
inline bool is_resolution_tick(uint64_t tick, uint16_t tpb) {
    return (tick % tpb) == (uint64_t)(tpb / 2);
}

// Tick-distance of a press from the open beat's instant. The open window is
// [beat*TPB - TPB/2, beat*TPB + TPB/2), so d <= TPB/2 always.
inline uint32_t press_distance(uint64_t tick, uint32_t beat_index, uint16_t tpb) {
    int64_t instant = (int64_t)beat_index * tpb;
    int64_t d = (int64_t)tick - instant;
    return (uint32_t)(d < 0 ? -d : d);
}

// Concentric bands around the instant (technical.md §1.3).
inline Tier tier_from_distance(uint32_t d, const Tuning& t) {
    if (d <= t.perfect_ticks) return Tier::Perfect;
    if (d <= t.normal_ticks) return Tier::Normal;
    return Tier::Miss; // sloppy outer band; silence is also Miss
}

} // namespace neg
