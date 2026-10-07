# エディタ用ターゲットをビルドする（UE エディタは閉じておくこと）
#   -Unity: 変更したファイルも含めて全部を 1 つにまとめてビルドする（別のパソコンや pull 後と同じビルドになる。コミット前の確認用）
param([switch]$Unity)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "EnginePath.ps1") # $Engine に UE 5.8 のインストール先が入る
$Project = Join-Path $PSScriptRoot "..\KakurenboIncremental\KakurenboIncremental.uproject" | Resolve-Path

& "$Engine\Engine\Build\BatchFiles\Build.bat" KakurenboIncrementalEditor Win64 Development "-Project=$Project" -WaitMutex -NoHotReloadFromIDE $(if ($Unity) { "-DisableAdaptiveUnity" })
exit $LASTEXITCODE
