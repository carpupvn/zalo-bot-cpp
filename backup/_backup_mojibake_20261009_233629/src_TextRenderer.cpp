#include "TextRenderer.hpp"
#include "AssetManager.hpp"
#include <iostream>
#include <fstream>
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
    return true;
}

bool TextRenderer::init_emoji(const std::string& emoji_path) {
    std::ifstream f(emoji_path, std::ios::binary);
    if (!f.good()) {
        std::cout << "[Renderer] Khong co emoji font: " << emoji_path
                  << " (emoji se hien tofu)\n";
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

static void blend_pixel(std::vector<uint8_t>& img, int W, int H,
                        int x, int y,
                        unsigned char r, unsigned char g, unsigned char b,
                        unsigned char a) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    int idx = (y * W + x) * 4;
    float af = a / 255.0f;
    img[idx + 0] = (unsigned char)(r * af + img[idx + 0] * (1 - af));
    img[idx + 1] = (unsigned char)(g * af + img[idx + 1] * (1 - af));
    img[idx + 2] = (unsigned char)(b * af + img[idx + 2] * (1 - af));
    img[idx + 3] = 255;
}

void TextRenderer::draw_image_rotated(std::vector<uint8_t>& canvas, int W, int H,
                                        const ImageRGBA* img,
                                        int cx, int cy, int size, float angle_rad)
{
    if (!img || !img->valid()) return;
    if (size <= 0) return;

    float ca = std::cos(angle_rad);
    float sa = std::sin(angle_rad);

    int bound_half = (int)(size * 0.75f) + 2;
    int img_half = size / 2;

    for (int dy = -bound_half; dy <= bound_half; dy++) {
        for (int dx = -bound_half; dx <= bound_half; dx++) {
            int px = cx + dx;
            int py = cy + dy;
            if (px < 0 || px >= W || py < 0 || py >= H) continue;

            float sx_f = (float)dx * ca + (float)dy * sa;
            float sy_f = -(float)dx * sa + (float)dy * ca;

            if (sx_f < -img_half || sx_f >= img_half) continue;
            if (sy_f < -img_half || sy_f >= img_half) continue;

            int img_x = (int)(sx_f + img_half);
            int img_y = (int)(sy_f + img_half);
            if (img_x < 0 || img_x >= size) continue;
            if (img_y < 0 || img_y >= size) continue;

            int src_x = img_x * img->width / size;
            int src_y = img_y * img->height / size;
            if (src_x < 0 || src_x >= img->width) continue;
            if (src_y < 0 || src_y >= img->height) continue;

            int src_idx = (src_y * img->width + src_x) * 4;
            unsigned char ar = img->data[src_idx + 0];
            unsigned char ag = img->data[src_idx + 1];
            unsigned char ab = img->data[src_idx + 2];
            unsigned char aa = img->data[src_idx + 3];
            if (aa == 0) continue;
            blend_pixel(canvas, W, H, px, py, ar, ag, ab, aa);
        }
    }
}

// ============================================================
// Draw char â€” tá»± chá»n font chÃ­nh/emoji
// ============================================================
static void draw_char(std::vector<unsigned char>& img, int W, int H,
                       int x, int y,
                       unsigned char r, unsigned char g, unsigned char b,
                       const stbtt_fontinfo* main_font, float main_scale,
                       int codepoint) {
    const stbtt_fontinfo* use_font = main_font;
    float use_scale = main_scale;

    int glyph_idx = stbtt_FindGlyphIndex(main_font, codepoint);

    // Náº¿u font chÃ­nh khÃ´ng cÃ³ glyph â†’ thá»­ emoji font
    if (glyph_idx == 0 && g_emoji_initialized) {
        int emoji_idx = stbtt_FindGlyphIndex(&g_emoji_info, codepoint);
        if (emoji_idx > 0) {
            use_font = &g_emoji_info;
            use_scale = main_scale * 1.05f;  // emoji hÆ¡i to hÆ¡n chÃºt
        }
    }

    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(use_font, codepoint, &advance, &lsb);

    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(use_font, codepoint, use_scale, use_scale,
                                 &x0, &y0, &x1, &y1);

    int w = x1 - x0;
    int h = y1 - y0;
    if (w <= 0 || h <= 0) return;

    std::vector<unsigned char> bitmap(w * h);
    stbtt_MakeCodepointBitmap(use_font, bitmap.data(), w, h, w, use_scale, use_scale, codepoint);

    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            int px = x + x0 + xx;
            int py = y + y0 + yy;
            unsigned char alpha = bitmap[yy * w + xx];
            if (alpha == 0) continue;
            blend_pixel(img, W, H, px, py, r, g, b, alpha);
        }
    }
}

