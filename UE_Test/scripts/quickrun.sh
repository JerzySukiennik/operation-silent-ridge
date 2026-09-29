#!/bin/zsh
# Runs the current desktop build (C:\Users\jurek\Desktop\SilentRidge) on the HP with game args, pulls log/CSV/screenshots to
# Niepotrzebne/runs/<tag>, prints the REPORT lines + perf summary, then wipes the build's Saved folder again.
#   quickrun.sh [tag] [game args...]      default: tag=quick-<time>, args "-autotest" (use "-autotest -noshots" for perf)
# env: SCALABILITY (default 3 = Epic), DURATION (s, default 130)
D="$(cd "$(dirname "$0")/.." && pwd)"
TAG=${1:-quick-$(date +%H%M%S)}; shift 2>/dev/null
ARGS=${*:-"-autotest"}
BUILD_DIR='C:\Users\jurek\Desktop\SilentRidge' UE_EXTRA_ARGS="$ARGS" RUNNER_ARGS="-Duration ${DURATION:-130} -ShotTimes 999" \
  "$D/scripts/hp_run_ue.sh" "$TAG" ${SCALABILITY:-3} > "$D/Niepotrzebne/runs/$TAG.runner.txt" 2>&1
rc=$?
ssh jurek@MACNOTBOOK.local "Remove-Item -Recurse -Force 'C:\Users\jurek\Desktop\SilentRidge\SilentRidgeUE\Saved' -ErrorAction SilentlyContinue; exit 0"
[ $rc = 0 ] || { echo "run failed ($rc), see Niepotrzebne/runs/$TAG.runner.txt"; tail -5 "$D/Niepotrzebne/runs/$TAG.runner.txt"; exit $rc; }
L="$D/Niepotrzebne/runs/$TAG"
grep REPORT "$L/game.log" 2>/dev/null | sed 's/.*REPORT /  /' | grep -v "shot canyon_0[1-6]"
python3 "$D/scripts/analyze_run.py" "$L" 2>/dev/null | python3 -c "
import json,sys; d=json.load(sys.stdin); a=d.get('all',{})
print('  perf all: %s fps avg, 1%% low %s, GPU %s ms | VRAM peak %s MB, RAM peak %s MB' % (a.get('avg_fps'), a.get('low1_fps'), a.get('gpu_ms_avg'), d.get('vram_used_peak_mb'), d.get('ram_ws_peak_mb')))" 2>/dev/null
echo "  -> $L ($(ls "$L"/*.png 2>/dev/null | wc -l | tr -d ' ') screenshots)"
