#include "render/draw.h"
#include "render/font5x7.h"
#include "sim/fighter.h"
#include <cstdio>

namespace neg {
namespace render {

// World -> screen: stage x in [0, stage_width] maps left-to-right with a
// margin; world y is height above the floor.
static const float STAGE_X = 40.0f;
static const float FLOOR_Y = 600.0f;

static float wx(Fixed x) { return STAGE_X + (float)x.to_int(); }
static float wy(Fixed y) { return FLOOR_Y - (float)y.to_int(); }

static void fill(SDL_Renderer* r, float x, float y, float w, float h) {
    SDL_FRect rc{x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

static const char* input_name(Input i) {
    switch (i) {
    case Input::A: return "A CLOSE";
    case Input::B: return "B FAR";
    case Input::C: return "C UP";
    case Input::D: return "D DOWN";
    default: return "-";
    }
}

static const char* tier_name(Tier t) {
    switch (t) {
    case Tier::Perfect: return "PERFECT";
    case Tier::Normal: return "NORMAL";
    default: return "MISS";
    }
}

static void tier_color(SDL_Renderer* r, Tier t) {
    switch (t) {
    case Tier::Perfect: SDL_SetRenderDrawColor(r, 255, 215, 80, 255); break;
    case Tier::Normal: SDL_SetRenderDrawColor(r, 200, 200, 210, 255); break;
    default: SDL_SetRenderDrawColor(r, 120, 100, 110, 255); break;
    }
}

static void draw_fighter(SDL_Renderer* r, const SimulationState& s, int p) {
    const Fighter& f = s.fighters[p];
    float w = p == 0 ? 58.0f : 48.0f;     // Breaker broad, Ballerina slender
    float h = p == 0 ? 150.0f : 170.0f;
    float x = wx(f.pos_x) - w / 2.0f;
    float y = wy(f.pos_y) - h;

    // Defender flashes white for a few ticks after taking damage.
    bool hit_flash = false;
    const ResolutionResult& lr = s.last_result;
    if (lr.resolved_tick != 0 && s.tick - lr.resolved_tick < 6) {
        int32_t dmg = p == 0 ? lr.damage_p0 : lr.damage_p1;
        hit_flash = dmg > 0;
    }

    if (hit_flash) SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    else if (p == 0) SDL_SetRenderDrawColor(r, 232, 112, 60, 255);
    else SDL_SetRenderDrawColor(r, 72, 190, 222, 255);
    fill(r, x, y, w, h);

    // Attacker outline.
    if (f.role == Role::Attacker) {
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_FRect rc{x - 2, y - 2, w + 4, h + 4};
        SDL_RenderRect(r, &rc);
    }

    // Facing notch at chest height.
    SDL_SetRenderDrawColor(r, 20, 16, 30, 255);
    float nx = f.facing_right ? x + w - 10.0f : x;
    fill(r, nx, y + h * 0.25f, 10.0f, 12.0f);

    // Shadow on the floor while airborne.
    if (f.pos_y > Fixed::zero()) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 120);
        fill(r, wx(f.pos_x) - w / 2.0f, FLOOR_Y - 4.0f, w, 4.0f);
    }
}

static void draw_health(SDL_Renderer* r, const SimulationState& s, const CharacterData chars[2]) {
    char buf[64];
    for (int p = 0; p < 2; ++p) {
        const Fighter& f = s.fighters[p];
        int32_t maxhp = chars[(int)f.character].health;
        float frac = maxhp > 0 ? (float)f.health / (float)maxhp : 0.0f;
        if (frac < 0) frac = 0;
        float bx = p == 0 ? 40.0f : 720.0f;
        float bw = 520.0f;

        SDL_SetRenderDrawColor(r, 70, 20, 26, 255);
        fill(r, bx, 40.0f, bw, 22.0f);
        SDL_SetRenderDrawColor(r, frac > 0.35f ? 90 : 220, frac > 0.35f ? 210 : 70, 80, 255);
        float fw = bw * frac;
        // P0 bar drains toward the center, P1 away from the center.
        fill(r, p == 0 ? bx + (bw - fw) : bx, 40.0f, fw, 22.0f);

        SDL_SetRenderDrawColor(r, 235, 235, 240, 255);
        const char* nm = chars[(int)f.character].name;
        if (p == 0) draw_text(r, bx, 70.0f, 2.0f, nm);
        else draw_text(r, bx + bw - text_width(2.0f, nm), 70.0f, 2.0f, nm);

        // Round-win pips.
        for (int wpip = 0; wpip < s.tune.rounds_to_win; ++wpip) {
            int wins = p == 0 ? s.match.wins_p0 : s.match.wins_p1;
            if (wpip < wins) SDL_SetRenderDrawColor(r, 255, 215, 80, 255);
            else SDL_SetRenderDrawColor(r, 60, 56, 76, 255);
            float px = p == 0 ? bx + bw - 14.0f - wpip * 20.0f : bx + wpip * 20.0f;
            fill(r, px, 12.0f, 14.0f, 14.0f);
        }

        // This beat's committed input + tier (the S3 readout).
        const Commit& c = f.commit;
        if (c.locked) {
            tier_color(r, c.tier);
            snprintf(buf, sizeof buf, "%s %s", input_name(c.input), tier_name(c.tier));
            if (p == 0) draw_text(r, bx, 92.0f, 2.0f, buf);
            else draw_text(r, bx + bw - text_width(2.0f, buf), 92.0f, 2.0f, buf);
        }
    }

    // Round timer.
    int secs = s.match.round_timer_ticks > 0 ? (s.match.round_timer_ticks + 59) / 60 : 0;
    snprintf(buf, sizeof buf, "%02d", secs);
    SDL_SetRenderDrawColor(r, 235, 235, 240, 255);
    draw_text(r, 640.0f - text_width(4.0f, buf) / 2.0f, 34.0f, 4.0f, buf);
}

static void draw_beat_bar(SDL_Renderer* r, const SimulationState& s) {
    // Phase bar: a marker sweeps each beat; the center tick is the instant.
    float bx = 440.0f, bw = 400.0f, by = 96.0f;
    uint16_t tpb = s.clock.ticks_per_beat;
    uint32_t phase = (uint32_t)(s.tick % tpb);

    SDL_SetRenderDrawColor(r, 44, 40, 60, 255);
    fill(r, bx, by, bw, 14.0f);

    // Perfect / Normal bands around the center instant, to scale.
    float center = bx + bw / 2.0f;
    float px_per_tick = bw / (float)tpb;
    SDL_SetRenderDrawColor(r, 70, 66, 96, 255);
    fill(r, center - s.tune.normal_ticks * px_per_tick, by, 2.0f * s.tune.normal_ticks * px_per_tick, 14.0f);
    SDL_SetRenderDrawColor(r, 120, 104, 60, 255);
    fill(r, center - s.tune.perfect_ticks * px_per_tick, by, 2.0f * (s.tune.perfect_ticks + 1) * px_per_tick, 14.0f);

    // Sweep marker: phase 0 == instant == center; window runs -half..+half.
    uint32_t half = tpb / 2u;
    float offset = (phase <= half) ? (float)phase : (float)phase - (float)tpb;
    float mx = center + offset * px_per_tick;
    bool pulse = phase <= s.tune.perfect_ticks || phase >= (uint32_t)(tpb - s.tune.perfect_ticks);
    SDL_SetRenderDrawColor(r, pulse ? 255 : 200, pulse ? 245 : 200, pulse ? 200 : 210, 255);
    fill(r, mx - 2.0f, by - 4.0f, 4.0f, 22.0f);

    // 4-count pips under the bar.
    for (int i = 0; i < 4; ++i) {
        if ((int)(s.clock.beat_index % 4) == i) SDL_SetRenderDrawColor(r, 255, 245, 200, 255);
        else SDL_SetRenderDrawColor(r, 60, 56, 76, 255);
        fill(r, bx + bw / 2.0f - 44.0f + i * 24.0f, by + 20.0f, 14.0f, 6.0f);
    }
}

static const char* outcome_text(const ResolutionResult& lr, char* buf, size_t n) {
    int w = lr.winner;
    switch (lr.outcome) {
    case Outcome::BothWhiff: return "WHIFF";
    case Outcome::BothMiss: return "CHIP - BOTH MISSED THE BEAT";
    case Outcome::OneLands: snprintf(buf, n, "P%d LANDS", w + 1); return buf;
    case Outcome::RpsDecided: snprintf(buf, n, "P%d WINS THE CLASH", w + 1); return buf;
    case Outcome::SameTypeTie:
        if (w < 0) return "TIE - RECLASH";
        snprintf(buf, n, "P%d OUT-TIMES THE TIE", w + 1);
        return buf;
    case Outcome::ComboContinue: return "HIT";
    case Outcome::ComboBreak: snprintf(buf, n, "P%d BREAKS FREE", w + 1); return buf;
    case Outcome::ComboSteal: snprintf(buf, n, "P%d STEALS!", w + 1); return buf;
    case Outcome::ComboCapEnd: return "COMBO CAP";
    case Outcome::ComboMissEnd: return "DROPPED THE BEAT";
    case Outcome::Launch: return "LAUNCH!";
    case Outcome::GroundOut: return "GROUNDED";
    default: return nullptr;
    }
}

static void draw_state_banner(SDL_Renderer* r, const SimulationState& s) {
    char buf[64], buf2[64];
    SDL_SetRenderDrawColor(r, 180, 176, 196, 255);
    if (s.duel.macro == Macro::Neutral) {
        draw_text(r, 640.0f - text_width(2.0f, "NEUTRAL") / 2.0f, 140.0f, 2.0f, "NEUTRAL");
    } else {
        snprintf(buf, sizeof buf, "P%d ADVANTAGE  COMBO %d%s", s.duel.attacker + 1,
                 s.duel.combo_count,
                 (s.fighters[0].airborne || s.fighters[1].airborne) ? "  AIR" : "");
        SDL_SetRenderDrawColor(r, 255, 215, 80, 255);
        draw_text(r, 640.0f - text_width(2.0f, buf) / 2.0f, 140.0f, 2.0f, buf);
    }

    // Outcome toast for ~2/3 beat after each resolution.
    const ResolutionResult& lr = s.last_result;
    if (lr.resolved_tick != 0 && s.tick - lr.resolved_tick < 20 && lr.outcome != Outcome::None) {
        const char* t = outcome_text(lr, buf2, sizeof buf2);
        if (t) {
            SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
            draw_text(r, 640.0f - text_width(2.0f, t) / 2.0f, 165.0f, 2.0f, t);
        }
    }
}

static void draw_phase_overlay(SDL_Renderer* r, const SimulationState& s) {
    char buf[64];
    SDL_SetRenderDrawColor(r, 255, 245, 200, 255);
    switch (s.match.phase) {
    case Phase::Intro: {
        snprintf(buf, sizeof buf, "ROUND %d", s.match.round);
        draw_text(r, 640.0f - text_width(5.0f, buf) / 2.0f, 280.0f, 5.0f, buf);
        int beats_left = (s.match.phase_timer_ticks + s.clock.ticks_per_beat - 1) /
                         s.clock.ticks_per_beat;
        snprintf(buf, sizeof buf, "%d", beats_left);
        draw_text(r, 640.0f - text_width(4.0f, buf) / 2.0f, 340.0f, 4.0f, buf);
        break;
    }
    case Phase::RoundEnd: {
        const char* reason = "";
        switch (s.match.end_reason) {
        case RoundEndReason::KO: reason = "KO!"; break;
        case RoundEndReason::TimeOver: reason = "TIME!"; break;
        case RoundEndReason::DoubleKO: reason = "DOUBLE KO"; break;
        case RoundEndReason::TimeOverDraw: reason = "DRAW"; break;
        default: break;
        }
        draw_text(r, 640.0f - text_width(6.0f, reason) / 2.0f, 280.0f, 6.0f, reason);
        if (s.match.round_winner >= 0) {
            snprintf(buf, sizeof buf, "P%d TAKES THE ROUND", s.match.round_winner + 1);
            draw_text(r, 640.0f - text_width(3.0f, buf) / 2.0f, 350.0f, 3.0f, buf);
        }
        break;
    }
    case Phase::MatchEnd: {
        snprintf(buf, sizeof buf, "P%d WINS THE MATCH", s.match.match_winner + 1);
        draw_text(r, 640.0f - text_width(5.0f, buf) / 2.0f, 280.0f, 5.0f, buf);
        SDL_SetRenderDrawColor(r, 200, 200, 210, 255);
        draw_text(r, 640.0f - text_width(2.0f, "PRESS ENTER TO REMATCH") / 2.0f, 340.0f, 2.0f,
                  "PRESS ENTER TO REMATCH");
        break;
    }
    default: break;
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
            SDL_SetRenderDrawColor(r, p == 0 ? 232 : 72, p == 0 ? 112 : 190,
                                   p == 0 ? 60 : 222, 110);
            float reach = (float)cd.range[i].to_int();
            float x0 = wx(f.pos_x);
            fill(r, dir > 0 ? x0 : x0 - reach, y, reach, 4.0f);
        }
        // Move target marker.
        SDL_SetRenderDrawColor(r, 255, 255, 255, 160);
        fill(r, wx(f.move_target_x) - 3.0f, wy(f.move_target_y) - 3.0f, 6.0f, 6.0f);
    }

