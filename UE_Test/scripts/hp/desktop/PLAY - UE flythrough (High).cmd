@echo off
rem Operation Silent Ridge - UE 5.7 test: 65 s auto flythrough, High preset, TSR 67%%. Alt+F4 quits. First start at a new preset compiles shaders (stutters for a few seconds).
cd /d "%~dp0Engine\Binaries\Win64"
start "" UnrealGame.exe "../../../SilentRidgeUE/SilentRidgeUE.uproject" -ResX=1920 -ResY=1080 -fullscreen -ExecCmds="scalability 2,r.ScreenPercentage 67,r.VSync 0,t.MaxFPS 0,stat fps,stat unit"
