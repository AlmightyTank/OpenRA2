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

## v0.2B blocked implementation — NOT DONE

1. **Slave Miner**: implementing `Enslaves=SLAV`, initial five workers, worker ore gathering and docking, regeneration (500 logic frames per applicable YR rules), release/liberation upon the parent miner's destruction and worker ownership/serialization. Avoid treating `SMIN` as a standard HARV: it would silently invent the wrong economy.
2. **Grinder**: implement enter orders, eligibility guards, unit consumption and cash refunds from the source rules with accurate captured-unit behavior. Route through deterministic game commands and keep all multiplayer peers in sync.
3. **Production and rendering verification**: the complete roster must be built, spawned, drawn, selected and exercised in actual gameplay, including containers, voxels/turrets and AI production.
4. **Persistence and networking**: new slave-manager/grinder state needs explicit save/load, replay, hash and multiplayer tests. Do not introduce un-serialized timers that desynchronize clients.

**Merge gates**: green Linux CI; gameplay smoke on legally owned YR data; Side/Owner build-tree correctness; working Slave Miner + Grinder economics; deterministic replay/network tests; no regression to Allied, Soviet or controller input. Until all gates pass, PRs #5 and #6 must remain draft.
