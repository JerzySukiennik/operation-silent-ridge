#!/bin/zsh
# One-liner: Mac project -> playable build on the HP desktop (C:\Users\jurek\Desktop\SilentRidge), with a launch test.
#   ship.sh [--mode auto|cpp|stage|cook|deploy] [--no-launch] [--rehash-remote]
# 1 sync     incremental MD5-manifest sync of Content/Config/Source (hp_sync.py), only changed files cross the network
# 2 build    picked from what changed (auto): Content -> cook (iterative, -iterate); Config -> stage (repak, no cook);
#            Source only -> cpp (UBT compile + exe swap, no cook/pak); nothing -> deploy only
# 3 deploy   robocopy of the staged build into Desktop\SilentRidge (only changed files) + PLAY.cmd / CONTROLS.txt
# 4 launch   PLAY.cmd in Jurek's session, wait for LoadMap, close, wipe Saved
# Prints per-phase and total times. HP rules: shared lock, battery-safe scheduled tasks, S4U (no console) for the build.
set -o pipefail
D="$(cd "$(dirname "$0")/.." && pwd)"; H=jurek@MACNOTBOOK.local; R='C:\Users\jurek\osr_ue'
DEST='C:\Users\jurek\Desktop\SilentRidge'
MODE=auto; LAUNCH=1; SYNCARGS=()
while [ $# -gt 0 ]; do
  case $1 in --mode) MODE=$2; shift;; --no-launch) LAUNCH=0;; --rehash-remote) SYNCARGS+=(--rehash-remote);; *) echo "unknown arg $1"; exit 2;; esac; shift
done
source "$D/scripts/hp_lock.sh"
hp_lock ship || exit 3
trap hp_unlock EXIT
T0=$(date +%s); TL=$T0
LOGD="$D/Niepotrzebne/fastiter"; mkdir -p "$LOGD"
phase() { local now=$(date +%s); printf "  %-8s %4ds   (total %ds)\n" "$1" $((now - TL)) $((now - T0)); PH+=("$1=$((now - TL))"); TL=$now; }
PH=()
ssh -o ConnectTimeout=15 $H "exit 0" || { echo "HP unreachable (check ~/.ssh/config HostName)"; exit 4; }

# ---- 1 sync (tools upload runs in parallel)
( scp -q "$D/scripts/hp/build.cmd" "$D/scripts/hp/task.ps1" $H:"C:/Users/jurek/osr_ue/tools/" ) &
UP=$!
python3 "$D/scripts/hp_sync.py" "${SYNCARGS[@]}" | tee "$LOGD/last_sync.txt" || { echo "sync failed"; exit 5; }
wait $UP
SUM=$(tail -1 "$LOGD/last_sync.txt")
phase sync

# ---- 2 build
if [ $MODE = auto ]; then
  case "$SUM" in
    *'"content"'*) MODE=cook;;
    *'"config"'*|*'"project"'*) MODE=stage;;
    *'"source"'*) MODE=cpp;;
    *) MODE=deploy;;
  esac
