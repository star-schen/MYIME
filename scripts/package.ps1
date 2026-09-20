param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$source = Join-Path $root "build/$Configuration"
$package = Join-Path $root "out/MYIME-$Configuration"
if (!(Test-Path "$source/data/shared/build/pinyin_simp.table.bin")) { throw 'Build and prepare data first using the matching Configuration.' }
New-Item -ItemType Directory -Force $package,"$package/notices" | Out-Null
foreach ($file in @('myime_host.dll','myime_core.dll','rime.dll','myime-compat.exe')) { Copy-Item "$source/$file" $package -Force }
Copy-Item "$source/data" $package -Recurse -Force
Copy-Item "$root/LICENSE","$root/README.md","$root/THIRD_PARTY.md","$root/dependencies.lock.json" $package -Force
Copy-Item "$root/config/product.example.toml" $package -Force
foreach ($entry in (Get-Content "$root/dependencies.lock.json" -Raw | ConvertFrom-Json).data) {
    $notice = Join-Path $package "notices/$($entry.repo)"
    New-Item -ItemType Directory -Force $notice | Out-Null
    Copy-Item "$root/.deps/$($entry.repo)/LICENSE" $notice -Force
    if (Test-Path "$root/.deps/$($entry.repo)/AUTHORS") { Copy-Item "$root/.deps/$($entry.repo)/AUTHORS" $notice -Force }
}
Write-Host "Local development package: $package (not a public release package; see THIRD_PARTY.md)"
