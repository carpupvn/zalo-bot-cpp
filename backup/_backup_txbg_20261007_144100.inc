// TaiXiuRender.inc - Port tu tai-xiu.js (createTaiXiuResultImage + createWaitingImage)
#pragma once
#include <boost/multiprecision/cpp_int.hpp>
using cpp_int = boost::multiprecision::cpp_int;

#include <vector>
#include <string>
#include <cmath>
#include <random>
#include <utility>
#include <cstdio>
#include "TextRenderer.hpp"
#include "AssetManager.hpp"
#include "Color.hpp"
#include "Gradient.hpp"
#include "Shape.hpp"
#include "Utils.hpp"

namespace taixiu_render {

inline std::mt19937& rng() {
    static std::mt19937 g(std::random_device{}());
    return g;
}
inline int ri(int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng());
}
inline float rf(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng());
}

inline void text_efx(std::vector<uint8_t>& canvas, int W, int H,
                     int cx, int cy, int size,
                     unsigned char r1, unsigned char g1, unsigned char b1,
                     unsigned char r2, unsigned char g2, unsigned char b2,
                     const std::string& text, int outline = 3)
{
    int tw = TextRenderer::measure_text_width(size, text);
    int tx = cx - tw / 2;
    int ty = cy - size / 2;
    for (int dx = -outline; dx <= outline; dx++) {
        for (int dy = -outline; dy <= outline; dy++) {
            if (dx == 0 && dy == 0) continue;
            if (dx*dx + dy*dy > outline*outline) continue;
            TextRenderer::draw_text_on_canvas(canvas, W, H,
                tx + dx, ty + dy, size, 0, 0, 0, text);
        }
    }
    TextRenderer::draw_text_gradient(canvas, W, H, tx, ty, size,
        r1, g1, b1, r2, g2, b2, text);
}

inline void draw_dice_rotated(std::vector<uint8_t>& canvas, int W, int H,
                              ImageRGBA* img, int cx, int cy, int size, float angle)
{
    if (!img || !img->valid()) return;
    float ca = std::cos(angle), sa = std::sin(angle);
    int half = size / 2;
    int bound = (int)(size * 0.75f) + 2;
    for (int dy = -bound; dy <= bound; dy++) {
        for (int dx = -bound; dx <= bound; dx++) {
            int px = cx + dx, py = cy + dy;
            if (px < 0 || px >= W || py < 0 || py >= H) continue;
            float sx_f = dx * ca + dy * sa;
            float sy_f = -dx * sa + dy * ca;
            if (sx_f < -half || sx_f >= half) continue;
            if (sy_f < -half || sy_f >= half) continue;
            int img_x = (int)(sx_f + half);
            int img_y = (int)(sy_f + half);
            if (img_x < 0 || img_x >= size || img_y < 0 || img_y >= size) continue;
            int src_x = img_x * img->width / size;
            int src_y = img_y * img->height / size;
            if (src_x < 0 || src_x >= img->width) continue;
            if (src_y < 0 || src_y >= img->height) continue;
            int src_i = (src_y * img->width + src_x) * 4;
            render::color::blend_pixel(canvas, W, H, px, py,
                render::color::RGBA(img->data[src_i], img->data[src_i+1],
                                    img->data[src_i+2], img->data[src_i+3]));
        }
    }
}

inline void draw_sparkles(std::vector<uint8_t>& canvas, int W, int H, int count)
{
    for (int i = 0; i < count; i++) {
        int x = ri(0, W - 1);
        int y = ri(0, H - 1);
        int rr = (int)rf(0.0f, 1.5f);
        unsigned char a = (unsigned char)(rf(0.0f, 0.7f) * 255);
        for (int py = y - rr; py <= y + rr; py++) {
            for (int px = x - rr; px <= x + rr; px++) {
                int dx = px - x, dy = py - y;
                if (dx*dx + dy*dy <= rr*rr + 1)
                    render::color::blend_pixel(canvas, W, H, px, py,
                        render::color::RGBA(255, 255, 255, a));
            }
        }
    }
}

