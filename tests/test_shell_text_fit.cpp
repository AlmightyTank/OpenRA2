#include "doctest.h"
#include "shell_text_fit.hpp"
using ra2engine::fit_shell_text;

TEST_CASE("shell readable labels keep large fractional scale instead of dropping to 1x") {
    // Logical 156x42 SDBTNANM button at 1.5x gives 234x63 pixels.
    const auto fit = fit_shell_text(115, 16, 700, 290, 234, 63, 2.0, 10, 5);
    CHECK(fit.scale > 1.8);
    CHECK(fit.scale < 2.0);
    CHECK(fit.width <= 234 - 20 + 0.001);
    CHECK(fit.x >= 710.0 - 0.01);
    CHECK(fit.x + fit.width <= 924.0 + 0.01);
}

TEST_CASE("shell readable labels respect both horizontal and vertical padding") {
    const auto fit = fit_shell_text(70, 16, 10, 40, 100, 23, 2.0, 5, 3);
    CHECK(fit.width <= 90.001);
    CHECK(fit.height <= 17.001);
    CHECK(fit.y >= 43.0 - 0.01);
    CHECK(fit.y + fit.height <= 60.0 + 0.01);
}

TEST_CASE("shell readable text remains centered for headers and dialog controls") {
    const auto fit = fit_shell_text(60, 12, 100, 200, 150, 30, 1.5, 4, 2);
    CHECK(fit.scale == doctest::Approx(1.5));
    CHECK(fit.x + fit.width / 2.0 == doctest::Approx(175.0));
    CHECK(fit.y + fit.height / 2.0 == doctest::Approx(215.0));
}

TEST_CASE("shell readable label never grows beyond requested scale") {
    auto a = fit_shell_text(100, 16, 0, 0, 500, 200, 1.0);
    CHECK(a.scale == doctest::Approx(1.0));
    auto b = fit_shell_text(100, 16, 0, 0, 100, 20, 1.0, 8, 3);
    CHECK(b.width <= 84.001);
    CHECK(b.height <= 14.001);
}
