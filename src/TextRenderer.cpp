#include "TextRenderer.hpp"
#include "AssetManager.hpp"
#include "EmojiRenderer.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <random>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

static std::vector<unsigned char> g_font_data;
static stbtt_fontinfo g_font_info;
static bool g_initialized = false;

static std::vector<unsigned char> g_emoji_data;
static stbtt_fontinfo g_emoji_info;
static bool g_emoji_initialized = false;

static std::vector<unsigned char> g_alt_data;
static stbtt_fontinfo g_alt_info;
static bool g_alt_initialized = false;

bool TextRenderer::init(const std::string& font_path) {
    std::ifstream f(font_path, std::ios::binary);
    if (!f.good()) {
        std::cerr << "[Renderer] Khong tim thay font: " << font_path << "\n";
        return false;
    }
    g_font_data.assign((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    if (!stbtt_InitFont(&g_font_info, g_font_data.data(), 0)) {
        std::cerr << "[Renderer] InitFont fail\n";
        return false;
    }
    g_initialized = true;
    std::cout << "[Renderer] Font OK: " << font_path << "\n";
    // Auto-load alt font if present
    init_alt_font("assets/SVN-Transformer.ttf");
    return true;
}

bool TextRenderer::init_emoji(const std::string& emoji_path) {
    std::ifstream f(emoji_path, std::ios::binary);
    if (!f.good()) {
        std::cout << "[Renderer] Khong co emoji font: " << emoji_path << "\n";
        return false;
    }
    g_emoji_data.assign((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    if (!stbtt_InitFont(&g_emoji_info, g_emoji_data.data(), 0)) {
        std::cerr << "[Renderer] InitEmojiFont fail\n";
        return false;
    }
    g_emoji_initialized = true;
    std::cout << "[Renderer] Emoji font OK: " << emoji_path << "\n";
    return true;
}

bool TextRenderer::init_alt_font(const std::string& alt_font_path) {
    if (g_alt_initialized) return true;
    std::ifstream f(alt_font_path, std::ios::binary);
    if (!f.good()) {
        std::ifstream f2("assets/repo/fonts/SVN-Transformer.ttf", std::ios::binary);
        if (!f2.good()) return false;
        g_alt_data.assign((std::istreambuf_iterator<char>(f2)), std::istreambuf_iterator<char>());
    } else {
        g_alt_data.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
    if (!stbtt_InitFont(&g_alt_info, g_alt_data.data(), 0)) return false;
    g_alt_initialized = true;
    return true;
}

static inline void blend_pixel(std::vector<uint8_t>& img, int W, int H,
                               int x, int y,
                               unsigned char r, unsigned char g, unsigned char b,
                               unsigned char a) {
    if (x < 0 || x >= W || y < 0 || y >= H || a == 0) return;
    int idx = (y * W + x) * 4;
    if (a == 255) {
        img[idx + 0] = r;
        img[idx + 1] = g;
        img[idx + 2] = b;
        img[idx + 3] = 255;
        return;
    }
    float af = a / 255.0f;
    float inv = 1.0f - af;
    img[idx + 0] = (unsigned char)(r * af + img[idx + 0] * inv + 0.5f);
    img[idx + 1] = (unsigned char)(g * af + img[idx + 1] * inv + 0.5f);
    img[idx + 2] = (unsigned char)(b * af + img[idx + 2] * inv + 0.5f);
    img[idx + 3] = 255;
}

static inline void sample_bilinear(const ImageRGBA* img, float u, float v,
                                   unsigned char& out_r, unsigned char& out_g,
                                   unsigned char& out_b, unsigned char& out_a) {
    float fx = u * (img->width - 1);
    float fy = v * (img->height - 1);
    if (fx < 0.0f) fx = 0.0f;
    if (fy < 0.0f) fy = 0.0f;
    if (fx > img->width - 1) fx = (float)(img->width - 1);
    if (fy > img->height - 1) fy = (float)(img->height - 1);

    int x0 = (int)fx;
    int y0 = (int)fy;
    int x1 = std::min(x0 + 1, img->width - 1);
    int y1 = std::min(y0 + 1, img->height - 1);
    float tx = fx - x0;
    float ty = fy - y0;

    const uint8_t* p00 = &img->data[(y0 * img->width + x0) * 4];
    const uint8_t* p10 = &img->data[(y0 * img->width + x1) * 4];
    const uint8_t* p01 = &img->data[(y1 * img->width + x0) * 4];
    const uint8_t* p11 = &img->data[(y1 * img->width + x1) * 4];

    float w00 = (1.0f - tx) * (1.0f - ty);
    float w10 = tx * (1.0f - ty);
    float w01 = (1.0f - tx) * ty;
    float w11 = tx * ty;

    float a00 = p00[3] / 255.0f, a10 = p10[3] / 255.0f;
    float a01 = p01[3] / 255.0f, a11 = p11[3] / 255.0f;
    float alpha = a00 * w00 + a10 * w10 + a01 * w01 + a11 * w11;
    if (alpha < 0.004f) {
        out_r = out_g = out_b = out_a = 0;
        return;
    }
    // Premultiplied RGB interpolation prevents dark fringes on transparent edges
    float pr = p00[0]*a00*w00 + p10[0]*a10*w10 + p01[0]*a01*w01 + p11[0]*a11*w11;
    float pg = p00[1]*a00*w00 + p10[1]*a10*w10 + p01[1]*a01*w01 + p11[1]*a11*w11;
    float pb = p00[2]*a00*w00 + p10[2]*a10*w10 + p01[2]*a01*w01 + p11[2]*a11*w11;

    out_r = (unsigned char)std::clamp(pr / alpha + 0.5f, 0.0f, 255.0f);
    out_g = (unsigned char)std::clamp(pg / alpha + 0.5f, 0.0f, 255.0f);
    out_b = (unsigned char)std::clamp(pb / alpha + 0.5f, 0.0f, 255.0f);
    out_a = (unsigned char)std::clamp(alpha * 255.0f + 0.5f, 0.0f, 255.0f);
}

static inline bool decode_utf8(const std::string& text, int& i, uint32_t& cp) {
    if (i >= (int)text.size()) return false;
    unsigned char c = (unsigned char)text[i];
    if (c < 0x80) {
        cp = c;
        i += 1;
        return true;
    } else if ((c & 0xE0) == 0xC0) {
        if (i + 1 >= (int)text.size()) { i++; return false; }
        cp = ((c & 0x1F) << 6) | ((unsigned char)text[i + 1] & 0x3F);
        i += 2;
        return true;
    } else if ((c & 0xF0) == 0xE0) {
        if (i + 2 >= (int)text.size()) { i++; return false; }
        cp = ((c & 0x0F) << 12) | (((unsigned char)text[i + 1] & 0x3F) << 6) | ((unsigned char)text[i + 2] & 0x3F);
        i += 3;
        return true;
    } else if ((c & 0xF8) == 0xF0) {
        if (i + 3 >= (int)text.size()) { i++; return false; }
        cp = ((c & 0x07) << 18) | (((unsigned char)text[i + 1] & 0x3F) << 12)
           | (((unsigned char)text[i + 2] & 0x3F) << 6) | ((unsigned char)text[i + 3] & 0x3F);
        i += 4;
        return true;
    }
    i++;
    return false;
}

static inline bool is_ignored_joiner(uint32_t cp) {
    return cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D;
}

static inline bool should_draw_color_emoji(const stbtt_fontinfo* main_font, uint32_t cp) {
    if (cp < 0x80) return false;
    bool is_emoji_block = (cp >= 0x1F000) || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2300 && cp <= 0x23FF) || cp == 0x2B50;
    if (is_emoji_block && EmojiRenderer::has_codepoint(cp)) return true;
    if (stbtt_FindGlyphIndex(main_font, (int)cp) == 0 && EmojiRenderer::has_codepoint(cp)) return true;
    return false;
}

static int glyph_advance_px(const stbtt_fontinfo* font, float scale, int size, uint32_t cp) {
    if (is_ignored_joiner(cp)) return 0;
    if (should_draw_color_emoji(font, cp)) {
        return (int)(size * 1.05f);
    }
    int adv = 0, lsb = 0;
    int gi = stbtt_FindGlyphIndex(font, (int)cp);
    if (gi == 0 && g_emoji_initialized && stbtt_FindGlyphIndex(&g_emoji_info, (int)cp) > 0) {
        stbtt_GetCodepointHMetrics(&g_emoji_info, (int)cp, &adv, &lsb);
        return (int)(adv * scale * 1.02f);
    }
    if (gi == 0) return 0; // avoid stray box advance
    stbtt_GetCodepointHMetrics(font, (int)cp, &adv, &lsb);
    return (int)(adv * scale);
}

static void draw_char(std::vector<unsigned char>& img, int W, int H,
                      int x, int y, int size,
                      unsigned char r, unsigned char g, unsigned char b,
                      const stbtt_fontinfo* main_font, float main_scale,
                      uint32_t codepoint, bool allow_color_emoji = true) {
    if (is_ignored_joiner(codepoint)) return;

    if (should_draw_color_emoji(main_font, codepoint)) {
        if (allow_color_emoji) {
            int esz = std::max(8, (int)(size * 0.92f));
            int ey = y - (int)(size * 0.78f);
            EmojiRenderer::draw_codepoint(img, W, H, x, ey, esz, codepoint);
        }
        return;
    }

    const stbtt_fontinfo* use_font = main_font;
    float use_scale = main_scale;

    int glyph_idx = stbtt_FindGlyphIndex(main_font, (int)codepoint);
    if (glyph_idx == 0 && g_emoji_initialized) {
        int emoji_idx = stbtt_FindGlyphIndex(&g_emoji_info, (int)codepoint);
        if (emoji_idx > 0) {
            use_font = &g_emoji_info;
            use_scale = main_scale * 1.02f;
            glyph_idx = emoji_idx;
        }
    }
    if (glyph_idx == 0) return;

    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(use_font, (int)codepoint, use_scale, use_scale,
                                &x0, &y0, &x1, &y1);
    int w = x1 - x0;
    int h = y1 - y0;
    if (w <= 0 || h <= 0) return;

    std::vector<unsigned char> bitmap(w * h);
    stbtt_MakeCodepointBitmap(use_font, bitmap.data(), w, h, w, use_scale, use_scale, (int)codepoint);

    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            unsigned char alpha = bitmap[yy * w + xx];
            if (alpha == 0) continue;
            blend_pixel(img, W, H, x + x0 + xx, y + y0 + yy, r, g, b, alpha);
        }
    }
}

static void draw_text_utf8_internal(std::vector<unsigned char>& img, int W, int H,
                                    int x, int y, int size,
                                    unsigned char r, unsigned char g, unsigned char b,
                                    const stbtt_fontinfo* font, float scale,
                                    const std::string& text, bool allow_color_emoji = true) {
    int i = 0;
    int cursor_x = x;
    while (i < (int)text.size()) {
        uint32_t cp = 0;
        if (!decode_utf8(text, i, cp)) continue;
        draw_char(img, W, H, cursor_x, y, size, r, g, b, font, scale, cp, allow_color_emoji);
        cursor_x += glyph_advance_px(font, scale, size, cp);
    }
}

void TextRenderer::draw_image_rotated(std::vector<uint8_t>& canvas, int W, int H,
                                        const ImageRGBA* img,
                                        int cx, int cy, int size, float angle_rad) {
    if (!img || !img->valid() || size <= 0) return;
    float ca = std::cos(angle_rad);
    float sa = std::sin(angle_rad);
    int bound_half = (int)(size * 0.75f) + 2;
    float half = size * 0.5f;

    for (int dy = -bound_half; dy <= bound_half; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= H) continue;
        for (int dx = -bound_half; dx <= bound_half; dx++) {
            int px = cx + dx;
            if (px < 0 || px >= W) continue;

            float sx_f = (float)dx * ca + (float)dy * sa;
            float sy_f = -(float)dx * sa + (float)dy * ca;

            if (sx_f < -half || sx_f >= half || sy_f < -half || sy_f >= half) continue;

            float u = (sx_f + half) / (float)size;
            float v = (sy_f + half) / (float)size;
            unsigned char r, g, b, a;
            sample_bilinear(img, u, v, r, g, b, a);
            if (a > 0) blend_pixel(canvas, W, H, px, py, r, g, b, a);
        }
    }
}

bool TextRenderer::render(const std::string& title,
                          const std::vector<std::string>& lines,
                          const std::string& output_path,
                          const TextStyle& style) {
    if (!g_initialized) return false;
    float title_scale = stbtt_ScaleForPixelHeight(&g_font_info, (float)style.title_size);
    float text_scale  = stbtt_ScaleForPixelHeight(&g_font_info, (float)style.text_size);

    int W = style.width;
    int title_h = style.title_size + 26;
    int content_h = (int)lines.size() * style.line_height;
    int H = style.padding * 2 + title_h + content_h + 36;

    std::vector<unsigned char> img(W * H * 4, 0);
    fill_linear_gradient(img, W, H, 0, H,
                         style.bg_r, style.bg_g, style.bg_b,
                         style.bg2_r, style.bg2_g, style.bg2_b);

    int card_x = style.padding - 10;
    int card_y = style.padding - 10;
    int card_w = W - (card_x * 2);
    int card_h = H - (card_y * 2);

    draw_rounded_rect_shadow(img, W, H, card_x, card_y, card_w, card_h, style.card_radius, 12, 90);
    draw_rounded_rect_filled(img, W, H, card_x, card_y, card_w, card_h,
                             style.card_radius, style.card_r, style.card_g, style.card_b, 235);
    draw_rounded_rect_outline(img, W, H, card_x, card_y, card_w, card_h,
                              style.card_radius, 2, style.accent_r, style.accent_g, style.accent_b, 180);

    int title_y = style.padding + style.title_size;
    draw_text_utf8_internal(img, W, H, style.padding + 10, title_y, style.title_size,
                            style.title_r, style.title_g, style.title_b,
                            &g_font_info, title_scale, title);

    int line_y = style.padding + title_h;
    draw_line(img, W, H, style.padding + 8, line_y, W - style.padding - 8, line_y,
              style.accent_r, style.accent_g, style.accent_b, 2, 180);

    int y = line_y + style.text_size + 14;
    for (const auto& line : lines) {
        draw_text_utf8_internal(img, W, H, style.padding + 10, y, style.text_size,
                                style.fg_r, style.fg_g, style.fg_b,
                                &g_font_info, text_scale, line);
        y += style.line_height;
    }
    return stbi_write_png(output_path.c_str(), W, H, 4, img.data(), W * 4) != 0;
}

void TextRenderer::init_canvas(std::vector<uint8_t>& canvas, int W, int H,
                               unsigned char r, unsigned char g, unsigned char b) {
    canvas.assign(W * H * 4, 255);
    for (int i = 0; i < W * H; i++) {
        canvas[i * 4 + 0] = r;
        canvas[i * 4 + 1] = g;
        canvas[i * 4 + 2] = b;
    }
}

void TextRenderer::fill_linear_gradient(std::vector<uint8_t>& canvas, int W, int H,
                                        int y_start, int y_end,
                                        unsigned char r1, unsigned char g1, unsigned char b1,
                                        unsigned char r2, unsigned char g2, unsigned char b2) {
    if (y_end <= y_start) y_end = y_start + 1;
    for (int y = y_start; y < y_end; y++) {
        if (y < 0 || y >= H) continue;
        float t = (float)(y - y_start) / (float)(y_end - y_start);
        unsigned char r = (unsigned char)(r1 + (r2 - r1) * t);
        unsigned char g = (unsigned char)(g1 + (g2 - g1) * t);
        unsigned char b = (unsigned char)(b1 + (b2 - b1) * t);
        for (int x = 0; x < W; x++) {
            int idx = (y * W + x) * 4;
            canvas[idx + 0] = r;
            canvas[idx + 1] = g;
            canvas[idx + 2] = b;
            canvas[idx + 3] = 255;
        }
    }
}

void TextRenderer::draw_rect_filled(std::vector<uint8_t>& canvas, int W, int H,
                                    int x, int y, int rw, int rh,
                                    unsigned char r, unsigned char g, unsigned char b,
                                    unsigned char a) {
    int y0 = std::max(0, y), y1 = std::min(H, y + rh);
    int x0 = std::max(0, x), x1 = std::min(W, x + rw);
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++)
            blend_pixel(canvas, W, H, px, py, r, g, b, a);
}

