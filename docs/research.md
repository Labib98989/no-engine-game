# No Engine Game — Foundational Research

**Project:** 2D fighting game, no engine, mainly C/C++, Street Fighter 1 inspired.
**Scope (milestone 1):** 2 characters, 1 stage, local 2-player.
**Date compiled:** 2026-05-19.

This document consolidates five parallel research streams (SDL3 stack, fighting-game engine architecture, fixed-timestep determinism, sprite/animation pipeline, and Street Fighter 1 design specifics) into a single opinionated reference. Where the streams disagreed, the contradiction is called out and resolved here.

> **⚠️ Partially superseded (2026-06-02).** This doc predates the genre pivot from "SF1-style fighter" to **rhythm yomi duel**. Its **infrastructure** sections — §1 (stack), §2 (game loop), §3 (determinism/fixed-point), §5 (animation/render), §7 (collision math), §10 (milestones), §11 (discipline) — are **still authoritative**. Its **gameplay** sections — **§4 (motion inputs/QCF/DP), §6 (frame-data move/cancel system), §8 (SF-style state machine), §9 (SF1 movesets), and throws** — describe a game the project **no longer builds** and are **superseded by `docs/technical.md`** (the rhythm-duel systems design). When in doubt about gameplay, read `technical.md`.

---

## 0. Executive Decisions (one page)

| Area | Decision | Why |
|---|---|---|
| Language | **C++17 in a C-like style** | RAII for SDL handles, `std::vector`/`std::array` for collections; no inheritance trees, no template metaprogramming. Plain C functions and POD structs everywhere else. |
| Compiler | **MSVC 2022** (`cl.exe`), `/fp:strict`, `/W4`, `/permissive-`, `/utf-8` | Best Windows debugger story, ASan available, RenderDoc integration. Keep `clang-cl` as a secondary toolchain for occasional UBSan passes. |
| Build system | **CMake 3.25+** with `CMakePresets.json`, Visual Studio 17 2022 generator | Standard, IDE-friendly, scales as deps grow. |
| Deps manager | **vcpkg in manifest mode** (`vcpkg.json`) | Versions pinned per project; binary caching; clean `find_package` integration. |
| Windowing/input/render/audio backbone | **SDL3 3.4.x** (currently 3.4.8 stable) | SDL3 stable since Jan 2025; do not start a new project on SDL2 today. |
| Image loading | **SDL3_image 3.4.x** (`IMG_LoadTexture`) | Direct `SDL_Texture` output; loading is not a differentiator. Don't roll your own stb path. |
| Audio | **miniaudio** (single-header, vendored) | SDL_mixer 3 only stabilised March 2026; too young to trust. miniaudio gives WASAPI shared/exclusive, polyphonic sample pools, deterministic mixing. |
| Renderer API | **SDL_Renderer** with `SDL_HINT_RENDER_DRIVER="direct3d11"` | Auto-batches in SDL3; ~10 draw calls/frame at our scale. Do *not* start on `SDL_GPU` — overkill. |
| Sim/render coupling | **Fixed 60Hz simulation, render the latest sim state directly, no interpolation, VSync on, free renderer at panel rate** | Universal fighter convention. Interpolation hides hitbox truth and corrupts frame-data debugging. |
| Game loop entry | **Classic `int main()`** (not `SDL_AppIterate`) | Need explicit control for frame-step, rollback resimulation, replay. |
| Numerics in `/sim` | **Q16.16 fixed-point** for positions/velocities; integer ticks for time; seeded PRNG inside `SimulationState` | Future-proofs rollback netcode and cross-compiler replays. Float-with-`/fp:strict` is the acceptable fallback if you're certain you'll stay Windows/MSVC forever — but the fixed-point conversion later is invasive, so pay the small upfront cost now. |
| Authoring | **Aseprite as single source** for sprites, frame durations, animation tags, pivots, and hitbox/hurtbox/pushbox **slices** | Co-locates art with metadata; CLI works in CI; standard pipeline in the indie pixel-art world. |
| Atlas | **Per-character atlas** (≤ 2048² to start) + **shared FX atlas** + **HUD atlas** + **stage atlas** | ~5 textures total → SDL3 auto-batches the entire frame in ~10 GPU draws. |
| Move data | **Hand-edited JSON per move** that references Aseprite frame tags; merged with Aseprite slices at load into POD `Move` structs | Gameplay payload (damage, hitstun, cancel rules) is hand-tuned; box shapes are art-driven. |
| Frame timing | **Convert Aseprite's millisecond durations to ticks at load**; gameplay never speaks in milliseconds | Fighters are frame-indexed. |
| Coordinate convention | All boxes authored canonical **facing-right**; mirror at runtime via a single `sx = ±1` everywhere (sprite blit, boxes, projectile spawns, knockback vectors) | Single source of truth; no duplicated authoring. |
| Pivot | **Per-frame foot anchor** (center-bottom of character) | Animations of different heights still land on the ground. |
| Debug overlay | Toggleable hitbox/hurtbox/pushbox/throwbox/pivot draw on day one (F1) | Non-negotiable; you will use it every working hour. |

---

## 1. Stack — Details and Rationale

### 1.1 SDL3 vs SDL2 (2026 state)

- SDL 3.2.0 shipped **2025-01-21**, first stable. 3.4.x is the current stable line (latest 3.4.8, May 2026).
- SDL2 is in maintenance only.
- **Migration is mostly trivial but a few changes are silent footguns when copy-pasting from old tutorials.** Tabulated below:

