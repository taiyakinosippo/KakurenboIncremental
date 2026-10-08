# 自動ビルド：pull → ビルド → （FBX があれば）プレイヤーと宝石の取り込み → 配布用の .exe（パッケージ化） → （指定があれば）テスト。
# Binaries は Git に入らないので、pull した後にビルドしないと古いゲームのまま動く。これを 1 回で済ませる。
#
#   powershell -ExecutionPolicy Bypass -File Tools\AutoBuild.ps1                 # pull してビルドとパッケージ化（いつもはこれ。.exe は Packaged/Windows）
#   powershell -ExecutionPolicy Bypass -File Tools\AutoBuild.ps1 -NoPackage      # パッケージ化はしない（エディタ用のビルドだけ。速い）
#   powershell -ExecutionPolicy Bypass -File Tools\AutoBuild.ps1 -Test           # 単体テストも流す
#   パッケージ化は、前回と同じコミットで手元に変更が無ければ飛ばす（-Package を付けると必ず作り直す）。初回は数十分かかる
#   powershell -ExecutionPolicy Bypass -File Tools\AutoBuild.ps1 -Unity          # 全部まとめてビルド（コミット前の確認と同じ）
#   powershell -ExecutionPolicy Bypass -File Tools\AutoBuild.ps1 -NoPull         # pull しないでビルドだけ
#   powershell -ExecutionPolicy Bypass -File Tools\AutoBuild.ps1 -Watch          # 10 分ごとに GitHub を見て、新しいコミットがあれば pull してビルド（Ctrl+C で止める）
#   powershell -ExecutionPolicy Bypass -File Tools\AutoBuild.ps1 -InstallHook    # このパソコンで「git pull したら自動でビルド」にする
#   powershell -ExecutionPolicy Bypass -File Tools\AutoBuild.ps1 -UninstallHook  # ↑をやめる
#
# ダブルクリックで動かすときは Tools\AutoBuild.bat。結果は KakurenboIncremental/Saved/Logs/AutoBuild.log にも残る。
# UE エディタ（とテスト中のゲーム）が開いていると DLL が使用中でビルドできないので、そのときは何もせず「閉じてから」と知らせる。
param(
    [switch]$NoPull,
    [switch]$Test,
    [switch]$Package,   # 最新でも必ずパッケージ化する
    [switch]$NoPackage, # パッケージ化しない
    [switch]$Unity,
    [switch]$Watch,
    [int]$IntervalMin = 10,
    [switch]$InstallHook,
    [switch]$UninstallHook,
    [switch]$FromHook  # git の post-merge フックから呼ばれた（pull はもう済んでいる）
)

$Root = Split-Path $PSScriptRoot -Parent
$ProjectDir = Join-Path $Root "KakurenboIncremental"
$LogDir = Join-Path $ProjectDir "Saved\Logs"
New-Item -ItemType Directory -Force $LogDir | Out-Null
$LogFile = Join-Path $LogDir "AutoBuild.log"
$BuildLog = Join-Path $LogDir "AutoBuild-Build.log"

function Say([string]$Message, [string]$Color = "Gray") {
    $Line = "[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $Message
    Write-Host $Line -ForegroundColor $Color
    Add-Content -Path $LogFile -Value $Line -Encoding UTF8
}

# ---------------------------------------------------------------- git のフック（pull したら自動でビルド）

$HookMarker = "# kakurenbo-autobuild"
$HookLine = "$HookMarker`npowershell.exe -NoProfile -ExecutionPolicy Bypass -File `"`$(git rev-parse --show-toplevel)/Tools/AutoBuild.ps1`" -FromHook || true"

function Get-HookPath {
    $HooksDir = (git -C $Root rev-parse --git-path hooks).Trim()
    if (-not [System.IO.Path]::IsPathRooted($HooksDir)) { $HooksDir = Join-Path $Root $HooksDir }
    return Join-Path $HooksDir "post-merge"
}

if ($InstallHook -or $UninstallHook) {
    # post-merge フックには Git LFS の行が入っているので、消さずに 1 行足す / その行だけ消す
    $Hook = Get-HookPath
    $Text = if (Test-Path $Hook) { [System.IO.File]::ReadAllText($Hook) } else { "#!/bin/sh`n" }
    $Text = ($Text -replace "(?ms)\r?\n?$([regex]::Escape($HookMarker)).*?\|\| true", "").TrimEnd() + "`n"
    if ($InstallHook) { $Text += "$HookLine`n" }
    [System.IO.File]::WriteAllText($Hook, $Text.Replace("`r`n", "`n"), (New-Object System.Text.UTF8Encoding($false)))
    if ($InstallHook) { Write-Host "git pull で新しいコミットを取ってきたら、自動でビルドするようにしました（$Hook）" }
    else { Write-Host "git pull の後の自動ビルドをやめました" }
    exit 0
}

# ---------------------------------------------------------------- 準備

. (Join-Path $PSScriptRoot "EnginePath.ps1") # $Engine に UE 5.8 のインストール先が入る
$Project = Join-Path $ProjectDir "KakurenboIncremental.uproject"

