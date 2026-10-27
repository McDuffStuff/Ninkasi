@echo off
rem Launches the current Ninkasi build without rebuilding it.
rem   run.cmd                   open Ninkasi with your normal database
rem   run.cmd --db scratch.db   any arguments are passed on to Ninkasi
rem To rebuild first, use scripts\build.ps1 -Run instead.
setlocal
set "EXE=%~dp0build\debug\ninkasi.exe"

if not exist "%EXE%" (
    echo Ninkasi hasn't been built yet. Build it with:
    echo   powershell -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1"
    pause
    exit /b 1
)

start "Ninkasi" "%EXE%" %*