| SDL2 | SDL3 | Note |
|---|---|---|
| `SDL_CreateRenderer(win, idx, flags)` | `SDL_CreateRenderer(win, name)` | `name` is a driver string or NULL. VSync set via `SDL_SetRenderVSync()`. |
| `SDL_CreateWindow(title, x, y, w, h, flags)` | `SDL_CreateWindow(title, w, h, flags)` | x/y removed. |
| `SDL_KEYDOWN` | `SDL_EVENT_KEY_DOWN` | All event enums prefixed `SDL_EVENT_`. |
| `event.key.keysym.scancode` | `event.key.scancode` | `keysym` struct flattened. |
| `event.key.state == SDL_PRESSED` | `event.key.down` (`bool`) | |
| `SDL_INIT_GAMECONTROLLER` | `SDL_INIT_GAMEPAD` | "GameController" → "Gamepad" everywhere. |
| `SDL_RenderCopy(r, tex, src, dst)` (`SDL_Rect`) | `SDL_RenderTexture(r, tex, src, dst)` (`SDL_FRect`) | Float rects throughout. |
| `SDL_RenderCopyEx` | `SDL_RenderTextureRotated` | |
| `SDL_RWFromFile` | `SDL_IOFromFile` | |
| `SDL_GetTicks()` → `Uint32 ms` | `Uint64 ms`; new `SDL_GetTicksNS()` → `Uint64 ns` | |
| Functions returning `int < 0` on error | Functions returning `bool` (`true` = success) | **The single most pervasive change.** Reverses every error check. |

### 1.2 Audio (the only contested choice)

- SDL_mixer 3.2.0 stable only since **March 2026** (~2 months at time of writing).
- miniaudio: two files, public domain, WASAPI on Windows, decoders for WAV/FLAC/MP3 built in, sample-pool polyphony via `ma_sound_init_copy` shared decode buffer.
- For a fighter you'll routinely have 4–6 simultaneous voices (hit SFX, voice clip, blockstun SFX, music). miniaudio's sample-pool model maps cleanly; SDL_mixer's "channels" abstraction is older and noisier.

```c
// minimal miniaudio pattern for polyphonic SFX
ma_engine engine;
ma_engine_init(NULL, &engine);

ma_sound hit_template;
ma_sound_init_from_file(&engine, "sfx/hit_light.wav",
                        MA_SOUND_FLAG_DECODE, NULL, NULL, &hit_template);

ma_sound hit_pool[8];
for (int i = 0; i < 8; ++i)
    ma_sound_init_copy(&engine, &hit_template, 0, NULL, &hit_pool[i]);

// round-robin on each new hit event
int next = 0;
void play_hit(void) {
    ma_sound_seek_to_pcm_frame(&hit_pool[next], 0);
    ma_sound_start(&hit_pool[next]);
    next = (next + 1) % 8;
}
```

### 1.3 Build system recipe

**`vcpkg.json`:**

```json
{
  "name": "no-engine-game",
  "version-string": "0.1.0",
  "dependencies": [
    "sdl3",
    { "name": "sdl3-image", "features": ["png"] },
    "nlohmann-json"
  ]
}
```

> **Gotcha (verified 2026-06-20):** vcpkg's `sdl3-image` enables **no image formats by default** — you must request the `png` feature explicitly, or `IMG_Load` fails at runtime with *"Unsupported image format"* (the build still succeeds). The `png` feature pulls in libpng + zlib, whose DLLs are staged automatically by `$<TARGET_RUNTIME_DLLS:neg>`.

**`CMakeLists.txt` skeleton:**

```cmake
cmake_minimum_required(VERSION 3.25)
project(neg C CXX)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)

find_package(SDL3       CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(miniaudio STATIC third_party/miniaudio/miniaudio.c)
target_include_directories(miniaudio PUBLIC third_party/miniaudio)

# Pure simulation library — NO SDL, NO float (after we go fixed-point), NO globals
add_library(neg_sim STATIC
    src/sim/fixed.cpp
    src/sim/rng.cpp
    src/sim/input.cpp
    src/sim/fighter.cpp
    src/sim/collision.cpp
    src/sim/simulation.cpp
)
target_include_directories(neg_sim PUBLIC src)

# Platform + render layer
add_executable(neg WIN32
    src/main.cpp
    src/platform/sdl_app.cpp
    src/platform/sdl_input.cpp
    src/render/renderer.cpp
    src/render/debug_draw.cpp
    src/data/loader.cpp
    src/audio/audio.cpp
)
target_link_libraries(neg PRIVATE
    neg_sim SDL3::SDL3 SDL3_image::SDL3_image
    nlohmann_json::nlohmann_json miniaudio)

if (MSVC)
    target_compile_options(neg PRIVATE
        /W4 /permissive- /Zc:preprocessor /utf-8 /fp:strict
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2 /GL>)
    target_link_options(neg PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

# Stage SDL3.dll alongside the exe
add_custom_command(TARGET neg POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:neg> $<TARGET_FILE_DIR:neg>
    COMMAND_EXPAND_LISTS)
```

**`CMakePresets.json`:**

```json
{
  "version": 6,
  "configurePresets": [{
    "name": "msvc-x64",
    "generator": "Visual Studio 17 2022",
    "architecture": "x64",
    "binaryDir": "${sourceDir}/build/msvc-x64",
    "toolchainFile": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
    "cacheVariables": { "VCPKG_TARGET_TRIPLET": "x64-windows" }
  }, {
    "name": "msvc-x64-asan",
    "inherits": "msvc-x64",
    "binaryDir": "${sourceDir}/build/msvc-x64-asan",
    "cacheVariables": { "ENABLE_ASAN": "ON" }
  }]
}
```

Then `set VCPKG_ROOT=C:\dev\vcpkg`, `cmake --preset msvc-x64`, open the resulting `.sln`.

### 1.4 Module layout (the cardinal rule)

```
src/
  sim/         ← deterministic. No SDL, no chrono, no float (Q16.16 only).
  data/        ← JSON → POD loaders
  render/      ← SDL3-dependent. Reads SimulationState; never mutates.
  platform/   ← SDL3 init, event pump, window
  audio/       ← miniaudio glue
  main.cpp
```

`neg_sim` is compiled as a static library with no SDL or system dependencies. This *physically* enforces the determinism boundary.