static void draw_text_utf8(std::vector<unsigned char>& img, int W, int H,
                            int x, int y,
                            unsigned char r, unsigned char g, unsigned char b,
                            const stbtt_fontinfo* font, float scale,
                            const std::string& text) {
    int i = 0;
    int cursor_x = x;
    while (i < (int)text.size()) {
        unsigned char c = (unsigned char)text[i];
        int cp = 0, len = 1;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = ((c & 0x1F) << 6) | (text[i+1] & 0x3F); len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = ((c & 0x0F) << 12) | ((text[i+1] & 0x3F) << 6) | (text[i+2] & 0x3F); len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = ((c & 0x07) << 18) | ((text[i+1] & 0x3F) << 12) | ((text[i+2] & 0x3F) << 6) | (text[i+3] & 0x3F); len = 4; }
        else { i++; continue; }

        // Cho phÃ©p emoji + variation selector
        draw_char(img, W, H, cursor_x, y, r, g, b, font, scale, cp);

        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(font, cp, &adv, &lsb);

        // Emoji cÃ³ thá»ƒ rá»™ng hÆ¡n
        int glyph_idx = stbtt_FindGlyphIndex(font, cp);
        if (glyph_idx == 0 && g_emoji_initialized) {
            int emoji_idx = stbtt_FindGlyphIndex(&g_emoji_info, cp);
            if (emoji_idx > 0) {
                stbtt_GetCodepointHMetrics(&g_emoji_info, cp, &adv, &lsb);
            }
        }

        cursor_x += (int)(adv * scale);
        i += len;
    }
}

// ============================================================
// Render (dÃ¹ng cho reply dÃ i)
// ============================================================
bool TextRenderer::render(const std::string& title,
                           const std::vector<std::string>& lines,
                           const std::string& output_path,
                           const TextStyle& style) {
    if (!g_initialized) return false;

    float title_scale = stbtt_ScaleForPixelHeight(&g_font_info, style.title_size);
    float text_scale  = stbtt_ScaleForPixelHeight(&g_font_info, style.text_size);

    int W = style.width;
    int title_h = style.title_size + style.padding;
    int content_h = (int)lines.size() * style.line_height;
    int H = style.padding * 2 + title_h + content_h + 40;

    std::vector<unsigned char> img(W * H * 4, 0);
    fill_linear_gradient(img, W, H, 0, H,
                          style.bg_r, style.bg_g, style.bg_b,
                          style.bg2_r, style.bg2_g, style.bg2_b);

    int card_x = style.padding - 10;
    int card_y = style.padding - 10;
    int card_w = W - (card_x * 2);
    int card_h = H - (card_y * 2);

    draw_rounded_rect_filled(img, W, H, card_x, card_y, card_w, card_h,
                              style.card_radius,
                              style.card_r, style.card_g, style.card_b, 220);

    draw_rounded_rect_outline(img, W, H, card_x, card_y, card_w, card_h,
                               style.card_radius, 2,
                               style.accent_r, style.accent_g, style.accent_b, 180);

    draw_text_utf8(img, W, H, style.padding, style.padding,
                   style.title_r, style.title_g, style.title_b,
                   &g_font_info, title_scale, title);

    int line_y = style.padding + title_h;
    for (int x = style.padding; x < W - style.padding; x++) {
        blend_pixel(img, W, H, x, line_y,
                    style.accent_r, style.accent_g, style.accent_b, 200);
        blend_pixel(img, W, H, x, line_y + 1,
                    style.accent_r, style.accent_g, style.accent_b, 80);
    }

    int y = line_y + 24;
    for (const auto& line : lines) {
        draw_text_utf8(img, W, H, style.padding + 10, y,
                       style.fg_r, style.fg_g, style.fg_b,
                       &g_font_info, text_scale, line);
        y += style.line_height;
    }

    return stbi_write_png(output_path.c_str(), W, H, 4, img.data(), W * 4) != 0;
}

