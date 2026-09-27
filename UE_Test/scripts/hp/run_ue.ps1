# Runs the packaged UE build in the interactive session: waits for LoadMap, screenshots at sequence times, samples RAM/VRAM, closes the window after the sequence.
param([string]$Exe, [string]$Out, [string]$ArgsFile, [string]$ShotTimes = "3,10,13,18,23,28,33,38,43,48,61", [double]$Duration = 65, [string]$Log)
$ErrorActionPreference = "Continue"
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type 'using System;using System.Runtime.InteropServices;public class DpiFix{[DllImport("user32.dll")]public static extern bool SetProcessDPIAware();}'
[DpiFix]::SetProcessDPIAware() | Out-Null
$GameArgs = (Get-Content $ArgsFile -Raw).Trim()
New-Item -ItemType Directory -Force $Out | Out-Null
function Note($m) { Add-Content "$Out\runner.txt" ("{0:HH:mm:ss.fff} {1}" -f (Get-Date), $m) }
$base = (& nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits) -join ""
Note "vram_baseline_mb=$base free_ram_mb=$([int]((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory/1024))"
$smi = Start-Process -FilePath "nvidia-smi" -ArgumentList "--query-gpu=timestamp,memory.used,utilization.gpu,clocks.gr,power.draw,temperature.gpu --format=csv -lms 500 -f `"$Out\nvsmi.csv`"" -WindowStyle Hidden -PassThru
$t0launch = Get-Date
$p = Start-Process -FilePath $Exe -ArgumentList $GameArgs -PassThru
Note "launched pid=$($p.Id) args=$GameArgs"
# wait for the map to finish loading (log line) - max 600 s
$t0 = $null
while (-not $t0 -and ((Get-Date) - $t0launch).TotalSeconds -lt 600 -and -not $p.HasExited) {
  Start-Sleep -Milliseconds 100
  if (Test-Path $Log) {
    $hit = Select-String -Path $Log -Pattern "Took .* seconds to LoadMap\(/Game/Maps" -SimpleMatch:$false -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($hit) { $t0 = Get-Date; Note "loadmap_seen after $([math]::Round(($t0 - $t0launch).TotalSeconds,1)) s: $($hit.Line)" }
  }
}
if (-not $t0) { Note "NO LOADMAP"; $t0 = Get-Date }
$times = $ShotTimes.Split(",") | ForEach-Object { [double]$_ }
$si = 0; $mem = @(); $peakWs = 0; $peakPriv = 0
$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
while (-not $p.HasExited) {
  $el = ((Get-Date) - $t0).TotalSeconds
  try { $p.Refresh(); $ws = $p.WorkingSet64; $pv = $p.PrivateMemorySize64; if ($ws -gt $peakWs) { $peakWs = $ws }; if ($pv -gt $peakPriv) { $peakPriv = $pv }; $mem += ("{0:F1},{1},{2}" -f $el, [int]($ws/1MB), [int]($pv/1MB)) } catch {}
  if ($si -lt $times.Count -and $el -ge $times[$si]) {
    $bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $bmp.Save(("{0}\shot_{1:00}s.png" -f $Out, [int]$times[$si]), [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
    Note ("shot t={0:F2}" -f $el); $si++
    continue
  }
  if ($el -ge $Duration) { Note "closing window at t=$([math]::Round($el,1))"; $p.CloseMainWindow() | Out-Null; if (-not $p.WaitForExit(25000)) { Note "kill"; $p.Kill() }; break }
  Start-Sleep -Milliseconds 250
}
$p.WaitForExit(30000) | Out-Null
try { $pk = $p.PeakWorkingSet64 } catch { $pk = 0 }
Note ("exit code={0} peak_ws_mb={1} peak_ws_sampled_mb={2} peak_private_mb={3}" -f $p.ExitCode, [int]($pk/1MB), [int]($peakWs/1MB), [int]($peakPriv/1MB))
$mem | Set-Content "$Out\mem.csv"
Stop-Process -Id $smi.Id -Force -ErrorAction SilentlyContinue
Note "DONE"