function Get-BlockingEditors {
    # このプロジェクトを開いている UE（エディタ・テスト中のゲーム・コマンドレット）
    Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe' OR Name='UnrealEditor-Cmd.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine -match "KakurenboIncremental\.uproject" }
}

function Invoke-Pull {
    # 戻り値：新しいコミットを取ってきたら true
    $Before = (git -C $Root rev-parse HEAD).Trim()
    Say "git pull ..."
    $Out = git -C $Root pull --ff-only 2>&1
    if ($LASTEXITCODE -ne 0) {
        Say "git pull できませんでした（手元に未コミットの変更がある・別のパソコンとコミットが分かれている など）。ビルドは今の状態で続けます" "Yellow"
        $Out | ForEach-Object { Say "  $_" "DarkYellow" }
        return $false
    }
    $After = (git -C $Root rev-parse HEAD).Trim()
    if ($Before -eq $After) {
        Say "新しいコミットはありません"
        return $false
    }
    Say "新しいコミット：" "Cyan"
    git -C $Root log --oneline "$Before..$After" | ForEach-Object { Say "  $_" "Cyan" }
    return $true
}

function Test-FabAssets {
    # 無くても動く（円柱・箱・色で代用）けれど、見た目が変わるので知らせる。Git に入らないアセット
    $Missing = @()
    $Checks = [ordered]@{
        "鬼の見た目（Fab: Cute Creature）"                 = "Content\CuteCreature"
        "館の家具（Fab: Stylized Library）"                = "Content\Stylized_Library"
        "鉄の壁（Fab: Stylized Metallic Floor）"           = "Content\Metallic_Floor"
        "石の壁（Fab: Stylized Hand-Painted Stone Wall）"  = "Content\Materials_Bundle_Vol1"
        "床・壁の木（Fab の木を縮めたもの）"            = "Content\Kakurenbo\Wood"
        "消音壁（Fab: Stucco Wall）"                       = "Content\QuietWall"
    }
    foreach ($Name in $Checks.Keys) {
        if (-not (Test-Path (Join-Path $ProjectDir $Checks[$Name]))) { $Missing += $Name }
    }
    foreach ($Name in $Missing) { Say "  まだ入っていないアセット: $Name（エディタの Fab から追加。docs/Setup.md）" "DarkYellow" }
}

function Invoke-ImportIfNeeded {
    # プレイヤーと宝石は FBX（SourceArt/）から取り込む。FBX があるのに取り込んでいなければ取り込む
    $Need = @()
    if ((Test-Path (Join-Path $ProjectDir "SourceArt\Player\Male_002.fbx")) -and -not (Test-Path (Join-Path $ProjectDir "Content\Player\Male_002.uasset"))) { $Need += "プレイヤー" }
    if ((Test-Path (Join-Path $ProjectDir "SourceArt\Gem\Diamond_Shape2.fbx")) -and -not (Test-Path (Join-Path $ProjectDir "Content\Gem\Circle_001.uasset"))) { $Need += "宝石" }
    if (-not (Test-Path (Join-Path $ProjectDir "Content\Characters\Mannequins\Meshes\SKM_Manny_Simple.uasset"))) { $Need += "公式のアニメーション（マネキン）" }
    if ($Need.Count -eq 0) {
        if (-not (Test-Path (Join-Path $ProjectDir "Content\Player")) -and -not (Test-Path (Join-Path $ProjectDir "SourceArt\Player"))) {
            Say "  プレイヤーと宝石の FBX はまだありません（無くても動く。入れ方は docs/Setup.md）" "DarkYellow"
        }
        return $true
    }
    Say ("FBX を取り込みます: " + ($Need -join "・"))
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "ImportSourceArt.ps1") | ForEach-Object { Add-Content -Path $BuildLog -Value $_ -Encoding UTF8 }
    if ($LASTEXITCODE -ne 0) { Say "FBX の取り込みに失敗しました（$BuildLog）" "Red"; return $false }
    Say "FBX を取り込みました" "Green"
    return $true
}

function Invoke-Build {
    Say ("ビルド中..." + $(if ($Unity) { "（全部まとめて）" } else { "" }))
    $Start = Get-Date
    $BuildArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "Build.ps1"))
    if ($Unity) { $BuildArgs += "-Unity" }
    & powershell @BuildArgs *>&1 | Out-File -FilePath $BuildLog -Encoding UTF8
    $Code = $LASTEXITCODE
    $Seconds = [int]((Get-Date) - $Start).TotalSeconds
    if ($Code -ne 0) {
        Say "ビルドに失敗しました（$Seconds 秒）。エラー：" "Red"
        Select-String -Path $BuildLog -Pattern "error |error:|: error" | Select-Object -First 15 | ForEach-Object { Say ("  " + $_.Line.Trim()) "Red" }
        Say "  全文: $BuildLog" "Red"
        return $false
    }
    Say "ビルドできました（$Seconds 秒）" "Green"
    return $true
}

