#!/bin/zsh
# Syncs the UE project to the HP (.uproject with the C++ module for the Game target only -- the HP lacks the .NET Framework SDK that editor targets need -- and without the editor Python plugins) and runs BuildCookRun there; prints timing and build size.
set -e
D="$(cd "$(dirname "$0")/.." && pwd)"; H=jurek@MACNOTBOOK.local; R='C:\Users\jurek\osr_ue'
source "$D/scripts/hp_lock.sh"
ST="$D/Niepotrzebne/hp_stage/SilentRidgeUE"; rm -rf "$ST"; mkdir -p "$ST"
rsync -a --delete "$D/SilentRidgeUE/Content" "$D/SilentRidgeUE/Config" "$D/SilentRidgeUE/Source" "$ST/"
printf '\n[DevOptions.Shaders]\nNumUnusedShaderCompilingThreads=6\n' >> "$ST/Config/DefaultEngine.ini"
cat > "$ST/SilentRidgeUE.uproject" <<'J'
{
	"FileVersion": 3,
	"EngineAssociation": "5.7",
	"Category": "",
	"Description": "Operation Silent Ridge (UE 5.7 port)",
	"Modules": [
		{ "Name": "SilentRidgeUE", "Type": "Runtime", "LoadingPhase": "Default", "TargetAllowList": [ "Game" ] }
	]
}
J
( cd "$D/Niepotrzebne/hp_stage" && COPYFILE_DISABLE=1 tar cf ../proj.tar SilentRidgeUE )
ls -la "$D/Niepotrzebne/proj.tar"
hp_lock package || exit 3
trap hp_unlock EXIT
ssh $H "New-Item -ItemType Directory -Force $R\\proj,$R\\tools | Out-Null"
PARTS="$D/Niepotrzebne/proj_parts"; rm -rf "$PARTS"; mkdir -p "$PARTS"
( cd "$PARTS" && split -b 50m "$D/Niepotrzebne/proj.tar" part_ )
ssh $H "Remove-Item -Recurse -Force $R\\parts -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $R\\parts | Out-Null"
for f in "$PARTS"/part_*; do
  for try in 1 2 3 4 5; do
    if scp -q -o ServerAliveInterval=15 -o ConnectTimeout=20 "$f" $H:"C:/Users/jurek/osr_ue/parts/$(basename $f)"; then break; fi
    echo "retry $(basename $f) ($try)"; sleep 10
  done
done
ssh $H "cmd /c \"copy /b $R\\parts\\part_* $R\\proj.tar >nul\"; (Get-Item $R\\proj.tar).Length; Remove-Item -Recurse -Force $R\\parts"
scp -q "$D/scripts/hp/package.cmd" "$D/scripts/hp/run_ue.ps1" "$D/scripts/hp/task.ps1" $H:"C:/Users/jurek/osr_ue/tools/"
# keep the HP-side Saved/Intermediate/DDC between runs (incremental cook); replace Content+Config only
ssh $H "cd $R\\proj; if (Test-Path SilentRidgeUE\\Content) { Remove-Item -Recurse -Force SilentRidgeUE\\Content, SilentRidgeUE\\Config, SilentRidgeUE\\Source -ErrorAction SilentlyContinue }; tar -xf $R\\proj.tar; Remove-Item $R\\proj.tar"
ssh $H "Set-Content -Path $R\\tools\\pkg.cmd -Value 'call $R\\tools\\package.cmd $R\\proj\\SilentRidgeUE SilentRidgeUE $R\\build $R\\package.log' -Encoding ASCII; Remove-Item $R\\package.log -ErrorAction SilentlyContinue; powershell -NoProfile -ExecutionPolicy Bypass -File $R\\tools\\task.ps1 -Name OSRUE_pkg -Background -Cmd $R\\tools\\pkg.cmd"
t0=$(date +%s)
while true; do
  sleep 20
  st=$(ssh $H "if (Test-Path $R\\package.log) { (Get-Content $R\\package.log -Tail 1) } else { 'nolog' }" | tr -d '\r')
  case "$st" in EXIT*) break;; esac
  if [ $(( $(date +%s) - t0 )) -gt 7200 ]; then echo "package timeout"; break; fi
done
echo "package wall time: $(( $(date +%s) - t0 )) s ; $st"
ssh $H "powershell -NoProfile -ExecutionPolicy Bypass -File $R\\tools\\task.ps1 -Name OSRUE_pkg -Remove; Select-String -Path $R\\package.log -Pattern 'Error:|error |BUILD FAILED|Cook by the book total time|AutomationTool executed' | Select -Last 25 | % { \$_.Line.Substring(0, [Math]::Min(300, \$_.Line.Length)) }; (Get-ChildItem -Recurse $R\\build -File | Measure-Object Length -Sum).Sum/1MB"
