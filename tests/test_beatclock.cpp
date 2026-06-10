#include "doctest.h"
#include "sim/beatclock.h"
#include "sim/chardata.h"

using namespace neg;

TEST_CASE("ticks per beat derives from bpm") {
    CHECK(ticks_per_beat_for(120) == 30);
    CHECK(ticks_per_beat_for(60) == 60);
    CHECK(ticks_per_beat_for(90) == 40);
    CHECK(ticks_per_beat_for(150) == 24);
}

TEST_CASE("resolution boundary is the window close at phase TPB/2") {
    CHECK(is_resolution_tick(15, 30));
    CHECK(is_resolution_tick(45, 30));
    CHECK(is_resolution_tick(75, 30));
    CHECK_FALSE(is_resolution_tick(0, 30));
    CHECK_FALSE(is_resolution_tick(30, 30));
    CHECK_FALSE(is_resolution_tick(44, 30));
}

TEST_CASE("press distance from the open beat's instant") {
    CHECK(press_distance(30, 1, 30) == 0);  // dead on the instant
    CHECK(press_distance(26, 1, 30) == 4);  // early
    CHECK(press_distance(36, 1, 30) == 6);  // late
    CHECK(press_distance(16, 1, 30) == 14); // very early (window edge)
    CHECK(press_distance(45, 2, 30) == 15); // boundary tick belongs to next beat
}

TEST_CASE("tier banding matches technical.md 1.3") {
    Tuning t = default_tuning(); // perfect 2, normal 11
    CHECK(tier_from_distance(0, t) == Tier::Perfect);
    CHECK(tier_from_distance(2, t) == Tier::Perfect);
    CHECK(tier_from_distance(3, t) == Tier::Normal);
    CHECK(tier_from_distance(11, t) == Tier::Normal);
    CHECK(tier_from_distance(12, t) == Tier::Miss);
    CHECK(tier_from_distance(15, t) == Tier::Miss);
}
