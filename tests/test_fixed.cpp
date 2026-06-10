#include "doctest.h"
#include "sim/fixed.h"

using neg::Fixed;

TEST_CASE("Fixed round-trips integers") {
    CHECK(Fixed::from_int(0).to_int() == 0);
    CHECK(Fixed::from_int(1).to_int() == 1);
    CHECK(Fixed::from_int(1200).to_int() == 1200);
    CHECK(Fixed::from_int(-7).to_int() == -7);
    CHECK(Fixed::from_int(32000).to_int() == 32000);
}

TEST_CASE("Fixed arithmetic") {
    Fixed a = Fixed::from_int(3), b = Fixed::from_int(2);
    CHECK((a + b).to_int() == 5);
    CHECK((a - b).to_int() == 1);
    CHECK((a * b).to_int() == 6);
    CHECK((a / b).v == (Fixed::ONE * 3) / 2); // 1.5 exactly
    CHECK((Fixed::from_int(-4) * Fixed::from_int(5)).to_int() == -20);
    CHECK((Fixed::from_raw(Fixed::ONE / 2) * Fixed::from_int(4)).to_int() == 2); // 0.5*4
}

TEST_CASE("Fixed helpers") {
    CHECK(Fixed::abs(Fixed::from_int(-9)) == Fixed::from_int(9));
    CHECK(Fixed::min(Fixed::from_int(1), Fixed::from_int(2)) == Fixed::from_int(1));
    CHECK(Fixed::max(Fixed::from_int(1), Fixed::from_int(2)) == Fixed::from_int(2));
    CHECK(Fixed::clamp(Fixed::from_int(5), Fixed::from_int(0), Fixed::from_int(3)) ==
          Fixed::from_int(3));
    CHECK(Fixed::clamp(Fixed::from_int(-5), Fixed::from_int(0), Fixed::from_int(3)) ==
          Fixed::from_int(0));
    CHECK(Fixed::div_int(Fixed::from_int(10), 2) == Fixed::from_int(5));
}

TEST_CASE("Fixed comparisons") {
    CHECK(Fixed::from_int(1) < Fixed::from_int(2));
    CHECK(Fixed::from_int(2) > Fixed::from_int(1));
    CHECK(Fixed::from_int(2) >= Fixed::from_int(2));
    CHECK(Fixed::from_int(2) != Fixed::from_int(3));
}
