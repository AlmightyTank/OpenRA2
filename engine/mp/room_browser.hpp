#pragma once
// S0.1B: SDL-free filtering, sorting and stable selection for the Internet
// room directory. The simulation and ra2lobbyd wire protocol are unchanged.
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

#include "mp/server_link.hpp"

namespace ra2mp {

enum class RoomBrowseFilter { All, Open, Joinable };

inline std::string room_lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline std::string room_rules_hex(std::uint32_t crc) {
    char buf[9] = {};
    std::snprintf(buf, sizeof buf, "%08x", crc);
    return buf;
}

inline bool room_open(const ServerRoom& r) {
    return !r.started && r.max >= 2 && r.max <= 8 && r.players >= 1 && r.players < r.max;
}

inline bool room_compatible(const ServerRoom& r, std::uint32_t sim, const std::string& rules_hex) {
    return r.sim == sim && room_lower(r.rules) == room_lower(rules_hex);
}

inline bool room_joinable(const ServerRoom& r, std::uint32_t sim, const std::string& rules_hex) {
    return room_open(r) && room_compatible(r, sim, rules_hex);
}

inline std::string room_join_reason(const ServerRoom& r, std::uint32_t sim, const std::string& rules_hex) {
    if (r.started) return "That match has already started.";
    if (r.max < 2 || r.max > 8 || r.players < 1 || r.players >= r.max)
        return "That match has no available slots.";
    if (r.sim != sim) return "That match uses a different engine build.";
    if (room_lower(r.rules) != room_lower(rules_hex))
        return "That match uses different game data.";
    return {};
}

inline bool room_query_matches(const ServerRoom& r, const std::string& query) {
    const std::string q = room_lower(query);
    if (q.empty()) return true;
    return room_lower(r.name).find(q) != std::string::npos ||
           room_lower(r.map).find(q) != std::string::npos ||
           room_lower(r.host).find(q) != std::string::npos ||
           room_lower(r.code).find(q) != std::string::npos;
}

// All: any public server room. Open: waiting and has a free seat. Joinable:
// Open + matching engine simulation version and effective game data. Whether
// a host enables experimental cross-platform play is decided by the in-room
// host; no cross-platform availability claim is made here.
inline std::vector<std::size_t> browse_rooms(const std::vector<ServerRoom>& rooms,
                                             const std::string& query,
                                             RoomBrowseFilter filter,
                                             std::uint32_t sim,
                                             const std::string& rules_hex) {
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < rooms.size(); ++i) {
        const ServerRoom& r = rooms[i];
        if (!room_query_matches(r, query)) continue;
        if (filter == RoomBrowseFilter::Open && !room_open(r)) continue;
        if (filter == RoomBrowseFilter::Joinable && !room_joinable(r, sim, rules_hex)) continue;
        indices.push_back(i);
    }
    // Available, compatible matches first. Stable tie-breakers make the
    // directory predictable even if the server's map iteration order changes.
    std::sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
        const ServerRoom& lhs = rooms[a];
        const ServerRoom& rhs = rooms[b];
        const int rank_a = room_joinable(lhs, sim, rules_hex) ? 0 :
                           room_open(lhs) ? 1 : !lhs.started ? 2 : 3;
        const int rank_b = room_joinable(rhs, sim, rules_hex) ? 0 :
                           room_open(rhs) ? 1 : !rhs.started ? 2 : 3;
        if (rank_a != rank_b) return rank_a < rank_b;
        const std::string na = room_lower(lhs.name), nb = room_lower(rhs.name);
        if (na != nb) return na < nb;
        return lhs.code < rhs.code;
    });
    return indices;
}

// The selected room is identified by code, never by a volatile list index.
inline int find_browsed_room(const std::vector<ServerRoom>& rooms,
                            const std::vector<std::size_t>& indices,
                            const std::string& code) {
    if (code.empty()) return -1;
    for (std::size_t i = 0; i < indices.size(); ++i)
        if (indices[i] < rooms.size() && rooms[indices[i]].code == code)
            return static_cast<int>(i);
    return -1;
}

} // namespace ra2mp