// Signed distance to rounded rectangle (negative = inside)
static inline float sd_rounded_rect(float px, float py, float x, float y, float w, float h, float r) {
    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;
    float hx = w * 0.5f - r;
    float hy = h * 0.5f - r;
    float dx = std::fabs(px - cx) - hx;
    float dy = std::fabs(py - cy) - hy;
    float ox = std::max(dx, 0.0f);
    float oy = std::max(dy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(dx, dy), 0.0f) - r;
}

void TextRenderer::draw_rounded_rect_filled(std::vector<uint8_t>& canvas, int W, int H,
                                            int x, int y, int rw, int rh, int radius,
                                            unsigned char r, unsigned char g, unsigned char b,
                                            unsigned char a) {
    if (rw <= 0 || rh <= 0) return;
    float rad = (float)std::min(radius, std::min(rw, rh) / 2);
    int y0 = std::max(0, y - 1), y1 = std::min(H, y + rh + 1);
    int x0 = std::max(0, x - 1), x1 = std::min(W, x + rw + 1);
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float d = sd_rounded_rect((float)px + 0.5f, (float)py + 0.5f,
                                      (float)x, (float)y, (float)rw, (float)rh, rad);
            if (d >= 0.5f) continue;
            float cov = std::clamp(0.5f - d, 0.0f, 1.0f);
            blend_pixel(canvas, W, H, px, py, r, g, b, (unsigned char)(a * cov));
        }
    }
}

