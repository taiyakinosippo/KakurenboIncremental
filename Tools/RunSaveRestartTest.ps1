# 再起動をまたいだセーブ・ロードのテスト。
#   1 回目の起動（SaveRun1）で遊んで保存 → 終了 → 2 回目の起動（SaveRun2）で続きから始まるかを確認する。
# テスト用のスロットを使い、ユーザーのセーブには触らない。
$Slot = "KakuRestartTest"
$Project = Join-Path $PSScriptRoot "..\KakurenboIncremental\KakurenboIncremental.uproject" | Resolve-Path
$SaveFile = Join-Path (Split-Path $Project) "Saved\SaveGames\$Slot.sav"

Remove-Item $SaveFile -Force -ErrorAction SilentlyContinue

Write-Host "=== 1st launch ==="
& (Join-Path $PSScriptRoot "RunAutoTest.ps1") -Scenario SaveRun1 -TimeoutSec 200 -SaveSlot $Slot | Select-String -Pattern "CHECK"
Write-Host "=== 2nd launch ==="
& (Join-Path $PSScriptRoot "RunAutoTest.ps1") -Scenario SaveRun2 -TimeoutSec 200 -SaveSlot $Slot | Select-String -Pattern "CHECK"

Remove-Item $SaveFile -Force -ErrorAction SilentlyContinue
