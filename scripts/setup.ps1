param([ValidateSet('Menu','Install','Uninstall')][string]$Action = 'Menu')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root

function Invoke-AdminAction([string]$SelectedAction) {
    $powershell = "$env:SystemRoot/System32/WindowsPowerShell/v1.0/powershell.exe"
    $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '" -Action ' + $SelectedAction
    # This is an explicitly interactive installer: show progress in its window.
    $worker = Start-Process -FilePath $powershell -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    if ($worker.ExitCode) { throw '安装操作未完成，请查看管理员窗口提示和安装日志。' }
}
$logDirectory = Join-Path $env:TEMP 'MYIME-Setup'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$log = Join-Path $logDirectory (('setup-{0}-{1}.log' -f (Get-Date -Format 'yyyyMMdd-HHmmss'),$PID))
$transcript = $false
$result = 0
try {
    Start-Transcript -LiteralPath $log | Out-Null
    $transcript = $true
    if ($Action -eq 'Install') {
        & "$PSScriptRoot/install.ps1" -Configuration Release
        Write-Host '安装完成。请保存工作后注销 Windows，再重新登录使用新版。' -ForegroundColor Green
    } elseif ($Action -eq 'Uninstall') {
        & "$PSScriptRoot/uninstall.ps1"
        Write-Host '卸载操作完成，词库和配置已保留。建议注销后重新登录。' -ForegroundColor Green
    } else {
        Write-Host ''
        Write-Host 'MYIME 开发版安装助手'
        Write-Host '1. 构建最新代码并安装 / 升级（推荐）'
        Write-Host '2. 安装 / 升级已有 Release 安装包（不会重新构建）'
        Write-Host '3. 卸载输入法（保留词库、配置和旧版本文件）'
        Write-Host '0. 退出'
        Write-Host '安装前请切换到其他输入法。安装助手不会自动运行测试。'
        $choice = Read-Host '请选择'
        switch ($choice) {
            '1' {
                Write-Host '准备新版本。只有构建和打包成功后才会申请管理员权限。'
                & "$PSScriptRoot/build.ps1" -Configuration Release
                & "$PSScriptRoot/prepare-data.ps1" -Configuration Release
                & "$PSScriptRoot/package.ps1" -Configuration Release
                Invoke-AdminAction 'Install'
            }
            '2' { Invoke-AdminAction 'Install' }
            '3' { Invoke-AdminAction 'Uninstall' }
            '0' { }
            default { throw '没有选择有效操作；未安装或卸载。' }
        }
    }
} catch {
    $result = 1
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host '操作未完成。不要删除用户词库；构建失败时旧安装保持原样。'
} finally {
    Write-Host "日志：$log"
    if ($transcript) { Stop-Transcript | Out-Null }
    Read-Host '按 Enter 关闭窗口' | Out-Null
}
exit $result
