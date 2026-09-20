param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
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
$cargoArgs = @('build','--workspace','--locked')
if ($Configuration -eq 'Release') { $cargoArgs += '--release' }
& cargo @cargoArgs
if ($LASTEXITCODE) { throw 'Rust build failed' }
& $cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 "-DRUST_PROFILE=$profile"
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& $cmake --build build --config $Configuration
if ($LASTEXITCODE) { throw 'C++ build failed' }
