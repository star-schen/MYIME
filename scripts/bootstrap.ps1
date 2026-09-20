$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$lock = Get-Content dependencies.lock.json -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Force .tools,.deps | Out-Null
function Get-VerifiedArchive($item, $path) {
    if (!(Test-Path $path)) { Invoke-WebRequest $item.url -OutFile $path }
    if ((Get-FileHash $path -Algorithm SHA256).Hash -ne $item.sha256) { throw "SHA256 mismatch: $path. Remove this archive and retry." }
}
Get-VerifiedArchive $lock.cmake '.tools/cmake.zip'
$cmake = "$root/.tools/cmake-$($lock.cmake.version)-windows-x86_64/bin/cmake.exe"
if (!(Test-Path $cmake)) { Expand-Archive .tools/cmake.zip -DestinationPath .tools -Force }
Get-VerifiedArchive $lock.librime '.deps/rime.7z'
New-Item -ItemType Directory -Force .deps/librime | Out-Null
Push-Location .deps/librime
try { & $cmake -E tar xf ../rime.7z; if ($LASTEXITCODE) { throw 'librime archive extraction failed' } } finally { Pop-Location }
foreach ($entry in $lock.data) {
    $path = Join-Path $root ".deps/$($entry.repo)"
    if (!(Test-Path "$path/.git")) {
        & git clone --no-checkout "https://github.com/rime/$($entry.repo).git" $path
        if ($LASTEXITCODE) { throw "Clone failed: $($entry.repo)" }
    }
    & git -C $path cat-file -e "$($entry.commit)^{commit}" 2>$null
    if ($LASTEXITCODE) { & git -C $path fetch origin $entry.commit; if ($LASTEXITCODE) { throw 'Fetch failed' } }
    & git -C $path checkout --detach $entry.commit
    if ($LASTEXITCODE) { throw 'Pinned data checkout failed' }
}
Write-Host 'Dependencies prepared. Run scripts/build.ps1, then scripts/prepare-data.ps1.'
