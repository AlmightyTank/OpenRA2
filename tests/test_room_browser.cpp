// S0.1B: pure room-directory logic tests (no SDL, game assets or network).
#include "doctest.h"
#include "mp/room_browser.hpp"

using namespace ra2mp;

namespace {
ServerRoom room(const char* code, const char* name, const char* map,
                int players, int max, bool started = false,
                std::uint32_t sim = 42, const char* rules = "deadbeef",
                const char* host = "Player") {
    ServerRoom r;
    r.code = code;
    r.name = name;
    r.map = map;
    r.players = players;
    r.max = max;
    r.started = started;
    r.sim = sim;
    r.rules = rules;
    r.host = host;
    return r;
}
}

TEST_CASE("online browser: exact engine and case-insensitive game data compatibility") {
    const auto a = room("A23456", "Ready", "Egypt", 2, 4);
    CHECK(room_open(a));
    CHECK(room_compatible(a, 42, "DEADBEEF"));
    CHECK(room_joinable(a, 42, "deadbeef"));
    CHECK_FALSE(room_compatible(a, 43, "deadbeef"));
    CHECK_FALSE(room_compatible(a, 42, "cafebabe"));
    CHECK(room_join_reason(a, 42, "deadbeef").empty());
    CHECK(room_join_reason(a, 43, "deadbeef") == "That match uses a different engine build.");
    CHECK(room_join_reason(a, 42, "cafebabe") == "That match uses different game data.");
    CHECK(room_rules_hex(0) == "00000000");
    CHECK(room_rules_hex(0xDEADBEEF) == "deadbeef");
}

TEST_CASE("online browser: waiting/open/joinable states and reasons") {
    auto a = room("A23456", "Ready", "Egypt", 2, 4);
    CHECK(room_open(a));
    a.players = a.max;
    CHECK_FALSE(room_open(a));
    CHECK_FALSE(room_joinable(a, 42, "deadbeef"));
    CHECK(room_join_reason(a, 42, "deadbeef") == "That match has no available slots.");
    a.players = 1;
    a.started = true;
    CHECK_FALSE(room_open(a));
    CHECK(room_join_reason(a, 42, "deadbeef") == "That match has already started.");
    a.started = false;
    a.max = 9;
    CHECK_FALSE(room_open(a));
    a.max = 4;
    a.players = 0;
    CHECK_FALSE(room_open(a));
}

TEST_CASE("online browser: search across host, name, map and room code") {
    const auto a = room("K7Q2MX", "Friday Battle", "Tour of Egypt", 1, 4, false, 42, "deadbeef", "Tanya");
    CHECK(room_query_matches(a, ""));
    CHECK(room_query_matches(a, "FRIDAY"));
    CHECK(room_query_matches(a, "eGyPt"));
    CHECK(room_query_matches(a, "tanya"));
    CHECK(room_query_matches(a, "k7q2mx"));
    CHECK_FALSE(room_query_matches(a, "Kane"));
}

TEST_CASE("online browser: deterministic joinable-first ordering with filters") {
    const std::vector<ServerRoom> rooms = {
        room("A23456", "Zulu", "Map", 2, 2),                  // full
        room("B23456", "Alpha", "Map", 1, 4),                 // joinable
        room("C23456", "Beta", "Map", 1, 4, false, 98),      // open, mismatch
        room("D23456", "Gamma", "Map", 2, 4, true),          // started
        room("E23456", "Charlie", "Map", 1, 4)               // joinable
    };
    const auto all = browse_rooms(rooms, "", RoomBrowseFilter::All, 42, "deadbeef");
    REQUIRE(all.size() == 5);
    CHECK(all[0] == 1);
    CHECK(all[1] == 4);
    CHECK(all[2] == 2);
    CHECK(all[3] == 0);
    CHECK(all[4] == 3);
    const auto open = browse_rooms(rooms, "", RoomBrowseFilter::Open, 42, "deadbeef");
    REQUIRE(open.size() == 3);
    CHECK(open[0] == 1);
    CHECK(open[1] == 4);
    CHECK(open[2] == 2);
    const auto joinable = browse_rooms(rooms, "", RoomBrowseFilter::Joinable, 42, "deadbeef");
    REQUIRE(joinable.size() == 2);
    CHECK(joinable[0] == 1);
    CHECK(joinable[1] == 4);
    const auto searched = browse_rooms(rooms, "beta", RoomBrowseFilter::All, 42, "deadbeef");
    REQUIRE(searched.size() == 1);
    CHECK(searched[0] == 2);
}

TEST_CASE("online browser: selection survives reorder but not a removed room") {
    std::vector<ServerRoom> rooms = {
        room("K7Q2MX", "Three", "Map", 1, 4),
        room("K7Q2MY", "One", "Map", 1, 4),
        room("K7Q2MZ", "Two", "Map", 1, 4)
    };
    auto indices = browse_rooms(rooms, "", RoomBrowseFilter::All, 42, "deadbeef");
    int selected = find_browsed_room(rooms, indices, "K7Q2MZ");
    REQUIRE(selected >= 0);
    CHECK(rooms[indices[static_cast<std::size_t>(selected)]].code == "K7Q2MZ");
    std::reverse(rooms.begin(), rooms.end());
    indices = browse_rooms(rooms, "", RoomBrowseFilter::All, 42, "deadbeef");
    selected = find_browsed_room(rooms, indices, "K7Q2MZ");
    REQUIRE(selected >= 0);
    CHECK(rooms[indices[static_cast<std::size_t>(selected)]].code == "K7Q2MZ");
    indices = browse_rooms(rooms, "One", RoomBrowseFilter::All, 42, "deadbeef");
    CHECK(find_browsed_room(rooms, indices, "K7Q2MZ") == -1);
    CHECK(find_browsed_room(rooms, indices, "") == -1);
}

TEST_CASE("online browser: cross-platform tag does not imply join permission") {
    auto a = room("A23456", "Android", "Map", 1, 4);
    a.plat = 2;
    // Host in-room cross-platform option is not advertised in ServerRoom.
    // Matching sim/data is only a preliminary browser compatibility check.
    CHECK(room_joinable(a, 42, "deadbeef"));
}
