// Render-side juice: particles, screen shake, hit flash and beat pulse.
// Reads SimulationState only (never mutates it), keeps its own float state and
// its own PRNG so it lives entirely OUTSIDE the deterministic core — nothing
// here touches the checksum, so recording/replay stay bit-exact (research.md
// §1.4). All coordinates are screen-space pixels.
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <vector>
#include "sim/types.h"

namespace neg {
namespace render {

enum ParticleKind { P_SPARK = 0, P_DUST, P_MOTE, P_RING, P_STREAK, P_STAR, P_SPEEDLINE };

struct Particle {
    float x = 0, y = 0;
    float vx = 0, vy = 0;
    float life = 0, max_life = 1;
    float size = 2;
    float grow = 0; // radius growth px/s (rings)
    float gravity = 0;
    float drag = 0;
    float spin = 0, rot = 0;
    Uint8 r = 255, g = 255, b = 255, a0 = 255;
    int kind = P_SPARK;
};

struct EffectSystem {
    std::vector<Particle> particles;
    uint32_t rng = 0x9e3779b9u;

    // Screen shake (world layer only — HUD stays put).
    float shake_mag = 0.0f;
    float shake_x = 0.0f, shake_y = 0.0f;

    // Full-screen tint flash (KO / big hit).
    float flash = 0.0f;
    Uint8 flash_r = 255, flash_g = 255, flash_b = 255;

    // Per-fighter white hit flash (drives sprite color-mod), 0..1 decaying.
    float hit_flash[2] = {0.0f, 0.0f};

    // Beat metronome pulse 0..1, kicked on every beat instant.
    float beat_pulse = 0.0f;
    float mote_accum = 0.0f;
    float time = 0.0f;

    // Slam-in action poster: a big headline announcing what just happened.
    // banner_t counts up from 0; banner_max is its total lifetime (seconds).
    char banner[32] = {0};
    float banner_t = 0.0f;
    float banner_max = 0.0f;
    Uint8 banner_r = 255, banner_g = 255, banner_b = 255;
    int banner_pip = -1; // 0/1 to tag the poster to a player, -1 = center only

    // Edge-detection bookkeeping.
    bool initialized = false;
    uint64_t last_resolved_tick = 0;
    uint32_t last_beat_index = 0;
    Phase last_phase = Phase::Intro;
    int32_t prev_pos_x[2] = {0, 0};
    uint16_t last_combo = 0;
    uint8_t last_round = 1;
    uint32_t last_commit_beat[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};
};

void effects_init(EffectSystem& fx, const SimulationState& s);

// Advance particles, decay shake/flash, and edge-detect sim events to spawn
// new bursts. Call once per rendered frame with dt in seconds.
void effects_update(EffectSystem& fx, const SimulationState& s, float dt);

// Ambient layer, drawn behind the fighters (drifting motes, no shake).
void effects_render_back(SDL_Renderer* r, const EffectSystem& fx);

// Impact layer, drawn in front of the fighters (sparks, dust, rings) with the
// current world shake offset applied.
void effects_render_front(SDL_Renderer* r, const EffectSystem& fx);

} // namespace render
} // namespace neg
