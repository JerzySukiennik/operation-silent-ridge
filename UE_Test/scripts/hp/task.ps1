# Registers and starts (or removes) a scheduled task that also runs on battery: task.ps1 -Name X -Cmd C:\path\file.cmd [-Background] | task.ps1 -Name X -Remove
param([string]$Name, [string]$Cmd, [switch]$Remove, [switch]$Background)
if ($Remove) { Unregister-ScheduledTask -TaskName $Name -Confirm:$false -ErrorAction SilentlyContinue; exit 0 }
$a = New-ScheduledTaskAction -Execute "cmd.exe" -Argument "/c `"$Cmd`""
$s = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Hours 4)
if ($Background) { $p = New-ScheduledTaskPrincipal -UserId "jurek" -LogonType S4U -RunLevel Limited }  # no console window (cook/package)
else { $p = New-ScheduledTaskPrincipal -UserId "jurek" -LogonType Interactive -RunLevel Limited }  # visible desktop (game runs)
Register-ScheduledTask -TaskName $Name -Action $a -Settings $s -Principal $p -Force | Out-Null
Start-ScheduledTask -TaskName $Name
