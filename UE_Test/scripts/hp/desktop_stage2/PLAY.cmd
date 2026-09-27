@echo off
rem Operation Silent Ridge - UE 5.7 stage 2: fly the F-35C yourself (gamepad; keyboard fallback). Epic look, 1080p. START = menu (respawn, invert pitch, quit), BACK = controls.
cd /d "%~dp0SilentRidgeUE\Binaries\Win64"
start "" SilentRidgeUE.exe -ResX=1920 -ResY=1080 -fullscreen
