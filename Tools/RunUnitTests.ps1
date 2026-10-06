# 単体テスト（Automation Test の "Kakurenbo" 以下）を描画なしで実行する
param([string]$Filter = "Kakurenbo")

. (Join-Path $PSScriptRoot "EnginePath.ps1") # $Engine に UE 5.8 のインストール先が入る
$Project = Join-Path $PSScriptRoot "..\KakurenboIncremental\KakurenboIncremental.uproject" | Resolve-Path
$Log = Join-Path (Split-Path $Project) "Saved\Logs\UnitTests.log"

& "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Project" -ExecCmds="Automation RunTests $Filter; Quit" -unattended -nullrhi -nosplash -NoSound "-abslog=$Log" | Out-Null

Select-String -Path $Log -Pattern "Test Completed|Error:|Expected|TEST COMPLETE|tests? (passed|failed)" | ForEach-Object Line
