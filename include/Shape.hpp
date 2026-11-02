#pragma once
#include "Color.hpp"
#include <vector>
#include <utility>

namespace render::shape {

// fill/stroke co the null (khong ve)
using ColorPtr = const render::color::RGBA*;

// Tuong duong roundRect trong shape.js (nhung ve bang scan-line cho nhanh)
void round_rect(std::vector<uint8_t>& canvas, int W, int H,
                int x, int y, int w, int h, int radius,
                ColorPtr fill, ColorPtr stroke, int stroke_width = 1);

void polygon(std::vector<uint8_t>& canvas, int W, int H,
             const std::vector<std::pair<int,int>>& points,
             ColorPtr fill, ColorPtr stroke, int stroke_width = 1);

void triangle(std::vector<uint8_t>& canvas, int W, int H,
              int x1, int y1, int x2, int y2, int x3, int y3,
              ColorPtr fill, ColorPtr stroke, int stroke_width = 1);

void ellipse(std::vector<uint8_t>& canvas, int W, int H,
             int cx, int cy, int rx, int ry,
             ColorPtr fill, ColorPtr stroke, int stroke_width = 1);

// Duong day (co thickness)
void line(std::vector<uint8_t>& canvas, int W, int H,
          int x1, int y1, int x2, int y2,
          render::color::RGBA color, int thickness = 1);

// Duong cong Bezier bac 2 (quadratic) - render bang polyline
void quadratic_curve(std::vector<uint8_t>& canvas, int W, int H,
                     int x0, int y0, int cx, int cy, int x1, int y1,
                     render::color::RGBA color,
                     int thickness = 1, int segments = 24);

// Duong cong Bezier bac 3 (cubic)
void cubic_curve(std::vector<uint8_t>& canvas, int W, int H,
                 int x0, int y0,
                 int c1x, int c1y,
                 int c2x, int c2y,
                 int x1, int y1,
                 render::color::RGBA color,
                 int thickness = 1, int segments = 32);

} // namespace render::shape