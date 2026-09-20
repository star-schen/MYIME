param([string]$RuntimeRoot = (Join-Path (Split-Path $PSScriptRoot -Parent) 'runtime'), [ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$shared = Join-Path $RuntimeRoot 'shared'
$user = Join-Path $RuntimeRoot 'user'
New-Item -ItemType Directory -Force $shared,$user | Out-Null
foreach ($repo in @('rime-prelude','rime-pinyin-simp','rime-stroke')) {
    Copy-Item "$root/.deps/$repo/*.yaml" $shared -Force
}
Copy-Item "$root/.deps/rime-essay/essay.txt" $shared -Force
# This is a product-owned patch. Never rewrite an existing user patch.
$patch = Join-Path $user 'default.custom.yaml'
if (!(Test-Path $patch)) {
    [IO.File]::WriteAllText($patch, "patch:`n  schema_list:`n    - schema: pinyin_simp`n  menu/page_size: 7`n", (New-Object Text.UTF8Encoding $false))
}
& "$root/build/$Configuration/myime-probe.exe" --deploy $shared $user
if ($LASTEXITCODE) { throw 'Rime deploy failed' }
if (!(Test-Path "$user/build/pinyin_simp.table.bin")) { throw 'Rime did not produce the pinyin dictionary; inspect Rime logs.' }
New-Item -ItemType Directory -Force "$shared/build" | Out-Null
Copy-Item "$user/build/*" "$shared/build" -Force
$destination = Join-Path $root "build/$Configuration/data/shared"
New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item "$shared/*" $destination -Recurse -Force
