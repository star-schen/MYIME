# Shared source of truth for installation and removal. No actions at import.
$MyimeRegistrationKey = 'Registry::HKEY_LOCAL_MACHINE\Software\Classes\CLSID\{9CA315A1-16D5-4B2E-9368-978D7E9C2215}\InprocServer32'
function Assert-InstallerAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if (!([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator rights required. Use Install-MYIME.cmd to request elevation automatically.'
    }
    if (![Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell for this x64 IME.' }
}
function Get-RegisteredMyimeDll {
    if (Test-Path -LiteralPath $MyimeRegistrationKey) {
        $key = Get-Item -LiteralPath $MyimeRegistrationKey
        try {
            $value = $key.GetValue('')
            if (!$value) { throw 'MYIME registration has no DLL path; manual repair is required.' }
            return [string]$value
        } finally { $key.Close() }
    }
    return $null
}
function Invoke-MyimeRegistration {
    param([Parameter(Mandatory)][string]$Dll,[switch]$Unregister)
    if (!(Test-Path -LiteralPath $Dll -PathType Leaf)) { throw "DLL not found: $Dll" }
    $arguments = @('/s')
    if ($Unregister) { $arguments += '/u' }
    $arguments += ('"' + $Dll + '"')
    $process = Start-Process "$env:SystemRoot/System32/regsvr32.exe" -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode) { throw "regsvr32 failed ($($process.ExitCode)): $Dll; unregister=$Unregister" }
}
