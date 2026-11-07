# テストプレイ AI（Tools/AgentPlaytest）の学習とプレイデータをリセットする（何の経験もない状態に戻す）。
#
#   powershell -ExecutionPolicy Bypass -File Tools\ResetAgentPlaytest.ps1                  # 全部（確かめてから。消さずにバックアップのフォルダへ移す）
#   powershell -ExecutionPolicy Bypass -File Tools\ResetAgentPlaytest.ps1 -Personas greedy  # その性格だけ（学習とゲームのセーブ。記録のフォルダは残す）
#   -KeepRecords（記録のフォルダ・バランスの推論は残し、学習とセーブだけ）・-NoBackup（移さずに消す）・-Yes（確かめない）・-DryRun（何をするか見せるだけ）
#
# 対象：
#   学習            KakurenboIncremental/Saved/AgentPlaytest/brains/brain_<性格>.json
#   ゲームのセーブ  Packaged/Windows/KakurenboIncremental/Saved/SaveGames/Agent_<性格>.sav（と、エディタ版で試した Saved/SaveGames/Agent_*.sav）
#   記録            KakurenboIncremental/Saved/AgentPlaytest/<起動日時>/（ラウンド・出来事・報告書・スクリーンショット）と analysis/（バランスの推論）
# 移す先：KakurenboIncremental/Saved/AgentPlaytest/backup_<日時>/（元に戻すときは中身を元の場所へ戻す）。
# あなたのセーブ（Kakurenbo.sav）・設定・GitHub の agent-reports の報告書には触れない。前のバックアップ（backup_* / *_backup_*）も残す。
param(
    [string]$Personas = "",
    [switch]$KeepRecords,
    [switch]$NoBackup,
    [switch]$Yes,
    [switch]$DryRun
)

$Root = Split-Path $PSScriptRoot -Parent
$OutRoot = Join-Path $Root "KakurenboIncremental\Saved\AgentPlaytest"
$PackSaves = Join-Path $Root "Packaged\Windows\KakurenboIncremental\Saved\SaveGames"
$EditorSaves = Join-Path $Root "KakurenboIncremental\Saved\SaveGames"
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$BackupDir = Join-Path $OutRoot "backup_$Stamp"

# 遊んでいる最中は消さない（AI が学習を書き戻し、ゲームがセーブを書き戻すので、リセットにならない）
$Running = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
    ($_.Name -like "python*" -and $_.CommandLine -match "run_agent\.py") -or
    ($_.Name -like "KakurenboIncremental*" -and $_.CommandLine -match "KakuAgentPort=") -or
    ($_.Name -like "powershell*" -and $_.CommandLine -match "(?<!Reset)AgentPlaytest\.ps1") })
if ($Running.Count -gt 0) {
    Write-Host "テストプレイ AI が動いています（$($Running.Count) 個のプロセス）。止めてからもう一度動かしてください（AgentPlaytest のウィンドウを閉じる）" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------- 対象を集める
$Targets = New-Object System.Collections.Generic.List[object]
function Add-Target([string]$Path, [string]$Kind) {
    if (Test-Path $Path) { $Targets.Add([pscustomobject]@{ Path = (Resolve-Path $Path).Path; Kind = $Kind }) }
}
$Only = @($Personas.Split(",") | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($Only.Count -gt 0) {
    foreach ($P in $Only) {
        Add-Target (Join-Path $OutRoot "brains\brain_$P.json") "学習"
        Add-Target (Join-Path $PackSaves "Agent_$P.sav") "ゲームのセーブ"
        Add-Target (Join-Path $EditorSaves "Agent_$P.sav") "ゲームのセーブ（エディタ版）"
    }
} else {
    Get-ChildItem (Join-Path $OutRoot "brains") -Filter "brain_*.json" -ErrorAction SilentlyContinue | ForEach-Object { Add-Target $_.FullName "学習" }
    Get-ChildItem $PackSaves -Filter "Agent*.sav" -ErrorAction SilentlyContinue | ForEach-Object { Add-Target $_.FullName "ゲームのセーブ" }
    Get-ChildItem $EditorSaves -Filter "Agent*.sav" -ErrorAction SilentlyContinue | ForEach-Object { Add-Target $_.FullName "ゲームのセーブ（エディタ版）" }
    if (-not $KeepRecords) {
        Get-ChildItem $OutRoot -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^\d{8}-\d{4}$' } | ForEach-Object { Add-Target $_.FullName "記録" }
        Add-Target (Join-Path $OutRoot "analysis") "バランスの推論"
    }
}

if ($Targets.Count -eq 0) {
    Write-Host "リセットするものはありません（もう何の経験もない状態です）" -ForegroundColor Green
    exit 0
}

# ---------------------------------------------------------------- 見せて、確かめる
function Get-Size([string]$Path) {
    if (Test-Path $Path -PathType Leaf) { return (Get-Item $Path).Length }
    return (Get-ChildItem $Path -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
}
$Total = 0
Write-Host ""
Write-Host "リセットするもの：" -ForegroundColor Cyan
foreach ($T in $Targets) {
    $Size = Get-Size $T.Path
    $Total += $Size
    Write-Host ("  {0,-26} {1,8:N1} MB  {2}" -f $T.Kind, ($Size / 1MB), $T.Path.Substring($Root.Length + 1))
}
Write-Host ("  合計 {0} 個・{1:N1} MB" -f $Targets.Count, ($Total / 1MB))
if ($NoBackup) {
    Write-Host "→ 消します（元に戻せません）" -ForegroundColor Yellow
} else {
    Write-Host "→ 消さずに $($BackupDir.Substring($Root.Length + 1)) へ移します（元に戻すときは中身を元の場所へ）"
}
if ($DryRun) {
    Write-Host "（-DryRun なので何もしません）" -ForegroundColor Cyan
    exit 0
}
if (-not $Yes) {
    $Answer = Read-Host "リセットしますか？ (y/N)"
    if ($Answer -notmatch '^(y|yes|はい)$') {
        Write-Host "やめました" -ForegroundColor Cyan
        exit 0
    }
}

# ---------------------------------------------------------------- リセット
$Failed = 0
foreach ($T in $Targets) {
    try {
        if ($NoBackup) {
            Remove-Item $T.Path -Recurse -Force -ErrorAction Stop
        } else {
            # 元の場所（リポジトリからの相対パス）のまま移す：backup_<日時>/<相対パス>
            $Rel = $T.Path.Substring($Root.Length + 1)
            $Dst = Join-Path $BackupDir $Rel
            New-Item -ItemType Directory -Force (Split-Path $Dst -Parent) | Out-Null
            Move-Item $T.Path $Dst -Force -ErrorAction Stop
        }
    } catch {
        $Failed++
        Write-Host "  できませんでした：$($T.Path)（$($_.Exception.Message)）" -ForegroundColor Red
    }
}
if ($Failed -gt 0) {
    Write-Host "$Failed 個はリセットできませんでした（ファイルを開いているプログラムを閉じて、もう一度）" -ForegroundColor Red
    exit 1
}
Write-Host "リセットしました。次の Tools\AgentPlaytest.bat は何の経験もない状態から始まります$(if (-not $NoBackup) { "（前のデータ：$BackupDir）" })" -ForegroundColor Green
