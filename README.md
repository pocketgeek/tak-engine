# TAK Engine

A modern, cross-platform engine recreation for **Total Annihilation: Kingdoms**
(Cavedog Entertainment, 1999), in the spirit of OpenRA and Robot War Engine.

This project contains **no game content**. You must own the original game
(e.g. the GOG release of *Total Annihilation: Kingdoms + The Iron Plague*)
and place its data files in `assets/` (gitignored) to use the engine.

## Roadmap

1. **Format tooling** — read TAK's HPI archives; decode GAF sprite banks,
   TNT maps, TDF/FBI unit definitions.
2. **Asset viewer** — SDL2/OpenGL app to browse sprites, animations, and maps.
3. **Simulation** — units, orders, COB script VM, mana economy, combat.
4. **Game** — playable skirmish, then deterministic lockstep multiplayer.

## Building

Requires CMake ≥ 3.24, a C++20 compiler, and Ninja. SDL2 is found on the
system or fetched automatically.

```sh
cmake -B build -G Ninja
cmake --build build
```

## Layout

- `src/hpi/` — HPI archive reader (TAK uses a revised format vs. classic TA)
- `src/gaf/` — GAF sprite/animation decoding
- `src/tnt/` — TNT map decoding
- `src/viewer/` — SDL2 asset viewer application
- `tools/` — command-line format tools (`hpitool`, …)
- `docs/` — format notes as we verify them against real data
