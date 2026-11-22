# Android Gamepad Support — v0.1B / v0.1C

## v0.1C: analog camera and virtual cursor

**Implementation:** committed to `feature/android-gamepad-v0.1`; complete-source
Android/desktop builds and real hardware testing are **not yet verified**.

- Left analog stick pans the existing tactical camera via `Input::pan()`.
  Pushing right moves the viewing position right (map pixels shift left).
- Right analog stick navigates a virtual cursor using the **existing**
  `GamepadCursor` C++17 module. Pointer movement is fed through
  `GameScene::handle(InputEvent::PointerMove)`; no SDL synthetic mouse
  injection, clicks or network command changes.
- Virtual cursor gets a visible cyan-green reticle, independent of
  MOUSE.SHP availability. The OS cursor is hidden while the controller is
  actively steering; physical touch/mouse regains pointer control immediately.
- Axes have dead zones, fractional pixel accumulation, screen-edge bounds,
  50 ms frame-delta capping, and logical-canvas resize support.
- Controller motion stops in options/load dialogs, briefing, trigger movies,
  abort confirmation, locked-input phases, multiplayer chat/reconnect/end.
  Closing a modal page discards stale time and fractions to avoid jumps.
- The controller-specific event update only runs during **real interactive**
  platform sessions. Scripted play, fixed-timestep gates, replay and
  raw-input recording are left deterministic and do not sample live gamepads.
  No camera/UI state is written into the replicated World simulation.
- When a controller disconnects, virtual cursor ownership is released to
  avoid unintended screen-edge camera scrolling.
- Analog sticks only: selecting/commanding units and gamepad menu navigation
  are **v0.1D+**, not claimed by this slice.

### v0.1C regression tests

`tests/test_gamepad_cursor.cpp` is included in `ra2assets_tests` and covers
positive/negative axes, dead zone thresholds, camera-vs-cursor isolation,
viewport bounds and resizing, suspend/resume behavior, large frame stalls,
and subpixel movement accumulation.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target ra2assets_tests
./build/ra2assets_tests --test-case="gamepad cursor*"
ctest --test-dir build --output-on-failure
cd android && ./gradlew assembleDebug
```

### v0.1C on-device acceptance (not yet verified)

1. Start skirmish with an Xbox / PlayStation / mapped generic controller.
2. Move left stick each direction; verify camera responds smoothly and stops at dead zone.
3. Move right stick each direction; verify a visible, bounded reticle and no camera drift.
4. Open options while moving sticks: camera and cursor freeze; close without jump.
5. Switch to touch/mouse: pointer and existing gestures continue to work.
6. Disconnect/reconnect mid-match; confirm no stale edge scrolling or cursor jump.
7. Verify logical view scaling across phone/tablet aspect ratios.
8. Regress desktop mouse/keyboard, touch gestures, campaign and multiplayer.

---

## v0.1B detection / hotplug baseline

## Scope

This slice adds **physical game-controller detection only** to the existing SDL2 Platform.
It does **not** send joystick events to the tactical simulation, synthesize mouse
clicks, open a radial command menu, or implement camera/cursor movement. Those
are v0.1C and later tasks. The previous SDL-free gamepad_cursor.hpp remains
unwired intentionally.

## Implementation

- SDL video remains required; SDL game-controller initialization is optional.
  Failure to initialize controller support is logged and does not stop touch,
  keyboard, or mouse use.
- Platform startup scans already-connected SDL-compatible game controllers.
- SDL_CONTROLLERDEVICEADDED uses a **device index**, while
  SDL_CONTROLLERDEVICEREMOVED uses a **joystick instance ID**.
- Each supported controller is opened exactly once. The oldest connected
  controller is active; removing it activates the next connected one.
- Controller handles are closed on disconnect and platform shutdown (including
  Platform move assignment). Controller events currently yield InputEvent::None.
- Platform::gamepad_count() returns the number of open, recognized controllers.
  Platform::gamepad_state() returns a snapshot of the active controller's name,
  SDL instance ID, raw six SDL axis values and button bitmask (SDL standard
  GameController button ordinals, bits 0..31). If none is connected, connected
  is false and axes/buttons are zero.
- Generic HID controllers without an SDL GameController mapping are intentionally
  not treated as supported controllers. Mapping/remapping coverage on real
  Android hardware still needs validation.

The Android manifest already marks gamepad, Bluetooth, and USB-host features
as optional, and needs no new dangerous permissions.

## Automated tests

`tests/test_gamepad_registry.cpp` checks insertion, duplicates, active-owner
selection, removal of inactive devices, fallback after active disconnect, and
clear/reconnection. Included in the desktop `ra2assets_tests` target.

These tests do **not** emulate real Android controller drivers or SDL's event
pump; they only verify the deterministic, SDL-free ownership policy.

## Run locally

From repo root:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target ra2assets_tests
./build/ra2assets_tests --test-case="gamepad registry*"
```

Android (JDK 17, Android SDK and NDK r27c):

```sh
cd android
./gradlew assembleDebug
```

## Android hardware acceptance

1. Launch using touch without any controller; confirm no startup regression.
2. Connect an Xbox-style gamepad over Bluetooth; look for
   `[gamepad] connected` in logcat and confirm the game remains responsive.
3. Repeat using a PlayStation controller and a USB gamepad.
4. Connect two supported controllers, remove the first and confirm the second
   becomes active (check logcat `active=...`).
5. Disconnect/reconnect repeatedly and resume after backgrounding the app;
   confirm no crash or stuck state.
6. Confirm mouse/touch actions still work unchanged. The controller will not
   control units yet (intentional for this slice).
7. Verify desktop SDL2 builds and existing touch regression tests.

**Verification status:** GitHub source committed; desktop/full Android builds
and real controller hardware checks are not yet verified in this environment.

## Next: v0.1D

Add controller-button selection/commands without replacing the existing input
model. Make production/navigation controller-friendly in later slices. Prioritize
real Android builds and on-device testing before claiming release readiness.
