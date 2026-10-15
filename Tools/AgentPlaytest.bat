@echo off
rem Double-click: the test-play AI plays the packaged game with 4 personas for 8 hours and writes a report (no Claude tokens). If nobody touches the PC for 5 minutes afterwards, it plays another 8 hours with the same data.
rem Options go to Tools\AgentPlaytest.ps1, e.g. AgentPlaytest.bat -Hours 2  /  AgentPlaytest.bat -Fresh
cd /d "%~dp0.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0AgentPlaytest.ps1" -Open %*
pause