---

## 2. Game Loop

### 2.1 The fighter's loop (final form)

```cpp
constexpr std::uint64_t DT_NS                = 16'666'667ULL;   // 60 Hz
constexpr std::uint64_t MAX_FRAME_NS         = 250'000'000ULL;  // anti-spiral clamp
constexpr int           MAX_STEPS_PER_FRAME  = 5;               // hard ceiling

int main(int, char**) {
    SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "1");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER,    "direct3d11");
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO);

    SDL_Window*   win = SDL_CreateWindow("neg", 1280, 720, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
    SDL_SetRenderVSync(ren, 1);

    SimulationState state{};
    Inputs          inputs{};
    LoopMode        mode = LoopMode::Running;
    bool            step_request = false;
    bool            quit = false;

    std::uint64_t last_ns    = SDL_GetTicksNS();
    std::uint64_t accumulator = 0;

    while (!quit) {
        std::uint64_t now_ns = SDL_GetTicksNS();
        std::uint64_t frame_ns = now_ns - last_ns;
        last_ns = now_ns;
        if (frame_ns > MAX_FRAME_NS) frame_ns = MAX_FRAME_NS;

        platform::poll_events(inputs, quit, mode, step_request);

        if (mode == LoopMode::Running) {
            accumulator += frame_ns;
            int steps = 0;
            while (accumulator >= DT_NS && steps < MAX_STEPS_PER_FRAME) {
                sim::tick(state, inputs);     // pure function
                accumulator -= DT_NS;
                ++steps;
            }
            if (accumulator >= DT_NS) accumulator = 0;  // drop overflow
        } else if (step_request) {
            sim::tick(state, inputs);
            step_request = false;
        }

        render::draw_frame(ren, state);
        SDL_RenderPresent(ren);
    }
    /* shutdown */
}
```

### 2.2 Why no render interpolation for a fighter

Two of the five research streams disagreed superficially here. Glenn Fiedler's canonical pattern renders an interpolated blend `previous * (1-α) + current * α`. **For a fighter, do not do this.** Reasons:

1. Animation is *frame-indexed*. Active-frame is discrete. Interpolating between sprite frames either drops you on the wrong sprite or smears two sprites together — both are bugs.
2. Hitboxes attach to specific frames. Interpolated boxes are a lie; players (and you, debugging) cannot read frame data visually.
3. Adds visible input lag (you always render one tick behind).
4. Every shipped 2D fighter (SF6 lock-60, GG Strive lock-60, Skullgirls 60+frameskip) renders the latest sim state. None interpolates gameplay.

High-refresh monitors (144Hz, 240Hz) still benefit from the free-running render: the *panel scanout latency* drops, even though the same sim state may be drawn 2–4 times in a row. With VRR enabled there's no perceptible judder.

### 2.3 Spiral-of-death belt-and-suspenders

The 250ms clamp prevents a paused debugger from queueing 30 sim steps. The per-frame ceiling (`MAX_STEPS_PER_FRAME = 5`) prevents a slow sim tick from feeding itself. In netplay you wouldn't drop accumulator (rollback wants lockstep) — but local-only is fine.

### 2.4 Frame-step (day-one feature)

Bind:
- **F5** → pause/resume
- **F6** → step one tick (paused only)
- **F7** → resume from pause
- **F9** → start input recording / **F10** → replay last recording

Frame-step requires no special code beyond what's above: in `Paused`, only run a sim tick when `step_request` is set, and *do not consume the accumulator*. Replays = log of `Inputs` per tick + initial `SimulationState`.

### 2.5 SDL3 timing notes

- `SDL_GetTicksNS()` → `Uint64` ns, monotonic, sub-µs on Windows (QPC under the hood).
- Use `SDL_DelayPrecise()` (SDL 3.2.0+) for sub-ms sleeps if needed; busy-waits the tail.
- `SDL_HINT_TIMER_RESOLUTION="1"` (default) raises Windows timer resolution at init.
- **Do not use `SDL_AppIterate`** — the new main-callbacks model. It exists for iOS and Emscripten; on desktop it strips control needed for frame-step and future rollback resimulation.

---

## 3. Determinism

### 3.1 Why fighters are built deterministic on day one

Even without netcode: replays as input streams (~50KB per round vs MB of state), frame-step debugging, deterministic AI/training mode dummies, reproducible bug reports, automated regression tests. Rollback netcode later becomes a 2–3 week integration; without determinism it's a 6-month rewrite.

### 3.2 The constraints (enforced at the `/sim` library boundary)

1. **No floats in the sim core** — Q16.16 fixed-point. `int32_t` storage, multiply via `int64_t` intermediate then shift.
2. **One seeded PRNG inside `SimulationState`**. No `rand()`, no `std::random_device`, no `time()`. PCG32 or `std::mt19937`.
3. **No `time()` as a sim input.** Hitstop is in frames.
4. **No `std::unordered_map` iteration.** Use sorted vectors or `std::map` or fixed-index arrays.
5. **No pointer comparisons for ordering.** No `std::hash<T*>`.
6. **Single-threaded sim.** Rendering and audio mixing can be parallel.
7. **Pre-allocate.** Or use a deterministic arena.

### 3.3 The float-vs-fixed contradiction, resolved

The research streams disagreed: agent-3 said float-with-`/fp:strict` is sufficient on a single Windows/MSVC target; agent-2 said go fixed-point unconditionally. **Resolution: go fixed-point from day one.** Reasons:

- The conversion later is invasive — touches every position, velocity, collision math, projectile spawn, knockback vector.
- Q16.16 covers ±32,767 with 1/65,536 resolution. Ample for any 2D fighter screen.
- The implementation is 60 lines of code (see §3.4).
- It makes the determinism boundary *enforceable* — `/sim` literally won't compile if a float sneaks in.

