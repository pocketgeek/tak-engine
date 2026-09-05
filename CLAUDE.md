# CLAUDE.md

Orientation for Claude Code working in this repo. `README.md` has the full
build/play/options docs; this file is the quick map plus the rules that are easy
to get wrong.

## What this is

A modern, clean-room C++20 / SDL2 re-implementation of **Total Annihilation:
Kingdoms** (Cavedog, 1999): asset pipeline, deterministic simulation, an SDL2
renderer, and client-server multiplayer. Behaviour is reverse-engineered from
the retail binary `KINGDOMS.icd` by static analysis only (see
`docs/retail-engine.md`).

## Hard rules

- **Never commit game assets or the retail binary.** `assets/` (which holds
  `KINGDOMS.icd`, `ironplague.icd`, and the extracted art) and root `*.hpi/*.HPI`
  are gitignored — keep it that way. `KINGDOMS.icd` is for reverse-engineering
  and static analysis **only**; never copy its code/data into the engine, and
  never commit any of it.
- **The simulation is deterministic lockstep** — every peer must compute
  byte-identical state:
  - No direct libm transcendentals in `src/sim/`. Use `src/sim/detmath.h`
    (`detmath::sin/cos/atan2/len/...`). `tools/check-detmath.sh` enforces this.
  - `World::stateHash()` is the lockstep checksum; everything it folds in must be
    deterministic. Unit headings use `detmath::atan2`.
  - **Fog of war / visibility (`vis_`) is LOCAL display, not hashed** — the
    headless referee skips `updateVisibility` (`visPlayer_ < 0`). LoS/fog math may
    use plain floats.
  - Verify with `tools/check-determinism.sh` (cross-compiler golden hash) and the
    headless `--mpai` run below (its `hash=...` must be reproducible run-to-run;
    it only *changes* when you deliberately alter hashed sim state).

## Build / run / test

```sh
cmake -B build -G Ninja && cmake --build build      # Release -> ./build/*
```

- **Two build dirs coexist: `build/` (Release) and `build-dbg/` (Debug).** After
  editing code, rebuild **whichever binary you actually run** — a stale build
  silently shows old behaviour (this has caused confusion). Rebuild both if unsure.
- Play: `./build/takview game <map.tnt> <terrain-dir> <data-root> [--side X --aiside Y]`
  (README lists all options).
- Headless determinism / smoke test:
  `./build/takserver --data <data> &` then
  `TAK_HEADLESS=1 SDL_VIDEODRIVER=dummy ./build/takview game <map> <ter> <data> --server 127.0.0.1 --mpai --time 60` —
  prints a state `hash=`.
- Asset-inspection CLIs (in `tools/`, built into `build/`): `cobtool`,
  `modeltool`, `gaftool`, `tnttool`, `tdftool`, `hpitool`. Handy for verifying
  claims about the shipped data instead of guessing.

## Architecture

- `src/sim/` — deterministic sim (movement, A* nav, combat, economy). The
  authority for gameplay state; guard determinism carefully here.
- `src/viewer/main.cpp` — the SDL2 app (`takview`): rendering + input, and the
  **COB animation VM runs here**, so animation never affects the sim hash.
- `src/net/` + `src/server/` — client-server MP (lockstep relay, referee,
  server-run AI). `src/ai/` — the skirmish AI (emits commands).
- Asset loaders: `src/{hpi,tnt,tdo,cob,gaf,tdf,terrain,crt}`. Full table in the
  README; deeper notes in `docs/` (`retail-engine.md`, `detmath-scope.md`,
  `multiplayer-design.md`, `model-rendering-plan.md`, `sp-mp-equivalence.md`).

## Recurring gotchas

- **"Is it a building?"** use `isStructure(type) = type->maxVel <= 0` — the FBI
  `canmove` flag is unreliable (the Keep sets `canmove=1` with no velocity).
- **Terrain is a FLAT tile mosaic**: cliff/height relief is baked into the tile
  art, and mobile units are *lifted on screen* to sit on it (`terrainLift` /
  `terrainLiftX`; buildings exempt). Any screen↔world interaction (selection,
  placement, fog) must be height-aware — use `pickWorld` (screen→world, inverts
  the lift) and `unitScreen` (world→screen, includes flyer altitude). Don't
  hand-roll a flat `offX + sx/zoom`.
- **Models** are authored front=−z / right=−x (a mirrored basis): `scriptRot`
  negates piece X and Y, and all movers (flyers included) face `−heading`. See
  `docs/model-rendering-plan.md`.
- Match the surrounding code's style, naming, and comment density. C++20.
- Put temporary/scratch files in the system temp dir, never in the repo.
