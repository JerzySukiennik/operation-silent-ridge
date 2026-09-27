@echo off
rem Cook + stage + pak a content-only UE project with the launcher engine (no C++ toolchain needed). Usage: package.cmd <project_dir> <project_name> <archive_dir> <log>
set UE=C:\Program Files\Epic Games\UE_5.7
echo START %DATE% %TIME% > "%~4"
call "%UE%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%~1\%~2.uproject" -noP4 -platform=Win64 -clientconfig=Development -cook -stage -pak -iostore -archive -archivedirectory="%~3" -nocompile -nocompileeditor -nocompileuat -installed -unattended -utf8output -nodebuginfo >> "%~4" 2>&1
echo EXIT %ERRORLEVEL% %DATE% %TIME% >> "%~4"