If you genuinely will never go cross-platform and never ship rollback, float+`/fp:strict` works. But for a learning project where the *point* is to build it correctly, fixed-point is the right call.

### 3.4 Q16.16 fixed-point primer

```cpp
struct Fixed {
    int32_t v;
    static constexpr int32_t ONE = 1 << 16;
    static Fixed from_int(int i)   { return { i << 16 }; }
    static Fixed from_raw(int32_t r){ return { r }; }
    int     to_int() const         { return v >> 16; }
    Fixed   operator+(Fixed o) const { return { v + o.v }; }
    Fixed   operator-(Fixed o) const { return { v - o.v }; }
    Fixed   operator*(Fixed o) const {
        return { (int32_t)(((int64_t)v * o.v) >> 16) };
    }
    Fixed   operator/(Fixed o) const {
        return { (int32_t)(((int64_t)v << 16) / o.v) };
    }
    bool    operator<(Fixed o) const { return v < o.v; }
    bool    operator==(Fixed o) const{ return v == o.v; }
};
```

No raw-int interop. Always go through `Fixed::from_int`.

### 3.5 The checksum trick

In dev builds, compute `SimulationState::checksum()` (a hash of all simulation state) each tick and log it. When you record/replay an input stream, checksums must match exactly per tick. First divergence = your bug. Standard practice in GGPO-integrated games.

---

## 4. Input

### 4.1 The pattern

- **Drain the SDL event queue completely** every outer-loop iteration (`while (SDL_PollEvent(&ev))`).
- Build a `FrameInput` snapshot containing `pressed[]`, `released[]`, `held[]` arrays indexed by logical button.
- Pass that snapshot to every sim tick in the catch-up burst (`MAX_STEPS_PER_FRAME` cap).
- **Inside the sim, push the snapshot onto a per-player `InputBuffer` ring** (last 64 frames).
- Motion detection runs against that ring.

### 4.2 Keyboard vs gamepad

- **Scancodes only** (`SDL_SCANCODE_J`), never keycodes. Scancodes are physical-key-location; keycodes are layout-dependent.
- **Hybrid model**: events (`SDL_EVENT_KEY_DOWN/UP`) capture transitions, including sub-frame press+release; `SDL_GetKeyboardState()` confirms held state in case of state desync (alt-tab).
- **`SDL_Gamepad` API**, never `SDL_Joystick`. Maps PS4 sticks, Xbox controllers, fightsticks, 8BitDo, etc. via the SDL community mapping db (built into SDL3).
- Set `SDL_SetHint(SDL_HINT_WINDOWS_RAW_KEYBOARD, "1")` before init for a frame or two of latency reduction (test for overlay compatibility — Discord/OBS can interact).

### 4.3 Input encoding (numpad notation)

Standard FGC convention. Fits in 4 bits.

```
7 8 9      up-back  up    up-fwd
4 5 6  =   back     5     forward
1 2 3      dn-back  down  dn-fwd       (5 = neutral)
```

```cpp
// 16-bit per-frame snapshot
//   bits  0-3 : numpad direction (1-9, 5 = neutral)
//   bits  4-9 : button states (LP, MP, HP, LK, MK, HK)
//   bits 10-15: button edges ("just pressed this frame")
using InputFrame = uint16_t;
```

### 4.4 Motion input detection

Backward-scan with leniency windows. Pseudo:

```cpp
struct MotionStep { uint16_t accepted_dirs; int within_frames; };

// Quarter-circle-forward (236)
const MotionStep QCF[] = {
    { 1<<2,                0  },   // most recent: down
    { (1<<2)|(1<<3),       8  },   // before: down or down-forward
    { (1<<3)|(1<<6),       8  },   // before that: down-forward or forward
};

// Dragon punch (623)
const MotionStep DP[] = {
    { 1<<6,                0  },
    { (1<<2)|(1<<3),      10  },
    { 1<<3,               10  },
};
```

**Lenience norms across fighters:**
- SF2–SF4: ~6 frames per step, strict.
- SF5: 8–12 frames, lenient.
- GG Strive: ~6 frames + 4-frame end-of-move action buffer.
- Marvel: 11+ frames, very lenient.

**Start at: 8 frames motion lenience, 4-frame action buffer.** Tune from there.

### 4.5 Negative edge (SF1's notorious quirk — do NOT replicate)

SF1's pneumatic-pad heritage meant specials fired on **button release**. The original arcade compounded execution difficulty by hiding inputs from players and using tight directional windows. **Use SF2-style on-press detection** for your project. Optionally support negative edge on specials only (as SF2+ does) — never on normals.

### 4.6 Simultaneous button detection

Allow ±1-frame slop. When LP arrives, peek the next frame's input before resolving the command (throw = LP+LK, super = three buttons). Resolve special moves first (higher priority), then normals.

---

## 5. Animation & Rendering

### 5.1 Authoring: Aseprite is the source of truth

One `.aseprite` per character. Layers for body parts if you want, but optional. Inside the file:

- **Frame tags** = animations: `"stand"`, `"crouch"`, `"walk_fwd"`, `"jab"`, `"hp"`, `"qcf_p"`, `"hurt_high"`, `"kd"`, etc. Each tag has `from`/`to` frame indices.
- **Frame durations** = per-frame ms. Converted to ticks at load (`ticks = round(ms / (1000.0/60.0))`).
- **Slices** = boxes and pivot. Each slice has named per-frame `keys` with `bounds {x,y,w,h}` so a hitbox can move/resize per active frame.

**Slice naming convention** (enforce in your importer):

| Slice name prefix | Role | Author color (hint, not consumed by code) |
|---|---|---|
| `pivot`            | Foot anchor (1×1) | Green |
| `hurtbox`          | Hurtbox (`hurtbox_torso`, `_head`, `_legs`) | Blue |
| `hitbox`           | Hitbox (`hitbox_0`, `_1` for multi-hit) | Red |
| `pushbox`          | Push collision | Yellow |
| `throwbox`         | Throw range | Purple |
| `throw_hurtbox`    | Throw vulnerability | Cyan |
| `spawn_*`          | Spawn anchor (projectile, vfx) | Magenta |