    SDL_SetRenderDrawColor(r, 160, 220, 160, 255);
    snprintf(buf, sizeof buf, "GAP %d  MACRO %s  ATT P%d  COMBO %d  AIRB %d",
             fighter_gap(s).to_int(), s.duel.macro == Macro::Neutral ? "NEUTRAL" : "ADV",
             s.duel.attacker + 1, s.duel.combo_count, s.duel.air_beats_elapsed);
    draw_text(r, 40.0f, 624.0f, 2.0f, buf);
    snprintf(buf, sizeof buf, "CHECKSUM %08X%08X", (uint32_t)(s.checksum() >> 32),
             (uint32_t)s.checksum());
    draw_text(r, 40.0f, 642.0f, 2.0f, buf);
}

void draw_frame(SDL_Renderer* ren, const SimulationState& s, const CharacterData chars[2],
                const ViewState& view) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 20, 16, 30, 255);
    SDL_RenderClear(ren);

    // Stage: floor line + wall posts.
    SDL_SetRenderDrawColor(ren, 188, 184, 204, 255);
    fill(ren, STAGE_X, FLOOR_Y, (float)s.tune.stage_width, 3.0f);
    SDL_SetRenderDrawColor(ren, 90, 86, 110, 255);
    fill(ren, STAGE_X + (float)s.tune.wall_margin - 4.0f, FLOOR_Y - 260.0f, 4.0f, 260.0f);
    fill(ren, STAGE_X + (float)(s.tune.stage_width - s.tune.wall_margin), FLOOR_Y - 260.0f, 4.0f,
         260.0f);
    // Center mark.
    SDL_SetRenderDrawColor(ren, 50, 46, 66, 255);
    fill(ren, STAGE_X + s.tune.stage_width / 2.0f - 1.0f, FLOOR_Y - 16.0f, 2.0f, 16.0f);

    draw_fighter(ren, s, 0);
    draw_fighter(ren, s, 1);
    draw_health(ren, s, chars);
    draw_beat_bar(ren, s);
    draw_state_banner(ren, s);
    draw_phase_overlay(ren, s);
    if (view.overlay) draw_debug_overlay(ren, s, chars);

    // Bottom status lines.
    char buf[160];
    SDL_SetRenderDrawColor(ren, 120, 116, 140, 255);
    snprintf(buf, sizeof buf, "FRAME %llu  BEAT %u.%02u  FPS %d",
             (unsigned long long)s.tick, s.clock.beat_index,
             (unsigned)(s.tick % s.clock.ticks_per_beat), (int)(view.fps + 0.5f));
    draw_text(ren, 40.0f, 668.0f, 2.0f, buf);

    snprintf(buf, sizeof buf, "P1 WASD  P2 ARROWS  F1 OVERLAY  F5 PAUSE F6 STEP F7 RUN  F9 REC F10 REPLAY");
    draw_text(ren, 40.0f, 692.0f, 2.0f, buf);

    float rx = 1240.0f;
    if (view.paused) {
        SDL_SetRenderDrawColor(ren, 255, 215, 80, 255);
        rx -= text_width(2.0f, "PAUSED") + 16.0f;
        draw_text(ren, rx, 668.0f, 2.0f, "PAUSED");
    }
    if (view.recording) {
        SDL_SetRenderDrawColor(ren, 255, 90, 90, 255);
        rx -= text_width(2.0f, "REC") + 16.0f;
        draw_text(ren, rx, 668.0f, 2.0f, "REC");
    }
    if (view.replaying) {
        SDL_SetRenderDrawColor(ren, 120, 200, 255, 255);
        rx -= text_width(2.0f, "REPLAY") + 16.0f;
        draw_text(ren, rx, 668.0f, 2.0f, "REPLAY");
    }
    if (view.replay_status == 1) {
        SDL_SetRenderDrawColor(ren, 120, 255, 140, 255);
        draw_text(ren, 1040.0f, 692.0f, 2.0f, "REPLAY: CHECKSUMS OK");
    } else if (view.replay_status == 2) {
        SDL_SetRenderDrawColor(ren, 255, 90, 90, 255);
        snprintf(buf, sizeof buf, "DIVERGED @ %llu", (unsigned long long)view.diverge_tick);
        draw_text(ren, 1000.0f, 692.0f, 2.0f, buf);
    }
}

} // namespace render
} // namespace neg
