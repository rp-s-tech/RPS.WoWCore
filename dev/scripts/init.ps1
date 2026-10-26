# Create the untracked local files from their templates. Never overwrites.
#   dev/local.mk          <- dev/local.mk.example (+ random DB password, + tool paths found on this machine)
#   CMakeUserPresets.json <- dev/CMakeUserPresets.json.example (+ the same paths)
# Whatever is not found keeps the example value; make doctor then says what to install.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/common.ps1"
$root = $RepoRoot
$enc = New-Object Text.UTF8Encoding $false
$found = Find-Machine

$mk = Join-Path $root 'dev/local.mk'
if (Test-Path $mk) {
    Write-Host 'init: dev/local.mk exists, kept'
} else {
    # Alphanumeric only: ';' is the DatabaseInfo separator
    $chars = [char[]]'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789'
    $rng = New-Object Security.Cryptography.RNGCryptoServiceProvider
    $bytes = New-Object byte[] 24
    $rng.GetBytes($bytes)
    $pass = -join ($bytes | ForEach-Object { $chars[$_ % $chars.Length] })
    $text = [IO.File]::ReadAllText((Join-Path $root 'dev/local.mk.example')).Replace('@CORE_PASS@', $pass)

    # Client data and the Lua repo are expected next to this repo
    $parent = ConvertTo-Fwd (Split-Path -Parent $root)
    $set = @{ DATA_DIR = "$parent/wow-data"; LUA_SRC = "$parent/NobleNextLua" }
    foreach ($k in 'CMAKE', 'CTEST', 'VCVARS', 'VCVARS_VER', 'MYSQL_DIR', 'OPENSSL_DIR', 'MARIADB_DIR', 'DB_MODE', 'DB_PORT') {
        if ($found[$k]) { $set[$k] = $found[$k] } else { Write-Host "init: $k not found on this machine, example value kept" }
    }
    foreach ($k in $set.Keys) {
        $text = [regex]::Replace($text, '(?m)^' + $k + '\s*=.*$', { param($m) "$k = $($set[$k])" })
    }
    [IO.File]::WriteAllText($mk, $text, $enc)
    Write-Host "init: dev/local.mk created (random DB_PASS, DB_MODE = $($found.DB_MODE))"
}

$up = Join-Path $root 'CMakeUserPresets.json'
if (Test-Path $up) {
    Write-Host 'init: CMakeUserPresets.json exists, kept'
} else {
    $text = [IO.File]::ReadAllText((Join-Path $root 'dev/CMakeUserPresets.json.example'))
    $map = [ordered]@{
        'C:/local/boost_1_83_0'                   = $found.BOOST_ROOT
        'C:/Program Files/MySQL/MySQL Server 8.0' = $found.MYSQL_DIR
        'C:/Program Files/OpenSSL-Win64'          = $found.OPENSSL_DIR
        'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe' = $found.NINJA
    }
    foreach ($k in $map.Keys) { if ($map[$k]) { $text = $text.Replace($k, $map[$k]) } }
    [IO.File]::WriteAllText($up, $text, $enc)
    Write-Host 'init: CMakeUserPresets.json created'
}
Write-Host 'init: next step - make doctor'
