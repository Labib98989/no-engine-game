// Tiny embedded 5x7 pixel font drawn as rectangles — placeholder text
// rendering until the real art pipeline lands in M1. No file assets.
#pragma once
#include <SDL3/SDL.h>

namespace neg {
namespace render {

// Draws ASCII text (lowercase folded to uppercase) at pixel scale `scale`.
// Uses the renderer's current draw color. Glyph cell is 6*scale wide.
void draw_text(SDL_Renderer* ren, float x, float y, float scale, const char* text);

float text_width(float scale, const char* text);

} // namespace render
} // namespace neg