(Box color conventions in *training modes* vary by game — SF uses green hurtbox / red hitbox; ArcSys uses blue hurtbox. Pick one for your debug overlay; the slice color in Aseprite is just an author hint.)

**Export (CI):**

```bash
aseprite -b ryu.aseprite --sheet ryu.png --sheet-pack \
    --data ryu.json --format json-array \
    --list-tags --list-slices --trim
```

Use `json-array` not `json-hash` — order-preserving and maps 1:1 to `std::vector<AtlasFrame>`.

### 5.2 In-memory layout

```cpp
struct AtlasFrame {
    SDL_FRect src;             // pixel rect on atlas
    int16_t   pivot_x, pivot_y;// foot anchor (canonical right-facing)
    int16_t   trim_off_x, trim_off_y;
    uint16_t  source_w, source_h;
    uint16_t  duration_ticks;
};

struct AABB { int16_t x, y, w, h; }; // sprite-local, canonical right-facing

struct FrameBoxes {
    std::vector<AABB> hurtboxes, hitboxes, pushboxes, throwboxes;
    std::vector<HitProperties> hit_props; // parallel to hitboxes
};
```

### 5.3 The animation player (tiny FSM)

Ticks once per simulation step. Fires events on specific cumulative ticks (sorted, indexed). Events are routed to the character state machine; the animation player has no opinion about what events *mean*.

Event kinds: `SpawnHitbox`, `RemoveHitbox`, `SetHurtbox`, `PlaySfx`, `SpawnProjectile`, `CameraShake`, `SpawnVfx`, `ApplyVelocity`, `FlagActiveFrame`.

### 5.4 SDL_Renderer: enough?

Yes, comfortably. SDL3 auto-batches consecutive draws of the same `(texture, blend mode, color mod)` into a single GPU call. With per-character + shared + HUD + stage atlases, the entire frame is ~8–12 GPU draws regardless of sprite count.

**Batch-breaking state changes:**
- Texture change
- Blend mode change
- Color/alpha mod change (per-vertex tint needs `SDL_RenderGeometry`)
- Viewport / clip rect change
- Render-target change
- `SDL_RenderReadPixels` (full GPU sync — never per frame)

**Draw order strategy** (sort once per frame):
1. Stage background + parallax layers
2. Behind-character VFX
3. Characters (sort by world Y for jump occlusion)
4. Projectiles
5. In-front VFX (hit sparks, super flash)
6. Stage foreground
7. HUD (screen-space)
8. Debug overlay

### 5.5 Facing flip — the hitbox-mirror problem

Sprite mirror is one SDL call; the *hard* part is mirroring every coordinate-bearing piece of data. Convention:

- Author and store *everything* in canonical right-facing.
- At runtime, when `facing == Left`, mirror via `b.x → source_w - (b.x + b.w)` for boxes, sign-flip `vx` and `launch_x` for velocities, mirror projectile spawn points.

```cpp
inline AABB mirror_x(AABB b, int16_t sprite_w) {
    return { (int16_t)(sprite_w - (b.x + b.w)), b.y, b.w, b.h };
}
```

The sprite blit uses `SDL_RenderTextureRotated` with `SDL_FLIP_HORIZONTAL` (rotation angle = 0 is fine; `Rotated` is the only call that takes a flip flag in SDL3).

### 5.6 Pivot and ground alignment

Center-bottom convention. Character world position = feet on ground. At blit time:

```cpp
SDL_FRect dst = {
    char.pos_x - frame.pivot_x + frame.trim_off_x,
    char.pos_y - frame.pivot_y + frame.trim_off_y,
    (float)frame.src.w, (float)frame.src.h
};
SDL_FPoint pivot = {
    frame.pivot_x - frame.trim_off_x,
    frame.pivot_y - frame.trim_off_y
};
SDL_RenderTextureRotated(ren, atlas, &frame.src, &dst, 0.0, &pivot,
    char.facing == Facing::Left ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
```

Pivot is authored as a 1×1 (or 2×2 for visibility) Aseprite slice named `pivot`, per frame. Trim offsets compensate for Aseprite's `--trim` removing transparent borders.

### 5.7 Debug overlay (day-one feature)

Two layers per box — translucent fill + opaque outline. Implementable as `SDL_RenderFillRect` + `SDL_RenderRect`. Toggle whole overlay on F1; individual box types on F2–F5. Pixel-snap outlines to integer coords to avoid sub-pixel shimmer at high refresh.

Convention (pick one and stick):
- Hurtbox blue (alpha 0.4)
- Hitbox red (alpha 0.4)
- Pushbox yellow
- Throwbox purple
- Throw hurtbox cyan
- Pivot 4×4 green square

Add **active-frame indicator** (flash hitbox white on active frame), **frame counter HUD** (`"Startup 4/7"`), and **hit history log** as soon as you have hitboxes connecting.

---

## 6. Frame Data & Move System

### 6.1 The contract

A "move" decomposes into:

| Phase | Definition |
|---|---|
| Startup | Frames from button press until first active frame |
| Active | Frames during which hitbox can connect |
| Recovery | Frames after active during which attacker cannot act/block |
| Hit advantage | Attacker recovery vs. defender hitstun (positive = attacker free first) |
| Block advantage | Same but defender in blockstun (usually smaller, often negative) |
| Hitstun / Blockstun | Defender lock duration on hit / block |
| Pushback | Velocity applied to one or both fighters on hit/block (usually separate values) |
| Damage / Chip / Stun | Health, guard, and dizzy gauge changes |

Invariant: `total frames = startup + active + recovery`. First-active-frame wins on trade.

### 6.2 Authoring

JSON per move, hand-edited, referencing Aseprite frame tags:

