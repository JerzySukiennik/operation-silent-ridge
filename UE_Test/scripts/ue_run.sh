#!/bin/zsh
# Runs one or more UE build scripts headless in the Mac editor commandlet: ue_run.sh <script.py> [...]; log -> Niepotrzebne/logs/
D="$(cd "$(dirname "$0")/.." && pwd)"
UE="/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor-Cmd"
mkdir -p "$D/Niepotrzebne/logs"
for s in "$@"; do
  L="$D/Niepotrzebne/logs/$(basename $s .py).log"
  echo "== $s -> $L"
  "$UE" "$D/SilentRidgeUE/SilentRidgeUE.uproject" -run=pythonscript -script="$D/scripts/$s" -unattended -nosplash -nullrhi -stdout -FullStdOutLogOutput > "$L" 2>&1
  echo "exit $?"; grep -E "OSR |Error|Traceback|rror:" "$L" | grep -v "LogPython: OSR" | cut -c1-400 | tail -40
done