inline void draw_disc_outline(std::vector<uint8_t>& canvas, int W, int H,
                              int cx, int cy, int radius)
{
    for (int s = 0; s < 360; s++) {
        float rad = s * 3.14159265f / 180.0f;
        for (int t = 0; t < 3; t++) {
            int px = cx + (int)(std::cos(rad) * (radius - t));
            int py = cy + (int)(std::sin(rad) * (radius - t));
            render::color::blend_pixel(canvas, W, H, px, py,
                render::color::RGBA(255, 255, 255, 128));
        }
    }
}

inline void render_result(
    const std::vector<int>& dice,
    int total,
    const std::string& result,
    const cpp_int& tai_total,
    const cpp_int& xiu_total,
    bool has_jackpot,
    const cpp_int& jackpot_amount,
    const std::string& out_path)
{
    const int W = 800;
    const int H = has_jackpot ? 360 : 300;
    const int cx = W / 2, cy = H / 2;
    const int lc = W / 4, rc = W * 3 / 4;
    const int radius = 100;

    std::vector<uint8_t> canvas;
    TextRenderer::init_canvas(canvas, W, H, 42, 0, 0);

    {
        // Fill nen bang LinearGradient (khong dung RadialGradient - bi bug)
        render::gradient::LinearGradient bg(0, 0, 0, (float)H);
        if (has_jackpot) {
            bg.add_stop(0.0f, render::color::from_hex("#654321"));
            bg.add_stop(0.5f, render::color::from_hex("#8B6914"));
            bg.add_stop(1.0f, render::color::from_hex("#B8860B"));
        } else {
            bg.add_stop(0.0f, render::color::from_hex("#320202"));
            bg.add_stop(0.5f, render::color::from_hex("#2A0000"));
            bg.add_stop(1.0f, render::color::from_hex("#4C0000"));
        }
        bg.fill(canvas, W, H);
    }

    {
        render::gradient::RadialGradient g(cx, cy, W / 2.0f);
        if (has_jackpot) {
            g.add_stop(0.0f, render::color::RGBA(255, 215, 0, 77));
            g.add_stop(0.5f, render::color::RGBA(255, 215, 0, 26));
            g.add_stop(1.0f, render::color::RGBA(255, 215, 0, 13));
        } else {
            g.add_stop(0.0f, render::color::RGBA(255, 255, 255, 38));
            g.add_stop(0.5f, render::color::RGBA(255, 255, 255, 13));
            g.add_stop(1.0f, render::color::RGBA(255, 255, 255, 0));
        }
        g.fill(canvas, W, H);
    }

    draw_sparkles(canvas, W, H, 70);

    TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, radius, 255, 255, 255, 51);
    draw_disc_outline(canvas, W, H, cx, cy, radius);

    const int dice_size = 40;
    // 3 diem co dinh chia goc 0/120/240 + jitter -> khong bao gio overlap
    const float base_angles[3] = { -1.5708f, 0.5236f, 2.6180f };
    std::vector<std::pair<int,int>> positions;
    for (int i = 0; i < (int)dice.size() && i < 3; i++) {
        float ang = base_angles[i] + rf(-0.35f, 0.35f);
        float dist = rf(0.55f, 0.85f) * (float)(radius - dice_size / 2);
        int px = cx + (int)(std::cos(ang) * dist);
        int py = cy + (int)(std::sin(ang) * dist);
        positions.push_back({px, py});

        std::string key = "dice_" + std::to_string(dice[i]);
        ImageRGBA* im = AssetManager::get(key);
        if (!im) im = AssetManager::load(key, "assets/repo/taixiu/" + key + ".png");
        if (im && im->valid()) {
            float rot = rf(0.0f, 6.2831853f);
            draw_dice_rotated(canvas, W, H, im, px, py, dice_size, rot);
        }
    }

    bool is_tai = (result == "tai");
    unsigned char r1, g1, b1, r2, g2, b2;
    if (is_tai) { r1=255; g1=0;   b1=0;   r2=235; g2=21;  b2=66;  }
    else        { r1=0;   g1=255; b1=255; r2=0;   g2=206; b2=209; }

    std::string rtext = is_tai ? "T\u00C0I" : "X\u1EC8U";
    text_efx(canvas, W, H, cx, cy - radius + 30, 48, r1, g1, b1, r2, g2, b2, rtext, 4);

    unsigned char wt1, wg1, wb1, wt2, wg2, wb2;
    unsigned char lt1, lg1, lb1, lt2, lg2, lb2;
    if (is_tai) {
        wt1=255; wg1=255; wb1=0;   wt2=255; wg2=165; wb2=0;
        lt1=169; lg1=169; lb1=169; lt2=105; lg2=105; lb2=105;
    } else {
        wt1=169; wg1=169; wb1=169; wt2=105; wg2=105; wb2=105;
        lt1=255; lg1=255; lb1=0;   lt2=255; lg2=165; lb2=0;
    }

    text_efx(canvas, W, H, lc - radius/2, cy - 20, 40, wt1, wg1, wb1, wt2, wg2, wb2,
             "T\u00E0i", 3);
    std::string tai_str = render::utils::format_number(tai_total);
    text_efx(canvas, W, H, lc - radius/2, cy + 20, 34, wt1, wg1, wb1, wt2, wg2, wb2,
             tai_str, 2);

    text_efx(canvas, W, H, rc + radius/2, cy - 20, 40, lt1, lg1, lb1, lt2, lg2, lb2,
             "X\u1EC9u", 3);
    std::string xiu_str = render::utils::format_number(xiu_total);
    text_efx(canvas, W, H, rc + radius/2, cy + 20, 34, lt1, lg1, lb1, lt2, lg2, lb2,
             xiu_str, 2);

    std::string total_str = std::to_string(total);
    text_efx(canvas, W, H, cx, cy + radius/2 + 45, 40, r1, g1, b1, r2, g2, b2, total_str, 4);

    if (has_jackpot) {
        std::string jtext = "N\u1ED4 H\u0168: " + render::utils::format_number(jackpot_amount) + " VN\u0110";
        text_efx(canvas, W, H, cx, H - 25, 34, 255, 215, 0, 255, 248, 220, jtext, 3);
    }

    TextRenderer::save_canvas(canvas, W, H, out_path);
}