// ============================================================
// Canvas API
// ============================================================
void TextRenderer::init_canvas(std::vector<uint8_t>& canvas, int W, int H,
                                unsigned char r, unsigned char g, unsigned char b) {
    canvas.assign(W * H * 4, 0);
    for (int i = 0; i < W * H; i++) {
        canvas[i*4 + 0] = r;
        canvas[i*4 + 1] = g;
        canvas[i*4 + 2] = b;
        canvas[i*4 + 3] = 255;
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
    for (int py = y; py < y + rh; py++)
        for (int px = x; px < x + rw; px++)
            blend_pixel(canvas, W, H, px, py, r, g, b, a);
}

static bool inside_rounded_rect(int px, int py, int x, int y, int w, int h, int radius) {
    if (px < x || px >= x + w || py < y || py >= y + h) return false;
    int cx, cy;
    if (px < x + radius && py < y + radius) { cx = x + radius; cy = y + radius; }
    else if (px >= x + w - radius && py < y + radius) { cx = x + w - radius - 1; cy = y + radius; }
    else if (px < x + radius && py >= y + h - radius) { cx = x + radius; cy = y + h - radius - 1; }
    else if (px >= x + w - radius && py >= y + h - radius) { cx = x + w - radius - 1; cy = y + h - radius - 1; }
    else return true;
    int dx = px - cx, dy = py - cy;
    return (dx*dx + dy*dy) <= (radius * radius);
}

void TextRenderer::draw_rounded_rect_filled(std::vector<uint8_t>& canvas, int W, int H,
                                              int x, int y, int rw, int rh, int radius,
                                              unsigned char r, unsigned char g, unsigned char b,
                                              unsigned char a) {
    if (radius < 0) radius = 0;
    if (radius > rw / 2) radius = rw / 2;
    if (radius > rh / 2) radius = rh / 2;

    for (int py = y; py < y + rh; py++) {
        if (py < 0 || py >= H) continue;
        for (int px = x; px < x + rw; px++) {
            if (px < 0 || px >= W) continue;
            if (!inside_rounded_rect(px, py, x, y, rw, rh, radius)) continue;
            blend_pixel(canvas, W, H, px, py, r, g, b, a);
        }
    }
}

void TextRenderer::draw_rounded_rect_outline(std::vector<uint8_t>& canvas, int W, int H,
                                               int x, int y, int rw, int rh, int radius,
                                               int thickness,
                                               unsigned char r, unsigned char g, unsigned char b,
                                               unsigned char a) {
    if (radius < 0) radius = 0;
    if (radius > rw / 2) radius = rw / 2;
    if (radius > rh / 2) radius = rh / 2;

    for (int py = y - 1; py <= y + rh; py++) {
        if (py < 0 || py >= H) continue;
        for (int px = x - 1; px <= x + rw; px++) {
            if (px < 0 || px >= W) continue;

            bool inner = inside_rounded_rect(px, py, x, y, rw, rh, radius);
            if (!inner) continue;

            // Náº¿u pixel cÃ¡ch biÃªn trong vÃ²ng thickness â†’ váº½
            bool is_edge = false;
            for (int t = 1; t <= thickness; t++) {
                if (!inside_rounded_rect(px - t, py, x, y, rw, rh, radius) ||
                    !inside_rounded_rect(px + t, py, x, y, rw, rh, radius) ||
                    !inside_rounded_rect(px, py - t, x, y, rw, rh, radius) ||
                    !inside_rounded_rect(px, py + t, x, y, rw, rh, radius)) {
                    is_edge = true;
                    break;
                }
            }
            if (is_edge) blend_pixel(canvas, W, H, px, py, r, g, b, a);
        }
    }
}

void TextRenderer::draw_circle_filled(std::vector<uint8_t>& canvas, int W, int H,
                                       int cx, int cy, int radius,
                                       unsigned char r, unsigned char g, unsigned char b,
                                       unsigned char a) {
    for (int py = cy - radius; py <= cy + radius; py++) {
        for (int px = cx - radius; px <= cx + radius; px++) {
            int dx = px - cx, dy = py - cy;
            if (dx*dx + dy*dy <= radius*radius)
                blend_pixel(canvas, W, H, px, py, r, g, b, a);
        }
    }
}

void TextRenderer::draw_line(std::vector<uint8_t>& canvas, int W, int H,
                              int x1, int y1, int x2, int y2,
                              unsigned char r, unsigned char g, unsigned char b,
                              int thickness, unsigned char a) {
    int dx = std::abs(x2 - x1);
    int dy = std::abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
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

void TextRenderer::draw_text_on_canvas(std::vector<uint8_t>& canvas, int W, int H,
                                        int x, int y, int size,
                                        unsigned char r, unsigned char g, unsigned char b,
                                        const std::string& text) {
    if (!g_initialized) return;
    float scale = stbtt_ScaleForPixelHeight(&g_font_info, size);
    draw_text_utf8(canvas, W, H, x, y, r, g, b, &g_font_info, scale, text);
}

void TextRenderer::draw_text_shadow(std::vector<uint8_t>& canvas, int W, int H,
                                     int x, int y, int size,
                                     unsigned char r, unsigned char g, unsigned char b,
                                     const std::string& text) {
    if (!g_initialized) return;
    float scale = stbtt_ScaleForPixelHeight(&g_font_info, size);
    draw_text_utf8(canvas, W, H, x + 3, y + 3, 0, 0, 0, &g_font_info, scale, text);
    draw_text_utf8(canvas, W, H, x, y, r, g, b, &g_font_info, scale, text);
}

int TextRenderer::measure_text_width(int size, const std::string& text) {
    if (!g_initialized) return 0;
    float scale = stbtt_ScaleForPixelHeight(&g_font_info, size);

    int i = 0;
    int total = 0;
    while (i < (int)text.size()) {
        unsigned char c = (unsigned char)text[i];
        int cp = 0, len = 1;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = ((c & 0x1F) << 6) | (text[i+1] & 0x3F); len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = ((c & 0x0F) << 12) | ((text[i+1] & 0x3F) << 6) | (text[i+2] & 0x3F); len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = ((c & 0x07) << 18) | ((text[i+1] & 0x3F) << 12) | ((text[i+2] & 0x3F) << 6) | (text[i+3] & 0x3F); len = 4; }
        else { i++; continue; }

        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&g_font_info, cp, &adv, &lsb);
        total += (int)(adv * scale);
        i += len;
    }
    return total;
}

