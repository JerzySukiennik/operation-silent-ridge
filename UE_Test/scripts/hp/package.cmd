@echo off
rem Build (C++ game + editor module, VS 2022 Build Tools) + cook + stage + pak the project with the launcher engine. Usage: package.cmd <project_dir> <project_name> <archive_dir> <log>
set UE=C:\Program Files\Epic Games\UE_5.7
echo START %DATE% %TIME% > "%~4"
call "%UE%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%~1\%~2.uproject" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -iostore -archive -archivedirectory="%~3" -nocompileeditor -unrealexe="%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -nocompileuat -installed -unattended -utf8output -nodebuginfo >> "%~4" 2>&1
echo EXIT %ERRORLEVEL% %DATE% %TIME% >> "%~4"
