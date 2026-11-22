#include "doctest.h"
#include "gamepad_navigation.hpp"
using ra2engine::GamepadNav;
using ra2engine::gamepad_menu_step;
TEST_CASE("gamepad menu: dpad edges fire once per press") {
    GamepadNav n; n.reset(0);
    CHECK(n.sample(GamepadNav::Down).down);
    CHECK_FALSE(n.sample(GamepadNav::Down).down);
    n.sample(0);
    CHECK(n.sample(GamepadNav::Down).down);
}
TEST_CASE("gamepad menu: connecting with button held cannot activate") {
    GamepadNav n; n.reset(GamepadNav::A|GamepadNav::Start);
    auto a=n.sample(GamepadNav::A|GamepadNav::Start);
    CHECK_FALSE(a.accept); CHECK_FALSE(a.start);
    n.sample(0);
    CHECK(n.sample(GamepadNav::A).accept);
}
TEST_CASE("gamepad menu: command bindings independent and no duplicate") {
    GamepadNav n; n.reset(0);
    auto a=n.sample(GamepadNav::A|GamepadNav::B|GamepadNav::X|GamepadNav::RightBumper);
    CHECK(a.accept); CHECK(a.cancel); CHECK(a.alternate); CHECK(a.toggle);
    CHECK_FALSE(n.sample(GamepadNav::A|GamepadNav::B).accept);
}
TEST_CASE("gamepad menu: wrap and skip unavailable entries") {
    const std::vector<bool> buttons={false,true,false,true};
    CHECK(gamepad_menu_step(buttons,-1,1)==1);
    CHECK(gamepad_menu_step(buttons,1,1)==3);
    CHECK(gamepad_menu_step(buttons,3,1)==1);
    CHECK(gamepad_menu_step(buttons,1,-1)==3);
}
TEST_CASE("gamepad menu: no focus with disabled or empty rows") {
    CHECK(gamepad_menu_step({},-1,1)==-1);
    CHECK(gamepad_menu_step({false,false},-1,1)==-1);
    CHECK(gamepad_menu_step({true},0,0)==0);
}
