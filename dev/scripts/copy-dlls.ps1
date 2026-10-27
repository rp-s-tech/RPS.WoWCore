# Copy the runtime DLLs next to the exe files (same order as .github/workflows/win-x64-build.yml).
param([string]$BinDir, [string]$MySqlDir, [string]$OpenSslDir)
$ErrorActionPreference = 'Stop'
# Runs before the first build too: the build starts tests.exe to list the tests
New-Item -ItemType Directory -Force $BinDir | Out-Null

Copy-Item "$MySqlDir/lib/libmysql.dll" $BinDir -Force
# MySQL may ship its own OpenSSL; copy those first, then let OpenSSL's own win
Get-ChildItem -Path "$MySqlDir/bin/*" -Include libcrypto*, libssl* -File |
    Copy-Item -Destination $BinDir -Force
Get-ChildItem -Path "$OpenSslDir/bin/*" -Include libcrypto*, libssl*, legacy* -File |
    Copy-Item -Destination $BinDir -Force

Get-ChildItem $BinDir -Filter *.dll | ForEach-Object { Write-Host "dlls: $($_.Name)" }
