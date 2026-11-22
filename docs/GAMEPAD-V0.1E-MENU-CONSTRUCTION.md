# OpenRA2 v0.1E — controller navigation, construction and production

Implemented on feature/android-gamepad-v0.1. This slice deliberately retains
the original Westwood menu font and RA2 shell art. It does not alter the game
simulation, order protocol, production economics, network lockstep or saves.

## Battlefield and production controls

| Input | Function |
| --- | --- |
| Left stick | Pan battlefield camera (existing v0.1C) |
| Right stick | Move tactical cursor (existing v0.1C) |
| A / Cross | Select or issue contextual order (v0.1D) |
| LT / L2 | Hold to select a rectangle (v0.1D) |
| RB / R1 | Enter or exit construction sidebar focus |
| D-pad Left / Right (sidebar tabs) | Switch Structures, Defense, Infantry, Vehicles |
| D-pad Down (sidebar tabs) | Enter cameo grid |
| D-pad Left / Right (cameos) | Navigate columns |
| D-pad Up / Down (cameos) | Navigate rows, scroll where necessary |
| D-pad Up from first cameo row | Return to tabs |
| A / Cross (focused cameo) | Use original left-click queue, resume or placement action |
| X / Square (focused cameo) | Use original right-click hold/cancel queue action |
| B / Circle or RB / R1 (sidebar) | Exit sidebar focus |
| Start / Options | Open or return from in-game options |

Sidebar focus uses a thin colored rectangle and the existing virtual cursor.
The same SideBarLayout, cameo_click(), strip_scroll() and input_.begin_placement()
functions are called as physical clicks. The Build/Defense/Infantry/Vehicle
tabs still use their original queues, affordability, unlocks, and network
event scheduling. Only one controller is active; all actions are press-edge
triggered and cannot repeat just because a button is held.

## Menus

- Main menu and shell pages: D-pad selects enabled buttons, A activates, B
  goes Back or opens the root exit confirmation.
- In-game menu: Start opens via the normal Escape route. D-pad changes
  focused, enabled shell button, A activates, B backs out.
- Skirmish setup: D-pad navigates button rows, A activates, B goes Back.
  In Customize, Right enters the map list, Up/Down picks a map, A confirms,
  Left returns to the sidebar buttons. Open comboboxes support Up/Down,
  A=Enter and B=Escape.
- LAN and Internet multiplayer shell: D-pad navigates enabled buttons and
  A activates them; B uses the existing Escape handler. Lobby text entry,
  room lists, and detailed settings may still require mouse, touch or an
  on-screen keyboard. Full controller-only text editing is not claimed.
- No custom button/menu graphics or font replacement: original RA2 shell
  layout and click targets are preserved.

## Safety and compatibility

- Focus or controller ownership is discarded on disconnect, and held buttons
  on reconnect are not treated as new presses.
- Physical mouse/touch movement yields sidebar focus immediately.
- In-game menu closes without sending a synthetic pointer release into
  tactical play; the old tactical cursor position is restored on return.
- Gameplay commands still pass through GameScene::handle, and cameo actions
  still pass through the original production state and event system.
- During modal overlays or locked input, the controller will not pan or
  issue battlefield commands; B/Escape can back out where supported.
- Existing button geometry and original Westwood font remain unchanged.

## Verification

SDL-free tests/test_gamepad_navigation.cpp checks press-edge debounce,
held-button baseline, action separation, disabled-row skipping and wrap.
It is registered in ra2assets_tests and the GitHub controller workflow.
GitHub runs a full Linux SDL2 engine compile, Windows portable packaging,
Android debug APK assembly, and unsigned iOS app build on branch pushes.

Physical controller acceptance remains necessary: navigate main menus,
set up a Skirmish, focus each production tab, start and cancel queues,
place a ready building, switch to touch/mouse, pause/resume, and disconnect
during modal navigation on Android and Windows. Do not claim an installable
iOS IPA without Apple provisioning/signing.
