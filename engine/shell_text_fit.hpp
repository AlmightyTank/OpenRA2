#pragma once
// SDL-free layout for sharp, legible 12METFNT shell text. This is presentation
// only; button rectangles and input hit tests do not change.
#include <algorithm>
#include <cmath>

namespace ra2engine {
struct ShellTextFit {
    double scale = 1.0;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

// The previous main menu dropped from 2x to 1x as soon as a label was a
// pixel too wide. Retain the largest possible size (including fractional
// scales, which the renderer sharpens with its existing texture pipeline).
inline ShellTextFit fit_shell_text(int glyph_width, int glyph_height,
                                   int x, int y, int width, int height,
                                   double wanted, int pad_x = 6, int pad_y = 3) {
    const double w = std::max(1, width - 2 * std::max(0, pad_x));
    const double h = std::max(1, height - 2 * std::max(0, pad_y));
    const double gw = std::max(1, glyph_width), gh = std::max(1, glyph_height);
    const double scale = std::min({std::max(0.01, wanted), w / gw, h / gh});
    const double tw = glyph_width * scale, th = glyph_height * scale;
    return {scale, x + (width - tw) / 2.0, y + (height - th) / 2.0, tw, th};
}
} // namespace ra2engine
