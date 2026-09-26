#!/bin/zsh
# Run an exported Windows build on the HP laptop in its interactive desktop session, then pull the log and screenshots back.
# Usage: hp-run.sh <tag> <local_build_dir_with_exe> <local_out_dir> <timeout_s> [exe user args...]
# The exe in the build dir must be named OperationSilentRidge.exe. One run at a time (mkdir lock); waits up to 20 min for the lock.
set -e
TAG=$1; BUILD=$2; OUT=$3; TIMEOUT=$4; shift 4
ARGS="$*"
H=jurek@MACNOTBOOK.local
LOCK=/private/tmp/claude-501/osr-hp.lock
waited=0
until mkdir "$LOCK" 2>/dev/null; do
  sleep 5; waited=$((waited+5))
  if [ $waited -gt 1200 ]; then echo "HP lock busy for 20 min: $(cat $LOCK/owner 2>/dev/null)"; exit 3; fi
done
echo "$TAG $(date +%T)" > "$LOCK/owner"
trap 'rm -rf "$LOCK"' EXIT
D="C:\\Users\\jurek\\osr\\$TAG"
ssh $H "Get-Process OperationSilentRidge -ErrorAction SilentlyContinue | Stop-Process -Force; Remove-Item -Recurse -Force $D -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $D\\out | Out-Null"
scp -q "$BUILD"/*.exe "$BUILD"/*.dll $H:"C:/Users/jurek/osr/$TAG/" 2>/dev/null || scp -q "$BUILD"/*.exe $H:"C:/Users/jurek/osr/$TAG/"
ssh $H "\$d='$D'; \$exe=\"\$d\\OperationSilentRidge.exe\"
\$cmd = \"cmd /c \`\"\`\"\$exe\`\" --log-file \`\"\$d\\out\\run.log\`\" -- --shots=\$d\\out $ARGS\`\"\"
schtasks /create /tn OSR_$TAG /tr \$cmd /sc once /st 23:59 /it /f | Out-Null
schtasks /run /tn OSR_$TAG | Out-Null"
started=0; t=0
while [ $t -lt $TIMEOUT ]; do
  r=$(ssh $H "if (Get-Process OperationSilentRidge -ErrorAction SilentlyContinue) { 'run' } elseif (Test-Path $D\\out\\run.log) { 'done' } else { 'idle' }" | tr -d '\r')
  if [ "$r" = "done" ]; then break; fi
  if [ "$r" = "run" ]; then started=1; fi
  if [ "$started" = 1 ] && [ "$r" = "idle" ]; then break; fi
  sleep 3; t=$((t+3))
done
if [ $t -ge $TIMEOUT ]; then echo "TIMEOUT after ${TIMEOUT}s - killing"; ssh $H "Get-Process OperationSilentRidge -ErrorAction SilentlyContinue | Stop-Process -Force"; fi
ssh $H "schtasks /delete /tn OSR_$TAG /f | Out-Null"
rm -rf "$OUT"; mkdir -p "$OUT"
scp -q $H:"C:/Users/jurek/osr/$TAG/out/*" "$OUT/" 2>/dev/null || true
echo "== pulled to $OUT:"; ls "$OUT"
grep -E "REPORT|SCRIPT ERROR|ERROR|FPS" "$OUT/run.log" 2>/dev/null | head -60 || true
