# SNS 用の絵（SNS/art/*.html）を Edge のヘッドレスで写して SNS/media/*.png にする（Tools/MakeLogo.ps1 と同じやり方）。
#   powershell -ExecutionPolicy Bypass -File SNS\art\Render.ps1
# 文字は Google Fonts から読むので、インターネットにつながっている必要がある
$ErrorActionPreference = "Stop"
$Art = $PSScriptRoot
$Media = Join-Path (Split-Path $Art -Parent) "media"
$Edge = @(
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Edge) { throw "Microsoft Edge が見つかりません" }
$EdgeProfile = Join-Path $env:TEMP "KakuSnsEdge"

# 名前, 幅, 高さ（2 倍で写す）
$Jobs = @(
    @("icon_peek", 400, 400),
    @("icon_bo", 400, 400),
    @("icon_koban", 400, 400),
    @("header", 1500, 500),
    @("header_game", 1500, 500),
    @("history/h01", 1200, 675),
    @("history/h02", 1200, 675),
    @("history/h03", 1200, 675)
)
foreach ($J in $Jobs) {
    $Html = Join-Path $Art "$($J[0]).html"
    $Out = Join-Path $Media "$($J[0]).png"
    if (Test-Path $Out) { Remove-Item $Out }
    $Url = "file:///" + ($Html -replace "\\", "/")
    $EdgeArgs = @("--headless=new", "--disable-gpu", "--hide-scrollbars", "--user-data-dir=`"$EdgeProfile`"",
        "--force-device-scale-factor=2", "--window-size=$($J[1]),$($J[2])",
        "--virtual-time-budget=8000", "--screenshot=`"$Out`"", $Url)
    $p = Start-Process -FilePath $Edge -ArgumentList $EdgeArgs -Wait -PassThru -WindowStyle Hidden
    if (-not (Test-Path $Out)) { throw "$Out ができませんでした（Edge の終了コード $($p.ExitCode)）" }
    Write-Host "[Render] $Out"
}
