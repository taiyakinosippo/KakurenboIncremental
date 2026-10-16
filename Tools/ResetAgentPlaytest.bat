@echo off
rem Double-click: reset the test-play AI's learning, game saves and play records (moved to a backup folder, asks first).
rem Options go to Tools\ResetAgentPlaytest.ps1, e.g. ResetAgentPlaytest.bat -Personas greedy  /  -KeepRecords  /  -NoBackup
cd /d "%~dp0.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0ResetAgentPlaytest.ps1" %*
pause
