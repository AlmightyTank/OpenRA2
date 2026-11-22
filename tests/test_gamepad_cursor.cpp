#include "doctest.h"
#include "gamepad_cursor.hpp"
using ra2engine::GamepadCursor;

TEST_CASE("gamepad cursor: signed axes, symmetric deadzone") {
    CHECK(GamepadCursor::axis(0) == doctest::Approx(0.0f));
    CHECK(GamepadCursor::axis(8000) == doctest::Approx(0.0f));
    CHECK(GamepadCursor::axis(-8000) == doctest::Approx(0.0f));
    CHECK(GamepadCursor::axis(32767) == doctest::Approx(1.0f));
    CHECK(GamepadCursor::axis(-32768) == doctest::Approx(-1.0f));
    CHECK(GamepadCursor::axis(12000) > 0.0f);
    CHECK(GamepadCursor::axis(-12000) < 0.0f);
}

TEST_CASE("gamepad cursor: right stick moves pointer only") {
    GamepadCursor c;
    c.reset(1280,720);
    CHECK_FALSE(c.tick(1000,0,0,32767,0).cursor_moved);
    const auto m = c.tick(1020,0,0,32767,0);
    CHECK(m.cursor_moved);
    CHECK(c.x() > 640);
    CHECK(c.y() == 360);
    CHECK(m.pan_x == 0);
    CHECK(m.pan_y == 0);
}

TEST_CASE("gamepad cursor: left stick pans camera only") {
    GamepadCursor c;
    c.reset(1280,720);
    c.tick(1000,32767,-32768,0,0);
    const auto m = c.tick(1020,32767,-32768,0,0);
    CHECK(m.pan_x > 0);
    CHECK(m.pan_y < 0);
    CHECK_FALSE(m.cursor_moved);
    CHECK(c.x() == 640);
    CHECK(c.y() == 360);
}

TEST_CASE("gamepad cursor: pointer bounded after stick hold and resizing") {
    GamepadCursor c;
    c.reset(400,240);
    c.tick(1000,0,0,32767,-32768);
    for (int i=1;i<=300;++i) c.tick(1000+static_cast<uint32_t>(i*16),0,0,32767,-32768);
    CHECK(c.x() == 375);
    CHECK(c.y() == 24);
    c.resize(100,80);
    CHECK(c.x() == 75);
    CHECK(c.y() == 24);
    c.sync_pointer(-100,10000);
    CHECK(c.x() == 24);
    CHECK(c.y() == 55);
}

TEST_CASE("gamepad cursor: suspend discards stale time and residual movement") {
    GamepadCursor c;
    c.reset(1280,720);
    c.tick(1000,32767,0,32767,0);
    const auto before = c.tick(1010,32767,0,32767,0);
    CHECK(before.pan_x > 0);
    const int old_x = c.x();
    c.suspend(100000);
    const auto held = c.tick(100001,0,0,0,0);
    CHECK_FALSE(held.cursor_moved);
    CHECK(held.pan_x == 0);
    CHECK(held.pan_y == 0);
    CHECK(c.x() == old_x);
}

TEST_CASE("gamepad cursor: extreme frame gaps are capped") {
    GamepadCursor c;
    c.reset(1280,720);
    c.tick(1000,32767,0,0,0);
    const auto m = c.tick(200000,32767,0,0,0);
    CHECK(m.pan_x > 0);
    CHECK(m.pan_x <= 33);
}

TEST_CASE("gamepad cursor: fractional motion eventually advances") {
    GamepadCursor c;
    c.reset(1280,720);
    c.tick(1000,0,0,8500,0);
    for (int i=1;i<=250;++i) c.tick(1000+static_cast<uint32_t>(10*i),0,0,8500,0);
    CHECK(c.x() > 640);
}
