# Shared helpers for init.ps1 and doctor.ps1 (dot-source it).
#   Read-LocalMk : KEY = value pairs from dev/local.mk, with $(VAR) expanded
#   Find-Machine : paths of the tools installed on this machine (what make init writes)
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function ConvertTo-Fwd([string]$Path) { if ($Path) { $Path -replace '\\', '/' } }

function Read-LocalMk([string]$Path) {
    $vars = @{ CURDIR = (ConvertTo-Fwd $RepoRoot) }
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        if ($line -match '^\s*([A-Z_]+)\s*\??=\s*(.*?)\s*$') {
            $key = $Matches[1]
            $vars[$key] = [regex]::Replace($Matches[2], '\$\(([A-Z_]+)\)', { param($m) "$($vars[$m.Groups[1].Value])" })
        }
    }
    $vars
}

# Newest subfolder by natural order (boost_1_83_0 > boost_1_9_0), optionally with a file inside
function Find-Newest([string]$Pattern, [string]$MustHave) {
    Get-ChildItem $Pattern -Directory -ErrorAction SilentlyContinue |
        Where-Object { -not $MustHave -or (Test-Path (Join-Path $_.FullName $MustHave)) } |
        Sort-Object { [regex]::Replace($_.Name, '\d+', { param($m) $m.Value.PadLeft(6, '0') }) } -Descending |
        Select-Object -First 1
}

function Find-Machine {
    $m = @{}
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = $null
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
            Select-Object -First 1
    }
    if ($vs) {
        $m.VS_DIR = ConvertTo-Fwd $vs
        $m.VCVARS = "$($m.VS_DIR)/VC/Auxiliary/Build/vcvars64.bat"
        $msvc = Find-Newest "$vs/VC/Tools/MSVC/*"
        if ($msvc) { $m.VCVARS_VER = ($msvc.Name -split '\.')[0..1] -join '.' }
        $ninja = "$($m.VS_DIR)/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
        if (Test-Path $ninja) { $m.NINJA = $ninja }
    }
    $cmake = @('C:/Program Files/CMake/bin/cmake.exe',
               (Get-Command cmake -ErrorAction SilentlyContinue).Source,
               "$($m.VS_DIR)/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe") |
        Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if ($cmake) { $m.CMAKE = ConvertTo-Fwd $cmake; $m.CTEST = $m.CMAKE -replace 'cmake\.exe$', 'ctest.exe' }

    $boost = if ($env:BOOST_ROOT -and (Test-Path "$env:BOOST_ROOT/lib64-msvc-14.3")) { Get-Item $env:BOOST_ROOT }
             else { Find-Newest 'C:/local/boost_*' 'lib64-msvc-14.3' }
    if ($boost) { $m.BOOST_ROOT = ConvertTo-Fwd $boost.FullName }
    $mysql = Find-Newest 'C:/Program Files/MySQL/MySQL Server *' 'lib/libmysql.dll'
    if ($mysql) { $m.MYSQL_DIR = ConvertTo-Fwd $mysql.FullName }
    $ssl = @('C:/Program Files/OpenSSL-Win64', 'C:/Program Files/OpenSSL') |
        Where-Object { Test-Path "$_/bin/libcrypto-3-x64.dll" } | Select-Object -First 1
    if ($ssl) { $m.OPENSSL_DIR = $ssl }
    $maria = Find-Newest 'C:/Program Files/MariaDB *' 'bin/mariadb.exe'
    if ($maria) { $m.MARIADB_DIR = ConvertTo-Fwd $maria.FullName }

    # DB mode: the site container if it exists, else a MariaDB server installed on this machine
    $state = $null
    if (Get-Command docker -ErrorAction SilentlyContinue) {
        try { $state = docker inspect -f '{{.State.Status}}' noblegarden-mariadb 2>$null } catch { $state = $null }
    }
    if ($state) { $m.DB_MODE = 'docker'; $m.DB_PORT = '3308' } else { $m.DB_MODE = 'native'; $m.DB_PORT = '3306' }
    $m
}
