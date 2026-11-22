#pragma once
// SDL-free analog cursor and camera motion. Local input only; no simulation writes.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ra2engine {

class GamepadCursor {
public:
    struct Motion { bool cursor_moved = false; int pan_x = 0; int pan_y = 0; };

    void reset(int width, int height) {
        width_ = std::max(1, width);
        height_ = std::max(1, height);
        x_ = width_ / 2; y_ = height_ / 2;
        cursor_fx_ = cursor_fy_ = pan_fx_ = pan_fy_ = 0.0f;
        last_ms_ = 0; started_ = false;
    }
    void resize(int width, int height) {
        width_ = std::max(1, width); height_ = std::max(1, height);
        x_ = clamp_x(x_); y_ = clamp_y(y_);
    }
    void sync_pointer(int x, int y) {
        x_ = clamp_x(x); y_ = clamp_y(y);
        cursor_fx_ = cursor_fy_ = 0.0f;
    }
    int x() const { return x_; }
    int y() const { return y_; }

    // Keep cursor position while a modal page owns input. Discard residual
    // fractions and stale elapsed time so resuming cannot cause drift or jumps.
    void suspend(uint32_t now_ms) {
        last_ms_ = now_ms;
        started_ = true;
        cursor_fx_ = cursor_fy_ = pan_fx_ = pan_fy_ = 0.0f;
    }

    // SDL axes are signed 16-bit; preserve the negative endpoint.
    static float axis(int value, int deadzone = 8000) {
        value = std::max(-32768, std::min(32767, value));
        deadzone = std::max(0, std::min(32766, deadzone));
        const int magnitude = std::abs(value == -32768 ? -32767 : value);
        if (magnitude <= deadzone) return 0.0f;
        const float amount = static_cast<float>(magnitude - deadzone) / (32767 - deadzone);
        return std::copysign(std::min(1.0f, amount), static_cast<float>(value));
    }

    Motion tick(uint32_t now_ms, int left_x, int left_y, int right_x, int right_y) {
        Motion m;
        if (!started_) { started_ = true; last_ms_ = now_ms; return m; }
        const uint32_t elapsed = now_ms - last_ms_;
        last_ms_ = now_ms;
        if (!elapsed) return m;
        const float dt = std::min(elapsed, uint32_t{50}) / 1000.0f;
        const float cx = axis(right_x), cy = axis(right_y);
        const float px = axis(left_x), py = axis(left_y);
        const int old_x = x_, old_y = y_;
        cursor_fx_ += cx * 750.0f * dt;
        cursor_fy_ += cy * 750.0f * dt;
        const int dx = static_cast<int>(cursor_fx_);
        const int dy = static_cast<int>(cursor_fy_);
        cursor_fx_ -= dx; cursor_fy_ -= dy;
        x_ = clamp_x(x_ + dx); y_ = clamp_y(y_ + dy);
        if ((cx < 0 && x_ == min_x()) || (cx > 0 && x_ == max_x())) cursor_fx_ = 0;
        if ((cy < 0 && y_ == min_y()) || (cy > 0 && y_ == max_y())) cursor_fy_ = 0;
        m.cursor_moved = x_ != old_x || y_ != old_y;
        // Pan follows the touch-pan convention: map follows left-stick direction.
        pan_fx_ += px * 650.0f * dt;
        pan_fy_ += py * 650.0f * dt;
        m.pan_x = static_cast<int>(pan_fx_);
        m.pan_y = static_cast<int>(pan_fy_);
        pan_fx_ -= m.pan_x; pan_fy_ -= m.pan_y;
        return m;
    }

private:
    int min_x() const { return std::min(24, width_ - 1); }
    int min_y() const { return std::min(24, height_ - 1); }
    int max_x() const { return std::max(min_x(), width_ - 25); }
    int max_y() const { return std::max(min_y(), height_ - 25); }
    int clamp_x(int x) const { return std::max(min_x(), std::min(max_x(), x)); }
    int clamp_y(int y) const { return std::max(min_y(), std::min(max_y(), y)); }
    int width_ = 1, height_ = 1, x_ = 0, y_ = 0;
    float cursor_fx_ = 0, cursor_fy_ = 0, pan_fx_ = 0, pan_fy_ = 0;
    uint32_t last_ms_ = 0;
    bool started_ = false;
};

} // namespace ra2engine