void TextRenderer::draw_rounded_rect_outline(std::vector<uint8_t>& canvas, int W, int H,
                                             int x, int y, int rw, int rh, int radius,
                                             int thickness,
                                             unsigned char r, unsigned char g, unsigned char b,
                                             unsigned char a) {
    if (rw <= 0 || rh <= 0 || thickness <= 0) return;
    float rad = (float)std::min(radius, std::min(rw, rh) / 2);
    float half_t = thickness * 0.5f;
    int y0 = std::max(0, y - thickness - 1), y1 = std::min(H, y + rh + thickness + 1);
    int x0 = std::max(0, x - thickness - 1), x1 = std::min(W, x + rw + thickness + 1);
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float d = sd_rounded_rect((float)px + 0.5f, (float)py + 0.5f,
                                      (float)x, (float)y, (float)rw, (float)rh, rad);
            float dist_edge = std::fabs(d) - half_t;
            if (dist_edge >= 0.7f) continue;
            float cov = std::clamp(0.7f - dist_edge, 0.0f, 1.0f);
            blend_pixel(canvas, W, H, px, py, r, g, b, (unsigned char)(a * cov));
        }
    }
}

void TextRenderer::draw_rounded_rect_shadow(std::vector<uint8_t>& canvas, int W, int H,
                                            int x, int y, int rw, int rh, int radius,
                                            int blur_px, unsigned char max_alpha) {
    if (rw <= 0 || rh <= 0 || blur_px <= 0) return;
    float rad = (float)std::min(radius, std::min(rw, rh) / 2);
    int ox = 0, oy = std::max(2, blur_px / 3);
    int y0 = std::max(0, y + oy - blur_px);
    int y1 = std::min(H, y + oy + rh + blur_px);
    int x0 = std::max(0, x + ox - blur_px);
    int x1 = std::min(W, x + ox + rw + blur_px);
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float d = sd_rounded_rect((float)px + 0.5f, (float)py + 0.5f,
                                      (float)(x + ox), (float)(y + oy), (float)rw, (float)rh, rad);
            if (d > blur_px) continue;
            float t = (d <= 0.0f) ? 1.0f : 1.0f - (d / (float)blur_px);
            unsigned char a = (unsigned char)(max_alpha * t * t);
            blend_pixel(canvas, W, H, px, py, 0, 0, 0, a);
        }
    }
}

