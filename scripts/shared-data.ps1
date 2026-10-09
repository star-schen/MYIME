# Installer-only provisioning; never called by a key callback.
function Initialize-MyimeSharedData([string]$OwnerSid,[string]$InstallDirectory) {
    $sid = New-Object Security.Principal.SecurityIdentifier $OwnerSid
    if ($sid.Value -ne $OwnerSid) { throw 'Invalid shared profile SID.' }
    $account = $sid.Translate([Security.Principal.NTAccount]).Value
    $digest = [Security.Cryptography.SHA256]::Create()
    try { $suffix = ([BitConverter]::ToString($digest.ComputeHash([Text.Encoding]::UTF8.GetBytes($OwnerSid)))).Replace('-','').Substring(0,16) } finally { $digest.Dispose() }
    $groupName = 'MYIME_' + $suffix
    # LocalAccounts limits descriptions to 48 characters; a full SID is longer.
    # The fixed identity digest matches the group name; membership checks use SID.
    $description = 'MYIME shared profile ' + $suffix
    $group = Get-LocalGroup -Name $groupName -ErrorAction SilentlyContinue
    if (!$group) { $group = New-LocalGroup -Name $groupName -Description $description }
    if ($group.Description -ne $description) { throw 'The MYIME data group name is already used by another group.' }
    $members = @(Get-LocalGroupMember -Group $groupName)
    if (@($members | Where-Object { $_.SID.Value -ne $OwnerSid }).Count) { throw 'Unexpected members in the private MYIME data group; installation stopped before registration.' }
    if (!$members.Count) { Add-LocalGroupMember -Group $groupName -Member $account }
    # Member_of evaluates an enabled token group; membership is refreshed at login.
    $condition = '(Member_of {SID(' + $group.SID.Value + ')})'
    $common = [Environment]::GetFolderPath('CommonApplicationData')
    if (!$common -or ![IO.Path]::IsPathRooted($common)) { throw 'Cannot resolve common application data.' }
    $product = Join-Path $common 'MYIME'
    $users = Join-Path $product 'users'
    $profile = Join-Path $users $OwnerSid
    $presentation = Join-Path $profile 'presentation'
    $learning = Join-Path $profile 'learning'
    foreach ($path in @($product,$users,$profile,$presentation,$learning)) {
        if (Test-Path -LiteralPath $path) {
            if ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Shared MYIME directory is a reparse point: $path" }
        } else { New-Item -ItemType Directory -Path $path -ErrorAction Stop | Out-Null }
    }
    function Set-ProductDirectoryAcl([string]$Path,[string]$Sddl) {
        $security = New-Object Security.AccessControl.DirectorySecurity
        $security.SetSecurityDescriptorSddlForm($Sddl,[Security.AccessControl.AccessControlSections]::Access)
        Set-Acl -LiteralPath $Path -AclObject $security
    }
    $parentAcl = 'D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;;FRFX;;;BU)(A;;FRFX;;;AC)'
    Set-ProductDirectoryAcl $product $parentAcl
    Set-ProductDirectoryAcl $users $parentAcl
    $private = 'D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;' + $OwnerSid + ')'
    Set-ProductDirectoryAcl $profile ($private + '(XA;;FRFX;;;AC;' + $condition + ')')
    Set-ProductDirectoryAcl $presentation ($private + '(XA;OICI;FRFX;;;AC;' + $condition + ')')
    Set-ProductDirectoryAcl $learning ($private + '(XA;OICI;0x1301bf;;;AC;' + $condition + ')')
    foreach ($path in @($presentation,$learning)) {
        $pending = New-Object 'System.Collections.Generic.Queue[string]'
        $pending.Enqueue($path)
        while ($pending.Count) {
            $directory = $pending.Dequeue()
            foreach ($entry in Get-ChildItem -LiteralPath $directory -Force) {
                if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point inside MYIME shared data: $($entry.FullName)" }
                if ($entry.PSIsContainer) { $pending.Enqueue($entry.FullName) }
            }
        }
    }
    $resolvedLearning = [IO.Path]::GetFullPath($learning)
    if (!$resolvedLearning.StartsWith([IO.Path]::GetFullPath($profile) + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Learning label target escaped the MYIME user directory.' }
    & "$env:SystemRoot/System32/icacls.exe" $resolvedLearning /setintegritylevel '(OI)(CI)L' /T /Q
    if ($LASTEXITCODE) { throw 'Cannot label the MYIME learning directory for AppContainer access.' }
    # Installed version files inherit Program Files read access for AppContainers.
    # Publish the machine base only; runtime derives the effective user's SID.
    if (!$InstallDirectory -or ![IO.Path]::IsPathRooted($InstallDirectory)) { throw 'Invalid installed shared storage locator directory.' }
    [IO.File]::WriteAllText((Join-Path $InstallDirectory 'shared-storage-root.txt'),[IO.Path]::GetFullPath($common),(New-Object Text.UTF8Encoding $false))
    Write-Host "MYIME shared data: $profile"
    Write-Host 'A private per-user data group was provisioned. Sign out and sign in for AppContainer access.'
}
