# 配布用のゲーム（.exe）を作る（パッケージ化）。UE エディタが無いパソコンでも遊べる形になる。
#   出来上がり: <リポジトリ>/Packaged/Windows/KakurenboIncremental.exe（フォルダごと配る）
#   初回はシェーダーの準備などで時間がかかる（数十分）。2 回目からは速い
#
#   -Config Development（既定）: 設置パートの枠・グリッド線（開発用の描画）も出る
#   -Config Shipping           : 配布向け。ただし今は設置パートの枠・グリッド線が出ない（メッシュに置き換えるまでは Development を使う）
param([ValidateSet("Development", "Shipping")][string]$Config = "Development")

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "EnginePath.ps1") # $Engine に UE 5.8 のインストール先が入る
$Project = Join-Path $PSScriptRoot "..\KakurenboIncremental\KakurenboIncremental.uproject" | Resolve-Path
$OutDir = Join-Path $PSScriptRoot "..\Packaged"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = Resolve-Path $OutDir

# ビルド → クック（アセットをゲーム用に変換）→ まとめる → OutDir へ書き出す
# -nocompileeditor: エディタ側のビルドはしない（エディタを開いていても DLL のロックで失敗しないように。先に Build.ps1 を済ませておく）
& "$Engine\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$Project" -noP4 -platform=Win64 "-clientconfig=$Config" `
    -build -cook -stage -pak -archive "-archivedirectory=$OutDir" -nocompileeditor -utf8output
if ($LASTEXITCODE -ne 0) { Write-Host "パッケージ化に失敗しました（上のログを見てください）"; exit $LASTEXITCODE }

# バランスの CSV（Data/*.csv）はアセットではないので自動では入らない。ゲームは <exe のフォルダ>/KakurenboIncremental/Data を読む
$GameDir = Join-Path $OutDir "Windows\KakurenboIncremental"
Copy-Item (Join-Path $PSScriptRoot "..\KakurenboIncremental\Data") $GameDir -Recurse -Force
Write-Host "完成: $(Join-Path $OutDir 'Windows\KakurenboIncremental.exe')"
