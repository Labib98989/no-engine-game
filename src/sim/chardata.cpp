#include "sim/chardata.h"

namespace neg {

static void set_name(CharacterData& c, const char* n) {
    int i = 0;
    for (; n[i] && i < 15; ++i) c.name[i] = n[i];
    c.name[i] = 0;
}

Tuning default_tuning() { return Tuning{}; }

CharacterData default_character(CharId id) {
    CharacterData c{};
    // index order: [None, A(close), B(far), C(up), D(down)]
    if (id == CharId::Breaker) {
        // Horizontal + ground specialist: lives on A/D, tanky, low air ceiling.
        set_name(c, "BREAKER");
        c.health = 1100;
        c.range[1] = Fixed::from_int(150); // A extended — gap-closer is strong
        c.range[2] = Fixed::from_int(200);
        c.range[3] = Fixed::from_int(120);
        c.range[4] = Fixed::from_int(180); // D-sweep wide ground coverage
        c.move_dist[1] = Fixed::from_int(115); // A advance longer — closes fast
        c.move_dist[2] = Fixed::from_int(70);  // B retreat shorter — commits hard
        c.move_dist[3] = Fixed::from_int(110); // C hop height (low)
        c.move_dist[4] = Fixed::zero();
        c.damage_ground[1] = 65;  // boosted on the strong/safe class
        c.damage_ground[2] = 100;
        c.damage_ground[3] = 95;
        c.damage_ground[4] = 75;
        c.launch_height = Fixed::from_int(140); // low ceiling
        c.air_beats = 3;
        c.passive = PassiveId::StickTheLanding;
        for (int i = 0; i < 5; ++i) {
            static const int32_t g[5] = {100, 80, 60, 40, 20};
            c.combo_scale_ground[i] = g[i];
            c.combo_scale_air[i] = g[i];
        }
    } else {
        // Vertical + air specialist: lives on B/C, fragile, best air game.
        set_name(c, "BALLERINA");
        c.health = 900;
        c.range[1] = Fixed::from_int(90);  // A short — gets bullied point-blank
        c.range[2] = Fixed::from_int(260); // B extended — pokes across the stage
        c.range[3] = Fixed::from_int(150);
        c.range[4] = Fixed::from_int(140);
        c.move_dist[1] = Fixed::from_int(80);  // A advance shorter
        c.move_dist[2] = Fixed::from_int(135); // B retreat longer — light footwork
        c.move_dist[3] = Fixed::from_int(170); // C hop height (high)
        c.move_dist[4] = Fixed::zero();
        c.damage_ground[1] = 45;
        c.damage_ground[2] = 135; // boosted on the weak/committed class
        c.damage_ground[3] = 125;
        c.damage_ground[4] = 55;
        c.launch_height = Fixed::from_int(220); // higher launch
        c.air_beats = 4; // +1 airborne beat
        c.passive = PassiveId::Sustain;
        static const int32_t g[5] = {100, 80, 60, 40, 20};
        static const int32_t a[5] = {100, 90, 70, 50, 30}; // slower air decay
        for (int i = 0; i < 5; ++i) {
            c.combo_scale_ground[i] = g[i];
            c.combo_scale_air[i] = a[i];
        }
    }
    return c;
}

} // namespace neg
