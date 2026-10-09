param([Parameter(Mandatory=$true)][string]$CMake)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$cache = Join-Path $root '.deps/rime-shared'
$upstream = Join-Path $cache 'upstream'
$vcpkg = Join-Path $root '.deps/vcpkg-myime'
$dist = Join-Path $cache 'dist'
$inputs = @('dependencies.lock.json','scripts/build-rime.ps1','scripts/patch-rime-storage.ps1','native/rime_storage/CMakeLists.txt','native/rime_storage/rime_storage.h','native/rime_storage/rime_storage.cpp')
$identity = ($inputs | ForEach-Object { (Get-FileHash -LiteralPath (Join-Path $root $_) -Algorithm SHA256).Hash }) -join ':'
$stamp = Join-Path $dist '.myime-storage-build'
if ((Test-Path -LiteralPath $stamp) -and [IO.File]::ReadAllText($stamp) -eq $identity -and (Test-Path -LiteralPath (Join-Path $dist 'lib/rime.dll')) -and (Test-Path -LiteralPath (Join-Path $dist 'lib/rime.lib')) -and (Test-Path -LiteralPath (Join-Path $dist 'include/rime_storage.h'))) { return }
function Pinned-Checkout([string]$Url,[string]$Path,[string]$Commit) {
    if (!(Test-Path -LiteralPath (Join-Path $Path '.git'))) {
        & git -c core.autocrlf=false clone --no-checkout -- $Url $Path
        if ($LASTEXITCODE) { throw "Cannot obtain dependency: $Url" }
    }
    $index = & git -C $Path rev-parse --git-path index
    if ($LASTEXITCODE -or !$index) { throw "Cannot inspect dependency index: $Path" }
    if (![IO.Path]::IsPathRooted($index)) { $index = Join-Path $Path $index }
    # --no-checkout leaves no index; status reports deletions before the first
    # checkout. Also recover that empty state when a previous attempt stopped.
    if (!(Test-Path -LiteralPath $index)) {
        $existing = @(Get-ChildItem -LiteralPath $Path -Force | Where-Object { $_.Name -ne '.git' })
        if ($existing.Count) { throw "Dependency checkout is incomplete and contains local files: $Path. Preserve them before rebuilding." }
        Write-Host "Preparing the first dependency checkout: $Path"
    } else {
        $dirty = & git -C $Path status --porcelain --untracked-files=no
        if ($LASTEXITCODE) { throw "Cannot inspect dependency checkout: $Path" }
        if ($dirty) { throw "Dependency checkout has local changes: $Path. Preserve them before rebuilding." }
    }
    $cachedCommit = & git -C $Path rev-parse --verify --quiet "${Commit}^{commit}"
    if ($LASTEXITCODE -ne 0) {
        & git -C $Path fetch --depth 1 origin $Commit
        if ($LASTEXITCODE) { throw "Cannot fetch pinned dependency: $Url" }
    } else {
        Write-Host "Using cached pinned dependency: $cachedCommit"
    }
    & git -C $Path checkout --detach $Commit
    if ($LASTEXITCODE) { throw 'Pinned dependency checkout failed.' }
}
function Invoke-CMakeLogged([string[]]$Arguments,[string]$Log,[string]$Failure) {
    $previousErrorAction = $ErrorActionPreference
    try {
        # Windows PowerShell otherwise treats native stderr as a terminating
        # error. Keep all diagnostics and use the native exit code instead.
        $ErrorActionPreference = 'Continue'
        & $CMake @Arguments 2>&1 | Tee-Object -FilePath $Log
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousErrorAction }
    if ($exitCode) { throw "$Failure. See $Log" }
}
New-Item -ItemType Directory -Path $cache -Force | Out-Null
Pinned-Checkout 'https://github.com/rime/librime.git' $upstream $lock.librime.commit
Pinned-Checkout 'https://github.com/microsoft/vcpkg.git' $vcpkg $lock.vcpkg.commit
if (!(Test-Path -LiteralPath (Join-Path $vcpkg 'vcpkg.exe'))) {
    & (Join-Path $vcpkg 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE) { throw 'vcpkg bootstrap failed.' }
}
$env:VCPKG_DISABLE_METRICS = '1'
$ports = @($lock.vcpkg.ports | ForEach-Object { $_ + ':x64-windows-static' })
& (Join-Path $vcpkg 'vcpkg.exe') install @ports --disable-metrics
if ($LASTEXITCODE) { throw 'Pinned librime dependency build failed.' }
# A fresh product source tree preserves the immutable upstream checkout.
$digest = [Security.Cryptography.SHA256]::Create()
try { $generation = ([BitConverter]::ToString($digest.ComputeHash([Text.Encoding]::UTF8.GetBytes($identity)))).Replace('-','').Substring(0,20).ToLowerInvariant() } finally { $digest.Dispose() }
$cacheRoot = [IO.Path]::GetFullPath($cache).TrimEnd('\')
$source = [IO.Path]::GetFullPath((Join-Path $cacheRoot ('source-' + $generation)))
$build = Join-Path $cacheRoot ('build-' + $generation)
# A failed build can leave a partially unpacked or patched source tree. Start
# from the immutable archive again rather than extracting over existing files.
if ($generation -notmatch '^[0-9a-f]{20}$' -or
    [IO.Path]::GetDirectoryName($source) -ne $cacheRoot -or
    [IO.Path]::GetFileName($source) -ne ('source-' + $generation)) {
    throw 'Generated librime source path is outside its cache.'
}
if ((Get-Item -LiteralPath $cacheRoot -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw 'Librime cache must not redirect to another directory.'
}
if (Test-Path -LiteralPath $source) {
    $stagedSource = Get-Item -LiteralPath $source -Force
    if (!$stagedSource.PSIsContainer -or ($stagedSource.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Generated librime source must be a regular cache directory.'
    }
    Remove-Item -LiteralPath $source -Recurse -Force
}
New-Item -ItemType Directory -Path $source -Force | Out-Null
$archive = Join-Path $cache ('source-' + $generation + '.tar')
& git -C $upstream archive --format=tar --output=$archive $lock.librime.commit
if ($LASTEXITCODE) { throw 'Cannot stage pinned librime source.' }
Push-Location $source
try { & $CMake -E tar xf $archive; if ($LASTEXITCODE) { throw 'Cannot unpack librime source.' } } finally { Pop-Location }
& (Join-Path $PSScriptRoot 'patch-rime-storage.ps1') -Source $source
$plugin = Join-Path $source 'plugins/myime-storage'
$headers = Join-Path $source 'src/myime_storage'
New-Item -ItemType Directory -Path $plugin,$headers -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'native/rime_storage/CMakeLists.txt'),(Join-Path $root 'native/rime_storage/rime_storage.cpp'),(Join-Path $root 'native/rime_storage/rime_storage.h') -Destination $plugin -Force
Copy-Item -LiteralPath (Join-Path $root 'native/rime_storage/rime_storage.h') -Destination $headers -Force
$prefix = Join-Path $vcpkg 'installed/x64-windows-static'
$previousPlugins = $env:RIME_PLUGINS
$env:RIME_PLUGINS = 'myime-storage'
try {
    New-Item -ItemType Directory -Path $build -Force | Out-Null
    $configureArguments = @('-S',$source,'-B',$build,'-G','Visual Studio 17 2022','-A','x64',
        "-DCMAKE_INSTALL_PREFIX=$dist", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        "-DCMAKE_PREFIX_PATH=$prefix", "-DCMAKE_INCLUDE_PATH=$prefix/include", "-DCMAKE_LIBRARY_PATH=$prefix/lib",
        '-DBUILD_SHARED_LIBS=ON', '-DBUILD_STATIC=ON', '-DBUILD_MERGED_PLUGINS=ON',
        '-DBUILD_TEST=OFF', '-DBUILD_SAMPLE=OFF', '-DBUILD_DATA=OFF', '-DENABLE_LOGGING=OFF', '-DENABLE_EXTERNAL_PLUGINS=OFF')
    Invoke-CMakeLogged -Arguments $configureArguments -Log (Join-Path $build 'configure.log') -Failure 'Product librime configuration failed'
    Invoke-CMakeLogged -Arguments @('--build',$build,'--config','Release','--target','rime','--parallel') -Log (Join-Path $build 'compile.log') -Failure 'Product librime build failed'
} finally { $env:RIME_PLUGINS = $previousPlugins }
New-Item -ItemType Directory -Path (Join-Path $dist 'lib'),(Join-Path $dist 'include') -Force | Out-Null
# Stage only the target we built; upstream's install rule also expects tools.
Copy-Item -LiteralPath (Join-Path $build 'bin/Release/rime.dll'),(Join-Path $build 'src/Release/rime.lib') -Destination (Join-Path $dist 'lib') -Force
Get-ChildItem -LiteralPath (Join-Path $source 'src') -Filter '*.h' -File | Where-Object { $_.Name -notlike '*_impl.h' } | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $dist 'include') -Force }
Copy-Item -LiteralPath (Join-Path $root 'native/rime_storage/rime_storage.h') -Destination (Join-Path $dist 'include/rime_storage.h') -Force
Copy-Item -LiteralPath (Join-Path $upstream 'LICENSE') -Destination (Join-Path $dist 'LICENSE') -Force
[IO.File]::WriteAllText($stamp,$identity,(New-Object Text.UTF8Encoding $false))
Write-Host 'Product librime prepared with shared learning storage. No tests were run.'
