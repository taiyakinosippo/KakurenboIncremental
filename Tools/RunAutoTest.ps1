# ゲームを起動して KakuAutoTest を実行し、スクリーンショットとログを確認できるようにする
#   スクリーンショット: KakurenboIncremental/Saved/AutoTest/*.png
#   ログ:              KakurenboIncremental/Saved/Logs/KakurenboIncremental.log
param([string]$Scenario = "Loop", [int]$TimeoutSec = 600, [string]$ExtraExec = "")

$Engine = "C:\Program Files\Epic Games\UE_5.8"
$Project = Join-Path $PSScriptRoot "..\KakurenboIncremental\KakurenboIncremental.uproject" | Resolve-Path
$Saved = Join-Path (Split-Path $Project) "Saved"

Remove-Item (Join-Path $Saved "AutoTest") -Recurse -Force -ErrorAction SilentlyContinue

$GameArgs = @("`"$Project`"", "-game", "-windowed", "-ResX=1280", "-ResY=720", "-NoSound", "-ExecCmds=`"$ExtraExec KakuAutoTest $Scenario`"", "-log")
$Proc = Start-Process -FilePath "$Engine\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList $GameArgs -PassThru
if (-not $Proc.WaitForExit($TimeoutSec * 1000)) {
    Write-Host "Timeout: killing game"
    $Proc.Kill()
}

$Log = Join-Path $Saved "Logs\KakurenboIncremental.log"
Select-String -Path $Log -Pattern "\[AutoTest\]|Error|Ensure|Assertion" | Where-Object { $_.Line -notmatch "UnifiedErrorTest|FError that|Error test|Error with" } | ForEach-Object Line
$Failed = @(Select-String -Path $Log -Pattern "CHECK FAILED").Count
$Passed = @(Select-String -Path $Log -Pattern "CHECK OK").Count
Write-Host "CHECK: $Passed passed, $Failed failed"
Get-ChildItem (Join-Path $Saved "AutoTest") -ErrorAction SilentlyContinue | ForEach-Object FullName
