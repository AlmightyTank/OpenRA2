#include "doctest.h"
#include "gamepad_registry.hpp"
using ra2engine::GamepadRegistry;

TEST_CASE("gamepad registry: first controller wins and disconnect falls back") {
    GamepadRegistry pads;
    CHECK(pads.active() == GamepadRegistry::none);
    CHECK(pads.count() == 0);
    CHECK(pads.add(101));
    CHECK(pads.add(202));
    CHECK(pads.add(303));
    CHECK(pads.active() == 101);
    CHECK(pads.remove(101));
    CHECK(pads.active() == 202);
    CHECK(pads.remove(303));
    CHECK(pads.active() == 202);
    CHECK(pads.remove(202));
    CHECK(pads.active() == GamepadRegistry::none);
}

TEST_CASE("gamepad registry: duplicates, invalid and unknown removals are inert") {
    GamepadRegistry pads;
    CHECK_FALSE(pads.add(-1));
    CHECK(pads.add(0));
    CHECK_FALSE(pads.add(0));
    CHECK_FALSE(pads.remove(9));
    CHECK(pads.count() == 1);
    CHECK(pads.active() == 0);
    pads.clear();
    CHECK(pads.count() == 0);
    CHECK(pads.active() == GamepadRegistry::none);
    CHECK_FALSE(pads.remove(0));
}