void TextRenderer::draw_circle_filled(std::vector<uint8_t>& canvas, int W, int H,
                                      int cx, int cy, int radius,
                                      unsigned char r, unsigned char g, unsigned char b,
                                      unsigned char a) {
    if (radius <= 0) return;
    int y0 = std::max(0, cy - radius - 1), y1 = std::min(H - 1, cy + radius + 1);
    int x0 = std::max(0, cx - radius - 1), x1 = std::min(W - 1, cx + radius + 1);
    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            float dx = (float)(px - cx);
            float dy = (float)(py - cy);
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist >= radius + 0.7f) continue;
            float cov = std::clamp((float)radius + 0.7f - dist, 0.0f, 1.0f);
            blend_pixel(canvas, W, H, px, py, r, g, b, (unsigned char)(a * cov));
        }
    }
}

void TextRenderer::draw_line(std::vector<uint8_t>& canvas, int W, int H,
                             int x1, int y1, int x2, int y2,
                             unsigned char r, unsigned char g, unsigned char b,
                             int thickness, unsigned char a) {
    int dx = std::abs(x2 - x1), sx = (x1 < x2) ? 1 : -1;
    int dy = std::abs(y2 - y1), sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    int half = thickness / 2;
    while (true) {
        for (int ty = -half; ty <= half; ty++)
            for (int tx = -half; tx <= half; tx++)
                blend_pixel(canvas, W, H, x1 + tx, y1 + ty, r, g, b, a);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx)  { err += dx; y1 += sy; }
    }
}

