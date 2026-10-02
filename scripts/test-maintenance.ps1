param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$bin = [IO.Path]::GetFullPath((Join-Path $root "build/$Configuration"))
$artifacts = Join-Path $bin 'test-artifacts'
New-Item -ItemType Directory -Path $artifacts -Force | Out-Null
$fixture = Join-Path $artifacts ('myime-maintenance-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
[IO.File]::WriteAllText((Join-Path $fixture '.myime-test-fixture'), 'MYIME test fixture')
$encoding = New-Object Text.UTF8Encoding $false
function Invoke-JsonTool([hashtable]$Request) {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = Join-Path $bin 'myime-tool.exe'
    $start.Arguments = '--request'
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true; $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $encoding; $start.StandardErrorEncoding = $encoding
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    try {
        [void]$process.Start()
        $writer = New-Object IO.StreamWriter($process.StandardInput.BaseStream, $encoding)
        $writer.Write(($Request | ConvertTo-Json -Depth 12 -Compress)); $writer.Close()
        $output = $process.StandardOutput.ReadToEndAsync(); $errors = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(30000)) { $process.Kill(); throw 'Maintenance JSON fixture timed out' }
        $reply = $output.Result | ConvertFrom-Json
        if ($process.ExitCode -or !$reply.ok) { throw "Maintenance fixture: $($reply.kind): $($reply.error)" }
        if ($reply.PSObject.Properties.Name -contains 'result') { return $reply.result }
        return $reply
    } finally { $process.Dispose() }
}
function Invoke-Deploy([string[]]$Arguments, [bool]$Success = $true) {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = Join-Path $bin 'myime-data-tool.exe'
    $quoted = foreach ($argument in $Arguments) { '"' + ($argument -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"' }
    $start.Arguments = $quoted -join ' '
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $encoding; $start.StandardErrorEncoding = $encoding
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    try {
        [void]$process.Start()
        $output = $process.StandardOutput.ReadToEndAsync(); $errors = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(180000)) { $process.Kill(); throw 'Isolated deployment timed out' }
        $reply = $output.Result | ConvertFrom-Json
        $code = $process.ExitCode
        if ($Success -and ($code -or !$reply.ok)) { throw "Isolated deployment failed: $($reply.error) $($errors.Result)" }
        if (!$Success -and (!$code -or $reply.ok)) { throw 'Expected deployment rejection did not occur' }
        return $reply
    } finally { $process.Dispose() }
}
$config = Join-Path $fixture 'config.toml'
function Set-Schema([string]$Schema, [string]$Scope = 'windows', [string]$Executable = '') {
    $snapshot = Invoke-JsonTool @{version=1;command='config.read';path=$config}
    $changes = @{schema=$Schema}
    if ([string]::IsNullOrEmpty($Schema)) { $changes.schema = $null }
    [void](Invoke-JsonTool @{version=1;command='config.update';path=$config;expected=$snapshot.revision;scope=$Scope;executable=$Executable;changes=$changes})
}
function Import-Package([string]$Id,[string]$Word,[string]$Code) {
    $input = Join-Path $fixture "$Id.csv"
    [IO.File]::WriteAllText($input, "word,code,frequency`n$Word,$Code,10000`n", $encoding)
    $preview = Invoke-JsonTool @{version=1;command='import.preview';input_path=$input;format='csv'}
    [void](Invoke-JsonTool @{version=1;command='import.write';input_path=$input;format='csv';input_revision=$preview.input_revision;packages_root=(Join-Path $fixture 'packages');id=$Id;name=$Id})
    return Join-Path $fixture "packages/$Id"
}
function Assert-Selector([string]$Expected) {
    if ([IO.File]::ReadAllText((Join-Path $fixture 'rime/active-workspace.txt')) -ne $Expected) { throw 'Rejected operation replaced the active workspace' }
}
$old = Invoke-JsonTool @{version=1;command='config.read';path=$config}
$saved = Invoke-JsonTool @{version=1;command='config.update';path=$config;expected=$old.revision;scope='default';changes=@{theme='dark';full_shape=$false}}
if ($saved.effective.theme -ne 'dark') { throw 'Config fixture did not save theme' }
# ASCII source also works in Windows PowerShell 5.1 without a UTF-8 BOM.
$firstWord = -join ([char[]]@(0x6d4b,0x8bd5,0x8bcd,0x5e93,0x7532))
$secondWord = -join ([char[]]@(0x6d4b,0x8bd5,0x8bcd,0x5e93,0x4e59))
$firstPackage = Import-Package 'fixture' $firstWord 'ce shi ci ku jia'
$secondPackage = Import-Package 'secondfixture' $secondWord 'ce shi ci ku yi'
$patches = Join-Path $fixture 'rime/patches'
New-Item -ItemType Directory -Path $patches -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $patches 'default.custom.yaml'), "# preserve fixture comment`npatch:`n  schema_list:`n    - schema: pinyin_simp`n  menu/page_size: 9`n  future/example: true`n", $encoding)
$buildArgs = @('--build','--data-root',$fixture,'--patch-dir',$patches)
$first = Invoke-Deploy ($buildArgs + @('--dictionary-dir',$firstPackage))
if ($first.schema -ne 'myime_global') { throw 'Global dictionary schema was not produced' }
$generation = Join-Path $fixture "rime/workspaces/$($first.workspace)"
if ([IO.File]::ReadAllText((Join-Path $generation 'user/default.custom.yaml')) -ne [IO.File]::ReadAllText((Join-Path $patches 'default.custom.yaml'))) { throw 'Native patch bytes were not preserved' }
$compiled = [IO.File]::ReadAllText((Join-Path $generation 'shared/build/pinyin_simp.schema.yaml'))
if ($compiled -notmatch 'page_size:\s*9') { throw 'Custom page size did not reach official deployment' }
$globalSchema = [IO.File]::ReadAllText((Join-Path $generation 'shared/build/myime_global.schema.yaml'))
if ($globalSchema -notmatch 'dependencies:[\s\S]*?stroke' -or $globalSchema -notmatch 'schema_id:\s*myime_global') { throw 'Derived schema metadata did not retain the base dependency and explicit identity' }
Set-Schema 'myime_global'
$second = Invoke-Deploy ($buildArgs + @('--dictionary-dir',$secondPackage))
if ($second.schema -ne 'myime_global' -or $second.workspace -eq $first.workspace) { throw 'Second package did not produce a new global workspace' }
$selectorFile = Join-Path $fixture 'rime/active-workspace.txt'
$selector = [IO.File]::ReadAllText($selectorFile)
& "$bin/myime-workspace-probe.exe" $fixture $bin 'myime_global' 'ceshicikujia' $firstWord 'ceshicikuyi' $secondWord
if ($LASTEXITCODE) { throw 'Both enabled packages must produce Rime candidates in one schema' }
# The probe deliberately changes its private selector for the lease test.
[IO.File]::WriteAllText($selectorFile, $selector, $encoding)
[IO.File]::WriteAllText((Join-Path $patches 'pinyin_simp.custom.yaml'), "patch:`n  engine/processors: []`n", $encoding)
[void](Invoke-Deploy $buildArgs $false); Assert-Selector $selector
Remove-Item -LiteralPath (Join-Path $patches 'pinyin_simp.custom.yaml')
[IO.File]::WriteAllText((Join-Path $patches 'stroke.custom.yaml'), "patch:`n  engine/processors: []`n", $encoding)
[void](Invoke-Deploy $buildArgs $false); Assert-Selector $selector
Remove-Item -LiteralPath (Join-Path $patches 'stroke.custom.yaml')
$dict = Join-Path $secondPackage 'secondfixture.dict.yaml'
$original = [IO.File]::ReadAllText($dict)
[IO.File]::WriteAllText($dict, $original + "# corrupted after preview`n", $encoding)
[void](Invoke-Deploy ($buildArgs + @('--dictionary-dir',$secondPackage)) $false); Assert-Selector $selector
[IO.File]::WriteAllText($dict, $original, $encoding)
$lock = [IO.File]::Open((Join-Path $fixture 'rime/.deploy.lock'),[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try { [void](Invoke-Deploy $buildArgs $false); Assert-Selector $selector } finally { $lock.Dispose() }
Set-Schema 'myime_secondfixture' 'profile' 'Fixture.exe'
[void](Invoke-Deploy @('--rollback','--data-root',$fixture) $false); Assert-Selector $selector
Set-Schema $null 'profile' 'Fixture.exe'
$rollback = Invoke-Deploy @('--rollback','--data-root',$fixture)
if ($rollback.workspace -ne $first.workspace -or $rollback.schema -ne 'myime_global') { throw 'Rollback did not restore the previous generation' }
$selector = [IO.File]::ReadAllText($selectorFile)
[void](Invoke-Deploy @('--reset','--data-root',$fixture) $false); Assert-Selector $selector
Set-Schema 'pinyin_simp'
$reset = Invoke-Deploy @('--reset','--data-root',$fixture)
if ($reset.workspace -ne 'base') { throw 'Workspace reset failed' }
Write-Host "Maintenance config/import/two-package Rime/deploy/failure/hash/lock/rollback/reset: PASS (isolated fixture: $fixture)"
