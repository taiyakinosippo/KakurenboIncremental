# タイトルロゴ（SourceArt/Logo/TitleLogo.html）を Edge のヘッドレスで写して、背景が透明な PNG にする。
#   powershell -ExecutionPolicy Bypass -File Tools\MakeLogo.ps1
# できた SourceArt/Logo/TitleLogo.png を UE のテクスチャにするのは Tools/ImportSourceArt.py --logo-only
# （UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="Tools/ImportSourceArt.py --logo-only"）。
# 文字は Google Fonts から読むので、インターネットにつながっている必要がある
param([string]$Out = "")
$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
$Html = Join-Path $Root "KakurenboIncremental\SourceArt\Logo\TitleLogo.html"
if (-not $Out) { $Out = Join-Path $Root "KakurenboIncremental\SourceArt\Logo\TitleLogo.png" }

$Edge = @(
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Edge) { throw "Microsoft Edge が見つかりません" }

# 別のプロファイルで動かす（ふだん使っている Edge の窓とぶつからないように）
$EdgeProfile = Join-Path $env:TEMP "KakuLogoEdge"
$Url = "file:///" + ($Html -replace "\\", "/")
if (Test-Path $Out) { Remove-Item $Out }
# 1000×284 の絵を 2 倍（2000×568）で写す。背景は透明（00000000）。文字を読み終わるまで待つ（virtual-time-budget）
$EdgeArgs = @("--headless=new", "--disable-gpu", "--hide-scrollbars", "--user-data-dir=`"$EdgeProfile`"",
    "--default-background-color=00000000", "--force-device-scale-factor=2", "--window-size=1000,284",
    "--virtual-time-budget=8000", "--screenshot=`"$Out`"", $Url)
$p = Start-Process -FilePath $Edge -ArgumentList $EdgeArgs -Wait -PassThru -WindowStyle Hidden
if (-not (Test-Path $Out)) { throw "PNG ができませんでした（Edge の終了コード $($p.ExitCode)）" }
Write-Host "[MakeLogo] $Out"
