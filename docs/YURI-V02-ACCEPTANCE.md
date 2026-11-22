# Yuri v0.2A/v0.2B verification and remaining work

This is a **development checklist**, not a completion certificate. No proprietary EA game data is included.

## v0.2A source implementation

- World side `Yuri` is already represented distinctly.
- The skirmish lobby exposes `YuriCountry` and maps `ThirdSide` to `World::Side::Yuri`.
- The initial BaseUnit candidates include `PCV`; rendering loads it from the player's data.
- Yuri AI planning and trigger classification use side 2 / trigger side 3 respectively.
- The player production resolver retains `YuriCountry` instead of falling back to Americans.
- `engine/gf_play.inc` has a `--gf-play yuri` scenario, which checks country, MCV, `YACNST`, `YAPOWR`, `YABRCK`, and `YAWEAP` through live scene commands.

**Required game-data validation**: supply a legitimately obtained Yuri's Revenge installation, then run:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target ra2engine ra2assets_tests -j 2
./build/ra2assets_tests
RA2_INSTALL_DIR=/path/to/owned/RA2 ./build/ra2engine --gf-play yuri /tmp/yuri-smoke
```

Use an environment variable and path form appropriate for the operating system. Check for `gf_play FAIL` in logs, inspect captured screenshots and verify the real YR art, units and construction chain. CI without game data can verify compilation and game-data-independent tests only.

## v0.2B source implementation

- The Yuri side resolves to `YuriCountry` for production eligibility.
- In a skirmish containing Yuri, unit art is preloaded for `INIT`, `VIRUS`, `BRUTE`, `YURI`, `YURIPR`, `SLAV`, `YENGINEER`, `LTNK`, `YTNK`, `TELE`, `MIND`, `CAOS`, `SMIN`, `PCV`, `YHVR`, `DISK`, `BSUB`, where the original archives contain them.
- The original `ls800yuri.shp` and `mplsy.pal` loading-screen pair is selected for Yuri.
- Unit-level regression cases cover generic `PCV -> YACNST` and `SMIN -> YAREFN` deployment, install-gated by the real map fixture.

## v0.2B economy implementation — code committed, acceptance NOT COMPLETE

1. **Slave Miner**: YAREFN tracks five worker IDs, workers use the existing ore harvesting and refinery unloading path, lost workers replenish with a 500-frame clock, and surviving workers stop mining and can change ownership when the refinery is destroyed. Initial and regenerated worker slots are recorded in the World save state. This is an initial approximation of the retail SlaveManager, not yet proven 1:1.
2. **Grinder**: the deterministic `MegaMission Enter` path detects an owned YAGRND, routes eligible units to its foundation, consumes the unit on arrival, and refunds half the unit's stored cost to the house wallet. Entry commands and worker clocks are serialized in World save format 48.
3. **Production and rendering validation remains**: the full roster must be built, spawned, drawn, selected and exercised in actual gameplay, including voxels/turrets and AI production. The original proprietary game archives are not present on hosted CI.
4. **Persistence and networking validation remains**: run a save/load and deterministic replay test containing active slaves, a worker death/regeneration, Grinder entry, and refinery destruction; verify no desync with multiple peers and account for captured-refinery worker behavior.
5. **Fidelity risks**: verify SlaveManager worker docking/scheduling against retail, YAGRND's exact refund rules and eligible unit classes, and event/navigation UI affordances. No retail gameplay pass has been completed.

**Merge gates**: green Linux CI; gameplay smoke on legally owned YR data; Side/Owner build-tree correctness; working Slave Miner + Grinder economics; deterministic replay/network tests; no regression to Allied, Soviet or controller input. Until all gates pass, PRs #5 and #6 must remain draft.
