# Agent runbook: first local setup of RPS.WoWCore (Windows)

For an AI agent helping a developer set up this repo. Written for small models: follow the steps literally, do not improvise.
Human guide: `dev/README.md`. Reply to the user in their language.

## Rules
- Never read or print `dev/local.mk` values `DB_PASS`, `dev/local.my.cnf`, `dev/local.root.cnf`, `*.conf` passwords, or dump contents.
- Never type the MariaDB root password yourself. Commands that need it prompt the user; tell the user to type it.
- Never run: `docker compose down -v`, `docker volume prune`, `--remove-orphans`, `DROP DATABASE`, `make db-import FORCE=1` (unless the user asked and `make db-backup` was done first).
- Never edit files outside `dev/local.mk` and `CMakeUserPresets.json` during setup. Never commit.
- Installing software: show the user the exact `winget` command from the FIX line and run it only after the user says yes.
- One step at a time. After each fix, run the check again. Stop and ask the user when the same FAIL repeats twice.

## Loop
1. Run `dev\doctor.cmd` from the repo root.
2. Read only lines starting with `FAIL`, `WARN`, `FIX:`, `Next:`.
3. No `FAIL` lines -> go to "Setup". Otherwise take the FIRST `FAIL` and its `FIX:` line:
   - `FIX: make init ...` -> run it.
   - `FIX: set in dev/local.mk: KEY = VALUE` -> replace the line `KEY = ...` in `dev/local.mk` with `KEY = VALUE`.
   - `FIX: set in CMakeUserPresets.json: KEY = VALUE` -> replace that key's value in `CMakeUserPresets.json` (in both places if it appears twice).
   - `FIX: winget install ...` -> ask the user, run it, then ask the user to open a NEW terminal (PATH changes), then continue.
   - `FIX: download ...` (Boost) -> give the user the link and target folder; wait until they say it is installed.
   - Anything else -> show the FIX line to the user and ask them to do it.
4. Go to 1.

## Database mode
`dev/local.mk` has `DB_MODE`:
- `docker` - the site's MariaDB container. If doctor says docker is not installed and the user does not run the site: set `DB_MODE = native`, `DB_PORT = 3306`, go to 1.
- `native` - MariaDB on this machine. The DBs come from dumps:
  - Ask the user for a dump from another developer (`make db-backup` output, `wow-local-*.sql.gz`).
  - Put it into `dev/dumps/` (the user copies it; do not open it).
  - If doctor says port 3306 is not MariaDB (MySQL is there): MariaDB must go to another port (3307), then `DB_PORT = 3307`.

## Setup
1. Build first: run `make configure build prep`. It takes 30-60 min the first time. Run it in the background if your tool allows; do not poll more than every 5 min.
2. Database steps:
   - `DB_MODE = docker`: run `make setup` yourself (no password prompts).
   - `DB_MODE = native`: `make setup` asks for the MariaDB root password, and you cannot type into it. Ask the USER to run `make setup` in their own terminal and paste you the last 30 lines when it ends.
3. It stops at the first error. Read the last 30 lines of output only:
   - error mentions a missing path or tool -> go to "Loop".
   - `db-import: ... has other DBs` -> the dump is wrong; ask the user for a dump of the 5 `wow_*` DBs only.
   - `db-import: no *.sql` -> dumps missing in `dev/dumps/`.
   - compile error in C++ code -> stop, show the first error line to the user; do not edit source code.
4. Fix, then run `make setup` again (it skips finished work).
5. Done when the output ends with `setup: done`. Then run `dev\doctor.cmd`: remaining WARN lines about client data and Lua are manual steps; point the user to `dev/README.md`, section "Первый вход в игру".
