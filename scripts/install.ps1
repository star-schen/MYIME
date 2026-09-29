param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/registration.ps1"
Assert-InstallerAdministrator
$root = Split-Path $PSScriptRoot -Parent
$package = Join-Path $root "out/MYIME-$Configuration"
foreach ($required in @('myime_host.dll','myime_core.dll','rime.dll','data/shared/build/pinyin_simp.table.bin')) {
    if (!(Test-Path -LiteralPath (Join-Path $package $required) -PathType Leaf)) {
        throw "Package incomplete ($required). Build, prepare data and package before installing."
    }
}
$previous = Get-RegisteredMyimeDll
if ($previous -and !(Test-Path -LiteralPath $previous -PathType Leaf)) {
    throw "Registered DLL is missing: $previous. Restore that version before upgrading; registration has not been changed."
}
# Never overwrite loaded binaries. Complete staging before changing registration.
$version = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$destination = Join-Path $env:ProgramFiles "MYIME/versions/$version"
New-Item -ItemType Directory -Path $destination -ErrorAction Stop | Out-Null
Get-ChildItem -LiteralPath $package -Force | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $destination -Recurse -Force
}
$newDll = Join-Path $destination 'myime_host.dll'
if ($previous) { Invoke-MyimeRegistration -Dll $previous -Unregister }
try {
    Invoke-MyimeRegistration -Dll $newDll
} catch {
    $failure = $_.Exception.Message
    try { Invoke-MyimeRegistration -Dll $newDll -Unregister }
    catch { Write-Warning "New registration cleanup failed: $($_.Exception.Message)" }
    if ($previous) {
        try { Invoke-MyimeRegistration -Dll $previous; Write-Warning 'Previous registration restored.' }
        catch { Write-Warning "Previous registration could not be restored: $($_.Exception.Message). Previous DLL: $previous" }
    }
    throw "New registration failed: $failure. Staged files retained at $destination."
}
Write-Host "MYIME registered: $destination"
Write-Host 'Sign out and sign in again before testing: running apps may still hold the previous IME.'
Write-Host 'User dictionaries, configuration and previous versions were retained. No apps were closed.'