fi
HASBUILD=$(ssh $H "Test-Path '$R\\proj\\SilentRidgeUE\\Saved\\StagedBuilds\\Windows\\SilentRidgeUE\\Binaries\\Win64\\SilentRidgeUE.exe'" | tr -d '\r')
if [ "$HASBUILD" != True ] && [ $MODE != cook ]; then echo "no staged build on the HP yet -> full cook"; MODE=cook; fi
echo "build mode: $MODE"
if [ $MODE != deploy ]; then
  ssh $H "Set-Content -Path $R\\tools\\ship.cmd -Value 'call $R\\tools\\build.cmd $MODE $R\\ship_build.log' -Encoding ASCII; Remove-Item $R\\ship_build.log -ErrorAction SilentlyContinue; powershell -NoProfile -ExecutionPolicy Bypass -File $R\\tools\\task.ps1 -Name OSRUE_ship -Background -Cmd $R\\tools\\ship.cmd"
  while true; do
    sleep 4
    st=$(ssh $H "if (Test-Path $R\\ship_build.log) { Get-Content $R\\ship_build.log -Tail 1 } else { 'wait' }" | tr -d '\r')
    case "$st" in EXIT*) break;; esac
    if [ $(( $(date +%s) - TL )) -gt 5400 ]; then echo "build timeout"; break; fi
  done
  ssh $H "powershell -NoProfile -ExecutionPolicy Bypass -File $R\\tools\\task.ps1 -Name OSRUE_ship -Remove" >/dev/null
  scp -q $H:"C:/Users/jurek/osr_ue/ship_build.log" "$LOGD/last_build.log" 2>/dev/null
  case "$st" in "EXIT 0"*) ;; *) echo "BUILD FAILED ($st) - see $LOGD/last_build.log"; grep -E "error|Error:" "$LOGD/last_build.log" | head -15; exit 6;; esac
  grep -E "Cook by the book total time|Total execution time|Stage command time" "$LOGD/last_build.log" | sed 's/^/    /' | tail -3
  phase "build:$MODE"
fi

# ---- 3 deploy (only changed files are copied)
SRC="$R\\proj\\SilentRidgeUE\\Saved\\StagedBuilds\\Windows"
ssh $H "Get-Process SilentRidgeUE,UnrealGame -ErrorAction SilentlyContinue | Stop-Process -Force; robocopy $SRC $DEST /MIR /XD Saved /XF PLAY.cmd CONTROLS.txt map_before_after.jpg /NFL /NDL /NJH /NJS /NP | Out-Null; if (\$LASTEXITCODE -ge 8) { 'ROBOCOPY FAILED' }; Remove-Item -Force '$DEST\\SilentRidgeUE.exe' -ErrorAction SilentlyContinue; exit 0" | grep -q "FAILED" && { echo "deploy failed"; exit 7; }
for f in "$D/scripts/hp/desktop_main"/*; do scp -q "$f" $H:"C:/Users/jurek/Desktop/SilentRidge/"; done
phase deploy

# ---- 4 launch test
if [ $LAUNCH = 1 ]; then
  LOG="$DEST\\SilentRidgeUE\\Saved\\Logs\\SilentRidgeUE.log"
  ssh $H "Remove-Item -Recurse -Force '$DEST\\SilentRidgeUE\\Saved' -ErrorAction SilentlyContinue; powershell -NoProfile -ExecutionPolicy Bypass -File $R\\tools\\task.ps1 -Name OSRUE_shiptest -Cmd '$DEST\\PLAY.cmd'"
  ok=0
  for i in $(seq 1 60); do
    sleep 2
    r=$(ssh $H "if (Test-Path '$LOG') { (Select-String -Path '$LOG' -Pattern 'seconds to LoadMap' | Select -First 1).Line }" | tr -d '\r')
    if [ -n "$r" ]; then ok=1; break; fi
  done
  sleep 3
  ssh $H "Get-Process SilentRidgeUE,UnrealGame -ErrorAction SilentlyContinue | % { \$_.CloseMainWindow() | Out-Null }; Start-Sleep 5; Get-Process SilentRidgeUE,UnrealGame -ErrorAction SilentlyContinue | Stop-Process -Force; powershell -NoProfile -File $R\\tools\\task.ps1 -Name OSRUE_shiptest -Remove; Remove-Item -Recurse -Force '$DEST\\SilentRidgeUE\\Saved' -ErrorAction SilentlyContinue"
  [ $ok = 1 ] && echo "  launch OK: ${r##*LogLoad: }" || { echo "LAUNCH DID NOT REACH LoadMap"; phase launch; exit 8; }
  phase launch
fi
echo "SHIP DONE mode=$MODE total=$(( $(date +%s) - T0 ))s  [${PH[*]}]" | tee -a "$LOGD/ship_times.txt"