function Invoke-UnitTests {
    Say "単体テスト中..."
    $Lines = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "RunUnitTests.ps1")
    $Results = @($Lines | Select-String -Pattern "Result=\{(\w+)\} Name=\{(\w+)\}" | ForEach-Object { $_.Matches[0] })
    $Bad = @($Results | Where-Object { $_.Groups[1].Value -ne "Success" })
    if ($Results.Count -eq 0 -or $Bad.Count -gt 0) {
        Say ("単体テスト：{0} 個中 {1} 個失敗" -f $Results.Count, $Bad.Count) "Red"
        $Bad | ForEach-Object { Say ("  " + $_.Groups[2].Value + ": " + $_.Groups[1].Value) "Red" }
        return $false
    }
    Say ("単体テスト：{0} 個すべて成功" -f $Results.Count) "Green"
    return $true
}

function Get-PackageStamp {
    # 今のコミットと、手元の変更（未コミット・Git に入らない Fab のアセットの有無）をまとめた印。前回のパッケージ化と同じなら作り直さない
    $Head = (git -C $Root rev-parse HEAD).Trim()
    $Dirty = (git -C $Root status --porcelain | Out-String).Trim()
    $Assets = (Get-ChildItem (Join-Path $ProjectDir "Content") -Directory -ErrorAction SilentlyContinue | ForEach-Object Name) -join ","
    return "$Head|$Dirty|$Assets"
}

function Invoke-Package {
    $Exe = Join-Path $Root "Packaged\Windows\KakurenboIncremental.exe"
    $StampFile = Join-Path $Root "Packaged\autobuild-stamp.txt"
    $Stamp = Get-PackageStamp
    if (-not $Package -and (Test-Path $Exe) -and (Test-Path $StampFile) -and ((Get-Content $StampFile -Raw -Encoding UTF8).Trim() -eq $Stamp.Trim())) {
        Say "配布用の .exe は最新です（$Exe）" "Green"
        return $true
    }
    $PackageLog = Join-Path $LogDir "AutoBuild-Package.log"
    Say "配布用の .exe を作っています（初回は数十分、2 回目からは数分。ログ: $PackageLog）..."
    $Start = Get-Date
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "Package.ps1") *>&1 | Out-File -FilePath $PackageLog -Encoding UTF8
    $Code = $LASTEXITCODE
    $Minutes = [Math]::Round(((Get-Date) - $Start).TotalMinutes, 1)
    if ($Code -ne 0 -or -not (Test-Path $Exe)) {
        Say "パッケージ化に失敗しました（$Minutes 分）。エラー：" "Red"
        Select-String -Path $PackageLog -Pattern "Error:|error |: error|ERROR:" | Select-Object -First 15 | ForEach-Object { Say ("  " + $_.Line.Trim()) "Red" }
        Say "  全文: $PackageLog" "Red"
        return $false
    }
    Set-Content -Path $StampFile -Value $Stamp -Encoding UTF8
    Say "配布用の .exe ができました（$Minutes 分）: $Exe" "Green"
    Say "  配るときは Packaged\Windows フォルダごと渡す" "Green"
    return $true
}

function Invoke-AutoBuild([bool]$DoPull) {
    Say "==== 自動ビルド開始（$(Split-Path $Root -Leaf)）====" "White"
    if ($DoPull) { [void](Invoke-Pull) }

    $Blocking = @(Get-BlockingEditors)
    if ($Blocking.Count -gt 0) {
        Say "UE エディタ（またはテスト中のゲーム）が開いているのでビルドできません。閉じてから、もう一度 Tools\AutoBuild を実行してください" "Yellow"
        $Blocking | ForEach-Object { Say ("  開いている: PID {0}" -f $_.ProcessId) "DarkYellow" }
        return 2
    }

    if (-not (Invoke-Build)) { return 1 }
    if (-not (Invoke-ImportIfNeeded)) { return 1 }
    Test-FabAssets
    if (-not $NoPackage -and -not (Invoke-Package)) { return 1 }
    if ($Test -and -not (Invoke-UnitTests)) { return 1 }
    if ($NoPackage) { Say "==== 完了：最新のゲームで遊べます（エディタ）====" "Green" }
    else { Say "==== 完了：最新のゲームで遊べます（エディタでも .exe でも）====" "Green" }
    return 0
}

# ---------------------------------------------------------------- 実行

if ($Watch) {
    # 一定間隔で GitHub を見て、新しいコミットがあれば pull してビルドする（別のパソコンで push した分を自動で取り込む）
    Say "見張りを始めます（$IntervalMin 分ごと。Ctrl+C で止める）" "White"
    while ($true) {
        git -C $Root fetch --quiet 2>$null
        $Behind = 0
        [int]::TryParse(((git -C $Root rev-list --count "HEAD..@{u}" 2>$null) | Out-String).Trim(), [ref]$Behind) | Out-Null
        if ($Behind -gt 0) {
            Say "GitHub に新しいコミットが $Behind 個あります" "Cyan"
            [void](Invoke-AutoBuild $true)
        }
        Start-Sleep -Seconds ([Math]::Max(1, $IntervalMin) * 60)
    }
}

$Code = Invoke-AutoBuild (-not ($NoPull -or $FromHook))
if ($FromHook) { exit 0 } # フックの失敗で git pull を失敗扱いにしない
exit $Code