void TextRenderer::draw_image_on_canvas(std::vector<uint8_t>& canvas, int W, int H,
                                         const ImageRGBA* img,
                                         int x, int y, int dst_w, int dst_h) {
    if (!img || !img->valid()) return;
    if (dst_w <= 0 || dst_h <= 0) return;

    for (int dy = 0; dy < dst_h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= H) continue;
        int sy = dy * img->height / dst_h;
        if (sy < 0 || sy >= img->height) continue;

        for (int dx = 0; dx < dst_w; dx++) {
            int px = x + dx;
            if (px < 0 || px >= W) continue;
            int sx = dx * img->width / dst_w;
            if (sx < 0 || sx >= img->width) continue;

            int src_idx = (sy * img->width + sx) * 4;
            blend_pixel(canvas, W, H, px, py,
                        img->data[src_idx + 0],
                        img->data[src_idx + 1],
                        img->data[src_idx + 2],
                        img->data[src_idx + 3]);
        }
    }
}

// ============================================================
// HSV â†’ RGB cho rainbow
// ============================================================
static void hsv_to_rgb(float h, float s, float v,
                        unsigned char& r, unsigned char& g, unsigned char& b) {
    h = fmod(h, 360.0f);
    if (h < 0) h += 360.0f;

    float c = v * s;
    float x = c * (1 - fabs(fmod(h / 60.0f, 2.0f) - 1));
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
    if (!img || !img->valid()) return;
    if (radius <= 0) return;

    int diameter = radius * 2;
    int r2 = radius * radius;

    for (int dy = -radius; dy < radius; dy++) {
        for (int dx = -radius; dx < radius; dx++) {
            if (dx*dx + dy*dy > r2) continue;

            int px = cx + dx;
            int py = cy + dy;
            if (px < 0 || px >= W || py < 0 || py >= H) continue;

            float u = (float)(dx + radius) / (float)diameter;
            float v = (float)(dy + radius) / (float)diameter;

            int sx = (int)(u * img->width);
            int sy = (int)(v * img->height);
            if (sx < 0 || sx >= img->width) continue;
            if (sy < 0 || sy >= img->height) continue;

            int src_idx = (sy * img->width + sx) * 4;
            blend_pixel(canvas, W, H, px, py,
                        img->data[src_idx + 0],
                        img->data[src_idx + 1],
                        img->data[src_idx + 2],
                        img->data[src_idx + 3]);
        }
    }
}

