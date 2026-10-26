# Start/stop the servers from the runtime dir.
#   bnet / world : start in a new console window (cwd = bin dir)
#   dbtest       : worldserver -u in this console (connect to all DBs, then exit)
#   update       : same, with the updater on for auth/characters/world/hotfixes (mask 15)
#   stop         : kill bnetserver and worldserver (also a CLion debug session!)
param([ValidateSet('bnet', 'world', 'dbtest', 'update', 'stop')][string]$Action, [string]$BinDir, [string]$TmpDir)
$ErrorActionPreference = 'Stop'

if ($Action -eq 'stop') {
    foreach ($n in 'worldserver', 'bnetserver') {
        $p = Get-Process $n -ErrorAction SilentlyContinue
        if ($p) { $p | Stop-Process -Force; Write-Host "stop: $n killed" } else { Write-Host "stop: $n not running" }
    }
    exit 0
}

$bin = (Resolve-Path $BinDir).Path
# The user profile path has Cyrillic letters; keep temp files on an ASCII path
if ($TmpDir) {
    New-Item -ItemType Directory -Force $TmpDir | Out-Null
    $env:TEMP = $TmpDir; $env:TMP = $TmpDir
}

$exe = if ($Action -eq 'bnet') { 'bnetserver.exe' } else { 'worldserver.exe' }
$path = Join-Path $bin $exe
if (-not (Test-Path $path)) { throw "missing $path (run: make build)" }
$conf = Join-Path $bin ($exe -replace '\.exe$', '.conf')
if (-not (Test-Path $conf)) { throw "missing $conf (run: make prep)" }

switch ($Action) {
    { $_ -in 'bnet', 'world' } {
        Start-Process -FilePath $path -WorkingDirectory $bin
        Write-Host "run: $exe started in a new window"
    }
    'dbtest' {
        Push-Location $bin
        try { & $path -u; $code = $LASTEXITCODE } finally { Pop-Location }
        Write-Host "run: worldserver -u exit code $code"; exit $code
    }
    'update' {
        $env:TC_UPDATES_ENABLE_DATABASES = '15'
        $env:TC_UPDATES_AUTO_SETUP = '0'
        Push-Location $bin
        try { & $path -u; $code = $LASTEXITCODE } finally { Pop-Location }
        Write-Host "run: update pass exit code $code"; exit $code
    }
}
