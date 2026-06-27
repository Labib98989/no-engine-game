#include "render/sprites.h"

#include <SDL3_image/SDL_image.h>
#include <fstream>
#include <nlohmann/json.hpp>

namespace neg {
namespace render {

using nlohmann::json;

static bool read_json(const std::string& path, json& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        f >> out;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// Scan one grid cell's pixels for the opaque bounding box. anchor defaults:
// horizontal = center of the trimmed body, vertical = bottom-most opaque pixel
// (the ground). Coordinates returned are src-local (relative to the trimmed
// rect's top-left).
static SpriteFrame derive_frame(const uint8_t* px, int pitch, int sheet_w, int sheet_h,
                                int cx, int cy, int cw, int ch, int alpha_thresh) {
    int x1 = (cx + cw < sheet_w) ? cx + cw : sheet_w;
    int y1 = (cy + ch < sheet_h) ? cy + ch : sheet_h;
    int minx = x1, miny = y1, maxx = cx - 1, maxy = cy - 1;
    for (int y = cy; y < y1; ++y) {
        const uint8_t* row = px + (size_t)y * pitch;
        for (int x = cx; x < x1; ++x) {
            if (row[x * 4 + 3] >= alpha_thresh) {
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
        }
    }
    SpriteFrame fr;
    if (maxx < minx || maxy < miny) {
        // Empty cell — fall back to the whole cell, bottom-centre anchor.
        fr.src = SDL_FRect{(float)cx, (float)cy, (float)cw, (float)ch};
        fr.anchor_x = cw * 0.5f;
        fr.anchor_y = (float)ch;
        return fr;
    }
    int w = maxx - minx + 1;
    int h = maxy - miny + 1;
    fr.src = SDL_FRect{(float)minx, (float)miny, (float)w, (float)h};
    fr.anchor_x = w * 0.5f;  // horizontal: centre of the trimmed body
    fr.anchor_y = (float)h;  // vertical: bottom-most opaque pixel == ground
    return fr;
}

SpriteSheet load_sprite_sheet(SDL_Renderer* ren, const std::string& base_dir,
                              const std::string& json_rel) {
    SpriteSheet sheet;
    json j;
    if (!read_json(base_dir + json_rel, j) || !j.contains("sprite")) return sheet;
    const json& sp = j["sprite"];
    if (!sp.contains("sheet")) return sheet;

    std::string png = base_dir + "assets/" + sp["sheet"].get<std::string>();
    SDL_Surface* raw = IMG_Load(png.c_str());
    if (!raw) {
        SDL_Log("sprite: %s not loaded (%s) — falling back to rectangle", png.c_str(),
                SDL_GetError());
        return sheet;
    }
    SDL_Surface* surf = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(raw);
    if (!surf) return sheet;

    int frames = sp.value("frames", 1);
    if (frames < 1) frames = 1;
    int cell_w = sp.value("cell_w", surf->w / frames);
    int cell_h = sp.value("cell_h", surf->h);
    if (cell_w < 1) cell_w = 1;
    if (cell_h < 1) cell_h = 1;
    int columns = sp.value("columns", surf->w / cell_w);
    if (columns < 1) columns = 1;
    int alpha_thresh = sp.value("alpha_threshold", 16);
    sheet.scale = sp.value("scale", 1.0f);
    sheet.source_faces_left = sp.value("source_faces_left", false);

    const uint8_t* px = (const uint8_t*)surf->pixels;
    for (int i = 0; i < frames; ++i) {
        int col = i % columns, row = i / columns;
        int cx = col * cell_w, cy = row * cell_h;
        SpriteFrame fr =
            derive_frame(px, surf->pitch, surf->w, surf->h, cx, cy, cell_w, cell_h, alpha_thresh);
        // Per-frame anchor override, authored in CELL-local pixels.
        if (sp.contains("overrides")) {
            std::string key = std::to_string(i);
            if (sp["overrides"].contains(key)) {
                const json& ov = sp["overrides"][key];
                if (ov.contains("anchor_x"))
                    fr.anchor_x = ov["anchor_x"].get<float>() - (fr.src.x - (float)cx);
                if (ov.contains("anchor_y"))
                    fr.anchor_y = ov["anchor_y"].get<float>() - (fr.src.y - (float)cy);
            }
        }
        sheet.frames.push_back(fr);
    }

    if (sp.contains("poses")) {
        const json& po = sp["poses"];
        static const char* names[7] = {"default", "idle", "A", "B", "C", "D", "hit"};
        for (int k = 0; k < 7; ++k)
            if (po.contains(names[k])) {
                int idx = po[names[k]].get<int>();
                if (idx >= 0 && idx < frames) sheet.pose[k] = idx;
            }
    }

    sheet.tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_DestroySurface(surf);
    if (!sheet.tex) return sheet;
    SDL_SetTextureScaleMode(sheet.tex, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(sheet.tex, SDL_BLENDMODE_BLEND);
    sheet.valid = true;
    return sheet;
}

void unload_sprite_sheet(SpriteSheet& sheet) {
    if (sheet.tex) SDL_DestroyTexture(sheet.tex);
    sheet.tex = nullptr;
    sheet.frames.clear();
    sheet.valid = false;
}

void draw_sprite(SDL_Renderer* ren, const SpriteSheet& sheet, int frame, float screen_x,
                 float screen_y, bool facing_right) {
    if (!sheet.valid || frame < 0 || frame >= (int)sheet.frames.size()) return;
    const SpriteFrame& fr = sheet.frames[frame];
    float scl = sheet.scale;
    bool flip = (facing_right == sheet.source_faces_left); // mirror when facing != source

    SDL_FRect dst;
    dst.y = screen_y - fr.anchor_y * scl;
    dst.w = fr.src.w * scl;
    dst.h = fr.src.h * scl;
    dst.x = flip ? screen_x - (fr.src.w - fr.anchor_x) * scl : screen_x - fr.anchor_x * scl;

    if (flip)
        SDL_RenderTextureRotated(ren, sheet.tex, &fr.src, &dst, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
    else
        SDL_RenderTexture(ren, sheet.tex, &fr.src, &dst);
}

} // namespace render
} // namespace neg
