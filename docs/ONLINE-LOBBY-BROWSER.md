# S0.1B — Online lobby browser

This slice improves the existing Internet screen on the Windows/Android
RA2 Engine client. It does **not** replace the player-hosted game or
the `ra2lobbyd` protocol and does not deploy anything.

## Features

- **Search:** filter public rooms by room name, host, map, or room code
  without a server round trip. Search is case-insensitive.
- **All / Open / Joinable:** All shows the public directory; Open shows
  waiting rooms with space; Joinable additionally requires the same
  simulation build and game data CRC. The host must still allow
  cross-platform play when relevant.
- **Sorted status:** READY rooms appear before open-but-incompatible
  rooms, full rooms and started rooms. Full and started games remain
  visible in All for awareness, but cannot be selected for joining.
- **Stable selection:** the chosen room is tracked by its room code
  rather than the server's changing array index. Refresh, sort and
  filtering never turn one selection into a different room.
- **Refresh:** click Refresh or press F5; automatic updates continue
  approximately every five seconds while connected and not joining.
- **Selection detail:** name, map, host, occupancy and explicit reasons
  for disabled Join Game.
- **Join by code:** existing 6-character private room join remains
  available independently of public-room search or filters.
- **Keyboard/touch:** Up/Down select rows, Enter joins an eligible
  selection, list swipe and tap still use the shared shell controls.

The UI never trusts displayed availability as a guarantee: another
player can claim the last slot after the list was received. The server
will make the authoritative join decision, with a friendly error if
the match filled or started meanwhile.

## Verification gates

- `tests/test_room_browser.cpp` covers case-insensitive matching,
  compatibility/version checks, Open/Joinable filters, deterministic
  ordering and retaining a selection across server list updates.
- `.github/workflows/online-browser.yml` compiles and runs the
  SDL-free browser tests, and builds the complete Linux engine/test
  binary in CI.
- Real Android touch layout, host/guest end-to-end matches, server
  disconnect/retry and physical gamepad interactions require device
  testing. This slice does not claim those checks passed.

## Follow-up S0.1C

Verify multiple concurrent live sessions against one server using
Windows/Android clients, and test room churn and host disconnects under
load without modifying the deterministic simulation.