void TextRenderer::blur_region(std::vector<uint8_t>& canvas, int W, int H, int strength) {
    if (strength <= 0 || W <= 0 || H <= 0) return;
    std::vector<uint8_t> tmp = canvas;
    int r = std::min(strength, 12);
    // Separable fast box blur (horizontal then vertical)
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int sr = 0, sg = 0, sb = 0, cnt = 0;
            int x0 = std::max(0, x - r), x1 = std::min(W - 1, x + r);
            for (int xx = x0; xx <= x1; xx++) {
                int idx = (y * W + xx) * 4;
                sr += canvas[idx]; sg += canvas[idx + 1]; sb += canvas[idx + 2];
                cnt++;
            }
            int idx = (y * W + x) * 4;
            tmp[idx] = sr / cnt; tmp[idx + 1] = sg / cnt; tmp[idx + 2] = sb / cnt;
        }
    }
    for (int y = 0; y < H; y++) {
        int y0 = std::max(0, y - r), y1 = std::min(H - 1, y + r);
        for (int x = 0; x < W; x++) {
            int sr = 0, sg = 0, sb = 0, cnt = 0;
            for (int yy = y0; yy <= y1; yy++) {
                int idx = (yy * W + x) * 4;
                sr += tmp[idx]; sg += tmp[idx + 1]; sb += tmp[idx + 2];
                cnt++;
            }
            int idx = (y * W + x) * 4;
            canvas[idx] = sr / cnt; canvas[idx + 1] = sg / cnt; canvas[idx + 2] = sb / cnt;
        }
    }
}

void TextRenderer::darken_region(std::vector<uint8_t>& canvas, int W, int H,
                                 int x, int y, int rw, int rh, unsigned char alpha) {
    draw_rect_filled(canvas, W, H, x, y, rw, rh, 0, 0, 0, alpha);
}

void TextRenderer::draw_text_on_canvas(std::vector<uint8_t>& canvas, int W, int H,
                                       int x, int y, int size,
                                       unsigned char r, unsigned char g, unsigned char b,
                                       const std::string& text) {
    if (!g_initialized) return;
    float scale = stbtt_ScaleForPixelHeight(&g_font_info, (float)size);
    draw_text_utf8_internal(canvas, W, H, x, y, size, r, g, b, &g_font_info, scale, text, true);
}

void TextRenderer::draw_text_on_canvas_baseline(std::vector<uint8_t>& canvas, int W, int H,
                                                int x, int y_baseline, int size,
                                                unsigned char r, unsigned char g, unsigned char b,
                                                const std::string& text) {
    draw_text_on_canvas(canvas, W, H, x, y_baseline, size, r, g, b, text);
}

