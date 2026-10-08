# Fab からダウンロードした FBX（SourceArt/Player・SourceArt/Gem）を UE のアセットにする。UE エディタは閉じておく。
#   powershell -ExecutionPolicy Bypass -File Tools\ImportSourceArt.ps1
# 中身は Tools/ImportSourceArt.py（エディタの Python をコマンドラインで動かす）
# プレイヤーの動き（公式のマネキンのアニメーション）は、UE に付いてくるテンプレートから Content/Characters/Mannequins へコピーする
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\EnginePath.ps1"
$Root = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $Root "KakurenboIncremental\KakurenboIncremental.uproject"
$Script = Join-Path $PSScriptRoot "ImportSourceArt.py"
$Editor = Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

# 公式のマネキン（Third Person テンプレートと同じもの）。パスは /Game/Characters/Mannequins（テンプレートと同じ場所に置くので、中の参照もそのまま使える）
$Mannequins = Join-Path $Root "KakurenboIncremental\Content\Characters\Mannequins"
if (-not (Test-Path (Join-Path $Mannequins "Meshes\SKM_Manny_Simple.uasset"))) {
    $Source = Join-Path $Engine "Templates\TemplateResources\High\Characters\Content\Mannequins"
    if (Test-Path $Source) {
        Write-Host "[ImportSourceArt] copy official mannequin animations from $Source"
        New-Item -ItemType Directory -Force (Split-Path $Mannequins) | Out-Null
        Copy-Item $Source $Mannequins -Recurse -Force
    } else {
        Write-Host "[ImportSourceArt] official mannequin not found ($Source). The player moves with the simple built-in motion."
    }
}

$Log = & $Editor $Project -run=pythonscript -script="$Script" -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput 2>&1
$Log | Where-Object { $_ -match "\[ImportSourceArt\]|LogPython: Error" }
# FBX の中の空のメッシュなどでエラーが出ても、最後まで動けば成功とする
if ($Log -match "\[ImportSourceArt\] done") { exit 0 } else { exit 1 }
