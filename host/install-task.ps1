# Registers (or replaces) a Task Scheduler job that starts the host agent at logon, hidden.
# Run from an elevated or normal PowerShell:  .\install-task.ps1   (use -Remove to uninstall)
param([switch]$Remove)

$taskName = "Pucky Host"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$python = (Get-Command python).Source
$pythonw = Join-Path (Split-Path $python) "pythonw.exe"
if (-not (Test-Path $pythonw)) { $pythonw = $python }

if ($Remove) {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "removed '$taskName'"
    return
}

$action   = New-ScheduledTaskAction -Execute $pythonw -Argument "`"$here\agent.py`"" -WorkingDirectory $here
$trigger  = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 3 `
            -RestartInterval (New-TimeSpan -Minutes 1) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
            -StartWhenAvailable
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -Force | Out-Null
Start-ScheduledTask -TaskName $taskName
Write-Host "installed and started '$taskName' -> http://localhost:8787/"