void TextRenderer::draw_text_shadow(std::vector<uint8_t>& canvas, int W, int H,
                                    int x, int y, int size,
                                    unsigned char r, unsigned char g, unsigned char b,
                                    const std::string& text) {
    if (!g_initialized) return;
    float scale = stbtt_ScaleForPixelHeight(&g_font_info, (float)size);
    draw_text_utf8_internal(canvas, W, H, x + 2, y + 2, size, 0, 0, 0, &g_font_info, scale, text, false);
    draw_text_utf8_internal(canvas, W, H, x, y, size, r, g, b, &g_font_info, scale, text, true);
}

void TextRenderer::draw_text_outlined(std::vector<uint8_t>& canvas, int W, int H,
                                      int x, int y, int size,
                                      unsigned char r, unsigned char g, unsigned char b,
                                      unsigned char or_, unsigned char og, unsigned char ob,
                                      int outline, const std::string& text) {
    if (!g_initialized) return;
    float scale = stbtt_ScaleForPixelHeight(&g_font_info, (float)size);
    for (int dy = -outline; dy <= outline; dy++) {
        for (int dx = -outline; dx <= outline; dx++) {
            if (dx == 0 && dy == 0) continue;
            if (dx * dx + dy * dy > outline * outline + 1) continue;
            draw_text_utf8_internal(canvas, W, H, x + dx, y + dy, size, or_, og, ob,
                                    &g_font_info, scale, text, false);
        }
    }
    draw_text_utf8_internal(canvas, W, H, x, y, size, r, g, b, &g_font_info, scale, text, true);
}

int TextRenderer::measure_alt_text_width(int size, const std::string& text) {
    const stbtt_fontinfo* f = g_alt_initialized ? &g_alt_info : (g_initialized ? &g_font_info : nullptr);
    if (!f) return 0;
    float scale = stbtt_ScaleForPixelHeight(f, (float)size);
    int i = 0, total = 0;
    while (i < (int)text.size()) {
        uint32_t cp = 0;
        if (!decode_utf8(text, i, cp)) continue;
        total += glyph_advance_px(f, scale, size, cp);
    }
    return total;
}

void TextRenderer::draw_text_alt_outlined(std::vector<uint8_t>& canvas, int W, int H,
                                          int x, int y, int size,
                                          unsigned char r1, unsigned char g1, unsigned char b1,
                                          unsigned char r2, unsigned char g2, unsigned char b2,
                                          unsigned char or_, unsigned char og, unsigned char ob,
                                          int outline, const std::string& text) {
    const stbtt_fontinfo* f = g_alt_initialized ? &g_alt_info : (g_initialized ? &g_font_info : nullptr);
    if (!f) return;
    float scale = stbtt_ScaleForPixelHeight(f, (float)size);
    for (int dy = -outline; dy <= outline; dy++) {
        for (int dx = -outline; dx <= outline; dx++) {
            if (dx == 0 && dy == 0) continue;
            if (dx * dx + dy * dy > outline * outline + 1) continue;
            draw_text_utf8_internal(canvas, W, H, x + dx, y + dy, size, or_, og, ob, f, scale, text, false);
        }
    }
    int total_w = std::max(1, measure_alt_text_width(size, text));
    int i = 0, cursor = 0;
    while (i < (int)text.size()) {
        uint32_t cp = 0;
        if (!decode_utf8(text, i, cp)) continue;
        float t = (float)cursor / (float)total_w;
        unsigned char cr = (unsigned char)(r1 + (r2 - r1) * t);
        unsigned char cg = (unsigned char)(g1 + (g2 - g1) * t);
        unsigned char cb = (unsigned char)(b1 + (b2 - b1) * t);
        draw_char(canvas, W, H, x + cursor, y, size, cr, cg, cb, f, scale, cp, true);
        cursor += glyph_advance_px(f, scale, size, cp);
    }
}

int TextRenderer::measure_text_width(int size, const std::string& text) {
    if (!g_initialized) return 0;
    float scale = stbtt_ScaleForPixelHeight(&g_font_info, (float)size);
    int i = 0;
    int total = 0;
    while (i < (int)text.size()) {
        uint32_t cp = 0;
        if (!decode_utf8(text, i, cp)) continue;
        total += glyph_advance_px(&g_font_info, scale, size, cp);
    }
    return total;
}

