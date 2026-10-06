# エディタ用ターゲットをビルドする（UE エディタは閉じておくこと）
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "EnginePath.ps1") # $Engine に UE 5.8 のインストール先が入る
$Project = Join-Path $PSScriptRoot "..\KakurenboIncremental\KakurenboIncremental.uproject" | Resolve-Path

& "$Engine\Engine\Build\BatchFiles\Build.bat" KakurenboIncrementalEditor Win64 Development "-Project=$Project" -WaitMutex -NoHotReloadFromIDE
exit $LASTEXITCODE
