#include "Shape.hpp"
#include <algorithm>
#include <cmath>
#include <climits>

namespace render::shape {

using render::color::RGBA;
using render::color::blend_pixel;

static bool inside_round_rect(int px, int py, int x, int y, int w, int h, int radius) {
    if (px < x || px >= x + w || py < y || py >= y + h) return false;
    if (radius <= 0) return true;
    int cx, cy;
    if (px < x + radius && py < y + radius) { cx = x + radius; cy = y + radius; }
    else if (px >= x + w - radius && py < y + radius) { cx = x + w - radius - 1; cy = y + radius; }
    else if (px < x + radius && py >= y + h - radius) { cx = x + radius; cy = y + h - radius - 1; }
    else if (px >= x + w - radius && py >= y + h - radius) { cx = x + w - radius - 1; cy = y + h - radius - 1; }
    else return true;
    int dx = px - cx, dy = py - cy;
    return (dx*dx + dy*dy) <= (radius * radius);
}

void round_rect(std::vector<uint8_t>& canvas, int W, int H,
                int x, int y, int w, int h, int radius,
                ColorPtr fill, ColorPtr stroke, int stroke_width) {
    if (radius < 0) radius = 0;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (fill) {
        for (int py = y; py < y + h; py++) {
            if (py < 0 || py >= H) continue;
            for (int px = x; px < x + w; px++) {
                if (px < 0 || px >= W) continue;
                if (inside_round_rect(px, py, x, y, w, h, radius))
                    blend_pixel(canvas, W, H, px, py, *fill);
            }
        }
    }
    if (stroke && stroke_width > 0) {
        for (int py = y - stroke_width; py <= y + h + stroke_width; py++) {
            if (py < 0 || py >= H) continue;
            for (int px = x - stroke_width; px <= x + w + stroke_width; px++) {
                if (px < 0 || px >= W) continue;
                if (!inside_round_rect(px, py, x, y, w, h, radius)) continue;
                bool is_edge = false;
                for (int t = 1; t <= stroke_width && !is_edge; t++) {
                    if (!inside_round_rect(px - t, py, x, y, w, h, radius) ||
                        !inside_round_rect(px + t, py, x, y, w, h, radius) ||
                        !inside_round_rect(px, py - t, x, y, w, h, radius) ||
                        !inside_round_rect(px, py + t, x, y, w, h, radius)) {
                        is_edge = true;
                    }
                }
                if (is_edge) blend_pixel(canvas, W, H, px, py, *stroke);
            }
        }
    }
}

void line(std::vector<uint8_t>& canvas, int W, int H,
          int x1, int y1, int x2, int y2, RGBA color, int thickness) {
    int dx = std::abs(x2 - x1);
    int dy = std::abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    int half = thickness / 2;
    while (true) {
        for (int ty = -half; ty <= half; ty++)
            for (int tx = -half; tx <= half; tx++)
                blend_pixel(canvas, W, H, x1 + tx, y1 + ty, color);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 <  dx) { err += dx; y1 += sy; }
    }
}

void polygon(std::vector<uint8_t>& canvas, int W, int H,
             const std::vector<std::pair<int,int>>& pts,
             ColorPtr fill, ColorPtr stroke, int stroke_width) {
    if (pts.size() < 3) {
        if (stroke) {
            for (size_t i = 0; i + 1 < pts.size(); i++)
                line(canvas, W, H, pts[i].first, pts[i].second,
                     pts[i+1].first, pts[i+1].second, *stroke, stroke_width);
        }
        return;
    }
    if (fill) {
        int min_y = INT_MAX, max_y = INT_MIN;
        for (auto& p : pts) {
            min_y = std::min(min_y, p.second);
            max_y = std::max(max_y, p.second);
        }
        std::vector<int> xs;
        for (int y = min_y; y <= max_y; y++) {
            if (y < 0 || y >= H) continue;
            xs.clear();
            for (size_t i = 0; i < pts.size(); i++) {
                auto a = pts[i];
                auto b = pts[(i + 1) % pts.size()];
                if ((a.second <= y && b.second > y) ||
                    (b.second <= y && a.second > y)) {
                    float t = (float)(y - a.second) / (float)(b.second - a.second);
                    xs.push_back((int)(a.first + t * (b.first - a.first)));
                }
            }
            std::sort(xs.begin(), xs.end());
            for (size_t i = 0; i + 1 < xs.size(); i += 2) {
                int x_start = std::max(xs[i], 0);
                int x_end   = std::min(xs[i+1], W - 1);
                for (int x = x_start; x <= x_end; x++)
                    blend_pixel(canvas, W, H, x, y, *fill);
            }
        }
    }
    if (stroke && stroke_width > 0) {
        for (size_t i = 0; i < pts.size(); i++) {
            auto a = pts[i];
            auto b = pts[(i + 1) % pts.size()];
            line(canvas, W, H, a.first, a.second, b.first, b.second,
                 *stroke, stroke_width);
        }
    }
}

