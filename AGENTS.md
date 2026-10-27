# RPS.WoWCore

Game server for the NobleNext WoW RP project: TrinityCore master + RoleplayCore + Eluna (C++20, CMake). Branch: `develop`. Retail client (not 3.3.5).
This file is self-contained: do not go above this repository folder unless the user asks.

## Build / run
- Needs CMake >= 3.24, VS 2022, MySQL/MariaDB client lib, Boost. Build path must have **no spaces**.
- Defaults match docs: `SCRIPTS=static`, `ELUNA=ON`, `TOOLS=ON`. Use `static` (docs example with `dynamic` is wrong).
- Typical: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DELUNA=ON -DTOOLS=ON`, then `cmake --build build --config RelWithDebInfo -- /m`.
- Config: copy `worldserver.conf.dist` -> `worldserver.conf` (`src/server/worldserver/`). Keys: `Eluna.*`, `Roleplay.*`, `RoleplayDatabaseInfo`.
- **Local build/run: `dev/README.md`** (Makefile + `dev/scripts`). Start with `make doctor` (or `dev\doctor.cmd`); first setup `make setup`; run `make run`, stop `make stop`. Runtime dir (exe, .conf, lua_scripts): `build/dev/bin/RelWithDebInfo`. Machine paths: `dev/local.mk` (has DB password, never print). Agent runbook for first setup: `dev/AGENT-SETUP.md`.
- Allen's `sync-and-build.ps1`, `build.sh`, `RuntimeData/` do not exist here.

## Structure
- `src/server/game/RolePlay/` - RP subsystem: custom NPC, phases, visibility, terrain presets.
- `src/server/game/LuaEngine/` - Eluna engine.
- `src/server/scripts/Custom/` - **our C++ scripts**: `NobleNext/` (GM commands: Army, Battle, Gob*, Master, Pet, Phase, Time, Waypoints, Weather), `Poi/`, `RolePlayFunction/`; loader is `custom_script_loader.cpp`.
- `src/server/scripts/CoreExtended/`, `DarkmoonIsland/` - custom zones and mini-games.
- `sql/custom/<db>/` - our incremental SQL (`auth, characters, hotfixes, roleplay, world`); `sql/RoleplayCore/` - full RP install, run in order 1 -> 5.
- `dep/`, `cmake/`, `contrib/` - upstream. Avoid edits unless required.

## Databases
- 5 DBs: auth, characters, world, hotfixes, **roleplay** (custom, `DATABASE_Roleplay = 16`). Local names are `wow_*` (MariaDB 11.8); conf.dist defaults have no prefix - set `wow_*` in your own conf.
- Custom NPC: `entry >= 400000`, outfits `>= 200001` (`Roleplay.CustomNpc.*`). Site-published NPCs need manual `.cnpc reload` in game.

## Gotchas
- GM chat commands (`.weather`, `.poi*`, `.wp*`, `.pet*`, `.gob tele`...) are C++ here; Lua only provides AIO UI/bootstrap.
- Don't touch Housing (native core Housing, `Housing`-named files) - someone else's work.
- Docs came from another developer's environment: stale file names possible. Code is the source of truth; verify before relying.
- Never import `system_databases` from dumps; never read `bkp-now/`.
- Style: match the surrounding file. Commit here only, don't mix with other repos; no push without confirmation.