inline void render_waiting(
    int remaining_sec,
    const cpp_int& tai_total,
    const cpp_int& xiu_total,
    const std::string& out_path)
{
    const int W = 800, H = 300;
    const int cx = W/2, cy = H/2;
    const int lc = W/4, rc = W*3/4;
    const int radius = 100;

    std::vector<uint8_t> canvas;
    TextRenderer::init_canvas(canvas, W, H, 42, 0, 0);

    render::gradient::LinearGradient bg(0, 0, 0, (float)H);
    bg.add_stop(0.0f, render::color::from_hex("#320202"));
    bg.add_stop(0.5f, render::color::from_hex("#2A0000"));
    bg.add_stop(1.0f, render::color::from_hex("#4C0000"));
    bg.fill(canvas, W, H);

    render::gradient::RadialGradient lg(cx, cy, W/2.0f);
    lg.add_stop(0.0f, render::color::RGBA(255,255,255,38));
    lg.add_stop(0.5f, render::color::RGBA(255,255,255,13));
    lg.add_stop(1.0f, render::color::RGBA(255,255,255,0));
    lg.fill(canvas, W, H);

    draw_sparkles(canvas, W, H, 70);

    TextRenderer::draw_circle_filled(canvas, W, H, cx, cy, radius, 100, 100, 100, 179);
    draw_disc_outline(canvas, W, H, cx, cy, radius);

    std::string sec_str = std::to_string(remaining_sec);
    text_efx(canvas, W, H, cx, cy, 70, 255, 255, 255, 221, 221, 221, sec_str, 4);

    text_efx(canvas, W, H, lc - radius/2, cy - 28, 48, 255, 153, 153, 255, 51, 51,
             "T\u00E0i", 3);
    std::string tt = render::utils::format_number(tai_total);
    text_efx(canvas, W, H, lc - radius/2, cy + 28, 40, 255, 153, 153, 255, 51, 51,
             tt, 2);

    text_efx(canvas, W, H, rc + radius/2, cy - 28, 48, 224, 255, 255, 135, 206, 235,
             "X\u1EC9u", 3);
    std::string xt = render::utils::format_number(xiu_total);
    text_efx(canvas, W, H, rc + radius/2, cy + 28, 40, 224, 255, 255, 135, 206, 235,
             xt, 2);

    TextRenderer::save_canvas(canvas, W, H, out_path);
}

} // namespace taixiu_render