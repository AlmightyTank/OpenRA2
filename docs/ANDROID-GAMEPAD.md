# Android Gamepad Support — v0.1B (Detection / Hotplug)

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

## Next: v0.1C

Poll `Platform::gamepad_state()` once per scene frame, feed the SDL-free
GamepadCursor, emit cursor motion/camera pan through the scene's existing
input routes, draw a virtual cursor, and pause movement during modal UI.
Avoid feeding controller axes through keyboard events or touching World state.
