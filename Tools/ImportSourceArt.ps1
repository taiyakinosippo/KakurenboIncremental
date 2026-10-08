# Fab からダウンロードした FBX（SourceArt/Player・SourceArt/Gem）を UE のアセットにする。UE エディタは閉じておく。
#   powershell -ExecutionPolicy Bypass -File Tools\ImportSourceArt.ps1
# 中身は Tools/ImportSourceArt.py（エディタの Python をコマンドラインで動かす）
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\EnginePath.ps1"
$Root = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $Root "KakurenboIncremental\KakurenboIncremental.uproject"
$Script = Join-Path $PSScriptRoot "ImportSourceArt.py"
$Editor = Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$Log = & $Editor $Project -run=pythonscript -script="$Script" -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput 2>&1
$Log | Where-Object { $_ -match "\[ImportSourceArt\]|LogPython: Error" }
# FBX の中の空のメッシュなどでエラーが出ても、最後まで動けば成功とする
if ($Log -match "\[ImportSourceArt\] done") { exit 0 } else { exit 1 }
