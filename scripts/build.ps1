param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
if (!(Test-Path -LiteralPath (Join-Path $root 'themes/default/theme.toml'))) { throw 'Source tree is incomplete: themes/default/theme.toml is required by include_str!. Restore the complete repository before building.' }
if (!(Get-Command cargo -ErrorAction SilentlyContinue) -and !(Test-Path -LiteralPath "$env:USERPROFILE/.cargo/bin/cargo.exe")) { throw 'Rust cargo is missing. Install the MSVC Rust toolchain before building.' }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path $vswhere)) { throw 'Install Visual Studio 2022 Desktop development with C++ and Windows SDK.' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC x64 tools are missing.' }
# Import the developer environment into this process only.
$dev = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
cmd /d /s /c "`"$dev`" -arch=x64 -host_arch=x64 >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$env:PATH = "$env:USERPROFILE\.cargo\bin;$env:PATH"
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmake) { $cmake = $cmake.Source } else {
    $cmake = Join-Path $root '.tools\cmake-4.4.3-windows-x86_64\bin\cmake.exe'
}
if (!(Test-Path $cmake)) { throw 'CMake is missing. Run scripts/bootstrap.ps1.' }
$profile = if ($Configuration -eq 'Release') { 'release' } else { 'debug' }
& "$PSScriptRoot/build-rime.ps1" -CMake $cmake
$env:RIME_ROOT = Join-Path $root '.deps/rime-shared/dist'
$cargoArgs = @('build','-p','myime-core','--locked')
if ($Configuration -eq 'Release') { $cargoArgs += '--release' }
& cargo @cargoArgs
if ($LASTEXITCODE) { throw 'Rust build failed' }
$toolArgs = @('build','-p','myime-tool','--locked','--target-dir',"target/maintenance-$profile")
if ($Configuration -eq 'Release') { $toolArgs += '--release' }
& cargo @toolArgs
if ($LASTEXITCODE) { throw 'Rust maintenance tool build failed' }
& $cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 "-DRUST_PROFILE=$profile"
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& $cmake --build build --config $Configuration
if ($LASTEXITCODE) { throw 'C++ build failed' }
& "$PSScriptRoot/build-settings.ps1" -Configuration $Configuration
