#pragma once
// V0.1D: SDL-free conversion from gamepad state to established pointer gestures.
#include <cstdint>
#include <vector>
namespace ra2engine {
struct GamepadCommand {
    enum class Kind { ClickLeft, ClickRight, BeginBox, EndBox, CancelBox };
    Kind kind;
    bool shift = false;
};
class GamepadCommandMapper {
public:
    // SDL2 GameController ordinals in GamepadState.buttons (not raw HID).
    static constexpr uint32_t kA = 1u << 0, kB = 1u << 1, kLeftShoulder = 1u << 9;
    static constexpr int16_t kTriggerThreshold = 12000;
    bool box_active() const { return box_active_; }
    // No action from already-held buttons when connecting/resuming.
    void reset(uint32_t buttons, int16_t lt_axis, int16_t rt_axis) {
        previous_buttons_ = buttons;
        previous_lt_ = down(lt_axis);
        previous_rt_ = down(rt_axis);
        box_active_ = false;
        suppress_box_until_release_ = previous_lt_;
    }
    // Caller must also call Input::cancel_gesture() when true.
    bool cancel_box() {
        if (!box_active_) return false;
        box_active_ = false;
        suppress_box_until_release_ = true;
        return true;
    }
    std::vector<GamepadCommand> sample(uint32_t buttons, int16_t lt_axis,
                                       int16_t rt_axis, bool can_begin_box) {
        const bool b_edge = (buttons & kB) && !(previous_buttons_ & kB);
        const bool a_edge = (buttons & kA) && !(previous_buttons_ & kA);
        const bool lt = down(lt_axis), rt = down(rt_axis);
        const bool lt_edge = lt && !previous_lt_;
        const bool rt_edge = rt && !previous_rt_;
        const bool shift = (buttons & kLeftShoulder) != 0;
        previous_buttons_ = buttons;
        previous_lt_ = lt;
        previous_rt_ = rt;
        if (!lt) suppress_box_until_release_ = false;
        std::vector<GamepadCommand> out;
        // B wins: while dragging, discard the rectangle WITHOUT releasing
        // the left pointer into Input (which would commit the selection).
        if (b_edge) {
            if (cancel_box()) out.push_back({GamepadCommand::Kind::CancelBox, false});
            else out.push_back({GamepadCommand::Kind::ClickRight, false});
            return out;
        }
        if (box_active_) {
            if (!lt) {
                box_active_ = false;
                out.push_back({GamepadCommand::Kind::EndBox, false});
            }
            return out;
        }
        if (lt_edge && !suppress_box_until_release_ && can_begin_box) {
            box_active_ = true;
            out.push_back({GamepadCommand::Kind::BeginBox, shift});
            return out;
        }
        // A and RT map to the SAME original action-at-cursor left click.
        // A+RT in the same frame produces a single click, never two orders.
        if (a_edge || rt_edge)
            out.push_back({GamepadCommand::Kind::ClickLeft, shift});
        return out;
    }
private:
    static bool down(int16_t axis) { return axis > kTriggerThreshold; }
    uint32_t previous_buttons_ = 0;
    bool previous_lt_ = false, previous_rt_ = false;
    bool box_active_ = false, suppress_box_until_release_ = false;
};
} // namespace ra2engine
