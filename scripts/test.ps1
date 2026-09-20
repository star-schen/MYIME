param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
& "$PSScriptRoot/build.ps1" -Configuration $Configuration
$root = Split-Path $PSScriptRoot -Parent
$env:PATH = "$root/.deps/librime/dist/lib;$env:PATH"
$testArgs = @('test','--workspace','--locked')
if ($Configuration -eq 'Release') { $testArgs += '--release' }
& cargo @testArgs
if ($LASTEXITCODE) { throw 'Rust tests failed' }
& "$PSScriptRoot/prepare-data.ps1" -Configuration $Configuration
& "$root/build/$Configuration/myime-probe.exe" "$root/runtime/shared" "$root/runtime/user"
if ($LASTEXITCODE) { throw 'C ABI / librime integration failed' }
& "$root/build/$Configuration/myime-com-probe.exe"
if ($LASTEXITCODE) { throw 'COM lifetime test failed' }
& "$root/build/$Configuration/myime-tsf-probe.exe"
if ($LASTEXITCODE) { throw 'TSF document test failed' }
& "$root/build/$Configuration/myime-tsf-probe.exe" --reject
if ($LASTEXITCODE) { throw 'TSF rejection isolation failed' }
& "$root/build/$Configuration/myime-compat.exe" --self-check
if ($LASTEXITCODE) { throw 'Compatibility app initialization failed' }
