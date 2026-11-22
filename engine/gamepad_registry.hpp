#pragma once
// V0.1B: SDL-independent connected-controller selection policy.
// The first connected supported controller owns input. On removal, ownership
// moves to the next still-connected device. Duplicate additions are ignored.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ra2engine {
class GamepadRegistry {
public:
    using Id = int32_t;
    static constexpr Id none = -1;
    Id active() const { return active_; }
    std::size_t count() const { return ids_.size(); }
    bool contains(Id id) const {
        return std::find(ids_.begin(), ids_.end(), id) != ids_.end();
    }
    bool add(Id id) {
        if (id < 0 || contains(id)) return false;
        ids_.push_back(id);
        if (active_ == none) active_ = id;
        return true;
    }
    bool remove(Id id) {
        const auto it = std::find(ids_.begin(), ids_.end(), id);
        if (it == ids_.end()) return false;
        ids_.erase(it);
        if (active_ == id) active_ = ids_.empty() ? none : ids_.front();
        return true;
    }
    void clear() { ids_.clear(); active_ = none; }
private:
    std::vector<Id> ids_;
    Id active_ = none;
};
} // namespace ra2engine