void TextRenderer::draw_circle_outline_rainbow(std::vector<uint8_t>& canvas, int W, int H,
                                                int cx, int cy, int radius, int thickness) {
    // Váº½ nhiá»u Ä‘iá»ƒm theo gÃ³c â†’ táº¡o viá»n trÃ²n rainbow
    const int STEPS = 720;
    for (int s = 0; s < STEPS; s++) {
        float angle = (float)s * 360.0f / (float)STEPS;
        float rad = angle * 3.14159265f / 180.0f;

        unsigned char r, g, b;
        hsv_to_rgb(angle, 0.9f, 1.0f, r, g, b);

        for (int t = 0; t < thickness; t++) {
            int rr = radius + t;
            int px = cx + (int)(cos(rad) * rr);
            int py = cy + (int)(sin(rad) * rr);
            blend_pixel(canvas, W, H, px, py, r, g, b, 255);
        }
    }
}

void TextRenderer::blur_region(std::vector<uint8_t>& canvas, int W, int H, int strength) {
    if (strength <= 0) return;

    // Box blur Ä‘Æ¡n giáº£n (sample step = strength)
    std::vector<uint8_t> src = canvas;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int r = 0, g = 0, b = 0, cnt = 0;

            for (int dy = -strength; dy <= strength; dy += strength) {
                for (int dx = -strength; dx <= strength; dx += strength) {
                    int sx = x + dx;
                    int sy = y + dy;
                    if (sx < 0 || sx >= W || sy < 0 || sy >= H) continue;

                    int idx = (sy * W + sx) * 4;
                    r += src[idx + 0];
                    g += src[idx + 1];
                    b += src[idx + 2];
                    cnt++;
                }
            }

            if (cnt > 0) {
                int idx = (y * W + x) * 4;
                canvas[idx + 0] = (unsigned char)(r / cnt);
                canvas[idx + 1] = (unsigned char)(g / cnt);
                canvas[idx + 2] = (unsigned char)(b / cnt);
            }
        }
    }
}

void TextRenderer::darken_region(std::vector<uint8_t>& canvas, int W, int H,
                                  int x, int y, int rw, int rh, unsigned char alpha) {
    for (int py = y; py < y + rh; py++) {
        if (py < 0 || py >= H) continue;
        for (int px = x; px < x + rw; px++) {
            if (px < 0 || px >= W) continue;
            blend_pixel(canvas, W, H, px, py, 0, 0, 0, alpha);
        }
    }
}

// ============================================================
// Random gradient — palette tu bot kia
// ============================================================
void TextRenderer::get_random_gradient(unsigned char& r1, unsigned char& g1, unsigned char& b1,
                                         unsigned char& r2, unsigned char& g2, unsigned char& b2) {
    struct RGB { unsigned char r, g, b; };
    static const RGB palette[] = {
        {255, 220, 100},   // Vang
        {120, 220, 120},   // Xanh la
        {100, 220, 240},   // Xanh ngoc
        {100, 160, 255},   // Xanh lam
        {220, 130, 255},   // Tim
        {255, 130, 180},   // Hong
        {255, 160, 100},   // Cam
    };
    const int N = sizeof(palette) / sizeof(palette[0]);

    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, N - 1);

    int i1 = dist(rng);
    int i2 = dist(rng);
    while (i2 == i1) i2 = dist(rng);

    r1 = palette[i1].r; g1 = palette[i1].g; b1 = palette[i1].b;
    r2 = palette[i2].r; g2 = palette[i2].g; b2 = palette[i2].b;
}

