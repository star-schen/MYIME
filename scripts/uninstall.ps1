$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/registration.ps1"
Assert-InstallerAdministrator
$dll = Get-RegisteredMyimeDll
if (!$dll) { Write-Host 'MYIME is not registered. No changes made.'; return }
Invoke-MyimeRegistration -Dll $dll -Unregister
Write-Host 'MYIME unregistered. User dictionaries, configuration and binaries were retained.'
Write-Host 'Sign out and sign in again to unload the IME from apps and Windows shell processes.'
