# 単体テスト（Automation Test の "Kakurenbo" 以下）を描画なしで実行する
param([string]$Filter = "Kakurenbo")

$Engine = "C:\Program Files\Epic Games\UE_5.8"
$Project = Join-Path $PSScriptRoot "..\KakurenboIncremental\KakurenboIncremental.uproject" | Resolve-Path
$Log = Join-Path (Split-Path $Project) "Saved\Logs\UnitTests.log"

& "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Project" -ExecCmds="Automation RunTests $Filter; Quit" -unattended -nullrhi -nosplash -NoSound "-abslog=$Log" | Out-Null

Select-String -Path $Log -Pattern "Test Completed|Error:|Expected|TEST COMPLETE|tests? (passed|failed)" | ForEach-Object Line