std::vector<std::string> TextRenderer::wrap_text(int size, const std::string& text, int max_width) {
    std::vector<std::string> out;
    if (max_width <= 20) { out.push_back(text); return out; }

    std::istringstream para_ss(text);
    std::string para;
    while (std::getline(para_ss, para, '\n')) {
        if (!para.empty() && para.back() == '\r') para.pop_back();
        if (para.empty()) { out.push_back(""); continue; }

        std::istringstream word_ss(para);
        std::string word;
        std::string cur;
        while (word_ss >> word) {
            std::string cand = cur.empty() ? word : (cur + " " + word);
            if (measure_text_width(size, cand) <= max_width) {
                cur = std::move(cand);
            } else {
                if (!cur.empty()) {
                    out.push_back(cur);
                    cur.clear();
                }
                if (measure_text_width(size, word) <= max_width) {
                    cur = word;
                } else {
                    // Split very long word along UTF-8 codepoint boundaries
                    std::string chunk;
                    int idx = 0;
                    while (idx < (int)word.size()) {
                        int prev = idx;
                        uint32_t cp = 0;
                        decode_utf8(word, idx, cp);
                        std::string ch = word.substr(prev, idx - prev);
                        if (!chunk.empty() && measure_text_width(size, chunk + ch) > max_width) {
                            out.push_back(chunk);
                            chunk = ch;
                        } else {
                            chunk += ch;
                        }
                    }
                    cur = chunk;
                }
            }
        }
        if (!cur.empty()) out.push_back(cur);
    }
    if (out.empty()) out.push_back("");
    return out;
}

void TextRenderer::draw_image_on_canvas(std::vector<uint8_t>& canvas, int W, int H,
                                        const ImageRGBA* img,
                                        int x, int y, int dst_w, int dst_h) {
    if (!img || !img->valid() || dst_w <= 0 || dst_h <= 0) return;

    for (int dy = 0; dy < dst_h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= H) continue;
        float v = (dst_h > 1) ? ((float)dy / (float)(dst_h - 1)) : 0.5f;

        for (int dx = 0; dx < dst_w; dx++) {
            int px = x + dx;
            if (px < 0 || px >= W) continue;
            float u = (dst_w > 1) ? ((float)dx / (float)(dst_w - 1)) : 0.5f;

            unsigned char r, g, b, a;
            sample_bilinear(img, u, v, r, g, b, a);
            if (a > 0) blend_pixel(canvas, W, H, px, py, r, g, b, a);
        }
    }
}

void TextRenderer::draw_image_aspect_fit(std::vector<uint8_t>& canvas, int W, int H,
                                         const ImageRGBA* img,
                                         int x, int y, int box_w, int box_h) {
    if (!img || !img->valid() || box_w <= 0 || box_h <= 0) return;
    float sx = (float)box_w / (float)img->width;
    float sy = (float)box_h / (float)img->height;
    float s = std::min(sx, sy);
    int dw = std::max(1, (int)(img->width * s + 0.5f));
    int dh = std::max(1, (int)(img->height * s + 0.5f));
    int dx = x + (box_w - dw) / 2;
    int dy = y + (box_h - dh) / 2;
    draw_image_on_canvas(canvas, W, H, img, dx, dy, dw, dh);
}

void TextRenderer::draw_image_aspect_cover(std::vector<uint8_t>& canvas, int W, int H,
                                           const ImageRGBA* img,
                                           int x, int y, int box_w, int box_h) {
    if (!img || !img->valid() || box_w <= 0 || box_h <= 0) return;
    float box_aspect = (float)box_w / (float)box_h;
    float img_aspect = (float)img->width / (float)img->height;
    float u0 = 0.0f, v0 = 0.0f, uw = 1.0f, vh = 1.0f;
    if (img_aspect > box_aspect) {
        uw = box_aspect / img_aspect;
        u0 = (1.0f - uw) * 0.5f;
    } else {
        vh = img_aspect / box_aspect;
        v0 = (1.0f - vh) * 0.5f;
    }
    for (int dy = 0; dy < box_h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= H) continue;
        float v = v0 + vh * ((box_h > 1) ? ((float)dy / (float)(box_h - 1)) : 0.5f);
        for (int dx = 0; dx < box_w; dx++) {
            int px = x + dx;
            if (px < 0 || px >= W) continue;
            float u = u0 + uw * ((box_w > 1) ? ((float)dx / (float)(box_w - 1)) : 0.5f);
            unsigned char r, g, b, a;
            sample_bilinear(img, u, v, r, g, b, a);
            if (a > 0) blend_pixel(canvas, W, H, px, py, r, g, b, a);
        }
    }
}