```json
{
  "name": "hp",
  "animation_tag": "hp",
  "cancel_into": ["special", "super"],
  "frames": { "startup": [0,7], "active": [8,10], "recovery": [11,18] },
  "hits": [{
    "active_window": [8,10],
    "damage": 80, "hitstun": 18, "blockstun": 14,
    "pushback_hit": 1200, "pushback_block": 800,
    "launch_y": 0, "hit_level": "mid",
    "sfx": "hit_heavy", "vfx": "spark_heavy",
    "from_slice": "hitbox"
  }],
  "events": [
    { "tick": 8,  "kind": "PlaySfx",     "param": "whoosh" },
    { "tick": 12, "kind": "CameraShake", "param": 4 }
  ]
}
```

At load, the importer merges this with the Aseprite slices (`from_slice: "hitbox"` becomes the per-frame `AABB`s for frames 8–10) into POD `Move` structures.

### 6.3 Cancel rules

```cpp
struct CancelRule {
    Frame from_frame, to_frame;
    std::vector<int> into_moves;  // move IDs reachable in this window
};
```

Every tick during an attack, scan `move.cancel_windows` for current frame ∈ window, then run input matching against the current `InputBuffer` for `into_moves`. First match wins.

Cancel hierarchy (Capcom convention):
- Normal → Special (the "2-in-1")
- Special → Super (super-cancel)
- Normal → Normal (chain/gatling — listed explicitly per-move in ArcSys games)
- Anything → Throw (kara-throw via early startup-cancel window: `from_frame: 1, to_frame: 3`)

---

## 7. Hitbox / Hurtbox / Collision

### 7.1 Box taxonomy

| Box | Purpose |
|---|---|
| Hurtbox | Where you can be hit |
| Hitbox (strike) | Where your attack can hit |
| Throwbox | Where your throw will grab |
| Throw-hurtbox | Where you can be thrown (disabled airborne / in hitstun) |
| Pushbox | Prevents fighters from overlapping |
| Proximity box | Optional: triggers different normal at close range |
| Projectile hitbox | Projectile owns its own AABB |

All AABBs. Capsules/OBBs are not used in 2D fighters — they buy nothing and complicate rollback serialization.

### 7.2 Collision resolution per tick (algorithm)

```cpp
void resolve_collisions(Fighter& A, Fighter& B) {
    // 1) Push out via pushboxes
    resolve_pushbox_overlap(A, B);

    // 2) Strike-vs-hurt detection (both directions)
    bool a_hits = any_hitbox_intersects_hurtbox(A, B);
    bool b_hits = any_hitbox_intersects_hurtbox(B, A);

    // SF convention: simultaneous = trade (both connect)
    if (a_hits && b_hits) {
        apply_hit(A, B, ContactType::Trade);
        apply_hit(B, A, ContactType::Trade);
    } else if (a_hits) {
        apply_hit(A, B, ContactType::Clean);
    } else if (b_hits) {
        apply_hit(B, A, ContactType::Clean);
    }

    // 3) Throw resolution (separate pass)
    resolve_throws(A, B);
}
```

**First-active-frame wins**: implement by detecting on a hitbox's *first* active frame only, then marking the box "spent" until the move ends. This is the cleanest way to enforce the convention without a priority field.

### 7.3 Hitstop ("freeze frames")

On contact, both fighters' state timers pause for N frames (10–14 typical) so the impact reads. Implementation: a `hitstop_frames` counter on each fighter; while `> 0`, state timer does not advance. **Pushback still applies during hitstop.**

### 7.4 Throws — note for SF1 scope

SF1 had NO throws. Throws entered the series with SF2 (adapted from *Final Fight*). For milestone 1, **skip throws entirely.** Mechanics remain: footsies, normals, specials, jump-ins, blocking. That's a complete, playable, satisfying fighter loop.

Reserve throws for milestone 2 when you add the second character or refine the first. When you do implement: throwbox on attacker active frames vs. throwable-state defender → atomic state swap into linked `Throwing` / `ThrownVictim`, with a ~7-frame tech window for the defender to escape by inputting throw.

---

## 8. State Machine

### 8.1 Hybrid enum + data-driven (recommended)

```cpp
enum class CoreState : uint8_t {
    Idle, WalkF, WalkB, CrouchDown, Crouch, CrouchUp,
    JumpStart, JumpAir, JumpLand,
    BlockStand, BlockCrouch,
    HitstunStand, HitstunCrouch, HitstunAir,
    KnockdownHard, KnockdownSoft, Wakeup,
    Attack,            // currentMove drives behavior
    KO
};

struct Fighter {
    CoreState   state          = CoreState::Idle;
    Frame       state_timer    = 0;
    const Move* current_move   = nullptr;
    Frame       hitstop_frames = 0;
    Frame       hitstun        = 0;
    Frame       blockstun      = 0;
    Fixed       pos_x, pos_y;
    Fixed       vel_x, vel_y;
    int         health;
    bool        facing_right;
    bool        airborne;
    InputBuffer input;          // ring of last 64 InputFrames
    uint32_t    state_flags;    // bitfield: invul, armor, throwable, etc.
};
```

~10 hand-coded core states (movement + reactions + KO) + an `Attack` umbrella state that interprets a `Move` from data. Adding a new attack = adding a JSON file, no engine code.

### 8.2 Why this and not full data-driven (MUGEN/CNS) or HSM

