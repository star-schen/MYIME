param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (!([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run this script from a 64-bit Administrator PowerShell.' }
if (![Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell for the x64 IME.' }
$root = Split-Path $PSScriptRoot -Parent
$package = Join-Path $root "out/MYIME-$Configuration"
$destination = Join-Path $env:ProgramFiles 'MYIME'
if (Test-Path 'Registry::HKEY_LOCAL_MACHINE\Software\Classes\CLSID\{9CA315A1-16D5-4B2E-9368-978D7E9C2215}') { throw 'MYIME is already registered. Uninstall, close applications using it, then install the new version.' }
if (!(Test-Path "$package/myime_host.dll")) { throw 'Run scripts/package.ps1 first.' }
New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item "$package/*" $destination -Recurse -Force
$process = Start-Process "$env:SystemRoot/System32/regsvr32.exe" -ArgumentList @('/s',('"' + "$destination/myime_host.dll" + '"')) -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode) { throw "Registration failed ($($process.ExitCode)); files retained at $destination for diagnosis." }
Write-Host 'MYIME registered. Select MYIME Rime from the Chinese input methods / Win+Space; restart target applications if needed.'