// ============================================================
// Ve text voi gradient ngang (trai -> phai)
// ============================================================
void TextRenderer::draw_text_gradient(std::vector<uint8_t>& canvas, int W, int H,
                                       int x, int y, int size,
                                       unsigned char r1, unsigned char g1, unsigned char b1,
                                       unsigned char r2, unsigned char g2, unsigned char b2,
                                       const std::string& text) {
    if (!g_initialized) return;
    float scale = stbtt_ScaleForPixelHeight(&g_font_info, size);

    // Dem tong chieu rong
    int total_w = 0;
    {
        int i = 0;
        while (i < (int)text.size()) {
            unsigned char c = (unsigned char)text[i];
            int cp = 0, len = 1;
            if (c < 0x80) { cp = c; len = 1; }
            else if ((c & 0xE0) == 0xC0) { cp = ((c & 0x1F) << 6) | (text[i+1] & 0x3F); len = 2; }
            else if ((c & 0xF0) == 0xE0) { cp = ((c & 0x0F) << 12) | ((text[i+1] & 0x3F) << 6) | (text[i+2] & 0x3F); len = 3; }
            else if ((c & 0xF8) == 0xF0) { cp = ((c & 0x07) << 18) | ((text[i+1] & 0x3F) << 12) | ((text[i+2] & 0x3F) << 6) | (text[i+3] & 0x3F); len = 4; }
            else { i++; continue; }
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&g_font_info, cp, &adv, &lsb);
            total_w += (int)(adv * scale);
            i += len;
        }
    }
    if (total_w == 0) return;

    int i = 0;
    int cursor = 0;
    while (i < (int)text.size()) {
        unsigned char c = (unsigned char)text[i];
        int cp = 0, len = 1;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = ((c & 0x1F) << 6) | (text[i+1] & 0x3F); len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = ((c & 0x0F) << 12) | ((text[i+1] & 0x3F) << 6) | (text[i+2] & 0x3F); len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = ((c & 0x07) << 18) | ((text[i+1] & 0x3F) << 12) | ((text[i+2] & 0x3F) << 6) | (text[i+3] & 0x3F); len = 4; }
        else { i++; continue; }

        // Interpolate color theo vi tri
        float t = (float)cursor / (float)total_w;
        unsigned char cr = (unsigned char)(r1 + (r2 - r1) * t);
        unsigned char cg = (unsigned char)(g1 + (g2 - g1) * t);
        unsigned char cb = (unsigned char)(b1 + (b2 - b1) * t);

        // Ve char voi mau da tinh
        draw_char(canvas, W, H, x + cursor, y, cr, cg, cb, &g_font_info, scale, cp);

        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&g_font_info, cp, &adv, &lsb);
        cursor += (int)(adv * scale);
        i += len;
    }
}

bool TextRenderer::save_canvas(const std::vector<uint8_t>& canvas, int W, int H,
                                const std::string& path) {
    return stbi_write_png(path.c_str(), W, H, 4, canvas.data(), W * 4) != 0;
}

