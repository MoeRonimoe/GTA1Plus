# Stellt das Paket zum Weitergeben zusammen: dist\GTA1Plus (Ordnerstruktur wie im Spielordner) und dist\GTA1Plus.zip.
# Vorher Release|x86 bauen.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root "dist"
$pkg = Join-Path $dist "GTA1Plus"

$files = @{
    "Release\Loader\dsound.dll"                    = "dsound.dll"
    "Release\GTA1Plus\GTA1Plus.asi"                = "scripts\GTA1Plus.asi"
    "Release\GTA1Plus\GTA1Plus_PlayerColors.png"   = "scripts\GTA1Plus_PlayerColors.png"
    "package\README.txt"                           = "README.txt"
}

if (Test-Path $pkg) { Remove-Item $pkg -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $pkg "scripts") | Out-Null
foreach ($src in $files.Keys) {
    $from = Join-Path $root $src
    if (-not (Test-Path $from)) { throw "Fehlt: $src (Release|x86 gebaut?)" }
    Copy-Item $from (Join-Path $pkg $files[$src])
}

$zip = Join-Path $dist "GTA1Plus.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $pkg -DestinationPath $zip
Get-ChildItem $pkg -Recurse -File | ForEach-Object { "{0,-40} {1,10:N0} Byte" -f $_.FullName.Substring($pkg.Length + 1), $_.Length }
"Paket: $zip"
