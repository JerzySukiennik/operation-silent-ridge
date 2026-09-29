@echo off
rem Fast-iteration build on the HP. Usage: build.cmd <mode> <log>
rem   cook  : compile Game target + iterative cook (-iterate, only changed packages) + stage + pak/iostore (content changes)
rem   stage : compile + restage/repak without cooking (config-only changes: Config is staged into the pak)
rem   cpp   : UBT compile of the Game target only, then the exe is copied into the staged build (C++-only changes)
rem Output: C:\Users\jurek\osr_ue\proj\SilentRidgeUE\Saved\StagedBuilds\Windows (no -archive copy).
set UE=C:\Program Files\Epic Games\UE_5.7
set P=C:\Users\jurek\osr_ue\proj\SilentRidgeUE
set UAT="%UE%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%P%\SilentRidgeUE.uproject" -noP4 -platform=Win64 -clientconfig=Development -nocompileeditor -unrealexe="%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -nocompileuat -installed -unattended -utf8output -nodebuginfo
echo START %DATE% %TIME% > "%~2"
if "%~1"=="cook" call %UAT% -build -cook -iterate -stage -pak -iostore >> "%~2" 2>&1
if "%~1"=="stage" call %UAT% -build -skipcook -stage -pak -iostore >> "%~2" 2>&1
if "%~1"=="cpp" call "%UE%\Engine\Build\BatchFiles\Build.bat" SilentRidgeUE Win64 Development -Project="%P%\SilentRidgeUE.uproject" -WaitMutex -NoHotReloadFromIDE >> "%~2" 2>&1
set RC=%ERRORLEVEL%
if "%~1"=="cpp" if "%RC%"=="0" copy /y "%P%\Binaries\Win64\SilentRidgeUE.exe" "%P%\Saved\StagedBuilds\Windows\SilentRidgeUE\Binaries\Win64\SilentRidgeUE.exe" >> "%~2" 2>&1
echo EXIT %RC% %DATE% %TIME% >> "%~2"
