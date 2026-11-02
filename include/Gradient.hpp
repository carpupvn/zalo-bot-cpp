#pragma once
#include "Color.hpp"
#include <vector>

namespace render::gradient {

struct Stop {
    float offset = 0.0f;      // 0..1
    render::color::RGBA color;
};

// Tuong duong ctx.createLinearGradient(x0,y0,x1,y1) + addColorStop
class LinearGradient {
public:
    LinearGradient(float x0, float y0, float x1, float y1);

    void add_stop(float offset, render::color::RGBA color);
    void clear();

    // t: 0..1 doc theo vector (x0,y0)->(x1,y1)
    render::color::RGBA sample(float t) const;

    // tinh t tu toa do pixel (dua tren projection)
    render::color::RGBA sample_at(float px, float py) const;

    // To ca canvas (fill toan bo)
    void fill(std::vector<uint8_t>& canvas, int W, int H) const;

    // To hinh chu nhat
    void fill_rect(std::vector<uint8_t>& canvas, int W, int H,
                   int rx, int ry, int rw, int rh) const;

private:
    float x0_, y0_, x1_, y1_;
    std::vector<Stop> stops_;
};

// Tuong duong ctx.createRadialGradient(x,y,r0, x,y,r1) - don gian hoa con 1 ban kinh
class RadialGradient {
public:
    RadialGradient(float cx, float cy, float radius);
    void add_stop(float offset, render::color::RGBA color);
    render::color::RGBA sample(float t) const;
    void fill(std::vector<uint8_t>& canvas, int W, int H) const;

private:
    float cx_, cy_, radius_;
    std::vector<Stop> stops_;
};

// ===== Preset tu color.js =====
namespace presets {
    LinearGradient pink_white(float width);       // getPinkWhiteGradient
    LinearGradient blue_black(float width);       // getBlueBlackGradient
    LinearGradient dark(float width);             // getDarkGradient
    LinearGradient random_rainbow(float width);   // getRandomRainbowGradient
    LinearGradient random_bright(float width);    // getRandomGradient
}

} // namespace render::gradient