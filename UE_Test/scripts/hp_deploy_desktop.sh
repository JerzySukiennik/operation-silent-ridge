#!/bin/zsh
# Copies the packaged build to the HP desktop folder with the launcher + comparison sheet, test-launches it via the launcher (waits for LoadMap), then closes it and wipes Saved.
# Usage: hp_deploy_desktop.sh <desktop folder name> <launcher dir (scripts/hp/...)> [image to copy] ; env CMP_NAME = name of the image on the desktop
set -e
D="$(cd "$(dirname "$0")/.." && pwd)"; H=jurek@MACNOTBOOK.local; R='C:\Users\jurek\osr_ue'
NAME=$1; LDIR=$2; CMP=$3
DEST="C:\\Users\\jurek\\Desktop\\$NAME"
source "$D/scripts/hp_lock.sh"; hp_lock "deploy-$NAME" || exit 3; trap hp_unlock EXIT
ssh $H "Get-Process UnrealGame,SilentRidgeUE -ErrorAction SilentlyContinue | Stop-Process -Force; robocopy $R\\build\\Windows $DEST /MIR /XD Saved /NFL /NDL /NJH /NP | Out-Null; if (\$LASTEXITCODE -ge 8) { 'robocopy failed' }; exit 0"
for f in "$D/$LDIR"/*; do scp -q "$f" $H:"C:/Users/jurek/Desktop/$NAME/"; done
[ -n "$CMP" ] && scp -q "$CMP" $H:"C:/Users/jurek/Desktop/$NAME/${CMP_NAME:-comparison.jpg}"
# the root bootstrap exe is not used (launch via the .cmd); remove it so nobody double-clicks the wrong thing
ssh $H "Remove-Item -Force '$DEST\\SilentRidgeUE.exe' -ErrorAction SilentlyContinue; exit 0"
L1=$(ls "$D/$LDIR"/*.cmd | head -1); LN=$(basename "$L1")
ssh $H "powershell -NoProfile -ExecutionPolicy Bypass -File $R\\tools\\task.ps1 -Name OSRUE_deploytest -Cmd '$DEST\\$LN'"
LOG="$DEST\\SilentRidgeUE\\Saved\\Logs\\SilentRidgeUE.log"
ok=0
for i in $(seq 1 30); do
  sleep 4
  r=$(ssh $H "if (Test-Path '$LOG') { (Select-String -Path '$LOG' -Pattern 'seconds to LoadMap' | Select -First 1).Line }" | tr -d '\r')
  if [ -n "$r" ]; then echo "launcher OK: $r"; ok=1; break; fi
done
sleep 8
ssh $H "Get-Process UnrealGame,SilentRidgeUE -ErrorAction SilentlyContinue | % { \$_.CloseMainWindow() | Out-Null }; Start-Sleep 8; Get-Process UnrealGame,SilentRidgeUE -ErrorAction SilentlyContinue | Stop-Process -Force; powershell -NoProfile -File $R\\tools\\task.ps1 -Name OSRUE_deploytest -Remove; Select-String -Path '$LOG' -Pattern 'r.ScreenPercentage|sg.ResolutionQuality|Scalability' | Select -First 6 | % { \$_.Line.Substring(0, [Math]::Min(200, \$_.Line.Length)) }; Remove-Item -Recurse -Force '$DEST\\SilentRidgeUE\\Saved' -ErrorAction SilentlyContinue; Get-ChildItem '$DEST' | Select -Expand Name; (Get-ChildItem -Recurse '$DEST' -File | Measure-Object Length -Sum).Sum/1MB"
[ $ok = 1 ] || { echo "LAUNCHER DID NOT REACH LoadMap"; exit 1; }
