$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (!([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run this script from a 64-bit Administrator PowerShell.' }
if (![Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell.' }
$dll = Join-Path $env:ProgramFiles 'MYIME/myime_host.dll'
if (!(Test-Path $dll)) { throw "Installed DLL not found: $dll" }
$process = Start-Process "$env:SystemRoot/System32/regsvr32.exe" -ArgumentList @('/s','/u',('"' + $dll + '"')) -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode) { throw "Unregistration failed ($($process.ExitCode))." }
Write-Host 'MYIME unregistered. Installed binaries and user dictionaries are retained; close target applications before replacing binaries.'
