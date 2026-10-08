@echo off
rem Double-click: pull and build (see Tools\AutoBuild.ps1 for options, e.g. AutoBuild.bat -Test)
cd /d "%~dp0.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0AutoBuild.ps1" %*
pause