// ============================================================
// Vẽ text với random rainbow gradient (5 stop) — giống JS
// ============================================================
void TextRenderer::draw_text_gradient_random(std::vector<uint8_t>& canvas, int W, int H,
                                              int x, int y, int size,
                                              const std::string& text) {
    if (!g_initialized) return;
    float scale = stbtt_ScaleForPixelHeight(&g_font_info, size);

    // 5 stop màu random bright HSL
    struct RGB8 { uint8_t r, g, b; };
    std::vector<RGB8> stops;
    static std::mt19937 rng{std::random_device{}()};
    static const int hues[] = {60, 120, 180, 200};
    std::uniform_int_distribution<int> dh(0, 3);
    std::uniform_int_distribution<int> ds(95, 100);
    std::uniform_int_distribution<int> dl(80, 85);

    for (int i = 0; i < 5; i++) {
        int hue = hues[dh(rng)];
        float h = (float)hue;
        float s = ds(rng) / 100.0f;
        float l = dl(rng) / 100.0f;
        float c = (1 - std::fabs(2*l - 1)) * s;
        float xx = c * (1 - std::fabs(std::fmod(h/60.0f, 2.0f) - 1));
        float m = l - c / 2.0f;
        float rf, gf, bf;
        if      (h <  60) { rf = c;  gf = xx; bf = 0; }
        else if (h < 120) { rf = xx; gf = c;  bf = 0; }
        else if (h < 180) { rf = 0;  gf = c;  bf = xx; }
        else if (h < 240) { rf = 0;  gf = xx; bf = c; }
        else if (h < 300) { rf = xx; gf = 0;  bf = c; }
        else              { rf = c;  gf = 0;  bf = xx; }
        RGB8 rgb;
        rgb.r = (uint8_t)((rf + m) * 255);
        rgb.g = (uint8_t)((gf + m) * 255);
        rgb.b = (uint8_t)((bf + m) * 255);
        stops.push_back(rgb);
    }

    auto sample = [&](float t) -> RGB8 {
        if (t <= 0) return stops[0];
        if (t >= 1) return stops[4];
        float pos = t * 4.0f;
        int i = (int)pos;
        if (i >= 4) i = 3;
        float local = pos - i;
        RGB8 c;
        c.r = (uint8_t)(stops[i].r + (stops[i+1].r - stops[i].r) * local);
        c.g = (uint8_t)(stops[i].g + (stops[i+1].g - stops[i].g) * local);
        c.b = (uint8_t)(stops[i].b + (stops[i+1].b - stops[i].b) * local);
        return c;
    };

    // Đo tổng width
    int total_w = 0;
    {
        int i = 0;
        while (i < (int)text.size()) {
            unsigned char ch = (unsigned char)text[i];
            int cp = 0, len = 1;
            if (ch < 0x80) { cp = ch; len = 1; }
            else if ((ch & 0xE0) == 0xC0) { cp = ((ch & 0x1F) << 6) | (text[i+1] & 0x3F); len = 2; }
            else if ((ch & 0xF0) == 0xE0) { cp = ((ch & 0x0F) << 12) | ((text[i+1] & 0x3F) << 6) | (text[i+2] & 0x3F); len = 3; }
            else if ((ch & 0xF8) == 0xF0) { cp = ((ch & 0x07) << 18) | ((text[i+1] & 0x3F) << 12) | ((text[i+2] & 0x3F) << 6) | (text[i+3] & 0x3F); len = 4; }
            else { i++; continue; }
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&g_font_info, cp, &adv, &lsb);
            total_w += (int)(adv * scale);
            i += len;
        }
    }
    if (total_w <= 0) return;

    // Vẽ
    int i = 0;
    int cursor = 0;
    while (i < (int)text.size()) {
        unsigned char ch = (unsigned char)text[i];
        int cp = 0, len = 1;
        if (ch < 0x80) { cp = ch; len = 1; }
        else if ((ch & 0xE0) == 0xC0) { cp = ((ch & 0x1F) << 6) | (text[i+1] & 0x3F); len = 2; }
        else if ((ch & 0xF0) == 0xE0) { cp = ((ch & 0x0F) << 12) | ((text[i+1] & 0x3F) << 6) | (text[i+2] & 0x3F); len = 3; }
        else if ((ch & 0xF8) == 0xF0) { cp = ((ch & 0x07) << 18) | ((text[i+1] & 0x3F) << 12) | ((text[i+2] & 0x3F) << 6) | (text[i+3] & 0x3F); len = 4; }
        else { i++; continue; }

        float t = (float)cursor / (float)total_w;
        RGB8 c = sample(t);
        draw_char(canvas, W, H, x + cursor, y, c.r, c.g, c.b, &g_font_info, scale, cp);

        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&g_font_info, cp, &adv, &lsb);
        cursor += (int)(adv * scale);
        i += len;
    }
}