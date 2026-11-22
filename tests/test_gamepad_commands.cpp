#include "doctest.h"
#include "gamepad_commands.hpp"
using ra2engine::GamepadCommand;
using ra2engine::GamepadCommandMapper;
using Kind = GamepadCommand::Kind;

TEST_CASE("gamepad commands: A selects or orders once per press") {
    GamepadCommandMapper m; m.reset(0,-32768,-32768);
    auto a = m.sample(GamepadCommandMapper::kA,-32768,-32768,true);
    REQUIRE(a.size()==1); CHECK(a[0].kind==Kind::ClickLeft);
    CHECK(m.sample(GamepadCommandMapper::kA,-32768,-32768,true).empty());
    m.sample(0,-32768,-32768,true);
    CHECK(m.sample(GamepadCommandMapper::kA,-32768,-32768,true).size()==1);
}
TEST_CASE("gamepad commands: B right cancels once per press") {
    GamepadCommandMapper m; m.reset(0,0,0);
    auto v=m.sample(GamepadCommandMapper::kB,0,0,true);
    REQUIRE(v.size()==1); CHECK(v[0].kind==Kind::ClickRight);
    CHECK(m.sample(GamepadCommandMapper::kB,0,0,true).empty());
}
TEST_CASE("gamepad commands: RT and A together issue only one click") {
    GamepadCommandMapper m; m.reset(0,-32768,-32768);
    CHECK(m.sample(0,-32768,11000,true).empty());
    auto v=m.sample(GamepadCommandMapper::kA,-32768,25000,true);
    REQUIRE(v.size()==1); CHECK(v[0].kind==Kind::ClickLeft);
    CHECK(m.sample(GamepadCommandMapper::kA,-32768,26000,true).empty());
    m.sample(0,-32768,0,true);
    CHECK(m.sample(0,-32768,32767,true).size()==1);
}
TEST_CASE("gamepad commands: LB adds Shift to click and selection rectangle") {
    GamepadCommandMapper m; m.reset(0,0,0);
    auto a=m.sample(GamepadCommandMapper::kA|GamepadCommandMapper::kLeftShoulder,0,0,true);
    REQUIRE(a.size()==1); CHECK(a[0].shift);
    m.sample(0,0,0,true);
    auto b=m.sample(GamepadCommandMapper::kLeftShoulder,25000,0,true);
    REQUIRE(b.size()==1); CHECK(b[0].kind==Kind::BeginBox); CHECK(b[0].shift);
    CHECK(m.box_active());
    CHECK(m.sample(GamepadCommandMapper::kLeftShoulder,25000,0,true).empty());
    auto e=m.sample(GamepadCommandMapper::kLeftShoulder,0,0,true);
    REQUIRE(e.size()==1); CHECK(e[0].kind==Kind::EndBox);
    CHECK_FALSE(m.box_active());
}
TEST_CASE("gamepad commands: no late box when LT crosses sidebar boundary") {
    GamepadCommandMapper m; m.reset(0,0,0);
    CHECK(m.sample(0,32000,0,false).empty());
    CHECK(m.sample(0,32000,0,true).empty());
    m.sample(0,0,0,true);
    CHECK(m.sample(0,32000,0,true)[0].kind==Kind::BeginBox);
}
TEST_CASE("gamepad commands: B cancels live rectangle without committing") {
    GamepadCommandMapper m; m.reset(0,0,0);
    m.sample(0,32000,0,true);
    auto e=m.sample(GamepadCommandMapper::kB,32000,0,true);
    REQUIRE(e.size()==1); CHECK(e[0].kind==Kind::CancelBox);
    CHECK_FALSE(m.box_active());
    CHECK(m.sample(GamepadCommandMapper::kB,32000,0,true).empty());
    m.sample(0,0,0,true);
    CHECK(m.sample(GamepadCommandMapper::kB,0,0,true)[0].kind==Kind::ClickRight);
}
TEST_CASE("gamepad commands: pointer takeover suppresses held box") {
    GamepadCommandMapper m; m.reset(0,0,0);
    m.sample(0,32000,0,true);
    CHECK(m.cancel_box()); CHECK_FALSE(m.cancel_box());
    CHECK(m.sample(0,32000,0,true).empty());
    m.sample(0,0,0,true);
    CHECK(m.sample(0,32000,0,true)[0].kind==Kind::BeginBox);
}
TEST_CASE("gamepad commands: held buttons at resume never replay") {
    GamepadCommandMapper m;
    m.reset(GamepadCommandMapper::kA,30000,30000);
    CHECK(m.sample(GamepadCommandMapper::kA,30000,30000,true).empty());
    m.sample(0,0,0,true);
    CHECK(m.sample(GamepadCommandMapper::kA,0,0,true).size()==1);
    m.reset(0,30000,0);
    CHECK(m.sample(0,30000,0,true).empty());
    m.sample(0,0,0,true);
    CHECK(m.sample(0,30000,0,true)[0].kind==Kind::BeginBox);
}
TEST_CASE("gamepad commands: sticks alone create no pointer clicks") {
    GamepadCommandMapper m; m.reset(0,0,0);
    for(int i=0;i<100;++i) CHECK(m.sample(0,0,0,true).empty());
}
