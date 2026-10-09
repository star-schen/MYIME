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
Write-Host 'Data and CMake prepared. build.ps1 will prepare the pinned source librime SDK, then build MYIME. No tests are run by this script.'
