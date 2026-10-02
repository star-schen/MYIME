param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework 4.8 x64 compiler is unavailable. Enable .NET Framework on Windows.' }
$source = Join-Path $root 'windows\settings'
$output = Join-Path $root "build\$Configuration"
New-Item -ItemType Directory -Force -Path $output | Out-Null
$arguments = @('/nologo', '/target:winexe', '/platform:x64', '/codepage:65001', '/warn:4', '/warnaserror', '/reference:System.Windows.Forms.dll', '/reference:System.Drawing.dll', '/reference:System.Web.Extensions.dll', "/win32manifest:$source\settings.manifest", "/out:$output\myime-settings.exe")
$arguments += "/win32icon:$root\windows\host\assets\brand.ico"
if ($Configuration -eq 'Release') { $arguments += '/optimize+' } else { $arguments += '/debug:full' }
$arguments += @(Get-ChildItem -LiteralPath $source -Filter '*.cs' | Sort-Object Name | ForEach-Object { $_.FullName })
& $compiler @arguments
if ($LASTEXITCODE) { throw 'C# settings build failed' }
Copy-Item -LiteralPath (Join-Path $source 'myime-settings.exe.config') -Destination $output -Force
Write-Host "C# settings: $output\myime-settings.exe"