static void hsv_to_rgb(float h, float s, float v,
                       unsigned char& r, unsigned char& g, unsigned char& b) {
    h = std::fmod(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf, gf, bf;
    if      (h < 60)  { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else              { rf = c; gf = 0; bf = x; }
    r = (unsigned char)((rf + m) * 255);
    g = (unsigned char)((gf + m) * 255);
    b = (unsigned char)((bf + m) * 255);
}

void TextRenderer::draw_circle_image(std::vector<uint8_t>& canvas, int W, int H,
                                     const ImageRGBA* img,
                                     int cx, int cy, int radius) {
    if (!img || !img->valid() || radius <= 0) return;
    float diameter = (float)(radius * 2);
    int y0 = std::max(0, cy - radius - 1), y1 = std::min(H - 1, cy + radius + 1);
    int x0 = std::max(0, cx - radius - 1), x1 = std::min(W - 1, cx + radius + 1);

    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            float dx = (float)(px - cx);
            float dy = (float)(py - cy);
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist >= radius + 0.7f) continue;

            float cov = std::clamp((float)radius + 0.7f - dist, 0.0f, 1.0f);
            float u = std::clamp((dx + radius) / diameter, 0.0f, 1.0f);
            float v = std::clamp((dy + radius) / diameter, 0.0f, 1.0f);

            unsigned char r, g, b, a;
            sample_bilinear(img, u, v, r, g, b, a);
            unsigned char fa = (unsigned char)(a * cov);
            if (fa > 0) blend_pixel(canvas, W, H, px, py, r, g, b, fa);
        }
    }
}

void TextRenderer::draw_circle_outline_rainbow(std::vector<uint8_t>& canvas, int W, int H,
                                               int cx, int cy, int radius, int thickness) {
    if (radius <= 0 || thickness <= 0) return;
    float inner_r = (float)radius - thickness * 0.5f;
    float outer_r = (float)radius + thickness * 0.5f;
    int max_r = (int)std::ceil(outer_r) + 1;
    int y0 = std::max(0, cy - max_r), y1 = std::min(H - 1, cy + max_r);
    int x0 = std::max(0, cx - max_r), x1 = std::min(W - 1, cx + max_r);

    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            float dx = (float)(px - cx);
            float dy = (float)(py - cy);
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < inner_r - 0.8f || dist > outer_r + 0.8f) continue;

            float cov = std::min(dist - (inner_r - 0.8f), (outer_r + 0.8f) - dist);
            cov = std::clamp(cov, 0.0f, 1.0f);
            float angle = std::atan2(dy, dx) * 180.0f / 3.14159265f;
            unsigned char r, g, b;
            hsv_to_rgb(angle, 0.92f, 1.0f, r, g, b);
            blend_pixel(canvas, W, H, px, py, r, g, b, (unsigned char)(255 * cov));
        }
    }
}

void TextRenderer::get_random_gradient(unsigned char& r1, unsigned char& g1, unsigned char& b1,
                                       unsigned char& r2, unsigned char& g2, unsigned char& b2) {
    struct RGB { unsigned char r, g, b; };
    static const RGB pairs[][2] = {
        { {255, 110, 130}, {255, 220, 100} },
        { {80,  220, 255}, {120, 255, 190} },
        { {140, 240, 160}, {56,  189, 248} },
        { {255, 145, 200}, {255, 215, 130} },
        { {190, 150, 255}, {110, 230, 255} },
        { {255, 215, 90},  {255, 125, 80}  },
    };
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 5);
    int i = dist(rng);
    r1 = pairs[i][0].r; g1 = pairs[i][0].g; b1 = pairs[i][0].b;
    r2 = pairs[i][1].r; g2 = pairs[i][1].g; b2 = pairs[i][1].b;
}

void TextRenderer::draw_text_gradient(std::vector<uint8_t>& canvas, int W, int H,
                                      int x, int y, int size,
                                      unsigned char r1, unsigned char g1, unsigned char b1,
                                      unsigned char r2, unsigned char g2, unsigned char b2,
                                      const std::string& text) {
    if (!g_initialized) return;
    float scale = stbtt_ScaleForPixelHeight(&g_font_info, (float)size);
    int total_w = std::max(1, measure_text_width(size, text));

    int i = 0, cursor = 0;
    while (i < (int)text.size()) {
        uint32_t cp = 0;
        if (!decode_utf8(text, i, cp)) continue;
        float t = std::clamp((float)cursor / (float)total_w, 0.0f, 1.0f);
        unsigned char cr = (unsigned char)(r1 + (r2 - r1) * t);
        unsigned char cg = (unsigned char)(g1 + (g2 - g1) * t);
        unsigned char cb = (unsigned char)(b1 + (b2 - b1) * t);
        draw_char(canvas, W, H, x + cursor, y, size, cr, cg, cb, &g_font_info, scale, cp, true);
        cursor += glyph_advance_px(&g_font_info, scale, size, cp);
    }
}

bool TextRenderer::save_canvas(const std::vector<uint8_t>& canvas, int W, int H,
                               const std::string& path) {
    return stbi_write_png(path.c_str(), W, H, 4, canvas.data(), W * 4) != 0;
}

void TextRenderer::draw_text_gradient_random(std::vector<uint8_t>& canvas, int W, int H,
                                             int x, int y, int size,
                                             const std::string& text) {
    if (!g_initialized) return;
    unsigned char r1, g1, b1, r2, g2, b2;
    get_random_gradient(r1, g1, b1, r2, g2, b2);
    draw_text_gradient(canvas, W, H, x, y, size, r1, g1, b1, r2, g2, b2, text);
}
