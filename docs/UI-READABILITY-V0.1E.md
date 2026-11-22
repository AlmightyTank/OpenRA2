# OpenRA2 v0.1E — text readability and menu UI pass

## Reference and cause

The user screenshot on 2026-11-22 shows low-contrast gold labels over intense red
button artwork; Movies & Credits is especially cramped. The previous renderer
rounded to an integer magnification, then abruptly shrank by a whole step
when one label exceeded its button width.

## Changes

- Keep original SDBTNANM/MNBTTN artwork, frames, animation, hitboxes and CRT.
- Fit menu and shell-control text at fractional pixel magnifications within
  existing rectangles instead of dropping from scale 2 to 1.
- Render a cached one-source-pixel dark outline around each authentic FNT glyph.
- Lift the gold metallic palette shades so letters remain visible over red.
- Add a subtle dark red label backing inside menu and popup button faces.
- Apply the outlined font path to Options, Skirmish, lobby, shell dialogs,
  popup labels and the in-game shell pages through ShellPainter.
- Leave sim rules, controller/touch commands and network protocol unchanged.
- Preserve the separate HUD renderer, which already shadows and backs text.

## Automated checks

- tests/test_shell_text_fit.cpp tests bounded fractional scaling and centering.
- Registered in ra2assets_tests and GitHub isolated input/layout tests.
- Full engine compilation and Windows/Android/iOS package checks run in CI.
- Build success does not establish visual readability on device.

## Outstanding manual visual acceptance

Using legally obtained game data, compare native-resolution before/after
captures for Windows menu (1280x720 and resized), Android phone landscape,
Android portrait letterboxing, iPhone and iPad (after authorized signing).
Inspect all six main menu labels, particularly Movies & Credits, plus
Options, campaign, save/load, Internet/network lobby and popup labels.
Check no clipping, blurry noninteger scale, overlap, lost disabled indication
or desynchronized click targets. Inspect screen crops at 100% size.

## Packaging notes

Android is debug-signed, iOS .app is unsigned, and no proprietary RA2 game
assets are included in GitHub builds.
