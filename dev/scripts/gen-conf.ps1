# Make worldserver.conf / bnetserver.conf / profanity_filters.conf from the .dist files
# and patch the local keys. Existing .conf files are kept unless -Force (then a .bak is saved).
# The DB password comes from $env:DB_PASS and is never printed.
param(
    [string]$BinDir, [string]$DbHost, [int]$DbPort, [string]$DbUser, [string]$Prefix,
    [string]$DataDir, [string]$SourceDir, [string]$MySqlExe, [int]$UpdatesMask, [int]$RealmId,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $BinDir)) { throw "bin dir not found: $BinDir (run: make build)" }
if (-not $env:DB_PASS -or $env:DB_PASS -match '[;"]' -or $env:DB_PASS -eq '@CORE_PASS@') {
    throw 'DB_PASS is empty or invalid (run: make init)'
}
if ($RealmId -lt 2) { throw 'REALM_ID must be >= 2: realm 1 belongs to the site' }
$enc = New-Object Text.UTF8Encoding $false   # no BOM: the ini reader does not skip it

function DbInfo([string]$db) { '"{0};{1};{2};{3};{4}{5}"' -f $DbHost, $DbPort, $DbUser, $env:DB_PASS, $Prefix, $db }
$src = $SourceDir -replace '\\', '/'

$sets = @{
    'worldserver.conf' = [ordered]@{
        'RealmID'                 = "$RealmId"
        'DataDir'                 = "`"$DataDir`""
        'LoginDatabaseInfo'       = DbInfo 'auth'
        'WorldDatabaseInfo'       = DbInfo 'world'
        'CharacterDatabaseInfo'   = DbInfo 'characters'
        'HotfixDatabaseInfo'      = DbInfo 'hotfixes'
        'RoleplayDatabaseInfo'    = DbInfo 'roleplay'
        'SourceDirectory'         = "`"$src`""
        'MySQLExecutable'         = "`"$MySqlExe`""
        'Updates.EnableDatabases' = "$UpdatesMask"
        'Updates.AutoSetup'       = '0'
    }
    'bnetserver.conf' = [ordered]@{
        'LoginDatabaseInfo'       = DbInfo 'auth'
        'SourceDirectory'         = "`"$src`""
        'MySQLExecutable'         = "`"$MySqlExe`""
        'Updates.EnableDatabases' = '0'
        'Updates.AutoSetup'       = '0'
    }
    'profanity_filters.conf' = [ordered]@{}
}

foreach ($name in $sets.Keys) {
    $conf = Join-Path $BinDir $name
    $dist = "$conf.dist"
    if (-not (Test-Path $dist)) { throw "missing $dist (run: make build)" }
    if (Test-Path $conf) {
        if (-not $Force) { Write-Host "conf: $name exists, kept (make conf FORCE=1 to regenerate)"; continue }
        Copy-Item $conf "$conf.bak" -Force
    }
    $text = [IO.File]::ReadAllText((Resolve-Path $dist), [Text.Encoding]::UTF8)
    foreach ($key in $sets[$name].Keys) {
        $pattern = '(?m)^' + [regex]::Escape($key) + '\s*=.*$'
        if (-not [regex]::IsMatch($text, $pattern)) { throw "$name.dist has no key $key" }
        $value = $sets[$name][$key]
        $text = [regex]::Replace($text, $pattern, { param($m) "$key = $value" })
    }
    # The .dist may repeat a key (the ini reader aborts on that): keep the first one only
    $seen = @{}
    $lines = foreach ($line in ($text -split "`r?`n")) {
        if ($line -match '^\s*([A-Za-z0-9_.]+)\s*=') {
            if ($seen.ContainsKey($Matches[1])) { Write-Host "conf: $name duplicate key $($Matches[1]) commented out"; "# (duplicate) $line"; continue }
            $seen[$Matches[1]] = 1
        }
        $line
    }
    $text = $lines -join "`r`n"
    [IO.File]::WriteAllText((Join-Path (Resolve-Path $BinDir) $name), $text, $enc)
    Write-Host "conf: $name written"
}
