#pragma once
// V0.1E: Pure local controller navigation state; never writes to World.
#include <cstdint>
#include <vector>

namespace ra2engine {
struct GamepadNavEdges {
    bool up=false, down=false, left=false, right=false;
    bool accept=false, cancel=false, alternate=false, toggle=false, start=false;
};
class GamepadNav {
public:
    static constexpr uint32_t A=1u<<0, B=1u<<1, X=1u<<2;
    static constexpr uint32_t Start=1u<<6, RightBumper=1u<<10;
    static constexpr uint32_t Up=1u<<11, Down=1u<<12, Left=1u<<13, Right=1u<<14;
    void reset(uint32_t buttons) { last_=buttons; }
    GamepadNavEdges sample(uint32_t b) {
        const uint32_t pressed=b & ~last_;
        last_=b;
        GamepadNavEdges e;
        e.up=(pressed & Up)!=0;
        e.down=(pressed & Down)!=0;
        e.left=(pressed & Left)!=0;
        e.right=(pressed & Right)!=0;
        e.accept=(pressed & A)!=0;
        e.cancel=(pressed & B)!=0;
        e.alternate=(pressed & X)!=0;
        e.toggle=(pressed & RightBumper)!=0;
        e.start=(pressed & Start)!=0;
        return e;
    }
private:
    uint32_t last_=0;
};

// Wrap around and skip disabled shell buttons. Returns -1 for an empty or
// entirely disabled page, and never moves off a valid enabled target.
inline int gamepad_menu_step(const std::vector<bool>& enabled, int current, int direction) {
    if(enabled.empty()) return -1;
    const int n=static_cast<int>(enabled.size());
    if(direction==0 && current>=0 && current<n && enabled[static_cast<std::size_t>(current)])
        return current;
    const int step=direction<0?-1:1;
    const int origin=current>=0 && current<n ? current : (step>0?-1:0);
    for(int i=1;i<=n;++i) {
        const int index=((origin+step*i)%n+n)%n;
        if(enabled[static_cast<std::size_t>(index)]) return index;
    }
    return -1;
}
} // namespace ra2engine
