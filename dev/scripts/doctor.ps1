# Check this machine for the build and the first start: tools, libraries, DB, runtime files.
# Works without make (dev\doctor.cmd). Every problem line is followed by a FIX line:
# the command to run or the setting to change. Exit 1 if a required check fails.
# Reads dev/local.mk; without it, checks what make init would detect.
$ErrorActionPreference = 'Continue'
. "$PSScriptRoot/common.ps1"
$script:fails = 0
$script:warns = 0
$script:next = $null
$wg = 'winget install --source winget -e --id'

function Report([string]$level, [string]$what, [string]$detail, [string]$fix) {
    if ($level -eq 'FAIL') { $script:fails++ } elseif ($level -eq 'WARN') { $script:warns++ }
    Write-Host ('{0,-5} {1,-26} {2}' -f $level, $what, $detail)
    if ($level -ne 'OK' -and $fix) {
        Write-Host "      FIX: $fix"
        if (-not $script:next -or ($level -eq 'FAIL' -and $script:nextLevel -ne 'FAIL')) { $script:next = $fix; $script:nextLevel = $level }
    }
}

# A path from the settings: OK if it exists; else the detected path to set, or how to install
function CheckPath([string]$what, [string]$value, [string]$sub, [string]$detected, [string]$setHint, [string]$install, [string]$level = 'FAIL') {
    if ($value -and (Test-Path "$value$sub")) { Report 'OK' $what $value; return $true }
    if ($detected -and (Test-Path "$detected$sub") -and $detected -ne $value) { Report $level $what "not found: $value$sub" "$setHint = $detected" }
    else { Report $level $what "not found: $value$sub" $install }
    $false
}

$found = Find-Machine
$mkPath = Join-Path $RepoRoot 'dev/local.mk'
if (Test-Path $mkPath) {
    $cfg = Read-LocalMk $mkPath
    Report 'OK' 'dev/local.mk' 'found'
} else {
    $cfg = $found
    Report 'FAIL' 'dev/local.mk' 'missing (values below are auto-detected)' 'make init   (without make: powershell -ExecutionPolicy Bypass -File dev\scripts\init.ps1)'
}
function V([string]$key, [string]$default) { if ($cfg[$key]) { $cfg[$key] } else { $default } }
$presets = $null
$upPath = Join-Path $RepoRoot 'CMakeUserPresets.json'
if (Test-Path $upPath) {
    try { $presets = (Get-Content $upPath -Raw | ConvertFrom-Json).configurePresets | Where-Object { $_.name -eq 'machine' } } catch { }
}
$setMk = 'set in dev/local.mk:'
$setPr = 'set in CMakeUserPresets.json:'

Write-Host '--- repo'
if ($RepoRoot -match '\s') { Report 'FAIL' 'repo path' $RepoRoot 'move the repo to a path without spaces, for example C:/dev/RPS.WoWCore' }
elseif ($RepoRoot -match '[^\x00-\x7F]') { Report 'WARN' 'repo path' $RepoRoot 'move the repo to a path with English letters only' }
else { Report 'OK' 'repo path' $RepoRoot }
if ($presets) { Report 'OK' 'CMakeUserPresets.json' 'found' }
else { Report 'FAIL' 'CMakeUserPresets.json' 'missing or broken' 'make init   (or delete the broken file first)' }
$ignored = git -C $RepoRoot check-ignore dev/local.mk CMakeUserPresets.json 2>$null
if (@($ignored).Count -eq 2) { Report 'OK' 'local files git-ignored' '' }
else { Report 'FAIL' 'local files git-ignored' 'dev/local.mk or CMakeUserPresets.json not ignored' 'git checkout .gitignore   (it must list both files)' }

