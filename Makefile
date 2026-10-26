# Local dev targets for RPS.WoWCore on Windows (GNU make + cmd.exe).
# Thin layer: each target calls a script from dev/scripts. Guide: dev/README.md
# Machine settings: dev/local.mk (created by `make init`, not tracked).

SHELL := cmd.exe
.SHELLFLAGS := /c
.DEFAULT_GOAL := help

-include dev/local.mk

PRESET ?= dev
CONFIG ?= RelWithDebInfo
BIN_DIR ?= build/$(PRESET)/bin/$(CONFIG)
# Defaults for a dev/local.mk made before these keys existed
DB_MODE ?= docker
MARIADB_DIR ?= C:/Program Files/MariaDB 11.8
DB_ROOT_USER ?= root
DUMP_DIR ?= $(CURDIR)/dev/dumps

# Secrets and MSVC settings go to scripts through the environment, not the command line
export DB_PASS VCVARS VCVARS_VER

PS    := powershell -NoProfile -ExecutionPolicy Bypass -File
VSENV := dev\scripts\vsenv.cmd
DB_ARGS := -Mode $(DB_MODE) -Container $(MARIADB_CONTAINER) -DbHost $(DB_HOST) -DbPort $(DB_PORT) \
	-DbUser $(DB_USER) -Prefix $(DB_PREFIX) -MySqlDir "$(MYSQL_DIR)" -MariaDbDir "$(MARIADB_DIR)" \
	-RootUser $(DB_ROOT_USER) -BackupDir "$(BACKUP_DIR)" -DumpDir "$(DUMP_DIR)" \
	-RealmId $(REALM_ID) -RealmName "$(REALM_NAME)"
RUN_ARGS := -BinDir "$(BIN_DIR)" -TmpDir "$(TMP_DIR)"

.PHONY: help setup init doctor configure build build-debug rebuild test debug \
	dlls conf lua prep db-up db-check db-backup db-user db-realmlist db-import db-update \
	world-db run-bnet run-world run stop clean

help:
	@echo First:   make setup   (everything below in order; safe to run again)
	@echo Setup:   make init doctor configure build prep
	@echo DB:      make db-up db-check db-backup db-user db-realmlist db-update
	@echo          make db-import   (DB_MODE=native: load dumps from DUMP_DIR into empty DBs)
	@echo Run:     make run-bnet run-world   (or: make run, make stop)
	@echo Check:   make world-db   (worldserver connects to 5 DBs and exits)
	@echo Other:   make test build-debug rebuild debug clean
	@echo          make conf FORCE=1   (regenerate .conf files, keeps .bak)
	@echo Guide:   dev/README.md

# Created on first make run; never overwritten
dev/local.mk:
	@$(PS) dev\scripts\init.ps1

init:
	@$(PS) dev\scripts\init.ps1

# Reads dev/local.mk itself; without make: dev\doctor.cmd
doctor:
	@$(PS) dev\scripts\doctor.ps1

# First setup from a clean clone. Stops at the first error; fix it and run again.
setup: init db-up doctor
	@$(MAKE) --no-print-directory configure build prep db-user db-import db-realmlist db-check world-db
	@echo setup: done. Next: client data and first login, dev/README.md

# --- build (same presets and build dir as CLion) ---
configure:
	@$(VSENV) "$(CMAKE)" --preset $(PRESET)

# DLLs first: the build runs tests.exe to list the tests
build: dlls
	@$(VSENV) "$(CMAKE)" --build --preset $(PRESET)

rebuild: dlls
	@$(VSENV) "$(CMAKE)" --build --preset $(PRESET) --clean-first

build-debug:
	@$(MAKE) --no-print-directory configure build PRESET=dev-debug CONFIG=Debug

test: dlls
	@$(VSENV) "$(CTEST)" --preset $(PRESET)

# Optional: open worldserver in the Visual Studio debugger (main debugger is CLion)
debug:
	@$(VSENV) start "" devenv /debugexe "$(BIN_DIR)\worldserver.exe"

# --- runtime dir: DLLs, conf files, lua_scripts ---
dlls:
	@$(PS) dev\scripts\copy-dlls.ps1 -BinDir "$(BIN_DIR)" -MySqlDir "$(MYSQL_DIR)" -OpenSslDir "$(OPENSSL_DIR)"

conf:
	@$(PS) dev\scripts\gen-conf.ps1 -BinDir "$(BIN_DIR)" -DbHost $(DB_HOST) -DbPort $(DB_PORT) \
		-DbUser $(DB_USER) -Prefix $(DB_PREFIX) -DataDir "$(DATA_DIR)" -SourceDir "$(CURDIR)" \
		-MySqlExe "$(MYSQL_DIR)/bin/mysql.exe" -UpdatesMask $(UPDATES_MASK) -RealmId $(REALM_ID) \
		$(if $(FORCE),-Force)

lua:
	@$(PS) dev\scripts\lua-link.ps1 -BinDir "$(BIN_DIR)" -LuaSrc "$(LUA_SRC)"

prep: dlls conf lua

# --- database (site container, see dev/README.md) ---
db-up:
	@$(PS) dev\scripts\db.ps1 -Action up $(DB_ARGS)

db-check:
	@$(PS) dev\scripts\db.ps1 -Action check $(DB_ARGS)

db-backup:
	@$(PS) dev\scripts\db.ps1 -Action backup $(DB_ARGS)

db-user:
	@$(PS) dev\scripts\db.ps1 -Action user $(DB_ARGS)

db-realmlist:
	@$(PS) dev\scripts\db.ps1 -Action realmlist $(DB_ARGS)

# Native mode only. Empty DBs only, unless FORCE=1 (then tables in the dumps are replaced)
db-import:
	@$(PS) dev\scripts\db.ps1 -Action import $(DB_ARGS) $(if $(FORCE),-Force)

# One update pass (mask 15: auth, characters, world, hotfixes). Back up first.
db-update:
	@$(PS) dev\scripts\run.ps1 -Action update $(RUN_ARGS)

# --- servers ---
world-db:
	@$(PS) dev\scripts\run.ps1 -Action dbtest $(RUN_ARGS)

run-bnet:
	@$(PS) dev\scripts\run.ps1 -Action bnet $(RUN_ARGS)

run-world:
	@$(PS) dev\scripts\run.ps1 -Action world $(RUN_ARGS)

run: run-bnet run-world

stop:
	@$(PS) dev\scripts\run.ps1 -Action stop $(RUN_ARGS)

clean:
	@powershell -NoProfile -Command "if ((Read-Host 'Delete build/$(PRESET)? [y/N]') -eq 'y') { Remove-Item -Recurse -Force 'build/$(PRESET)' }"
