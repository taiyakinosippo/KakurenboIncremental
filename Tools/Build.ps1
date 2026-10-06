# エディタ用ターゲットをビルドする（UE エディタは閉じておくこと）
$ErrorActionPreference = "Stop"
$Engine = "C:\Program Files\Epic Games\UE_5.8"
$Project = Join-Path $PSScriptRoot "..\KakurenboIncremental\KakurenboIncremental.uproject" | Resolve-Path

& "$Engine\Engine\Build\BatchFiles\Build.bat" KakurenboIncrementalEditor Win64 Development "-Project=$Project" -WaitMutex -NoHotReloadFromIDE
exit $LASTEXITCODE
