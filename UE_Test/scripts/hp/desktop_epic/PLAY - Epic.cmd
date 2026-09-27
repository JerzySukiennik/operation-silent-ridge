@echo off
rem Operation Silent Ridge - UE 5.7 stage 1 (Epic look, baked terrain GI, no Lumen): 65 s auto flythrough. Alt+F4 quits.
cd /d "%~dp0Engine\Binaries\Win64"
start "" UnrealGame.exe "../../../SilentRidgeUE/SilentRidgeUE.uproject" -ResX=1920 -ResY=1080 -fullscreen -ExecCmds="stat fps,stat unit"
