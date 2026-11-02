#pragma once
#include <cstdint>
#include <vector>

namespace render::background {

// Tuong duong drawAnimatedBackground trong color.js
void animated(std::vector<uint8_t>& canvas, int W, int H);

// drawGradientGreenBackground
void gradient_green(std::vector<uint8_t>& canvas, int W, int H);

// drawGradientGreenNextground
void gradient_green_next(std::vector<uint8_t>& canvas, int W, int H);

// drawDynamicGradientBackground
void dynamic_gradient(std::vector<uint8_t>& canvas, int W, int H);

} // namespace render::background