- Hierarchical FSM (parent states `Grounded`/`Airborne`/`Stunned` with shared transitions) is cleaner for 30+ states but over-engineered for 2 characters.
- Full data-driven (MUGEN's `[Statedef N]` with embedded scripting) is the right pro answer but is weeks of work to design the scripting layer.
- The hybrid keeps the surface tiny while giving designers unlimited attacks via JSON. Migrate to full data-driven later if scope expands.

---

## 9. Street Fighter 1 Design Notes

### 9.1 What SF1 actually was

- 1987, Capcom, Motorola 68000 @ 8MHz custom board (predates CPS-1 by one year — do not conflate).
- Two cabinet versions: pneumatic-pad deluxe (pressure-sensitive pad, broke constantly) and 6-button standard. The 6-button layout became the genre template.
- **Only Ryu and Ken playable** (Ken in P2 mode). Statistically identical — full mirror.
- 10 CPU opponents across 5 countries: Retsu/Geki (Japan), Joe/Mike (USA), Lee/Gen (China), Eagle/Birdie (England), Adon/Sagat (Thailand). Sagat is the final boss. Gen, Birdie, Adon, and Sagat became recurring characters.

### 9.2 What was foundational vs primitive

| Foundational (KEEP) | Primitive / abandoned (SKIP) |
|---|---|
| 6-button strength system (LP/MP/HP/LK/MK/HK) | Throws (added in SF2) |
| Motion specials (QCF, DP, QCB) | Combos (didn't exist; came in SF2 by accident via cancels) |
| Hold-back blocking | Super meter, EX moves, V-gauge (didn't exist) |
| Standing/crouching/jumping attacks | Negative-edge-only special trigger (SF1's notorious quirk) |
| High/low/overhead mixup | Damage scaling, juggle limits |
| Round timer + best-of-3 | Detailed UI/HUD (was bare-bones) |
| Anti-air via shoryuken | Throw-tech (came with SF3) |

### 9.3 Ryu/Ken move set (canonical)

**Normals:** 6 attacks × 3 stances (stand/crouch/jump) = 18 distinct normals per character. Behavior varies meaningfully by stance.

**Specials:**
| Move | Input | Notes |
|---|---|---|
| Hadoken | QCF + P | Three speeds by button strength |
| Shoryuken | DP + P | Anti-air; invul on startup |
| Tatsumaki Senpukyaku | QCB + K | Multi-hit; aerial version optional |

### 9.4 Match structure

- Best-of-3 rounds; first to 2 wins.
- 99-second round timer (configurable in original arcade; 99 is the genre default).
- KO at 0 health.
- Time-over → higher-health wins; equal-health → P1 wins (or CPU in 1P mode).
- "PERFECT!" on flawless round.
- Double KO → "DRAW GAME"; in 1P, player loses.

### 9.5 The 6-button vs 2-button question — keep 6

The 6-button layout *is* the grammar of the genre, not its complexity. It gives you:
- Range/recovery tradeoffs baked into button choice (light = fast/safe, heavy = slow/strong).
- Hit-confirm setups (`LP > LP > QCF+P` is a 2-in-1).
- Anti-air choice (HP-Shoryuken vs. close standing HP).
- Correct muscle memory for anyone coming from real fighters.

2-button (Divekick, Footsies) is a deliberate artistic minimalism, not a starting point for an SF clone.

### 9.6 Adjacent reference implementations to study

| Project | Why to read |
|---|---|
| **Footsies (HiFight)** — `hifight/Footsies` on GitHub (GPL3, C#/Unity) | The minimum viable fighter. ~3000 lines covering input buffer, normals, specials, blocking, win/lose. Rollback edition has a GGPO-C# implementation. Read for state machine + input handling architecture. |
| **Ikemen GO** — `ikemen-engine/Ikemen-GO` (Go + Lua) | Open-source MUGEN-compatible engine. Read its character state evaluator, hit-detection pipeline, and `HitDef` struct. |
| **GGPO** — `pond3r/ggpo` (C++) | Even if you skip rollback initially, the architecture (`ggpo_advance_frame`, `ggpo_save_game_state`, `ggpo_load_game_state`) tells you exactly what your sim layer must expose to add rollback later. |
| **MUGEN CNS docs** (Elecbyte) | The most influential fighting-game-as-data design ever shipped. Read end-to-end. |
| **SoFGV** — `PinkySmile/SoFGV` (C++/SFML) | Small-scope, structurally clear. |
| **OpenFight** — `nonameentername/openfight` (C++/SDL) | Quality is uneven but it's the only C++/SDL precedent. |

---

## 10. Milestone Roadmap

### M0 — Foundation gate (1–2 weeks)
- CMake + vcpkg + SDL3 + SDL3_image + miniaudio building cleanly on Windows/MSVC.
- Window opens, floor line drawn.
- Two colored rectangles; P1 (WASD) and P2 (arrow keys) can walk left/right and jump.
- Fixed 60Hz simulation loop with on-screen frame counter.
- Debug overlay infrastructure stub (toggleable empty layer).
- `neg_sim` static library has zero SDL/float dependencies.

### M1 — Playable fighter (6–10 weeks)
- Two characters with ~5 normals each + one fireball-style special.
- JSON-driven moves and per-frame hitboxes/hurtboxes/pushboxes.
- Hold-back blocking; high/low/overhead.
- Hitstop, hitstun, blockstun, pushback.
- Health bars, round timer, best-of-3, KO / time-over / draw.
- Aseprite → atlas pipeline functional with placeholder pixel art.
- Hitbox debug overlay (F1) showing all live boxes.
- Frame-step debugging (F5/F6/F7) and input recording/replay (F9/F10).

### M2 — Depth + polish
- Specials with full motion-input library (QCF, DP, QCB, charge).
- Throws + 7-frame throw tech.
- Cancel windows (normal → special).
- Audio: hit SFX pool, voice clips, BGM via miniaudio.
- AI training-mode dummy (records human inputs, plays them back).

### M3 — Replays and frame-step UI
- Save/load `SimulationState` (already POD).
- Replay UI: scrubber via state-ring-buffer.

### M4 — Optional: rollback netcode (GGPO integration)
- `sim::tick(state, inputs)` is already pure → drop-in GGPO. ~2–3 weeks of integration work, not a rewrite, because the determinism boundary was paid for in M0.

---

## 11. Engineering Discipline Checklist

- [ ] `/sim` library has zero SDL deps and zero floats (Q16.16 only).
- [ ] All sim randomness routes through `SimulationState::rng`.
- [ ] Every state change goes through `sim::tick`. No back-door mutation from rendering or input layers.
- [ ] `SimulationState::checksum()` logged each tick in dev builds.
- [ ] Replay record/playback works on day one (M0). Diverged checksums abort with a frame-N divergence report.
- [ ] Hitbox overlay (F1) draws all live boxes color-coded.
- [ ] Frame-step debug (F5/F6/F7) is wired before the first hitbox lands.
- [ ] Training-mode dummy is an AI that reads `SimulationState` and emits `InputFrame` — same interface as a human, so future bots and netcode just slot in.
- [ ] All boxes authored in canonical right-facing form; mirroring is one function applied uniformly at runtime.
- [ ] Frame timings stored as ticks, never milliseconds, after load.

---

## 12. Sources (primary references)

**SDL3:**
- SDL Wiki: [README-versions](https://wiki.libsdl.org/SDL3/README-versions), [README-migration](https://wiki.libsdl.org/SDL3/README-migration), [README-cmake](https://wiki.libsdl.org/SDL3/README-cmake), [BestKeyboardPractices](https://wiki.libsdl.org/SDL3/BestKeyboardPractices), [CategoryGamepad](https://wiki.libsdl.org/SDL3/CategoryGamepad), [SDL_RenderTexture](https://wiki.libsdl.org/SDL3/SDL_RenderTexture), [SDL_RenderTextureRotated](https://wiki.libsdl.org/SDL3/SDL_RenderTextureRotated), [SDL_RenderGeometry](https://wiki.libsdl.org/SDL3/SDL_RenderGeometry), [SDL_GetTicksNS](https://wiki.libsdl.org/SDL3/SDL_GetTicksNS), [SDL_DelayPrecise](https://wiki.libsdl.org/SDL3/SDL_DelayPrecise), [SDL_AppIterate](https://wiki.libsdl.org/SDL3/SDL_AppIterate).
- [Phoronix — SDL3 Render Batching](https://www.phoronix.com/news/SDL3-Batch-Rendering).
- [Ryan C. Gordon — SDL render batching (Patreon)](https://www.patreon.com/posts/project-sdl-21856507).

**Game loop / determinism:**
- [Glenn Fiedler — Fix Your Timestep!](https://gafferongames.com/post/fix_your_timestep/).
- [Glenn Fiedler — Floating Point Determinism](https://gafferongames.com/post/floating_point_determinism/).
- [Bruce Dawson — Floating-Point Determinism](https://randomascii.wordpress.com/2013/07/16/floating-point-determinism/).
- [MSVC /fp docs](https://learn.microsoft.com/en-us/cpp/build/reference/fp-specify-floating-point-behavior?view=msvc-170).
- [Tony Cannon — GGPO](https://www.ggpo.net/), [GGPO Developer Guide](https://github.com/pond3r/ggpo/blob/master/doc/DeveloperGuide.md).
- [Snapnet — Rollback architecture](https://www.snapnet.dev/blog/netcode-architectures-part-2-rollback/).

**Fighting-game architecture:**
- [Elecbyte — MUGEN CNS Format](https://www.elecbyte.com/mugendocs/cns.html).
- [Ikemen-GO Wiki](https://github.com/ikemen-engine/Ikemen-GO/wiki).
- [Arc System Works — Guilty Gear Xrd GDC 2015](https://www.gdcvault.com/play/1022031/GuiltyGearXrd-s-Art-Style-The).
- [Mike Zaimont Skullgirls interview — Skullheart](https://skullheart.com/threads/an-interview-with-skullgirls-mike-zaimont.5284/).
- [Critpoints — How to Code Fighting Game Motion Inputs](https://critpoints.net/2025/02/05/how-to-code-fighting-game-motion-inputs/).
- [Andrea Demetrio — I Wanna Make a Fighting Game (Medium series)](https://andrea-jens.medium.com/).
- [Dustloop Wiki — Hitboxes](https://www.dustloop.com/w/Hitboxes).
- [Street Fighter Wiki — Frame Data](https://streetfighter.fandom.com/wiki/Frame_Data).
- [Capcom — SF Seminar: Basics of Boxes](https://game.capcom.com/cfn/sfv/column/131422?lang=en).

**Sprite / animation pipeline:**
- [Aseprite — CLI](https://www.aseprite.org/docs/cli/), [Slices](https://www.aseprite.org/docs/slices/).
- [Aseprite JSON example (gist)](https://gist.github.com/dacap/db18e5747a4b6e208d3c).
- [TexturePacker](https://www.codeandweb.com/texturepacker).
- [kaiiboraka/Aseprite_Hitbox_Editor](https://github.com/kaiiboraka/Aseprite_Hitbox_Editor).

**Audio:**
- [miniaudio](https://github.com/mackron/miniaudio).

**SF1 history:**
- [Street Fighter (video game) — Wikipedia](https://en.wikipedia.org/wiki/Street_Fighter_(video_game)).
- [Devs Look Back At The Original Street Fighter — Nintendo Life](https://www.nintendolife.com/news/2016/09/devs_look_back_at_the_original_street_fighter_the_clunky_classic_which_birthed_a_genre).
- [Hadouken — Wikipedia](https://en.wikipedia.org/wiki/Hadouken).
- [Shoryuken — Street Fighter Wiki](https://streetfighter.fandom.com/wiki/Shoryuken).

**Reference projects:**
- [Footsies (HiFight)](https://github.com/hifight/Footsies).
- [Ikemen GO](https://github.com/ikemen-engine/Ikemen-GO).
- [GGPO](https://github.com/pond3r/ggpo).
- [SoFGV](https://github.com/PinkySmile/SoFGV).
- [OpenFight](https://github.com/nonameentername/openfight).
