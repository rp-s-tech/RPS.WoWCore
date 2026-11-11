# Database helpers. Mode docker = the site's MariaDB container; native = MariaDB on this machine.
#   up        : docker - start the container (never compose), wait for healthy; native - check the server
#   check     : DB list, table counts, realmlist, pending update files (read-only)
#   backup    : dump of the 5 DBs to BackupDir (.sql.gz, the format import reads)
#   user      : create/refresh the core user (root: inside the container, or a password prompt)
#   realmlist : add/refresh the local realm row (as the core user)
#   import    : native only - load *.sql / *.sql.gz from DumpDir into empty DBs (-Force: also into full ones)
#   ping      : quiet status for doctor: table count per DB and the realm row
# Core password: $env:DB_PASS, written only to dev/local.my.cnf (git-ignored), never printed.
# Native root password: $env:DB_ROOT_PASS or a prompt; kept in dev/local.root.cnf only while the script runs.
param(
    [ValidateSet('up', 'check', 'backup', 'user', 'realmlist', 'import', 'ping')][string]$Action,
    [ValidateSet('docker', 'native')][string]$Mode = 'docker',
    [string]$Container, [string]$DbHost, [int]$DbPort, [string]$DbUser, [string]$Prefix,
    [string]$MySqlDir, [string]$MariaDbDir, [string]$RootUser = 'root',
    [string]$BackupDir, [string]$DumpDir, [int]$RealmId, [string]$RealmName, [switch]$Force,
    [string]$BinDir = '.', [switch]$VerifyUpdateTarget
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$dbs = 'auth', 'characters', 'world', 'hotfixes', 'roleplay'
$enc = New-Object Text.UTF8Encoding $false
$rootCnf = Join-Path $root 'dev/local.root.cnf'

function Render([string]$file) {
    $t = [IO.File]::ReadAllText((Join-Path $root "dev/sql/$file"))
    # Docker: '%' (connections come through the Docker NAT, the port is bound to 127.0.0.1).
    # Native: the server may listen on the LAN, so the core user may connect from this machine only.
    $hostMask = if ($Mode -eq 'native') { '127.0.0.1' } else { '%' }
    $t.Replace("'@DB_USER@'@'%'", "'@DB_USER@'@'$hostMask'").
       Replace('@DB_USER@', $DbUser).Replace('@PREFIX@', $Prefix).Replace('@CORE_PASS@', $env:DB_PASS).
       Replace('@REALM_ID@', "$RealmId").Replace('@REALM_NAME@', $RealmName.Replace("'", "''"))
}

# MariaDB's own client: MySQL 8 client tools fail on MariaDB dump headers
function MariaExe([string]$name = 'mariadb.exe') {
    $exe = "$MariaDbDir/bin/$name"
    if (-not (Test-Path $exe)) { throw "missing ${exe}: install MariaDB or set MARIADB_DIR in dev/local.mk (make doctor)" }
    $exe
}

function RootOptions {
    if (-not (Test-Path $rootCnf)) {
        $pass = $env:DB_ROOT_PASS
        if (-not $pass) {
            $sec = Read-Host "MariaDB password for $RootUser at ${DbHost}:$DbPort" -AsSecureString
            $pass = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec))
        }
        $q = $pass.Replace('\', '\\').Replace('"', '\"')
        [IO.File]::WriteAllText($rootCnf, "[client]`nhost=$DbHost`nport=$DbPort`nuser=$RootUser`npassword=`"$q`"`nprotocol=TCP`n", $enc)
    }
    "--defaults-extra-file=$rootCnf"
}

# Run a native client; PS 5.1 turns native stderr into terminating errors under 'Stop', so judge by the exit code
function NativeSql([string]$exe, [string]$opts, [string]$sql, [string]$what) {
    $ErrorActionPreference = 'Continue'
    $out = $sql | & $exe $opts -N -B 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    # The client may echo a fragment of the SQL, which can hold the core password
    if ($code) { $msg = "$out"; if ($env:DB_PASS) { $msg = $msg.Replace($env:DB_PASS, '****') }; throw "$what failed: $msg" }
    $out | Where-Object { $_ -isnot [Management.Automation.ErrorRecord] }
}

# SQL as root: inside the container (root password stays in the container env), or the local server
function RootSql([string]$sql) {
    if ($Mode -eq 'native') { return NativeSql (MariaExe) (RootOptions) $sql 'root SQL' }
    $out = $sql | docker exec -i $Container sh -c 'MYSQL_PWD=$MARIADB_ROOT_PASSWORD mariadb -uroot -N -B'
    if ($LASTEXITCODE) { throw "root SQL failed (exit $LASTEXITCODE)" }
    $out
}

# SQL as the core user through mysql.exe (the same client lib the core uses)
function CoreSql([string]$sql) {
    $cnf = Join-Path $root 'dev/local.my.cnf'
    $text = "[client]`nhost=$DbHost`nport=$DbPort`nuser=$DbUser`npassword=$($env:DB_PASS)`nprotocol=TCP`n"
    [IO.File]::WriteAllText($cnf, $text, $enc)
    NativeSql "$MySqlDir/bin/mysql.exe" "--defaults-extra-file=$cnf" $sql 'core SQL'
}

function CoreUserWorks {
    try { CoreSql 'SELECT 1;' | Out-Null; $true } catch { $false }
}

function TableCount([scriptblock]$q, [string]$db) {
    [int]("$(& $q "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='$db';")".Trim())
}

# ConfigMgr reads the first populated INI section, then *.conf overrides, then TC_*.
function Get-UpdateSourceDirectory([string]$runtime) {
    $conf = Join-Path $runtime 'worldserver.conf'
    $configDir = Join-Path $runtime 'worldserver.conf.d'
    $files = @()
    if (Test-Path -LiteralPath $conf -PathType Leaf) { $files += $conf }
    elseif ($VerifyUpdateTarget) { throw "db: cannot verify update target without $conf (run: make conf)" }
    if (Test-Path -LiteralPath $configDir -PathType Container) {
        $files += @(Get-ChildItem -LiteralPath $configDir -File -Recurse -Force -Filter '*.conf' |
            Where-Object { $_.Extension -ceq '.conf' } | ForEach-Object { $_.FullName })
    }

    $settings = @{}
    $overrides = @{}
    foreach ($file in $files) {
        $inSection = $false
        $hasValues = $false
        foreach ($raw in [IO.File]::ReadLines($file)) {
            $line = $raw.Trim()
            if (-not $line -or $line.StartsWith('#') -or $line.StartsWith(';')) { continue }
            if ($line.StartsWith('[')) {
                if ($hasValues) { break }
                $inSection = $true
                continue
            }
            if (-not $inSection) { break }
            $hasValues = $true
            if ($line -cnotmatch '^(SourceDirectory|(?:Login|Character|World|Hotfix)DatabaseInfo)\s*=\s*(.*)$') { continue }
            $key = $Matches[1]
            $value = $Matches[2].Trim().Replace('"', '')
            if ($file -ne $conf) {
                # Native traversal order is unspecified. Never guess between different overrides.
                if ($overrides.ContainsKey($key) -and $overrides[$key] -cne $value) { $overrides[$key] = $null }
                else { $overrides[$key] = $value }
            }
            $settings[$key] = $value
        }
    }
    $pools = @{ LoginDatabaseInfo='auth'; CharacterDatabaseInfo='characters'; WorldDatabaseInfo='world'; HotfixDatabaseInfo='hotfixes' }
    foreach ($key in @('SourceDirectory') + @($pools.Keys)) {
        $envKey = 'TC_' + ([regex]::Replace($key, '(?<=[a-z])(?=[A-Z])', '_')).ToUpperInvariant()
        $value = [Environment]::GetEnvironmentVariable($envKey)
        if ($null -ne $value) { $settings[$key] = $value.Replace('"', '') }
        elseif ($overrides.ContainsKey($key) -and $null -eq $overrides[$key]) { throw "db: conflicting $key settings in $configDir" }
        if ($VerifyUpdateTarget -and $key -ne 'SourceDirectory') {
            $parts = [string]$settings[$key] -split ';'
            $expected = "$DbHost;$DbPort;$DbUser;$($env:DB_PASS);$Prefix$($pools[$key])"
            if ($parts.Count -notin 5,6 -or [string]::Join(';', $parts[0..4]) -cne $expected) {
                throw "db: $key differs from the make DB settings (check worldserver.conf, worldserver.conf.d and $envKey)"
            }
        }
    }
    $dir = $settings['SourceDirectory']
    if (-not $dir) {
        if ($VerifyUpdateTarget) { throw 'db: update requires an explicit SourceDirectory (run: make conf or set TC_SOURCE_DIRECTORY)' }
        $dir = $root
    }
    if (-not [IO.Path]::IsPathRooted($dir)) { $dir = Join-Path $runtime $dir }
    $dir = [IO.Path]::GetFullPath($dir)
    if ($VerifyUpdateTarget -and -not (Test-Path -LiteralPath $dir -PathType Container)) { throw "db: SourceDirectory does not exist: $dir" }
    $dir
}

function OpenDump([string]$path) {
    $fs = [IO.File]::OpenRead($path)
    if ($path -like '*.gz') { New-Object IO.Compression.GZipStream($fs, [IO.Compression.CompressionMode]::Decompress) } else { $fs }
}

# DB names from CREATE DATABASE / USE lines; empty for a single-DB dump.
# Also refuses statements a plain data dump never needs (users, grants, dropping DBs, system tables).
# A sanity check for dumps from colleagues, not a security boundary.
function DumpDbs([string]$path) {
    $names = @{}
    $r = New-Object IO.StreamReader (OpenDump $path)
    try {
        while ($null -ne ($line = $r.ReadLine())) {
            $head = if ($line.Length -gt 120) { $line.Substring(0, 120) } else { $line }
            if ($head -match '^\s*(CREATE\s+DATABASE(\s+/\*.*?\*/)?(\s+IF\s+NOT\s+EXISTS)?|USE)\s+`?([^`;\s]+)`?') { $names[$Matches[4]] = 1 }
            elseif ($head -match '^\s*(GRANT|REVOKE|CREATE\s+USER|ALTER\s+USER|DROP\s+USER|SET\s+PASSWORD|DROP\s+DATABASE)\b' -or
                    $head -match '^\s*(INSERT|REPLACE|UPDATE|DELETE)\b.*?`?mysql`?\.') {
                throw "$(Split-Path -Leaf $path) has a statement a data dump must not have: $($head.Trim()) ; nothing imported from it"
            }
        }
    } finally { $r.Dispose() }
    @($names.Keys)
}

# Stream the dump (gz unpacked on the fly) into the client's stdin: no temp files
function ImportFile([string]$path, [string]$db) {
    $psi = New-Object Diagnostics.ProcessStartInfo (MariaExe)
    $psi.Arguments = "`"$(RootOptions)`" --default-character-set=utf8mb4" + $(if ($db) { " `"$db`"" } else { '' })
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    # The stdin writer takes the console input encoding; a UTF-8 one with BOM (chcp 65001) would inject a BOM
    $oldEnc = [Console]::InputEncoding
    try { [Console]::InputEncoding = New-Object Text.UTF8Encoding $false } catch { }
    try { $p = [Diagnostics.Process]::Start($psi) } finally { try { [Console]::InputEncoding = $oldEnc } catch { } }
    $src = OpenDump $path
    $copyError = $null
    try { $src.CopyTo($p.StandardInput.BaseStream) } catch { $copyError = $_ }
    finally { $src.Dispose(); try { $p.StandardInput.Close() } catch { } }
    $p.WaitForExit()
    $partial = "the DB may be half-loaded now: fix the cause, then make db-import FORCE=1"
    if ($p.ExitCode) { throw "import of $path failed (exit $($p.ExitCode)); $partial" }
    if ($copyError) { throw "import of $path stopped: $copyError; $partial" }
}

try {
switch ($Action) {
    'up' {
        if ($Mode -eq 'native') {
            if (Get-NetTCPConnection -LocalPort $DbPort -State Listen -ErrorAction SilentlyContinue) { Write-Host "db: server listens on port $DbPort"; exit 0 }
            $svc = Get-Service 'MariaDB*' -ErrorAction SilentlyContinue | Select-Object -First 1
            if (-not $svc) { throw "nothing listens on port $DbPort and no MariaDB service found: install MariaDB (make doctor shows the command)" }
            Write-Host "db: starting service $($svc.Name)"
            try { Start-Service $svc.Name } catch { throw "cannot start $($svc.Name): open services.msc and start it, or run 'Start-Service $($svc.Name)' as administrator" }
            Write-Host "db: service $($svc.Name) running"; exit 0
        }
        $state = docker inspect -f '{{.State.Status}}' $Container 2>$null
        if (-not $state) { throw "container $Container not found; start the site stack first (see noble-site docker/dev/README.md) or use DB_MODE = native" }
        if ($state -ne 'running') { docker start $Container | Out-Null; Write-Host "db: $Container started" }
        for ($i = 0; $i -lt 60; $i++) {
            $h = docker inspect -f '{{.State.Health.Status}}' $Container
            if ($h -eq 'healthy') { Write-Host "db: $Container healthy"; exit 0 }
            Start-Sleep 2
        }
        throw "db: $Container not healthy after 120 s"
    }

    'ping' {
        if (-not (CoreUserWorks)) { Write-Output 'login=0'; exit 0 }
        Write-Output 'login=1'
        $q = { param($s) CoreSql $s }
        foreach ($d in $dbs) { Write-Output ("{0}{1}={2}" -f $Prefix, $d, (TableCount $q "$Prefix$d")) }
        $realm = $null
        try { $realm = CoreSql "SELECT id FROM ``${Prefix}auth``.realmlist WHERE id=$RealmId;" } catch { }
        Write-Output ("realm={0}" -f $(if ($realm) { 1 } else { 0 }))
    }

    'check' {
        $runtime = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BinDir)
        $sourceDir = Get-UpdateSourceDirectory $runtime
        $core = CoreUserWorks
        if (-not $core -and ($VerifyUpdateTarget -or $Mode -eq 'native')) { throw "db: login as $DbUser failed (run: make db-user)" }
        Write-Host ("db: connect as {0}@{1}:{2} -> {3}" -f $DbUser, $DbHost, $DbPort, $(if ($core) { 'OK' } else { 'FAILED (run: make db-user); using root in container' }))
        $q = { param($s) if ($core) { CoreSql $s } else { RootSql $s } }
        Write-Host '--- databases (tables)'
        foreach ($d in $dbs) { Write-Host ("  {0,-16} {1}" -f "$Prefix$d", (TableCount $q "$Prefix$d")) }
        Write-Host '--- realmlist (id, name, address, port, gamebuild)'
        & $q "SELECT id, name, address, port, gamebuild FROM ``${Prefix}auth``.realmlist ORDER BY id;" | ForEach-Object { "  $_" }
        Write-Host '--- pending filenames (make db-update, mask 15; absent from updates)'
        Write-Host "  SourceDirectory: $sourceDir"
        $hasDuplicate = $false
        $conflicts = [Collections.Generic.List[string]]::new()
        foreach ($d in 'auth', 'characters', 'world', 'hotfixes') {
            $db = "$Prefix$d"
            $has = & $q "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='$db' AND table_name='updates_include';"
            if ("$has".Trim() -ne '1') { Write-Host "  $db : no updates_include table"; continue }
            $applied = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
            & $q "SELECT name FROM ``$db``.updates;" | ForEach-Object { [void]$applied.Add($_) }
            $files = [Collections.Generic.SortedDictionary[string,string]]::new([StringComparer]::Ordinal)
            foreach ($inc in (& $q "SELECT path FROM ``$db``.updates_include;")) {
                if ($inc.StartsWith('$')) { $dir = $sourceDir + $inc.Substring(1) }
                elseif ([IO.Path]::IsPathRooted($inc)) { $dir = $inc }
                else { $dir = Join-Path $runtime $inc }
                if (-not (Test-Path -LiteralPath $dir -PathType Container)) { Write-Warning "$db : missing include directory $dir"; continue }
                # Native UpdateFetcher starts at depth 1 and visits at most 10 levels.
                foreach ($file in (Get-ChildItem -LiteralPath $dir -Filter *.sql -File -Depth 9)) {
                    if ($file.Extension -cne '.sql') { continue }
                    $path = $file.FullName
                    if ($path.StartsWith($sourceDir.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                        $path = $path.Substring($sourceDir.TrimEnd('\','/').Length + 1)
                    }
                    $path = $path.Replace('\','/')
                    if ($files.ContainsKey($file.Name)) {
                        Write-Warning "$db : duplicate filename $($file.Name): $($files[$file.Name]) and $path"
                        $hasDuplicate = $true
                    } else { $files.Add($file.Name, $path) }
                }
            }
            $pending = @($files.Keys | Where-Object { -not $applied.Contains($_) })
            Write-Host ("  {0,-16} applied {1}, pending {2}" -f $db, $applied.Count, $pending.Count)
            foreach ($name in $pending) {
                "    $name  $($files[$name])"
                if ($name -cin '2026_10_27_00_rps_min_level_10.sql','2026_10_27_02_rps_xp_for_level.sql',
                        '2026_10_27_10_rps_starter_outfit_mail.sql','2026_10_27_11_rps_starter_outfit_mail_v2.sql',
                        '2026_10_27_07_rps_playercreateinfo_cloth.sql') {
                    $conflicts.Add("$db : manual SQL in update queue: $($files[$name]). Remove the stale automatic copy or fix updates_include; keep the file under sql/custom/manual/rps_combat/.")
                }
            }
            if ($d -eq 'auth' -and $pending -ccontains '2026_10_04_housing_commands_rbac.sql') {
                $permissions = [int](& $q "SELECT COUNT(*) FROM ``$db``.rbac_permissions WHERE id BETWEEN 886 AND 891;")
                if ($permissions) {
                    $conflicts.Add("$db : pending Housing RBAC would delete $permissions existing permissions in IDs 886..891 and their grants")
                }
            }
            if ($d -eq 'world' -and $pending -ccontains '2026_09_27_01_world_housing.sql') {
                $counts = & $q @"
SELECT (SELECT COUNT(*) FROM ``$db``.creature WHERE map IN (2735,2736)),
       (SELECT COUNT(*) FROM ``$db``.gameobject WHERE map IN (2735,2736)),
       (SELECT COUNT(*) FROM ``$db``.areatrigger WHERE MapId IN (2735,2736));
"@
                $creatures, $objects, $triggers = $counts -split "`t"
                if ([long]$creatures -or [long]$objects -or [long]$triggers) {
                    $conflicts.Add("$db : pending Housing SQL would clear maps 2735/2736 (creature=$creatures, gameobject=$objects, areatrigger=$triggers)")
                }
            }
        }
        if ($conflicts.Count) { throw ($conflicts -join [Environment]::NewLine) }
        if ($hasDuplicate) { throw 'db: duplicate SQL filenames would stop the native updater' }
    }

    'backup' {
        New-Item -ItemType Directory -Force $BackupDir | Out-Null
        $ts = Get-Date -Format 'yyyyMMdd-HHmmss'
        $name = "wow-local-$ts.sql.gz"
        $list = $dbs | ForEach-Object { "$Prefix$_" }
        $target = Join-Path $BackupDir $name
        if ($Mode -eq 'native') {
            $sqlFile = $target -replace '\.gz$', ''
            $ErrorActionPreference = 'Continue'
            & (MariaExe 'mariadb-dump.exe') (RootOptions) --single-transaction --routines --triggers --events "--result-file=$sqlFile" --databases @list
            $code = $LASTEXITCODE
            $ErrorActionPreference = 'Stop'
            if ($code -or -not ((Get-Content $sqlFile -Tail 1) -match 'Dump completed')) {
                Remove-Item $sqlFile -ErrorAction SilentlyContinue; throw "backup failed (exit $code)"
            }
            $in = [IO.File]::OpenRead($sqlFile)
            $out = [IO.File]::Create($target)
            $gz = New-Object IO.Compression.GZipStream($out, [IO.Compression.CompressionMode]::Compress)
            try { $in.CopyTo($gz) } finally { $gz.Dispose(); $out.Dispose(); $in.Dispose() }
            Remove-Item $sqlFile
        } else {
            # Dump to a file first: in a pipe the exit code would be gzip's, not the dump's
            $sqlFile = "/tmp/" + ($name -replace '\.gz$', '')
            $cmd = 'MYSQL_PWD=$MARIADB_ROOT_PASSWORD mariadb-dump -uroot --single-transaction --routines --triggers --events --databases ' + ($list -join ' ') + " > $sqlFile && tail -n 1 $sqlFile | grep -q 'Dump completed' && gzip -f $sqlFile"
            docker exec $Container sh -c $cmd
            $code = $LASTEXITCODE
            if ($code) { docker exec $Container rm -f $sqlFile "/tmp/$name" | Out-Null; throw "backup failed in container (exit $code)" }
            docker cp "${Container}:/tmp/$name" $target
            if ($LASTEXITCODE) { throw 'docker cp failed' }
            docker exec $Container rm -f "/tmp/$name" | Out-Null
        }
        Write-Host ("db: backup {0} ({1:N1} MB)" -f $target, ((Get-Item $target).Length / 1MB))
    }

    'user' {
        if (-not $env:DB_PASS -or $env:DB_PASS -eq '@CORE_PASS@') { throw 'DB_PASS is empty (run: make init)' }
        RootSql (Render 'core-user.sql') | Out-Null
        Write-Host "db: user $DbUser@'%' ready with ALL on ${Prefix}auth/characters/world/hotfixes/roleplay"
        if (CoreUserWorks) { Write-Host "db: login as $DbUser OK" } else { throw "db: login as $DbUser failed" }
    }

    'realmlist' {
        if ($RealmId -lt 2) { throw 'REALM_ID must be >= 2: row 1 belongs to the site' }
        $row = CoreSql "SELECT name FROM ``${Prefix}auth``.realmlist WHERE id=$RealmId;"
        if ($row -and "$row".Trim() -ne $RealmName) { throw "realmlist id=$RealmId is taken by '$row'; set REALM_ID in dev/local.mk" }
        CoreSql (Render 'realmlist-local.sql') | Out-Null
        $res = CoreSql "SELECT id, name, address, port, gamebuild FROM ``${Prefix}auth``.realmlist WHERE id=$RealmId;"
        if (-not $res) { throw "realmlist id=$RealmId was not created (is row 1 missing? it is the template)" }
        $res | ForEach-Object { "db: realm $_" }
    }

    'import' {
        if ($Mode -ne 'native') { Write-Host 'db: import skipped - in docker mode the DBs come from the site stack'; exit 0 }
        $want = @($dbs | ForEach-Object { "$Prefix$_" })
        $rq = { param($s) RootSql $s }
        $tables = @{}
        foreach ($d in $want) { $tables[$d] = TableCount $rq $d }
        $files = @(Get-ChildItem $DumpDir -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '\.sql(\.gz)?$' } | Sort-Object Name)
        if (-not $files) {
            $empty = @($want | Where-Object { $tables[$_] -eq 0 })
            if ($empty) { throw "no *.sql or *.sql.gz in $DumpDir, empty DBs: $($empty -join ', '). Get a dump from a developer (make db-backup) and put it there" }
            Write-Host 'db: no dumps and all DBs have tables - nothing to import'; exit 0
        }
        foreach ($f in $files) {
            Write-Host "db: reading $($f.Name)"
            $names = DumpDbs $f.FullName
            $into = $null
            if ($names) {
                # Multi-DB dump: every DB in it must be one of ours (never system DBs such as mysql)
                $bad = @($names | Where-Object { $_ -notin $want })
                if ($bad) { throw "$($f.Name) has other DBs: $($bad -join ', '); nothing imported from it. Expected: $($want -join ', ') (DB_PREFIX in dev/local.mk)" }
                $targets = $names
            } else {
                $m = [regex]::Match($f.Name, 'auth|characters|world|hotfixes|roleplay', 'IgnoreCase')
                if (-not $m.Success) { throw "$($f.Name): no CREATE DATABASE inside and no DB name (auth, characters, world, hotfixes, roleplay) in the file name" }
                $into = "$Prefix$($m.Value.ToLower())"
                $targets = @($into)
            }
            $full = @($targets | Where-Object { $tables[$_] -gt 0 })
            if ($full -and -not $Force) { Write-Host "db: skip $($f.Name) - $($full -join ', ') already has tables (make db-import FORCE=1 overwrites)"; continue }
            if ($into) { RootSql "CREATE DATABASE IF NOT EXISTS ``$into`` DEFAULT CHARACTER SET utf8mb4;" | Out-Null }
            Write-Host ("db: import {0} ({1:N0} MB) -> {2}" -f $f.Name, ($f.Length / 1MB), ($targets -join ', '))
            ImportFile $f.FullName $into
            foreach ($t in $targets) { $tables[$t] = TableCount $rq $t }
        }
        foreach ($d in $want) { Write-Host ("db: {0,-16} {1} tables" -f $d, $tables[$d]) }
        $empty = @($want | Where-Object { $tables[$_] -eq 0 })
        if ($empty) { throw "still empty: $($empty -join ', ') - add a dump for them to $DumpDir" }
    }
}
} finally {
    if (Test-Path $rootCnf) { Remove-Item $rootCnf -Force }
}
