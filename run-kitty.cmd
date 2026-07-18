@echo off
rem Launch the native-Windows build of kitty.
rem Adds the MinGW runtime DLLs to PATH and enables UTF-8, then starts kitty detached.
set "PATH=C:\msys64\mingw64\bin;%PATH%"
set "PYTHONUTF8=1"
start "kitty" "%~dp0kitty\launcher\kitty.exe" %*
