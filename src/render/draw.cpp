#include "render/draw.h"
#include "render/font5x7.h"
#include "render/effects.h"
#include "sim/fighter.h"
#include <cmath>
#include <cstdio>

namespace neg {
namespace render {

// World -> screen: stage x in [0, stage_width] maps left-to-right with a
// margin; world y is height above the floor.
static const float STAGE_X = 40.0f;
static const float FLOOR_Y = 600.0f;

// World shake offset, set per-frame from the effect system. The HUD ignores it
// so only the action layer (stage + fighters + impact particles) shakes.
static float g_ox = 0.0f;
static float g_oy = 0.0f;

static float wx(Fixed x) {
    return STAGE_X + g_ox + (float)x.to_int();
}
static float wy(Fixed y) {
    return FLOOR_Y + g_oy - (float)y.to_int();
}

static void fill(SDL_Renderer* r, float x, float y, float w, float h) {
    SDL_FRect rc{x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

static void col(SDL_Renderer* r, int cr, int cg, int cb, int ca = 255) {
    SDL_SetRenderDrawColor(r, (Uint8)cr, (Uint8)cg, (Uint8)cb, (Uint8)ca);
}

static const char* input_name(Input i) {
    switch (i) {
    case Input::A:
        return "A CLOSE";
    case Input::B:
        return "B FAR";
    case Input::C:
        return "C UP";
    case Input::D:
        return "D DOWN";
    default:
        return "-";
    }
}

static const char* tier_name(Tier t) {
    switch (t) {
    case Tier::Perfect:
        return "PERFECT";
    case Tier::Normal:
        return "NORMAL";
    default:
        return "MISS";
    }
}

static void tier_color(SDL_Renderer* r, Tier t) {
    switch (t) {
    case Tier::Perfect:
        col(r, 255, 215, 80);
        break;
    case Tier::Normal:
        col(r, 200, 200, 210);
        break;
    default:
        col(r, 120, 100, 110);
        break;
    }
}

// Which pose to show this tick, derived from sim state only (render-side; the
// sim is untouched). Hit reaction wins, then the committed attack for the beat,
// else idle. design.md's "freeze on the beat": the pose snaps when the input
// commits and holds until the beat rolls over.
static int pose_slot(const SimulationState& s, int p) {
    const Fighter& f = s.fighters[p];
    if (s.match.phase == Phase::Intro)
        return POSE_DEFAULT;
    const ResolutionResult& lr = s.last_result;
    if (lr.resolved_tick != 0 && s.tick - lr.resolved_tick < 12) {
        int32_t dmg = p == 0 ? lr.damage_p0 : lr.damage_p1;
        if (dmg > 0)
            return POSE_HIT;
    }
    if (f.commit.locked && f.commit.input != Input::None)
        return POSE_A + ((int)f.commit.input - 1); // Input::A==1 -> POSE_A ... D -> POSE_D
    return POSE_IDLE;
}

// Cosmetic per-fighter animation offset (pixels), purely render-side. Gives the
// static sprite frames life: idle breathing, a lean into a committed attack,
// and a recoil when freshly hit. Never feeds back into the sim.
static void juice_offset(const SimulationState& s, int p, const EffectSystem& fx, float& dx,
                         float& dy) {
    const Fighter& f = s.fighters[p];
    dx = 0.0f;
    dy = 0.0f;
    float dir = f.facing_right ? 1.0f : -1.0f;

    // Idle breathing bob, phase-offset per fighter so they don't sync up.
    float t = fx.time * 2.6f + (p ? 1.6f : 0.0f);
    dy += sinf(t) * 2.0f;

    // Thrust into the committed attack: snaps out hard on the beat pulse, then
    // eases back — gives the static pose a fast "swoosh" feel.
    if (f.commit.locked && f.commit.input != Input::None && s.match.phase == Phase::Fighting) {
        float thrust = 8.0f + fx.beat_pulse * 12.0f;
        dx += dir * thrust;
        dy += 2.0f + fx.beat_pulse * 3.0f;
    }

    // Recoil away from the hit, decaying with the shared hit flash.
    float hf = fx.hit_flash[p];
    if (hf > 0.0f) {
        dx -= dir * 16.0f * hf;
        dy -= 4.0f * hf;
    }
}

static void draw_shadow(SDL_Renderer* r, float sx, float airborne_h, float w) {
    // Ground shadow shrinks the higher the fighter is, keeping jumps readable.
    float k = 1.0f - airborne_h * 0.0018f;
    if (k < 0.35f)
        k = 0.35f;
    float sw = w * k;
    col(r, 0, 0, 0, 130);
    fill(r, sx - sw * 0.5f, FLOOR_Y + g_oy - 5.0f, sw, 6.0f);
}

static void draw_fighter(SDL_Renderer* r, const SimulationState& s, int p,
                         const SpriteSheet sheets[2], const EffectSystem& fx) {
    const Fighter& f = s.fighters[p];
    float jx, jy;
    juice_offset(s, p, fx, jx, jy);
    float airborne_h = (float)f.pos_y.to_int();

    // Sprite path: anchor the chosen pose at the fighter's feet (pos). Falls
    // through to the rectangle path below when the sheet isn't loaded.
    const SpriteSheet& sheet = sheets[p];
    if (sheet.valid) {
        float sx = wx(f.pos_x) + jx;
        float sy = wy(f.pos_y) + jy;
        draw_shadow(r, wx(f.pos_x), airborne_h, 52.0f);

        // Soft attacker aura behind the sprite.
        if (f.role == Role::Attacker) {
            float pulse = 0.5f + 0.5f * sinf(fx.time * 6.0f);
            for (int i = 3; i >= 1; --i) {
                col(r, 255, 215, 80, (int)(26 * pulse) + i * 6);
                float hw = 26.0f + i * 9.0f;
                fill(r, sx - hw, sy - 150.0f, hw * 2.0f, 150.0f);
            }
        }

        draw_sprite(r, sheet, sheet.pose[pose_slot(s, p)], sx, sy, f.facing_right);

        // White-hot additive flash on the sprite silhouette when freshly hit.
        float hf = fx.hit_flash[p];
        if (hf > 0.0f && sheet.tex) {
            Uint8 add = (Uint8)(hf * 210.0f);
            SDL_SetTextureBlendMode(sheet.tex, SDL_BLENDMODE_ADD);
            SDL_SetTextureColorMod(sheet.tex, add, add, add);
            draw_sprite(r, sheet, sheet.pose[pose_slot(s, p)], sx, sy, f.facing_right);
            SDL_SetTextureColorMod(sheet.tex, 255, 255, 255);
            SDL_SetTextureBlendMode(sheet.tex, SDL_BLENDMODE_BLEND);
        }

        if (f.role == Role::Attacker) { // ground role indicator
            col(r, 255, 215, 80, 255);
            fill(r, wx(f.pos_x) - 16.0f, FLOOR_Y + g_oy + 2.0f, 32.0f, 3.0f);
        }
        return;
    }

    float w = p == 0 ? 58.0f : 48.0f; // Breaker broad, Ballerina slender
    float h = p == 0 ? 150.0f : 170.0f;
    float x = wx(f.pos_x) - w / 2.0f + jx;
    float y = wy(f.pos_y) - h + jy;

    draw_shadow(r, wx(f.pos_x), airborne_h, w);

    // Defender flashes white for a few ticks after taking damage.
    float hf = fx.hit_flash[p];
    if (hf > 0.3f)
        col(r, 255, 255, 255);
    else if (p == 0)
        col(r, 232, 112, 60);
    else
        col(r, 72, 190, 222);
    fill(r, x, y, w, h);

    // Attacker outline.
    if (f.role == Role::Attacker) {
        float pulse = 0.5f + 0.5f * sinf(fx.time * 6.0f);
        col(r, 255, 215, 80, (int)(120 + 120 * pulse));
        SDL_FRect rc{x - 2, y - 2, w + 4, h + 4};
        SDL_RenderRect(r, &rc);
    }

    // Facing notch at chest height.
    col(r, 20, 16, 30);
    float nx = f.facing_right ? x + w - 10.0f : x;
    fill(r, nx, y + h * 0.25f, 10.0f, 12.0f);
}

static void draw_health(SDL_Renderer* r, const SimulationState& s, const CharacterData chars[2]) {
    char buf[64];
    for (int p = 0; p < 2; ++p) {
        const Fighter& f = s.fighters[p];
        int32_t maxhp = chars[(int)f.character].health;
        float frac = maxhp > 0 ? (float)f.health / (float)maxhp : 0.0f;
        if (frac < 0)
            frac = 0;
        float bx = p == 0 ? 40.0f : 720.0f;
        float bw = 520.0f;

        // Frame + backing.
        col(r, 12, 10, 18, 255);
        fill(r, bx - 3, 37.0f, bw + 6, 28.0f);
        col(r, 70, 20, 26, 255);
        fill(r, bx, 40.0f, bw, 22.0f);
        col(r, frac > 0.35f ? 90 : 220, frac > 0.35f ? 210 : 70, 80, 255);
        float fw = bw * frac;
        // P0 bar drains toward the center, P1 away from the center.
        float fbx = p == 0 ? bx + (bw - fw) : bx;
        fill(r, fbx, 40.0f, fw, 22.0f);
        // Bright leading edge.
        col(r, 235, 245, 210, 220);
        if (fw > 2.0f)
            fill(r, p == 0 ? fbx : fbx + fw - 3.0f, 40.0f, 3.0f, 22.0f);

        col(r, 235, 235, 240, 255);
        const char* nm = chars[(int)f.character].name;
        if (p == 0)
            draw_text(r, bx, 70.0f, 2.0f, nm);
        else
            draw_text(r, bx + bw - text_width(2.0f, nm), 70.0f, 2.0f, nm);

        // Round-win pips.
        for (int wpip = 0; wpip < s.tune.rounds_to_win; ++wpip) {
            int wins = p == 0 ? s.match.wins_p0 : s.match.wins_p1;
            if (wpip < wins)
                col(r, 255, 215, 80, 255);
            else
                col(r, 60, 56, 76, 255);
            float px = p == 0 ? bx + bw - 14.0f - wpip * 20.0f : bx + wpip * 20.0f;
            fill(r, px, 12.0f, 14.0f, 14.0f);
        }

        // This beat's committed input + tier (the S3 readout).
        const Commit& c = f.commit;
        if (c.locked) {
            tier_color(r, c.tier);
            snprintf(buf, sizeof buf, "%s %s", input_name(c.input), tier_name(c.tier));
            if (p == 0)
                draw_text(r, bx, 92.0f, 2.0f, buf);
            else
                draw_text(r, bx + bw - text_width(2.0f, buf), 92.0f, 2.0f, buf);
        }
    }

    // Round timer.
    int secs = s.match.round_timer_ticks > 0 ? (s.match.round_timer_ticks + 59) / 60 : 0;
    snprintf(buf, sizeof buf, "%02d", secs);
    col(r, 235, 235, 240, 255);
    draw_text(r, 640.0f - text_width(4.0f, buf) / 2.0f, 34.0f, 4.0f, buf);
}

static void draw_beat_bar(SDL_Renderer* r, const SimulationState& s, const EffectSystem& fx) {
    // Phase bar: a marker sweeps each beat; the center tick is the instant.
    float bx = 440.0f, bw = 400.0f, by = 96.0f;
    uint16_t tpb = s.clock.ticks_per_beat;
    uint32_t phase = (uint32_t)(s.tick % tpb);

    col(r, 44, 40, 60, 255);
    fill(r, bx, by, bw, 14.0f);

    // Perfect / Normal bands around the center instant, to scale.
    float center = bx + bw / 2.0f;
    float px_per_tick = bw / (float)tpb;
    col(r, 70, 66, 96, 255);
    fill(r, center - s.tune.normal_ticks * px_per_tick, by,
         2.0f * s.tune.normal_ticks * px_per_tick, 14.0f);
    // Perfect band glows on the beat pulse.
    int pg = 60 + (int)(fx.beat_pulse * 120.0f);
    col(r, 200, 170, pg, 255);
    fill(r, center - s.tune.perfect_ticks * px_per_tick, by,
         2.0f * (s.tune.perfect_ticks + 1) * px_per_tick, 14.0f);

    // Beat-pulse halo around the center instant.
    if (fx.beat_pulse > 0.02f) {
        col(r, 255, 245, 200, (int)(fx.beat_pulse * 160.0f));
        float hw = 6.0f + fx.beat_pulse * 22.0f;
        SDL_FRect rc{center - hw, by - 4.0f, hw * 2.0f, 22.0f};
        SDL_RenderRect(r, &rc);
    }

    // Sweep marker: phase 0 == instant == center; window runs -half..+half.
    uint32_t half = tpb / 2u;
    float offset = (phase <= half) ? (float)phase : (float)phase - (float)tpb;
    float mx = center + offset * px_per_tick;
    bool pulse = phase <= s.tune.perfect_ticks || phase >= (uint32_t)(tpb - s.tune.perfect_ticks);
    col(r, pulse ? 255 : 200, pulse ? 245 : 200, pulse ? 200 : 210, 255);
    fill(r, mx - 2.0f, by - 4.0f, 4.0f, 22.0f);

    // 4-count pips under the bar, the active one blooming on the beat.
    for (int i = 0; i < 4; ++i) {
        bool active = (int)(s.clock.beat_index % 4) == i;
        if (active)
            col(r, 255, 245, 200, 255);
        else
            col(r, 60, 56, 76, 255);
        float grow = active ? fx.beat_pulse * 4.0f : 0.0f;
        fill(r, bx + bw / 2.0f - 44.0f + i * 24.0f - grow * 0.5f, by + 20.0f - grow * 0.5f,
             14.0f + grow, 6.0f + grow);
    }
}

static const char* outcome_text(const ResolutionResult& lr, char* buf, size_t n) {
    int w = lr.winner;
    switch (lr.outcome) {
    case Outcome::BothWhiff:
        return "WHIFF";
    case Outcome::BothMiss:
        return "CHIP - BOTH MISSED THE BEAT";
    case Outcome::OneLands:
        snprintf(buf, n, "P%d LANDS", w + 1);
        return buf;
    case Outcome::RpsDecided:
        snprintf(buf, n, "P%d WINS THE CLASH", w + 1);
        return buf;
    case Outcome::SameTypeTie:
        if (w < 0)
            return "TIE - RECLASH";
        snprintf(buf, n, "P%d OUT-TIMES THE TIE", w + 1);
        return buf;
    case Outcome::ComboContinue:
        return "HIT";
    case Outcome::ComboBreak:
        snprintf(buf, n, "P%d BREAKS FREE", w + 1);
        return buf;
    case Outcome::ComboSteal:
        snprintf(buf, n, "P%d STEALS!", w + 1);
        return buf;
    case Outcome::ComboCapEnd:
        return "COMBO CAP";
    case Outcome::ComboMissEnd:
        return "DROPPED THE BEAT";
    case Outcome::Launch:
        return "LAUNCH!";
    case Outcome::GroundOut:
        return "GROUNDED";
    default:
        return nullptr;
    }
}

static void draw_state_banner(SDL_Renderer* r, const SimulationState& s, const EffectSystem& fx) {
    char buf[64], buf2[64];
    col(r, 180, 176, 196, 255);
    if (s.duel.macro == Macro::Neutral) {
        draw_text(r, 640.0f - text_width(2.0f, "NEUTRAL") / 2.0f, 140.0f, 2.0f, "NEUTRAL");
    } else {
        snprintf(buf, sizeof buf, "P%d ADVANTAGE", s.duel.attacker + 1);
        col(r, 255, 215, 80, 255);
        draw_text(r, 640.0f - text_width(2.0f, buf) / 2.0f, 140.0f, 2.0f, buf);

        // Big combo counter that pops on each new hit (scales with beat pulse).
        char cbuf[32];
        snprintf(cbuf, sizeof cbuf, "%d HITS%s", s.duel.combo_count,
                 (s.fighters[0].airborne || s.fighters[1].airborne) ? " AIR" : "");
        float scale = 2.6f + fx.beat_pulse * 1.2f;
        int cg = 120 + (int)(fx.beat_pulse * 135.0f);
        col(r, 255, 200, cg, 255);
        draw_text(r, 640.0f - text_width(scale, cbuf) / 2.0f, 108.0f - fx.beat_pulse * 4.0f, scale,
                  cbuf);
    }

    // Outcome toast for ~2/3 beat after each resolution.
    const ResolutionResult& lr = s.last_result;
    if (lr.resolved_tick != 0 && s.tick - lr.resolved_tick < 20 && lr.outcome != Outcome::None) {
        const char* t = outcome_text(lr, buf2, sizeof buf2);
        if (t) {
            col(r, 255, 255, 255, 255);
            draw_text(r, 640.0f - text_width(2.0f, t) / 2.0f, 165.0f, 2.0f, t);
        }
    }
}

static void draw_phase_overlay(SDL_Renderer* r, const SimulationState& s) {
    char buf[64];
    col(r, 255, 245, 200, 255);
    switch (s.match.phase) {
    case Phase::Intro: {
        snprintf(buf, sizeof buf, "ROUND %d", s.match.round);
        draw_text(r, 640.0f - text_width(5.0f, buf) / 2.0f, 280.0f, 5.0f, buf);
        int beats_left =
            (s.match.phase_timer_ticks + s.clock.ticks_per_beat - 1) / s.clock.ticks_per_beat;
        snprintf(buf, sizeof buf, "%d", beats_left);
        draw_text(r, 640.0f - text_width(4.0f, buf) / 2.0f, 340.0f, 4.0f, buf);
        break;
    }
    case Phase::RoundEnd: {
        const char* reason = "";
        switch (s.match.end_reason) {
        case RoundEndReason::KO:
            reason = "KO!";
            break;
        case RoundEndReason::TimeOver:
            reason = "TIME!";
            break;
        case RoundEndReason::DoubleKO:
            reason = "DOUBLE KO";
            break;
        case RoundEndReason::TimeOverDraw:
            reason = "DRAW";
            break;
        default:
            break;
        }
        draw_text(r, 640.0f - text_width(6.0f, reason) / 2.0f, 280.0f, 6.0f, reason);
        if (s.match.round_winner >= 0) {
            snprintf(buf, sizeof buf, "P%d TAKES THE ROUND", s.match.round_winner + 1);
            draw_text(r, 640.0f - text_width(3.0f, buf) / 2.0f, 350.0f, 3.0f, buf);
        }
        break;
    }
    case Phase::MatchEnd: {
        // Rematch/menu choices live on the shell's Results screen, drawn on top.
        snprintf(buf, sizeof buf, "P%d WINS THE MATCH", s.match.match_winner + 1);
        draw_text(r, 640.0f - text_width(5.0f, buf) / 2.0f, 280.0f, 5.0f, buf);
        break;
    }
    default:
        break;
    }
}

static void draw_debug_overlay(SDL_Renderer* r, const SimulationState& s,
                               const CharacterData chars[2]) {
    char buf[128];
    // Per-input reach bars from each fighter toward the opponent (placeholder
    // for the M1 hit/hurtbox overlay).
    for (int p = 0; p < 2; ++p) {
        const Fighter& f = s.fighters[p];
        const CharacterData& cd = chars[(int)f.character];
        float dir = s.fighters[1 - p].pos_x >= f.pos_x ? 1.0f : -1.0f;
        for (int i = 1; i <= 4; ++i) {
            float y = FLOOR_Y - 120.0f + i * 18.0f;
            col(r, p == 0 ? 232 : 72, p == 0 ? 112 : 190, p == 0 ? 60 : 222, 110);
            float reach = (float)cd.range[i].to_int();
            float x0 = wx(f.pos_x);
            fill(r, dir > 0 ? x0 : x0 - reach, y, reach, 4.0f);
        }
        // Move target marker.
        col(r, 255, 255, 255, 160);
        fill(r, wx(f.move_target_x) - 3.0f, wy(f.move_target_y) - 3.0f, 6.0f, 6.0f);
    }

    col(r, 160, 220, 160, 255);
    snprintf(buf, sizeof buf, "GAP %d  MACRO %s  ATT P%d  COMBO %d  AIRB %d",
             fighter_gap(s).to_int(), s.duel.macro == Macro::Neutral ? "NEUTRAL" : "ADV",
             s.duel.attacker + 1, s.duel.combo_count, s.duel.air_beats_elapsed);
    draw_text(r, 40.0f, 624.0f, 2.0f, buf);
    snprintf(buf, sizeof buf, "CHECKSUM %08X%08X", (uint32_t)(s.checksum() >> 32),
             (uint32_t)s.checksum());
    draw_text(r, 40.0f, 642.0f, 2.0f, buf);
}

// Animated stage backdrop: vertical gradient sky, a beat-synced glow behind the
// arena, back-wall accents and a floor with a faint reflection band.
static void draw_background(SDL_Renderer* r, const SimulationState& s, const EffectSystem& fx) {
    const int W = 1280, H = 720;
    const int bands = 32;
    for (int i = 0; i < bands; ++i) {
        float t = (float)i / (float)(bands - 1);
        int cr = (int)(14 + 26 * t);
        int cg = (int)(12 + 14 * t);
        int cb = (int)(26 + 34 * t);
        col(r, cr, cg, cb, 255);
        fill(r, 0.0f, (float)H * i / bands, (float)W, (float)H / bands + 1.0f);
    }

    // Beat-synced arena glow, brightest on the downbeat.
    float glow = 0.25f + fx.beat_pulse * 0.75f;
    float cx = 640.0f + g_ox;
    float cy = FLOOR_Y + g_oy - 150.0f;
    for (int i = 6; i >= 1; --i) {
        int a = (int)(glow * 10.0f) + i;
        col(r, 120, 70, 150, a);
        float hw = 120.0f + i * 70.0f;
        float hh = 90.0f + i * 45.0f;
        fill(r, cx - hw, cy - hh, hw * 2.0f, hh * 2.0f);
    }

    // Back-wall accent bars framing the arena.
    col(r, 40, 34, 58, 255);
    fill(r, STAGE_X + g_ox, 150.0f + g_oy, 6.0f, FLOOR_Y - 150.0f);
    fill(r, STAGE_X + g_ox + (float)s.tune.stage_width - 6.0f, 150.0f + g_oy, 6.0f,
         FLOOR_Y - 150.0f);

    // Downbeat spotlight sweep pips along the back wall.
    for (int i = 0; i < 8; ++i) {
        bool on = (int)(s.clock.beat_index % 8) == i;
        col(r, on ? 200 : 60, on ? 180 : 54, on ? 120 : 76, on ? 200 : 90);
        float px = STAGE_X + g_ox + 120.0f + i * 130.0f;
        fill(r, px, 156.0f + g_oy, 40.0f, 4.0f);
    }
}

// Clear floating role tag above each fighter's head so it's always obvious who
// is attacking and who is defending during Advantage.
static void draw_role_label(SDL_Renderer* r, const SimulationState& s, int p) {
    const Fighter& f = s.fighters[p];
    if (s.match.phase != Phase::Fighting || f.role == Role::Free)
        return;
    const char* lbl = f.role == Role::Attacker ? "ATTACKER" : "DEFENDER";
    float sc = 1.5f;
    float lw = text_width(sc, lbl);
    float lx = wx(f.pos_x) - lw / 2.0f;
    float ly = wy(f.pos_y) - 196.0f;
    // Backing chip for contrast.
    col(r, 10, 8, 16, 180);
    fill(r, lx - 6.0f, ly - 3.0f, lw + 12.0f, sc * 7.0f + 6.0f);
    if (f.role == Role::Attacker)
        col(r, 255, 215, 80, 255);
    else
        col(r, 120, 200, 255, 255);
    draw_text(r, lx, ly, sc, lbl);
}

// Slam-in "poster": a big headline that punches in (scale overshoot), holds,
// then drifts out. Announces exactly what the last resolution was.
static void draw_action_poster(SDL_Renderer* r, const EffectSystem& fx) {
    if (fx.banner_max <= 0.0f || fx.banner[0] == '\0')
        return;
    float u = fx.banner_t / fx.banner_max;
    if (u < 0.0f)
        u = 0.0f;
    if (u > 1.0f)
        u = 1.0f;

    float scale, alpha;
    if (u < 0.18f) { // slam in: overshoot big -> settle
        float k = u / 0.18f;
        scale = 1.9f - 0.9f * k;
        alpha = k;
    } else if (u > 0.70f) { // fade out: drift larger
        float k = (u - 0.70f) / 0.30f;
        scale = 1.0f + 0.35f * k;
        alpha = 1.0f - k;
    } else {
        scale = 1.0f;
        alpha = 1.0f;
    }

    float ts = 5.0f * scale;
    float tw = text_width(ts, fx.banner);
    float th = ts * 7.0f;
    float cx = 640.0f;
    if (fx.banner_pip == 0)
        cx = 430.0f;
    else if (fx.banner_pip == 1)
        cx = 850.0f;
    float x = cx - tw / 2.0f;
    float y = 214.0f;

    float pad = 18.0f * scale;
    // Poster panel.
    col(r, 12, 10, 20, (int)(alpha * 200.0f));
    fill(r, x - pad, y - pad * 0.5f, tw + pad * 2.0f, th + pad);
    // Colored rule bars top and bottom.
    col(r, fx.banner_r, fx.banner_g, fx.banner_b, (int)(alpha * 255.0f));
    fill(r, x - pad, y - pad * 0.5f, tw + pad * 2.0f, 5.0f);
    fill(r, x - pad, y + th + pad * 0.5f - 5.0f, tw + pad * 2.0f, 5.0f);
    // Drop shadow + colored headline.
    col(r, 0, 0, 0, (int)(alpha * 170.0f));
    draw_text(r, x + 4.0f, y + 4.0f, ts, fx.banner);
    col(r, fx.banner_r, fx.banner_g, fx.banner_b, (int)(alpha * 255.0f));
    draw_text(r, x, y, ts, fx.banner);
}

void draw_frame(SDL_Renderer* ren, const SimulationState& s, const CharacterData chars[2],
                const SpriteSheet sheets[2], const EffectSystem& fx, const ViewState& view) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    g_ox = fx.shake_x;
    g_oy = fx.shake_y;

    col(ren, 20, 16, 30, 255);
    SDL_RenderClear(ren);

    draw_background(ren, s, fx);
    effects_render_back(ren, fx);

    // Stage: floor line + wall posts (shake with the action layer).
    col(ren, 188, 184, 204, 255);
    fill(ren, STAGE_X + g_ox, FLOOR_Y + g_oy, (float)s.tune.stage_width, 3.0f);
    // Floor reflection band under the line.
    for (int i = 1; i <= 5; ++i) {
        col(ren, 120, 116, 150, 40 - i * 6);
        fill(ren, STAGE_X + g_ox, FLOOR_Y + g_oy + 3.0f + i * 3.0f, (float)s.tune.stage_width,
             3.0f);
    }
    col(ren, 90, 86, 110, 255);
    fill(ren, STAGE_X + g_ox + (float)s.tune.wall_margin - 4.0f, FLOOR_Y + g_oy - 260.0f, 4.0f,
         260.0f);
    fill(ren, STAGE_X + g_ox + (float)(s.tune.stage_width - s.tune.wall_margin),
         FLOOR_Y + g_oy - 260.0f, 4.0f, 260.0f);
    // Center mark.
    col(ren, 50, 46, 66, 255);
    fill(ren, STAGE_X + g_ox + s.tune.stage_width / 2.0f - 1.0f, FLOOR_Y + g_oy - 16.0f, 2.0f,
         16.0f);

    draw_fighter(ren, s, 0, sheets, fx);
    draw_fighter(ren, s, 1, sheets, fx);
    draw_role_label(ren, s, 0);
    draw_role_label(ren, s, 1);
    effects_render_front(ren, fx);

    draw_health(ren, s, chars);
    draw_beat_bar(ren, s, fx);
    draw_state_banner(ren, s, fx);
    draw_phase_overlay(ren, s);
    if (view.overlay)
        draw_debug_overlay(ren, s, chars);

    // Full-screen impact flash (KO, launch, steal).
    if (fx.flash > 0.01f) {
        int a = (int)(fx.flash * 200.0f);
        if (a > 220)
            a = 220;
        col(ren, fx.flash_r, fx.flash_g, fx.flash_b, a);
        fill(ren, 0.0f, 0.0f, 1280.0f, 720.0f);
    }

    // Slam-in action poster, on top of the flash so it stays readable.
    draw_action_poster(ren, fx);

    // Bottom status lines (debug HUD, Options-toggleable).
    char buf[160];
    if (view.debug_hud) {
        col(ren, 120, 116, 140, 255);
        snprintf(buf, sizeof buf, "FRAME %llu  BEAT %u.%02u  FPS %d", (unsigned long long)s.tick,
                 s.clock.beat_index, (unsigned)(s.tick % s.clock.ticks_per_beat),
                 (int)(view.fps + 0.5f));
        draw_text(ren, 40.0f, 668.0f, 2.0f, buf);

        snprintf(buf, sizeof buf,
                 "P1 WASD  P2 ARROWS/F8 CPU  F1 OVERLAY  F5 PAUSE F6 STEP F7 RUN  F9 REC F10 REPLAY");
        draw_text(ren, 40.0f, 692.0f, 2.0f, buf);
    }

    float rx = 1240.0f;
    if (view.paused) {
        col(ren, 255, 215, 80, 255);
        rx -= text_width(2.0f, "PAUSED") + 16.0f;
        draw_text(ren, rx, 668.0f, 2.0f, "PAUSED");
    }
    if (view.recording) {
        col(ren, 255, 90, 90, 255);
        rx -= text_width(2.0f, "REC") + 16.0f;
        draw_text(ren, rx, 668.0f, 2.0f, "REC");
    }
    if (view.replaying) {
        col(ren, 120, 200, 255, 255);
        rx -= text_width(2.0f, "REPLAY") + 16.0f;
        draw_text(ren, rx, 668.0f, 2.0f, "REPLAY");
    }
    {
        static const char* cpu_names[3] = {"EASY", "NORMAL", "HARD"};
        int seats[2] = {view.cpu_p0, view.cpu_p1};
        for (int p = 1; p >= 0; --p) {
            if (seats[p] < 1 || seats[p] > 3) continue;
            snprintf(buf, sizeof buf, "P%d CPU %s", p + 1, cpu_names[seats[p] - 1]);
            col(ren, 200, 150, 255, 255);
            rx -= text_width(2.0f, buf) + 16.0f;
            draw_text(ren, rx, 668.0f, 2.0f, buf);
        }
    }
    if (view.replay_status == 1) {
        col(ren, 120, 255, 140, 255);
        draw_text(ren, 1040.0f, 692.0f, 2.0f, "REPLAY: CHECKSUMS OK");
    } else if (view.replay_status == 2) {
        col(ren, 255, 90, 90, 255);
        snprintf(buf, sizeof buf, "DIVERGED @ %llu", (unsigned long long)view.diverge_tick);
        draw_text(ren, 1000.0f, 692.0f, 2.0f, buf);
    }
}

// ---- shell screens -------------------------------------------------------------
// Text menus in the placeholder 5x7 font. The selected row rides fx.beat_pulse
// so even the UI moves on the metronome.

static void text_centered(SDL_Renderer* r, float y, float scale, const char* t) {
    draw_text(r, 640.0f - text_width(scale, t) / 2.0f, y, scale, t);
}

static void menu_item(SDL_Renderer* r, float y, float scale, const char* t, bool sel,
                      const EffectSystem& fx) {
    if (sel) {
        int pulse = (int)(fx.beat_pulse * 100.0f);
        col(r, 255, 215 + (int)(fx.beat_pulse * 40.0f), 80 + pulse, 255);
        float w = text_width(scale, t);
        draw_text(r, 640.0f - w / 2.0f - 24.0f, y, scale, ">");
        draw_text(r, 640.0f + w / 2.0f + 12.0f, y, scale, "<");
    } else {
        col(r, 165, 160, 185, 255);
    }
    text_centered(r, y, scale, t);
}

static void hint_line(SDL_Renderer* r, const char* t) {
    col(r, 120, 116, 140, 255);
    text_centered(r, 692.0f, 2.0f, t);
}

static const char* char_name(CharId c) {
    return c == CharId::Breaker ? "BREAKER" : "BALLERINA";
}

static const char* control_name(int c) {
    static const char* n[4] = {"HUMAN", "CPU EASY", "CPU NORMAL", "CPU HARD"};
    return n[c < 0 || c > 3 ? 0 : c];
}

static void draw_title(SDL_Renderer* r, const shell::Shell& sh, const EffectSystem& fx) {
    col(r, 255, 245, 200, 255);
    text_centered(r, 130.0f, 9.0f, "NO ENGINE GAME");
    col(r, 200, 150, 255, 255);
    text_centered(r, 205.0f, 3.0f, "A RHYTHM YOMI DUEL");

    static const char* items[shell::TITLE_ITEMS] = {"VS PLAYER", "VS CPU",  "WATCH",
                                                    "HOW TO PLAY", "OPTIONS", "QUIT"};
    for (int i = 0; i < shell::TITLE_ITEMS; ++i)
        menu_item(r, 300.0f + 42.0f * (float)i, 3.0f, items[i], sh.cursor == i, fx);
    hint_line(r, "W/S OR ARROWS - MOVE   ENTER - SELECT   ESC - QUIT");
}

static void draw_setup(SDL_Renderer* r, const shell::Shell& sh, const EffectSystem& fx) {
    col(r, 255, 245, 200, 255);
    text_centered(r, 120.0f, 5.0f, "MATCH SETUP");

    static const char* rounds_name[3] = {"BEST OF 1", "BEST OF 3", "BEST OF 5"};
    char buf[64];
    const shell::MatchSetup& m = sh.setup;
    const char* rows[shell::SETUP_ROWS];
    char lines[5][64];
    snprintf(lines[0], sizeof lines[0], "P1 FIGHTER   < %s >", char_name(m.chars[0]));
    snprintf(lines[1], sizeof lines[1], "P1 CONTROL   < %s >", control_name(m.control[0]));
    snprintf(lines[2], sizeof lines[2], "P2 FIGHTER   < %s >", char_name(m.chars[1]));
    snprintf(lines[3], sizeof lines[3], "P2 CONTROL   < %s >", control_name(m.control[1]));
    snprintf(lines[4], sizeof lines[4], "ROUNDS       < %s >", rounds_name[m.rounds_idx]);
    for (int i = 0; i < 5; ++i) rows[i] = lines[i];
    rows[5] = "START";
    for (int i = 0; i < shell::SETUP_ROWS; ++i)
        menu_item(r, 240.0f + 52.0f * (float)i, 3.0f, rows[i], sh.cursor == i, fx);

    if (m.chars[0] == m.chars[1]) {
        col(r, 140, 136, 160, 255);
        snprintf(buf, sizeof buf, "MIRROR MATCH");
        text_centered(r, 560.0f, 2.0f, buf);
    }
    hint_line(r, "A/D - CHANGE   W/S - MOVE   ENTER - START   ESC - BACK");
}

static void draw_howto(SDL_Renderer* r, const shell::Shell& sh) {
    col(r, 255, 245, 200, 255);
    static const char* headers[shell::HOWTO_PAGES] = {"HOW TO PLAY - THE BEAT  1/2",
                                                      "HOW TO PLAY - THE MIND GAME  2/2"};
    text_centered(r, 100.0f, 4.0f, headers[sh.page]);

    static const char* page0[] = {
        "ONE MOVE PER BEAT. BOTH SIDES COMMIT BLIND;",
        "THE BEAT RESOLVES THEM TOGETHER.",
        "",
        "A - CLOSE   STEP IN AND JAB       P1: D    P2: LEFT",
        "B - FAR     POKE AND RETREAT      P1: A    P2: RIGHT",
        "C - UP      LEAP AND LAUNCH       P1: W    P2: UP",
        "D - DOWN    SWEEP IN PLACE        P1: S    P2: DOWN",
        "",
        "PRESS CLOSE TO THE BEAT: PERFECT BEATS NORMAL.",
        "TOO FAR OFF - OR SILENT - IS A MISS.",
        "IF YOU BOTH MISS, YOU BOTH TAKE CHIP DAMAGE.",
    };
    static const char* page1[] = {
        "IN NEUTRAL, ATTACKS IN RANGE CLASH:",
        "A BEATS B,C    D BEATS A,B    B BEATS C    C BEATS D",
        "A AND D ARE SAFE BUT LIGHT. B AND C ARE RISKY BUT HEAVY.",
        "",
        "WIN A CLASH TO BECOME THE ATTACKER.",
        "ATTACKER: ANY INPUT EXTENDS THE COMBO - 5 HITS MAX.",
        "DEFENDER: MATCH THE ATTACKER'S INPUT TO BREAK FREE.",
        "MATCH IT WITH PERFECT TIMING TO STEAL THE COMBO.",
        "",
        "READ YOUR OPPONENT. PREDICT, DON'T REACT.",
    };
    const char** lines = sh.page == 0 ? page0 : page1;
    int n = sh.page == 0 ? (int)(sizeof page0 / sizeof page0[0]) : (int)(sizeof page1 / sizeof page1[0]);
    col(r, 200, 200, 210, 255);
    for (int i = 0; i < n; ++i)
        text_centered(r, 190.0f + 36.0f * (float)i, 2.0f, lines[i]);
    hint_line(r, "A/D - PAGE   ENTER - NEXT   ESC - BACK");
}

static void draw_options(SDL_Renderer* r, const shell::Shell& sh, const EffectSystem& fx) {
    col(r, 255, 245, 200, 255);
    text_centered(r, 120.0f, 5.0f, "OPTIONS");
    const data::Settings& st = *sh.settings;

    char vol[64];
    char bar[12];
    for (int i = 0; i < 10; ++i) bar[i] = i < st.volume ? '#' : '-';
    bar[10] = 0;
    snprintf(vol, sizeof vol, "VOLUME       < %s >", bar);
    char lines[4][64];
    snprintf(lines[0], sizeof lines[0], "%s", vol);
    snprintf(lines[1], sizeof lines[1], "SCREEN SHAKE < %s >", st.screenshake ? "ON" : "OFF");
    snprintf(lines[2], sizeof lines[2], "FULLSCREEN   < %s >", st.fullscreen ? "ON" : "OFF");
    snprintf(lines[3], sizeof lines[3], "DEBUG HUD    < %s >", st.debug_hud ? "ON" : "OFF");
    for (int i = 0; i < 4; ++i)
        menu_item(r, 260.0f + 52.0f * (float)i, 3.0f, lines[i], sh.cursor == i, fx);
    menu_item(r, 260.0f + 52.0f * 4.0f, 3.0f, "BACK", sh.cursor == 4, fx);
    hint_line(r, "A/D - CHANGE   W/S - MOVE   ESC - SAVE AND BACK");
}

static void draw_pause(SDL_Renderer* r, const shell::Shell& sh, const EffectSystem& fx) {
    col(r, 255, 245, 200, 255);
    text_centered(r, 180.0f, 6.0f, "PAUSED");
    static const char* items[shell::PAUSE_ITEMS] = {"RESUME", "RESTART MATCH", "MATCH SETUP",
                                                    "QUIT TO TITLE"};
    for (int i = 0; i < shell::PAUSE_ITEMS; ++i)
        menu_item(r, 300.0f + 48.0f * (float)i, 3.0f, items[i], sh.cursor == i, fx);
    hint_line(r, "ENTER - SELECT   ESC - RESUME");
}

static void draw_results(SDL_Renderer* r, const shell::Shell& sh, const EffectSystem& fx) {
    // The match's own "P WINS THE MATCH" banner stays visible above.
    static const char* items[shell::RESULT_ITEMS] = {"REMATCH", "MATCH SETUP", "TITLE"};
    for (int i = 0; i < shell::RESULT_ITEMS; ++i)
        menu_item(r, 400.0f + 48.0f * (float)i, 3.0f, items[i], sh.cursor == i, fx);
    hint_line(r, "ENTER - SELECT   ESC - TITLE");
}

void draw_shell(SDL_Renderer* ren, const shell::Shell& sh, const EffectSystem& fx) {
    if (sh.screen == shell::Screen::Match)
        return;

    // Dim what's behind: heavier over the attract demo, lighter over a match.
    bool over_match = sh.screen == shell::Screen::Pause || sh.screen == shell::Screen::Results;
    col(ren, 8, 6, 14, over_match ? 130 : 185);
    fill(ren, 0.0f, 0.0f, 1280.0f, 720.0f);

    switch (sh.screen) {
    case shell::Screen::Title: draw_title(ren, sh, fx); break;
    case shell::Screen::Setup: draw_setup(ren, sh, fx); break;
    case shell::Screen::HowTo: draw_howto(ren, sh); break;
    case shell::Screen::Options: draw_options(ren, sh, fx); break;
    case shell::Screen::Pause: draw_pause(ren, sh, fx); break;
    case shell::Screen::Results: draw_results(ren, sh, fx); break;
    default: break;
    }
}

} // namespace render
} // namespace neg
