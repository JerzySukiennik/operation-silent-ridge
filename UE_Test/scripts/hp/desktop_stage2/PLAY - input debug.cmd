@echo off
rem Same game with a live raw gamepad readout (top-left) for diagnosing controller mapping.
cd /d "%~dp0SilentRidgeUE\Binaries\Win64"
start "" SilentRidgeUE.exe -ResX=1920 -ResY=1080 -fullscreen -inputdebug
