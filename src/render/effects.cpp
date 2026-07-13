#include "render/effects.h"
#include <cmath>
#include <cstdio>

namespace neg {
namespace render {

// Must match draw.cpp's world->screen mapping so spawned particles land on the
// fighters. (Kept local to avoid coupling the two files.)
static const float STAGE_X = 40.0f;
static const float FLOOR_Y = 600.0f;
static float wx(int xi) {
    return STAGE_X + (float)xi;
}
static float wy(int yi) {
    return FLOOR_Y - (float)yi;
}

// ---- tiny render-side PRNG (xorshift32) — never feeds the sim ---------------
static float frand01(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (float)(s & 0x00FFFFFFu) / (float)0x01000000u;
}
static float frange(uint32_t& s, float a, float b) {
    return a + (b - a) * frand01(s);
}

static const size_t MAX_PARTICLES = 900;

static void add(EffectSystem& fx, const Particle& p) {
    if (fx.particles.size() >= MAX_PARTICLES)
        return;
    fx.particles.push_back(p);
}

static void spawn_burst(EffectSystem& fx, float x, float y, int n, float speed, Uint8 r, Uint8 g,
                        Uint8 b, int kind, float grav) {
    for (int i = 0; i < n; ++i) {
        Particle p{};
        float ang = frange(fx.rng, 0.0f, 6.2831853f);
        float sp = frange(fx.rng, speed * 0.25f, speed);
        p.x = x;
        p.y = y;
        p.vx = cosf(ang) * sp;
        p.vy = sinf(ang) * sp;
        p.life = p.max_life = frange(fx.rng, 0.22f, 0.6f);
        p.size = frange(fx.rng, 2.0f, 5.0f);
        p.gravity = grav;
        p.drag = 1.8f;
        p.r = r;
        p.g = g;
        p.b = b;
        p.a0 = 255;
        p.kind = kind;
        add(fx, p);
    }
}

static void spawn_ring(EffectSystem& fx, float x, float y, float r0, float grow, float life,
                       Uint8 r, Uint8 g, Uint8 b) {
    Particle p{};
    p.x = x;
    p.y = y;
    p.size = r0;
    p.grow = grow;
    p.life = p.max_life = life;
    p.r = r;
    p.g = g;
    p.b = b;
    p.a0 = 220;
    p.kind = P_RING;
    add(fx, p);
}

static void spawn_dust(EffectSystem& fx, float x, float y, int n, float dir) {
    for (int i = 0; i < n; ++i) {
        Particle p{};
        p.x = x + frange(fx.rng, -8.0f, 8.0f);
        p.y = y - frange(fx.rng, 0.0f, 6.0f);
        p.vx = dir * frange(fx.rng, 20.0f, 120.0f);
        p.vy = -frange(fx.rng, 20.0f, 90.0f);
        p.life = p.max_life = frange(fx.rng, 0.3f, 0.7f);
        p.size = frange(fx.rng, 3.0f, 7.0f);
        p.gravity = 260.0f;
        p.drag = 2.4f;
        p.r = 190;
        p.g = 184;
        p.b = 170;
        p.a0 = 150;
        p.kind = P_DUST;
        add(fx, p);
    }
}

static void spawn_star(EffectSystem& fx, float x, float y, int n, float speed, Uint8 r, Uint8 g,
                       Uint8 b) {
    for (int i = 0; i < n; ++i) {
        Particle p{};
        float ang = frange(fx.rng, -1.9f, -1.25f); // fan upward
        float sp = frange(fx.rng, speed * 0.4f, speed);
        p.x = x + frange(fx.rng, -14.0f, 14.0f);
        p.y = y;
        p.vx = cosf(ang) * sp * frange(fx.rng, -1.0f, 1.0f);
        p.vy = sinf(ang) * sp;
        p.life = p.max_life = frange(fx.rng, 0.5f, 1.1f);
        p.size = frange(fx.rng, 3.0f, 6.0f);
        p.gravity = 180.0f;
        p.drag = 0.8f;
        p.spin = frange(fx.rng, -8.0f, 8.0f);
        p.r = r;
        p.g = g;
        p.b = b;
        p.a0 = 255;
        p.kind = P_STAR;
        add(fx, p);
    }
}

// Horizontal "swoosh" streaks trailing a fast move / attack, fired opposite the
// travel direction so they read as speed lines.
static void spawn_speedlines(EffectSystem& fx, float x, float y, float dir, int n, Uint8 r, Uint8 g,
                             Uint8 b) {
    for (int i = 0; i < n; ++i) {
        Particle p{};
        p.x = x - dir * frange(fx.rng, 0.0f, 20.0f);
        p.y = y + frange(fx.rng, -46.0f, 34.0f);
        p.vx = -dir * frange(fx.rng, 460.0f, 900.0f);
        p.vy = frange(fx.rng, -10.0f, 10.0f);
        p.life = p.max_life = frange(fx.rng, 0.10f, 0.24f);
        p.size = frange(fx.rng, 18.0f, 40.0f); // line length in px
        p.drag = 3.2f;
        p.r = r;
        p.g = g;
        p.b = b;
        p.a0 = 190;
        p.kind = P_SPEEDLINE;
        add(fx, p);
    }
}

// Arm the slam-in headline poster. pip tags it to a player (-1 = centered).
static void set_banner(EffectSystem& fx, const char* text, Uint8 r, Uint8 g, Uint8 b, float life,
                       int pip) {
    std::snprintf(fx.banner, sizeof fx.banner, "%s", text);
    fx.banner_t = 0.0f;
    fx.banner_max = life;
    fx.banner_r = r;
    fx.banner_g = g;
    fx.banner_b = b;
    fx.banner_pip = pip;
}

static void fighter_screen(const Fighter& f, float& sx, float& feet_y, float& chest_y) {
    sx = wx(f.pos_x.to_int());
    feet_y = wy(f.pos_y.to_int());
    chest_y = feet_y - 92.0f;
}

void effects_init(EffectSystem& fx, const SimulationState& s) {
    fx = EffectSystem{};
    fx.initialized = true;
    fx.last_resolved_tick = s.last_result.resolved_tick;
    fx.last_beat_index = s.clock.beat_index;
    fx.last_phase = s.match.phase;
    fx.last_combo = s.duel.combo_count;
    fx.last_round = s.match.round;
    for (int p = 0; p < 2; ++p) {
        fx.prev_pos_x[p] = s.fighters[p].pos_x.to_int();
    }
}

static void on_resolution(EffectSystem& fx, const SimulationState& s) {
    const ResolutionResult& lr = s.last_result;

    // Per-fighter hit reactions: sparks + white flash + shake scaled by damage.
    for (int p = 0; p < 2; ++p) {
        int32_t dmg = (p == 0) ? lr.damage_p0 : lr.damage_p1;
        if (dmg <= 0)
            continue;
        float sx, fy, cy;
        fighter_screen(s.fighters[p], sx, fy, cy);
        int n = 8 + (int)(dmg / 6);
        if (n > 34)
            n = 34;
        spawn_burst(fx, sx, cy, n, 320.0f + (float)dmg * 2.0f, 255, 236, 150, P_SPARK, 700.0f);
        spawn_burst(fx, sx, cy, n / 2, 180.0f, 255, 120, 70, P_STREAK, 300.0f);
        spawn_ring(fx, sx, cy, 6.0f, 340.0f, 0.28f, 255, 240, 190);
        fx.hit_flash[p] = 1.0f;
        float mag = 3.0f + (float)dmg * 0.10f;
        if (mag > 16.0f)
            mag = 16.0f;
        if (mag > fx.shake_mag)
            fx.shake_mag = mag;
    }

    // Outcome flavor.
    int w = lr.winner;
    float wx_pos = 0, wfy = 0, wcy = 0;
    if (w >= 0)
        fighter_screen(s.fighters[w], wx_pos, wfy, wcy);
    switch (lr.outcome) {
    case Outcome::Launch:
        if (w >= 0) {
            int loser = 1 - w;
            float lx, lfy, lcy;
            fighter_screen(s.fighters[loser], lx, lfy, lcy);
            spawn_star(fx, lx, lcy, 16, 360.0f, 150, 220, 255);
            spawn_ring(fx, lx, lfy, 10.0f, 420.0f, 0.4f, 150, 220, 255);
            spawn_dust(fx, lx, lfy, 14, -1.0f);
            spawn_dust(fx, lx, lfy, 14, 1.0f);
        }
        fx.flash = 0.28f;
        fx.flash_r = 170;
        fx.flash_g = 220;
        fx.flash_b = 255;
        fx.shake_mag = fx.shake_mag > 12.0f ? fx.shake_mag : 12.0f;
        break;
    case Outcome::ComboSteal:
        if (w >= 0) {
            spawn_ring(fx, wx_pos, wcy, 8.0f, 520.0f, 0.45f, 255, 90, 200);
            spawn_ring(fx, wx_pos, wcy, 8.0f, 360.0f, 0.5f, 255, 200, 90);
            spawn_burst(fx, wx_pos, wcy, 22, 380.0f, 255, 120, 220, P_STAR, 260.0f);
        }
        fx.flash = 0.34f;
        fx.flash_r = 255;
        fx.flash_g = 140;
        fx.flash_b = 220;
        fx.shake_mag = 10.0f;
        break;
    case Outcome::RpsDecided:
    case Outcome::OneLands:
        if (w >= 0)
            spawn_ring(fx, wx_pos, wcy, 6.0f, 300.0f, 0.3f, 255, 230, 160);
        break;
    case Outcome::GroundOut:
        if (w >= 0) {
            int loser = 1 - w;
            float lx, lfy, lcy;
            fighter_screen(s.fighters[loser], lx, lfy, lcy);
            spawn_dust(fx, lx, lfy, 22, -1.0f);
            spawn_dust(fx, lx, lfy, 22, 1.0f);
            spawn_ring(fx, lx, lfy, 8.0f, 460.0f, 0.35f, 210, 200, 180);
        }
        fx.shake_mag = fx.shake_mag > 9.0f ? fx.shake_mag : 9.0f;
        break;
    case Outcome::BothMiss:
        fx.flash = 0.12f;
        fx.flash_r = 120;
        fx.flash_g = 100;
        fx.flash_b = 110;
        break;
    default:
        break;
    }

    // ---- slam-in action poster: name exactly what just happened -----------
    Input win_in = Input::None;
    if (w == 0)
        win_in = lr.p0_input;
    else if (w == 1)
        win_in = lr.p1_input;
    char bt[32];
    switch (lr.outcome) {
    case Outcome::OneLands:
    case Outcome::RpsDecided:
        std::snprintf(bt, sizeof bt, "P%d HIT!", w + 1);
        set_banner(fx, bt, 255, 215, 80, 0.60f, w);
        break;
    case Outcome::SameTypeTie:
        if (w < 0) {
            set_banner(fx, "CLASH!", 235, 235, 245, 0.50f, -1);
        } else {
            std::snprintf(bt, sizeof bt, "P%d EDGE!", w + 1);
            set_banner(fx, bt, 255, 215, 80, 0.55f, w);
        }
        break;
    case Outcome::BothMiss:
        set_banner(fx, "CHIP!", 210, 100, 110, 0.45f, -1);
        break;
    case Outcome::ComboContinue:
        if (win_in == Input::B) {
            set_banner(fx, "CROSS-UP!", 255, 120, 220, 0.55f, w);
        } else {
            std::snprintf(bt, sizeof bt, "COMBO X%d", (int)s.duel.combo_count);
            set_banner(fx, bt, 255, 150, 60, 0.50f, w);
        }
        break;
    case Outcome::ComboBreak:
        set_banner(fx, "BREAK!", 120, 200, 255, 0.60f, w);
        break;
    case Outcome::ComboSteal:
        std::snprintf(bt, sizeof bt, "P%d STEAL!", w + 1);
        set_banner(fx, bt, 255, 90, 200, 0.90f, w);
        break;
    case Outcome::ComboCapEnd:
        set_banner(fx, "MAX COMBO!", 255, 215, 80, 0.80f, -1);
        break;
    case Outcome::ComboMissEnd:
        set_banner(fx, "DROPPED!", 150, 150, 160, 0.60f, -1);
        break;
    case Outcome::Launch:
        set_banner(fx, "LAUNCH!", 150, 220, 255, 0.80f, -1);
        break;
    case Outcome::GroundOut:
        set_banner(fx, "SLAM!", 255, 150, 60, 0.70f, -1);
        break;
    default:
        break;
    }
}

void effects_update(EffectSystem& fx, const SimulationState& s, float dt) {
    if (!fx.initialized) {
        effects_init(fx, s);
        return;
    }
    if (dt <= 0.0f)
        dt = 1.0f / 60.0f;
    if (dt > 0.1f)
        dt = 0.1f;
    fx.time += dt;

    // Advance the action poster's slam/hold/fade timeline.
    if (fx.banner_max > 0.0f) {
        fx.banner_t += dt;
        if (fx.banner_t >= fx.banner_max)
            fx.banner_max = 0.0f;
    }

    // ---- edge-detect sim events ------------------------------------------
    if (s.clock.beat_index != fx.last_beat_index) {
        fx.beat_pulse = 1.0f;
        fx.last_beat_index = s.clock.beat_index;
    }
    if (s.last_result.resolved_tick != fx.last_resolved_tick && s.last_result.resolved_tick != 0) {
        on_resolution(fx, s);
        fx.last_resolved_tick = s.last_result.resolved_tick;
    }
    // KO / round transition: big flash + shake.
    if (s.match.phase != fx.last_phase) {
        if (s.match.phase == Phase::RoundEnd) {
            fx.flash = 0.6f;
            fx.flash_r = 255;
            fx.flash_g = 250;
            fx.flash_b = 235;
            fx.shake_mag = 20.0f;
            // (Round-end headline is handled by the phase overlay, not a poster.)
            if (s.match.round_winner >= 0) {
                int loser = 1 - s.match.round_winner;
                float lx, lfy, lcy;
                fighter_screen(s.fighters[loser], lx, lfy, lcy);
                spawn_burst(fx, lx, lcy, 40, 520.0f, 255, 220, 140, P_SPARK, 600.0f);
                spawn_star(fx, lx, lcy, 20, 460.0f, 255, 210, 120);
            }
        }
        fx.last_phase = s.match.phase;
    }

    // Fast horizontal movement kicks up dust and speed lines (dashes, cross-ups).
    for (int p = 0; p < 2; ++p) {
        int32_t px = s.fighters[p].pos_x.to_int();
        int32_t dx = px - fx.prev_pos_x[p];
        int adx = dx < 0 ? -dx : dx;
        if (s.match.phase == Phase::Fighting) {
            float sx, fy, cy;
            fighter_screen(s.fighters[p], sx, fy, cy);
            if (adx > 5) {
                int n = adx > 40 ? 8 : 3;
                spawn_dust(fx, sx, fy, n, dx > 0 ? -1.0f : 1.0f);
            }
            if (adx > 12) {
                Uint8 lr8 = p == 0 ? 255 : 150;
                Uint8 lg = p == 0 ? 180 : 210;
                Uint8 lb = p == 0 ? 120 : 255;
                spawn_speedlines(fx, sx, cy, dx > 0 ? 1.0f : -1.0f, adx > 60 ? 5 : 3, lr8, lg, lb);
            }
        }
        fx.prev_pos_x[p] = px;
    }

    // Attack-commit swoosh: a quick arc of streaks the beat an attack locks in.
    for (int p = 0; p < 2; ++p) {
        const Fighter& f = s.fighters[p];
        if (s.match.phase == Phase::Fighting && f.commit.locked && f.commit.input != Input::None &&
            s.clock.beat_index != fx.last_commit_beat[p]) {
            fx.last_commit_beat[p] = s.clock.beat_index;
            float sx, fy, cy;
            fighter_screen(f, sx, fy, cy);
            float dir = f.facing_right ? 1.0f : -1.0f;
            Uint8 sr = f.commit.tier == Tier::Perfect ? 255 : 210;
            Uint8 sg = f.commit.tier == Tier::Perfect ? 235 : 210;
            Uint8 sb = f.commit.tier == Tier::Perfect ? 120 : 230;
            spawn_speedlines(fx, sx + dir * 30.0f, cy, dir, 6, sr, sg, sb);
        }
    }

    // Ambient drifting motes for atmosphere.
    fx.mote_accum += dt;
    while (fx.mote_accum > 0.11f) {
        fx.mote_accum -= 0.11f;
        Particle p{};
        p.x = frange(fx.rng, 60.0f, 1220.0f);
        p.y = frange(fx.rng, 120.0f, 560.0f);
        p.vx = frange(fx.rng, -10.0f, 10.0f);
        p.vy = -frange(fx.rng, 6.0f, 22.0f);
        p.life = p.max_life = frange(fx.rng, 2.5f, 5.0f);
        p.size = frange(fx.rng, 1.0f, 3.0f);
        p.drag = 0.2f;
        p.r = 150;
        p.g = 140;
        p.b = 180;
        p.a0 = 60;
        p.kind = P_MOTE;
        add(fx, p);
    }

    // ---- decay envelopes --------------------------------------------------
    fx.shake_mag *= expf(-dt * 11.0f);
    if (fx.shake_mag < 0.25f)
        fx.shake_mag = 0.0f;
    fx.shake_x = frange(fx.rng, -1.0f, 1.0f) * fx.shake_mag;
    fx.shake_y = frange(fx.rng, -1.0f, 1.0f) * fx.shake_mag;
    fx.flash *= expf(-dt * 6.5f);
    if (fx.flash < 0.01f)
        fx.flash = 0.0f;
    fx.beat_pulse *= expf(-dt * 6.0f);
    for (int p = 0; p < 2; ++p) {
        fx.hit_flash[p] *= expf(-dt * 9.0f);
        if (fx.hit_flash[p] < 0.02f)
            fx.hit_flash[p] = 0.0f;
    }

    // ---- integrate particles ---------------------------------------------
    for (Particle& p : fx.particles) {
        p.vx -= p.vx * p.drag * dt;
        p.vy += p.gravity * dt;
        p.vy -= p.vy * p.drag * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.size += p.grow * dt;
        p.rot += p.spin * dt;
        p.life -= dt;
    }
    size_t keep = 0;
    for (size_t i = 0; i < fx.particles.size(); ++i)
        if (fx.particles[i].life > 0.0f)
            fx.particles[keep++] = fx.particles[i];
    fx.particles.resize(keep);
}

static Uint8 fade(Uint8 a0, float life, float max_life) {
    float t = life / max_life;
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    return (Uint8)((float)a0 * t);
}

static void draw_particle(SDL_Renderer* r, const Particle& p, float ox, float oy) {
    Uint8 a = fade(p.a0, p.life, p.max_life);
    if (a == 0)
        return;
    SDL_SetRenderDrawColor(r, p.r, p.g, p.b, a);
    if (p.kind == P_RING) {
        float h = p.size;
        SDL_FRect rc{p.x - h + ox, p.y - h + oy, 2 * h, 2 * h};
        SDL_RenderRect(r, &rc);
        SDL_FRect rc2{rc.x + 1, rc.y + 1, rc.w - 2, rc.h - 2};
        SDL_RenderRect(r, &rc2);
    } else if (p.kind == P_STAR) {
        float h = p.size;
        SDL_FRect a1{p.x - h + ox, p.y - 1 + oy, 2 * h, 2};
        SDL_FRect a2{p.x - 1 + ox, p.y - h + oy, 2, 2 * h};
        SDL_RenderFillRect(r, &a1);
        SDL_RenderFillRect(r, &a2);
    } else if (p.kind == P_STREAK) {
        SDL_FRect rc{p.x + ox, p.y + oy, p.size * 1.6f, p.size * 0.7f};
        SDL_RenderFillRect(r, &rc);
    } else if (p.kind == P_SPEEDLINE) {
        float len = p.size;
        float x0 = p.vx < 0.0f ? p.x - len : p.x;
        SDL_FRect rc{x0 + ox, p.y + oy, len, 2.0f};
        SDL_RenderFillRect(r, &rc);
    } else {
        SDL_FRect rc{p.x - p.size * 0.5f + ox, p.y - p.size * 0.5f + oy, p.size, p.size};
        SDL_RenderFillRect(r, &rc);
    }
}

void effects_render_back(SDL_Renderer* r, const EffectSystem& fx) {
    for (const Particle& p : fx.particles)
        if (p.kind == P_MOTE)
            draw_particle(r, p, 0.0f, 0.0f);
}

void effects_render_front(SDL_Renderer* r, const EffectSystem& fx) {
    for (const Particle& p : fx.particles)
        if (p.kind != P_MOTE)
            draw_particle(r, p, fx.shake_x, fx.shake_y);
}

} // namespace render
} // namespace neg
