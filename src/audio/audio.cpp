#include "audio/audio.h"

#include <miniaudio.h>
#include <vector>
#include <cmath>
#include <cstdint>

namespace neg {
namespace audio {

static const ma_uint32 RATE = 48000;
static const int POOL = 4;

struct VoicePool {
    std::vector<float> pcm;
    ma_audio_buffer buffers[POOL];
    ma_sound sounds[POOL];
    int inited = 0;
    int next = 0;

    bool init(ma_engine* eng, std::vector<float>&& data, float volume) {
        pcm = std::move(data);
        for (int i = 0; i < POOL; ++i) {
            ma_audio_buffer_config cfg = ma_audio_buffer_config_init(
                ma_format_f32, 1, (ma_uint64)pcm.size(), pcm.data(), nullptr);
            cfg.sampleRate = RATE;
            if (ma_audio_buffer_init(&cfg, &buffers[i]) != MA_SUCCESS) return false;
            if (ma_sound_init_from_data_source(eng, &buffers[i],
                                               MA_SOUND_FLAG_NO_PITCH |
                                                   MA_SOUND_FLAG_NO_SPATIALIZATION,
                                               nullptr, &sounds[i]) != MA_SUCCESS) {
                ma_audio_buffer_uninit(&buffers[i]);
                return false;
            }
            ma_sound_set_volume(&sounds[i], volume);
            inited = i + 1;
        }
        return true;
    }

    void play() {
        if (inited < POOL) return;
        ma_sound_seek_to_pcm_frame(&sounds[next], 0);
        ma_sound_start(&sounds[next]);
        next = (next + 1) % POOL;
    }

    void shutdown() {
        for (int i = 0; i < inited; ++i) {
            ma_sound_uninit(&sounds[i]);
            ma_audio_buffer_uninit(&buffers[i]);
        }
        inited = 0;
    }
};

struct Audio {
    ma_engine engine;
    bool ok = false;
    VoicePool click, accent, hit, sting;
    uint64_t last_instant = ~0ULL;
    uint64_t last_resolution = 0;
    Phase last_phase = Phase::Intro;
};

// ---- tiny synth helpers ------------------------------------------------------

static std::vector<float> sine_burst(float freq, int ms, float amp, float decay) {
    int n = (int)(RATE * ms / 1000);
    std::vector<float> v((size_t)n);
    for (int i = 0; i < n; ++i) {
        float t = (float)i / (float)RATE;
        float env = std::exp(-decay * t);
        v[(size_t)i] = amp * env * std::sin(6.2831853f * freq * t);
    }
    return v;
}

static std::vector<float> thud(int ms, float amp) {
    // Low sine + decaying noise: a placeholder impact.
    int n = (int)(RATE * ms / 1000);
    std::vector<float> v((size_t)n);
    uint32_t noise = 0x12345678u;
    for (int i = 0; i < n; ++i) {
        float t = (float)i / (float)RATE;
        float env = std::exp(-28.0f * t);
        noise = noise * 1664525u + 1013904223u;
        float nz = ((float)(noise >> 8) / 8388608.0f - 1.0f);
        v[(size_t)i] = amp * env * (0.7f * std::sin(6.2831853f * 170.0f * t) + 0.3f * nz);
    }
    return v;
}

static std::vector<float> sweep(float f0, float f1, int ms, float amp) {
    int n = (int)(RATE * ms / 1000);
    std::vector<float> v((size_t)n);
    float phase = 0.0f;
    for (int i = 0; i < n; ++i) {
        float t = (float)i / (float)(n ? n : 1);
        float f = f0 + (f1 - f0) * t;
        phase += 6.2831853f * f / (float)RATE;
        float env = std::sin(3.1415926f * t); // fade in/out
        v[(size_t)i] = amp * env * std::sin(phase);
    }
    return v;
}

// ---- public API --------------------------------------------------------------

Audio* init() {
    Audio* a = new Audio();
    if (ma_engine_init(nullptr, &a->engine) != MA_SUCCESS) {
        a->ok = false; // no audio device: game runs silent
        return a;
    }
    bool ok = true;
    ok &= a->click.init(&a->engine, sine_burst(2100.0f, 40, 0.55f, 90.0f), 0.5f);
    ok &= a->accent.init(&a->engine, sine_burst(1400.0f, 60, 0.7f, 60.0f), 0.65f);
    ok &= a->hit.init(&a->engine, thud(110, 0.9f), 0.8f);
    ok &= a->sting.init(&a->engine, sweep(420.0f, 980.0f, 160, 0.6f), 0.7f);
    a->ok = ok;
    return a;
}

void shutdown(Audio* a) {
    if (!a) return;
    a->click.shutdown();
    a->accent.shutdown();
    a->hit.shutdown();
    a->sting.shutdown();
    if (a->ok) ma_engine_uninit(&a->engine);
    delete a;
}

void update(Audio* a, const SimulationState& s) {
    if (!a || !a->ok) return;

    // Metronome: a click on every beat instant, accented on the 4-count.
    uint64_t instant = s.tick / s.clock.ticks_per_beat;
    if (instant != a->last_instant) {
        if (a->last_instant != ~0ULL) { // skip the very first frame
            if (instant % 4 == 0) a->accent.play();
            else a->click.play();
        }
        a->last_instant = instant;
    }

    // Resolution edges: impacts and the steal sting.
    const ResolutionResult& lr = s.last_result;
    if (lr.resolved_tick != 0 && lr.resolved_tick != a->last_resolution) {
        a->last_resolution = lr.resolved_tick;
        if (lr.outcome == Outcome::ComboSteal) a->sting.play();
        else if (lr.damage_p0 > 0 || lr.damage_p1 > 0) a->hit.play();
    }

    // Round-end sting.
    if (s.match.phase != a->last_phase) {
        if (s.match.phase == Phase::RoundEnd) a->sting.play();
        a->last_phase = s.match.phase;
    }
}

} // namespace audio
} // namespace neg
