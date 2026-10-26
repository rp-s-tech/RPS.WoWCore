# Create <bin>/lua_scripts and a junction lua_scripts/NobleNextLua -> LuaSrc (live edits).
# Other Lua (for example AIO) goes into lua_scripts by hand.
param([string]$BinDir, [string]$LuaSrc)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $BinDir)) { throw "bin dir not found: $BinDir (run: make build)" }
# Not fatal: the server starts without it, so make setup goes on
if (-not (Test-Path $LuaSrc)) { Write-Host "lua: skipped, LUA_SRC not found: $LuaSrc (clone NobleNextLua there or set LUA_SRC in dev/local.mk, then make lua)"; exit 0 }

$dir = Join-Path $BinDir 'lua_scripts'
New-Item -ItemType Directory -Force $dir | Out-Null
$link = Join-Path $dir 'NobleNextLua'
if (Test-Path $link) {
    $item = Get-Item $link -Force
    if ($item.LinkType -eq 'Junction') { Write-Host "lua: junction exists -> $($item.Target)"; exit 0 }
    throw "lua: $link exists and is not a junction; move it away first"
}
New-Item -ItemType Junction -Path $link -Target (Resolve-Path $LuaSrc) | Out-Null
Write-Host "lua: $link -> $LuaSrc"