void triangle(std::vector<uint8_t>& canvas, int W, int H,
              int x1, int y1, int x2, int y2, int x3, int y3,
              ColorPtr fill, ColorPtr stroke, int stroke_width) {
    polygon(canvas, W, H, {{x1,y1},{x2,y2},{x3,y3}},
            fill, stroke, stroke_width);
}

void ellipse(std::vector<uint8_t>& canvas, int W, int H,
             int cx, int cy, int rx, int ry,
             ColorPtr fill, ColorPtr stroke, int stroke_width) {
    if (rx <= 0 || ry <= 0) return;
    if (fill) {
        for (int y = cy - ry; y <= cy + ry; y++) {
            if (y < 0 || y >= H) continue;
            for (int x = cx - rx; x <= cx + rx; x++) {
                if (x < 0 || x >= W) continue;
                float dx = (float)(x - cx) / rx;
                float dy = (float)(y - cy) / ry;
                if (dx*dx + dy*dy <= 1.0f)
                    blend_pixel(canvas, W, H, x, y, *fill);
            }
        }
    }
    if (stroke && stroke_width > 0) {
        const int SEG = 128;
        for (int i = 0; i < SEG; i++) {
            float a0 = (float)i / SEG * 6.2831853f;
            float a1 = (float)(i + 1) / SEG * 6.2831853f;
            int px0 = cx + (int)(std::cos(a0) * rx);
            int py0 = cy + (int)(std::sin(a0) * ry);
            int px1 = cx + (int)(std::cos(a1) * rx);
            int py1 = cy + (int)(std::sin(a1) * ry);
            line(canvas, W, H, px0, py0, px1, py1, *stroke, stroke_width);
        }
    }
}

void quadratic_curve(std::vector<uint8_t>& canvas, int W, int H,
                     int x0, int y0, int cx, int cy, int x1, int y1,
                     RGBA color, int thickness, int segments) {
    int prev_x = x0, prev_y = y0;
    for (int i = 1; i <= segments; i++) {
        float t = (float)i / segments;
        float mt = 1 - t;
        int x = (int)(mt*mt*x0 + 2*mt*t*cx + t*t*x1);
        int y = (int)(mt*mt*y0 + 2*mt*t*cy + t*t*y1);
        line(canvas, W, H, prev_x, prev_y, x, y, color, thickness);
        prev_x = x; prev_y = y;
    }
}

void cubic_curve(std::vector<uint8_t>& canvas, int W, int H,
                 int x0, int y0, int c1x, int c1y, int c2x, int c2y, int x1, int y1,
                 RGBA color, int thickness, int segments) {
    int prev_x = x0, prev_y = y0;
    for (int i = 1; i <= segments; i++) {
        float t = (float)i / segments;
        float mt = 1 - t;
        float mt2 = mt * mt;
        float t2  = t * t;
        int x = (int)(mt2*mt*x0 + 3*mt2*t*c1x + 3*mt*t2*c2x + t2*t*x1);
        int y = (int)(mt2*mt*y0 + 3*mt2*t*c1y + 3*mt*t2*c2y + t2*t*y1);
        line(canvas, W, H, prev_x, prev_y, x, y, color, thickness);
        prev_x = x; prev_y = y;
    }
}

} // namespace render::shape