Write-Host '--- tools'
$make = (Get-Command make -ErrorAction SilentlyContinue).Source
if ($make) { Report 'OK' 'make' $make } else { Report 'FAIL' 'make' 'not in PATH' "$wg ezwinports.make   (then open a new terminal)" }
$git = (Get-Command git -ErrorAction SilentlyContinue).Source
if ($git) { Report 'OK' 'git' $git } else { Report 'FAIL' 'git' 'not in PATH' "$wg Git.Git" }
$vsInstall = "$wg Microsoft.VisualStudio.2022.Community --override `"--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --passive`""
$vcvars = V 'VCVARS'
if (CheckPath 'Visual Studio (vcvars64)' $vcvars '' $found.VCVARS "$setMk VCVARS" $vsInstall) {
    $vsDir = $vcvars -replace '/VC/Auxiliary/Build/vcvars64\.bat$', ''
    $ver = V 'VCVARS_VER'
    if (Get-ChildItem "$vsDir/VC/Tools/MSVC/$ver*" -Directory -ErrorAction SilentlyContinue) { Report 'OK' 'MSVC toolset' $ver }
    else { Report 'FAIL' 'MSVC toolset' "no $ver in $vsDir/VC/Tools/MSVC" "$setMk VCVARS_VER = $($found.VCVARS_VER)" }
}
$cmake = V 'CMAKE'
if (CheckPath 'cmake' $cmake '' $found.CMAKE "$setMk CMAKE" "$wg Kitware.CMake") {
    $line = & $cmake --version | Select-Object -First 1
    if ($line -match '(\d+)\.(\d+)' -and ([int]$Matches[1] -gt 3 -or ([int]$Matches[1] -eq 3 -and [int]$Matches[2] -ge 24))) { Report 'OK' 'cmake version' $line }
    else { Report 'FAIL' 'cmake version' "$line (need 3.24+)" "$wg Kitware.CMake" }
}
$ninja = if ($presets) { $presets.cacheVariables.CMAKE_MAKE_PROGRAM }
CheckPath 'ninja (from VS)' $ninja '' $found.NINJA "$setPr CMAKE_MAKE_PROGRAM" 'Visual Studio Installer > Modify > add "C++ CMake tools for Windows"' | Out-Null

Write-Host '--- libraries'
$boost = if ($presets) { $presets.environment.BOOST_ROOT } else { $env:BOOST_ROOT }
CheckPath 'Boost (msvc-14.3, x64)' $boost '/lib64-msvc-14.3/cmake' $found.BOOST_ROOT "$setPr BOOST_ROOT" 'download boost_1_83_0-msvc-14.3-64.exe from https://sourceforge.net/projects/boost/files/boost-binaries/1.83.0/ and install to C:/local/boost_1_83_0' | Out-Null
$mysqlInstall = "$wg Oracle.MySQL --version 8.0.43   (the core needs only lib/libmysql.dll and bin/mysql.exe from it)"
$mysql = V 'MYSQL_DIR'
CheckPath 'MySQL libmysql.dll' $mysql '/lib/libmysql.dll' $found.MYSQL_DIR "$setMk MYSQL_DIR" $mysqlInstall | Out-Null
CheckPath 'MySQL mysql.exe' $mysql '/bin/mysql.exe' $found.MYSQL_DIR "$setMk MYSQL_DIR" $mysqlInstall | Out-Null
if ($presets -and $presets.cacheVariables.MYSQL_ROOT_DIR -ne $mysql) { Report 'WARN' 'MySQL dir in presets' $presets.cacheVariables.MYSQL_ROOT_DIR "$setPr MYSQL_ROOT_DIR (both places) = $mysql" }
CheckPath 'OpenSSL 3' (V 'OPENSSL_DIR') '/bin/libcrypto-3-x64.dll' $found.OPENSSL_DIR "$setMk OPENSSL_DIR" "$wg ShiningLight.OpenSSL.Dev --version 3.6.2" | Out-Null

Write-Host '--- database'
$mode = V 'DB_MODE' 'docker'
$port = [int](V 'DB_PORT' '3308')
Report 'OK' 'DB_MODE' "$mode, port $port"
$listen = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
if ($mode -eq 'docker') {
    $container = V 'MARIADB_CONTAINER' 'noblegarden-mariadb'
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        Report 'FAIL' 'docker' 'not installed' "no site on this machine? $setMk DB_MODE = native and DB_PORT = 3306 (dev/README.md, section without docker)"
    } else {
        $health = docker inspect -f '{{.State.Health.Status}}' $container 2>$null
        if ($health -eq 'healthy') { Report 'OK' "container $container" $health }
        else { Report 'FAIL' "container $container" "state: $health" "make db-up   (Docker Desktop must be running; no site on this machine? $setMk DB_MODE = native and DB_PORT = 3306)" }
    }
} else {
    CheckPath 'MariaDB client' (V 'MARIADB_DIR' 'C:/Program Files/MariaDB 11.8') '/bin/mariadb.exe' $found.MARIADB_DIR "$setMk MARIADB_DIR" "$wg MariaDB.Server --version 11.8.2.0 --interactive   (in the installer: set a root password, port 3306)" | Out-Null
    if ($listen) {
        # MySQL often sits on 3306 too; dumps from the site's MariaDB do not load into MySQL
        $proc = Get-Process -Id $listen.OwningProcess -ErrorAction SilentlyContinue
        $svc = Get-CimInstance Win32_Service -ErrorAction SilentlyContinue | Where-Object { $_.ProcessId -eq $listen.OwningProcess } | Select-Object -First 1
        $what = "$($proc.ProcessName) (pid $($listen.OwningProcess))"
        if ($proc.ProcessName -eq 'mariadbd' -or "$($proc.Path) $($svc.PathName)" -match 'MariaDB') { Report 'OK' "DB port $port" "MariaDB: $what" }
        else { Report 'WARN' "DB port $port" "not confirmed as MariaDB: $what" "if this is MySQL: install MariaDB on another port (e.g. 3307) and $setMk DB_PORT" }
    } else {
        Report 'FAIL' "DB port $port" 'nothing listens' 'make db-up   (starts the MariaDB service; if there is none, install MariaDB, see the MariaDB client line)'
    }
}
$pass = V 'DB_PASS'
if ($listen -and $pass -and $pass -ne '@CORE_PASS@' -and (Test-Path "$(V 'MYSQL_DIR')/bin/mysql.exe")) {
    $env:DB_PASS = $pass
    $ping = & powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot/db.ps1" -Action ping -Mode $mode `
        -DbHost (V 'DB_HOST' '127.0.0.1') -DbPort $port -DbUser (V 'DB_USER' 'wow_core') -Prefix (V 'DB_PREFIX' 'wow_') `
        -MySqlDir (V 'MYSQL_DIR') -RealmId ([int](V 'REALM_ID' '2')) -RealmName (V 'REALM_NAME' 'NobleNext Local') 2>$null
    $st = @{}
    foreach ($l in $ping) { if ("$l" -match '^([\w]+)=(\d+)$') { $st[$Matches[1]] = [int]$Matches[2] } }
    if ($st['login'] -ne 1) { Report 'WARN' "login as $(V 'DB_USER')" 'failed' 'make db-user' }
    else {
        Report 'OK' "login as $(V 'DB_USER')" 'OK'
        $fill = if ($mode -eq 'native') { "put dumps into $(V 'DUMP_DIR' "$(ConvertTo-Fwd $RepoRoot)/dev/dumps"), then make db-import" } else { 'the DBs come from the site volume: check the site stack' }
        foreach ($k in $st.Keys | Where-Object { $_ -notin 'login', 'realm' } | Sort-Object) {
            if ($st[$k] -gt 0) { Report 'OK' $k "$($st[$k]) tables" } else { Report 'WARN' $k 'empty' $fill }
        }
        if ($st['realm'] -eq 1) { Report 'OK' "realm $(V 'REALM_ID' '2')" 'in realmlist' }
        elseif ($st["$(V 'DB_PREFIX' 'wow_')auth"] -gt 0) { Report 'WARN' "realm $(V 'REALM_ID' '2')" 'not in realmlist' 'make db-realmlist' }
    }
}

Write-Host '--- runtime'
$bin = Join-Path $RepoRoot (V 'BIN_DIR' 'build/dev/bin/RelWithDebInfo')
if (Test-Path "$bin/worldserver.exe") { Report 'OK' 'worldserver.exe' $bin }
else { Report 'WARN' 'worldserver.exe' 'not built yet' 'make configure build   (first build takes 30-60 min)' }
if (Test-Path "$bin/worldserver.exe") {
    if (Test-Path "$bin/worldserver.conf") { Report 'OK' 'conf files' 'found' } else { Report 'WARN' 'conf files' 'missing' 'make prep' }
}
CheckPath 'Lua (NobleNextLua)' (V 'LUA_SRC') '' $null '' "clone NobleNextLua to $(V 'LUA_SRC') or $setMk LUA_SRC, then make lua" 'WARN' | Out-Null
CheckPath 'client data (DataDir)' (V 'DATA_DIR') '/maps' $null '' "extract client data into $(V 'DATA_DIR') (dev/README.md, first login)" 'WARN' | Out-Null
foreach ($p in 1119, 8081, 8085) {
    $c = Get-NetTCPConnection -LocalPort $p -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $c) { Report 'OK' "port $p" 'free'; continue }
    $name = (Get-Process -Id $c.OwningProcess -ErrorAction SilentlyContinue).ProcessName
    if ($name -in 'bnetserver', 'worldserver') { Report 'OK' "port $p" "used by $name" }
    else { Report 'WARN' "port $p" "busy: $name (pid $($c.OwningProcess))" 'close that program, the server needs this port' }
}

Write-Host ''
if ($script:fails) {
    Write-Host "doctor: $($script:fails) problem(s), $($script:warns) warning(s). Fix the FAIL lines from the top, then run doctor again."
} else {
    Write-Host "doctor: all required checks passed, $($script:warns) warning(s)"
}
if ($script:next) { Write-Host "Next: $($script:next)" }
if ($script:fails) { exit 1 }
