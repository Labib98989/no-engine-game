// Sprite atlas for the fighters. Render-side only (reads no sim, mutates no
// sim). Per-frame trimmed rect + ground anchor are DERIVED FROM THE PIXELS at
// load: the bottom-most opaque pixel is the ground, the trimmed bounding box is
// the size (the disclaimers from the design discussion), with optional
// per-frame anchor overrides for poses where the lowest pixel isn't the real
// contact. Missing JSON block or missing PNG => invalid sheet => the renderer
// falls back to rectangles, so the game always runs.
#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include "sim/types.h"

namespace neg {
namespace render {

struct SpriteFrame {
    SDL_FRect src{};    // trimmed rect on the atlas, pixels
    float anchor_x = 0; // src-local px; maps to fighter pos_x (horizontal anchor)
    float anchor_y = 0; // src-local px; maps to fighter pos_y (ground anchor)
};

// Logical pose slots, in the order the standing sheet was authored:
// default, idle, then one attack per input A/B/C/D, then getting-hit.
enum Pose { POSE_DEFAULT = 0, POSE_IDLE, POSE_A, POSE_B, POSE_C, POSE_D, POSE_HIT };

struct SpriteSheet {
    SDL_Texture* tex = nullptr;
    std::vector<SpriteFrame> frames;
    float scale = 1.0f;
    bool source_faces_left = false;       // set if the art is authored facing left
    int pose[7] = {0, 1, 2, 3, 4, 5, 6};  // Pose slot -> frame index
    bool valid = false;
};

// Loads <base_dir><json_rel>'s "sprite" block and its PNG, deriving each
// frame's trimmed rect + anchor from the alpha channel. Resolves the sheet path
// relative to <base_dir>assets/. Returns an invalid sheet (valid == false) when
// there is no sprite block or the PNG can't be loaded.
SpriteSheet load_sprite_sheet(SDL_Renderer* ren, const std::string& base_dir,
                              const std::string& json_rel);

void unload_sprite_sheet(SpriteSheet& sheet);

// Draws frame so its anchor lands exactly at (screen_x, screen_y), mirrored
// when the fighter's facing differs from the authored facing.
void draw_sprite(SDL_Renderer* ren, const SpriteSheet& sheet, int frame,
                 float screen_x, float screen_y, bool facing_right);

} // namespace render
} // namespace neg
