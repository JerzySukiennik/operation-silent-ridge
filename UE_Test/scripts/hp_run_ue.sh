#!/bin/zsh
# Runs the packaged build on the HP in Jurek's session with run_ue.ps1 (scalability preset, 67% TSR, vsync off, CSV profile), pulls results to Niepotrzebne/runs/<tag>.
# Usage: hp_run_ue.sh <tag> <scalability 0-3> [extra ExecCmds, comma separated]
# env: UE_EXTRA_ARGS (game command line, e.g. "-autotest" or "-flythrough"), RUNNER_ARGS (run_ue.ps1 params, e.g. "-Duration 110 -ShotTimes 999")
set -e
D="$(cd "$(dirname "$0")/.." && pwd)"; H=jurek@MACNOTBOOK.local; R='C:\Users\jurek\osr_ue'
TAG=$1; Q=$2; EXTRA=${3:-}
source "$D/scripts/hp_lock.sh"
hp_lock "run-$TAG" || exit 3
trap hp_unlock EXIT
OUT="$R\\runs\\$TAG"
BD=${BUILD_DIR:-"$R\\build\\Windows"}                                    # env BUILD_DIR: another packaged build (quickrun.sh: the desktop build)
EXE="$BD\\SilentRidgeUE\\Binaries\\Win64\\SilentRidgeUE.exe"   # C++ project exe (stage 2+)
LOG="$BD\\SilentRidgeUE\\Saved\\Logs\\SilentRidgeUE.log"
CMDS="scalability $Q,r.ScreenPercentage 67,r.VSync 0,t.MaxFPS 0,r.DynamicRes.OperationMode 0,r.GPUCsvStatsEnabled 1${EXTRA:+,$EXTRA},csv.ContinuousWrites 1,csvprofile start,r.ScreenPercentage,r.AntiAliasingMethod,r.DynamicGlobalIlluminationMethod,r.Shadow.Virtual.Enable"
TMP="$D/Niepotrzebne/runs/_tmp"; mkdir -p "$TMP"
print -r -- "-ResX=1920 -ResY=1080 -fullscreen ${UE_SOUND_FLAG} -csvGpuStats ${UE_EXTRA_ARGS} -ExecCmds=\"$CMDS\"" > "$TMP/args.txt"
print -r -- "powershell -NoProfile -ExecutionPolicy Bypass -File $R\\tools\\run_ue.ps1 -Exe \"$EXE\" -Out \"$OUT\" -Log \"$LOG\" -ArgsFile \"$R\\tools\\args.txt\" ${RUNNER_ARGS}" > "$TMP/run.cmd"
cat "$TMP/args.txt" "$TMP/run.cmd"
ssh $H "Remove-Item -Recurse -Force $OUT -ErrorAction SilentlyContinue; Remove-Item -Force $BD\\SilentRidgeUE\\Saved\\Profiling\\CSV\\*.csv -ErrorAction SilentlyContinue; Remove-Item -Force $LOG -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $OUT | Out-Null"
scp -q "$TMP/args.txt" "$TMP/run.cmd" "$D/scripts/hp/run_ue.ps1" "$D/scripts/hp/task.ps1" $H:"C:/Users/jurek/osr_ue/tools/"
ssh $H "powershell -NoProfile -ExecutionPolicy Bypass -File $R\\tools\\task.ps1 -Name OSRUE_run -Cmd $R\\tools\\run.cmd"
t0=$(date +%s)
while true; do
  sleep 5
  st=$(ssh $H "if (Test-Path $OUT\\runner.txt) { Get-Content $OUT\\runner.txt -Tail 1 } else { 'wait' }" | tr -d '\r')
  case "$st" in *DONE*) break;; esac
  if [ $(( $(date +%s) - t0 )) -gt 900 ]; then echo "run timeout"; ssh $H "Get-Process UnrealGame -ErrorAction SilentlyContinue | Stop-Process -Force"; break; fi
done
ssh $H "powershell -NoProfile -ExecutionPolicy Bypass -File $R\\tools\\task.ps1 -Name OSRUE_run -Remove; Copy-Item $LOG $OUT\\game.log -ErrorAction SilentlyContinue; Copy-Item $BD\\SilentRidgeUE\\Saved\\Profiling\\CSV\\*.csv $OUT\\ -ErrorAction SilentlyContinue; Copy-Item $BD\\SilentRidgeUE\\Saved\\Autotest\\*.png $OUT\\ -ErrorAction SilentlyContinue; Remove-Item -Recurse -Force $BD\\SilentRidgeUE\\Saved\\Autotest -ErrorAction SilentlyContinue; exit 0"
L="$D/Niepotrzebne/runs/$TAG"; rm -rf "$L"; mkdir -p "$L"
scp -q -r $H:"C:/Users/jurek/osr_ue/runs/$TAG/*" "$L/"
echo "== $L"; ls "$L"; cat "$L/runner.txt"
