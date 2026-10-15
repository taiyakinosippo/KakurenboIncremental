# テストプレイ AI（Tools/AgentPlaytest）で、パッケージ化したゲーム（Packaged/Windows の .exe）をひたすら遊ばせる。
# Claude は使わない（AI は手元の Python。学習もこのパソコンの中だけ）。
#
#   powershell -ExecutionPolicy Bypass -File Tools\AgentPlaytest.ps1                   # 3 つの性格を同時に 8 時間（既定）
#   powershell -ExecutionPolicy Bypass -File Tools\AgentPlaytest.ps1 -Hours 2          # 2 時間
#   powershell -ExecutionPolicy Bypass -File Tools\AgentPlaytest.ps1 -Until 07:30      # その時刻まで
#   powershell -ExecutionPolicy Bypass -File Tools\AgentPlaytest.ps1 -Personas near     # 1 つだけ（stay / near / greedy をカンマで）
#   powershell -ExecutionPolicy Bypass -File Tools\AgentPlaytest.ps1 -Fresh            # セーブと学習を消して、はじめから
#   -Speed 2（ゲームの時間の速さ。既定 2。1 なら人と同じ）・-Watch（ウィンドウを大きく）・-Sound（音を出す）・-Open（終わったら報告書を開く）
#   -NoPublish（報告書を GitHub のブランチ agent-reports へ送らない。既定では 15 分ごとに送るので、外出先でも GitHub で LATEST.md を見られる）
#
# ダブルクリックで動かすときは Tools\AgentPlaytest.bat（8 時間・3 つの性格）。
# 記録と報告書：KakurenboIncremental/Saved/AgentPlaytest/<始めた日時>/report.html（と report.md）。15 分ごとと最後に作り直す。
# 学習（続けて上達する）：KakurenboIncremental/Saved/AgentPlaytest/brains/brain_<性格>.json
# ゲームのセーブは専用のスロット（Agent_<性格>）。あなたのセーブ・設定は読み書きしない。
param(
    [double]$Hours = 8,
    [string]$Until = "",
    [string]$Personas = "stay,near,greedy",
    [double]$Speed = 2,
    [switch]$Fresh,
    [switch]$Watch,
    [switch]$Sound,
    [switch]$Open,
    [switch]$NoPublish,
    [int]$BasePort = 7701
)

$Root = Split-Path $PSScriptRoot -Parent
$Exe = Join-Path $Root "Packaged\Windows\KakurenboIncremental.exe"
$GameSaved = Join-Path $Root "Packaged\Windows\KakurenboIncremental\Saved\SaveGames"
$OutRoot = Join-Path $Root "KakurenboIncremental\Saved\AgentPlaytest"
$AgentDir = Join-Path $PSScriptRoot "AgentPlaytest"

if (-not (Test-Path $Exe)) {
    Write-Host "パッケージ化したゲームがありません（$Exe）。先に Tools\AutoBuild.bat（またはTools\Package.ps1）でパッケージ化してください" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------- Python を探す（標準ライブラリだけで動く。3.8 以上）
function Find-Python {
    $cands = New-Object System.Collections.Generic.List[string]
    foreach ($name in @("python", "python3")) {
        $c = Get-Command $name -ErrorAction SilentlyContinue
        if ($c -and $c.Source -notmatch "WindowsApps") { $cands.Add($c.Source) }
    }
    foreach ($pattern in @("$env:USERPROFILE\.local\bin\python3*.exe", "$env:APPDATA\uv\python\cpython-3*\python.exe",
                           "$env:LOCALAPPDATA\Programs\Python\Python3*\python.exe", "C:\Python3*\python.exe")) {
        Get-ChildItem $pattern -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | ForEach-Object { $cands.Add($_.FullName) }
    }
    $uv = Get-Command uv -ErrorAction SilentlyContinue
    if (-not $uv -and (Test-Path "$env:USERPROFILE\.local\bin\uv.exe")) { $uv = Get-Item "$env:USERPROFILE\.local\bin\uv.exe" }
    if ($uv) {
        $found = & $uv.Source python find 2>$null
        if ($LASTEXITCODE -eq 0 -and $found) { $cands.Add($found.Trim()) }
    }
    foreach ($p in $cands) {
        $v = & $p -c "import sys; print(sys.version_info >= (3, 8))" 2>$null
        if ($LASTEXITCODE -eq 0 -and "$v".Trim() -eq "True") { return $p }
    }
    return $null
}
$Python = Find-Python
if (-not $Python) {
    Write-Host "Python 3.8 以上が見つかりません。https://www.python.org/ から入れるか、uv（https://docs.astral.sh/uv/）で 'uv python install 3.12' をしてください" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------- 準備
$PersonaList = $Personas.Split(",") | ForEach-Object { $_.Trim() } | Where-Object { $_ }
if ($Until) {
    $H, $M = $Until.Split(":")
    $Deadline = (Get-Date).Date.AddHours([int]$H).AddMinutes([int]$M)
    if ($Deadline -lt (Get-Date)) { $Deadline = $Deadline.AddDays(1) }
} else {
    $Deadline = (Get-Date).AddHours($Hours)
}
$DeadlineUnix = [DateTimeOffset]::new($Deadline).ToUnixTimeSeconds()

if ($Fresh) {
    foreach ($p in $PersonaList) {
        Remove-Item (Join-Path $GameSaved "Agent_$p.sav") -Force -ErrorAction SilentlyContinue
        Remove-Item (Join-Path $OutRoot "brains\brain_$p.json") -Force -ErrorAction SilentlyContinue
    }
}
$RunDir = Join-Path $OutRoot (Get-Date -Format "yyyyMMdd-HHmm")
New-Item -ItemType Directory -Force $RunDir | Out-Null
$LauncherLog = Join-Path $RunDir "launcher.log"
function Say([string]$Message, [string]$Color = "Gray") {
    $Line = "[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $Message
    Write-Host $Line -ForegroundColor $Color
    Add-Content -Path $LauncherLog -Value $Line -Encoding UTF8
}
Say "テストプレイ AI：$($PersonaList -join ', ')・$($Deadline.ToString('MM/dd HH:mm')) まで・速さ ×$Speed・記録 $RunDir" "Cyan"
Say "Python: $Python"

$W = if ($Watch) { 960 } else { 640 }
$Hh = [int]($W * 9 / 16)

# ---------------------------------------------------------------- 1 つの性格の、ゲームと AI を起動する
$State = @{}
function Start-Pair([string]$P, [int]$Index) {
    $Dir = Join-Path $RunDir $P
    New-Item -ItemType Directory -Force (Join-Path $Dir "shots") | Out-Null
    $Port = $BasePort + $Index
    $S = $State[$P]
    $S.Launch++
    $GameLog = Join-Path $Dir ("game_{0:D2}.log" -f $S.Launch)
    $X = ($Index % 3) * ($W + 8)
    $Y = [int]([math]::Floor($Index / 3)) * ($Hh + 40)
    $GameArgs = @("-windowed", "-ResX=$W", "-ResY=$Hh", "-WinX=$X", "-WinY=$Y", "-KakuNoSave", "-KakuSaveSlot=Agent_$P",
        "-KakuAgentPort=$Port", "-KakuAgentSpeed=$Speed", "-KakuAgentShots=`"$(Join-Path $Dir 'shots')`"", "-log", "-abslog=`"$GameLog`"", "-unattended")
    if (-not $Sound) { $GameArgs += "-NoSound" }
    $S.Game = Start-Process -FilePath $Exe -ArgumentList $GameArgs -PassThru
    $AgentArgs = @("`"$(Join-Path $AgentDir 'run_agent.py')`"", "--persona", $P, "--port", "$Port", "--dir", "`"$RunDir`"",
        "--deadline", "$DeadlineUnix", "--speed", "$Speed")
    $S.Agent = Start-Process -FilePath $Python -ArgumentList $AgentArgs -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $Dir ("agent_out_{0:D2}.txt" -f $S.Launch)) -RedirectStandardError (Join-Path $Dir ("agent_err_{0:D2}.txt" -f $S.Launch))
    $null = $S.Agent.Handle # 終わったときの戻り値（ExitCode）を読めるように、ハンドルを持っておく
    $S.GameLog = $GameLog
    $S.Started = Get-Date
    Say "$P を起動（$($S.Launch) 回目・ポート $Port）"
}

function Write-Crash([string]$P, [string]$Kind, [string]$Detail) {
    $Dir = Join-Path $RunDir $P
    $Obj = @{ time = (Get-Date -Format "yyyy-MM-dd HH:mm:ss"); kind = $Kind; detail = $Detail } | ConvertTo-Json -Compress
    Add-Content -Path (Join-Path $Dir "crashes.jsonl") -Value $Obj -Encoding UTF8
}

function Stop-Pair([string]$P) {
    $S = $State[$P]
    foreach ($Proc in @($S.Agent, $S.Game)) {
        if ($Proc -and -not $Proc.HasExited) { try { $Proc.Kill() } catch {} }
    }
    # パッケージの .exe は本体（Binaries の .exe）を別に起動するので、本体も止める（このポートのもの）
    Get-CimInstance Win32_Process -Filter "Name='KakurenboIncremental.exe' OR Name='KakurenboIncremental-Win64-Shipping.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -match "KakuAgentPort=$($BasePort + $S.Index)\b" } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

function Get-LogErrors([string]$Path) {
    if (-not (Test-Path $Path)) { return "" }
    $Lines = Get-Content $Path -Tail 400 -ErrorAction SilentlyContinue | Where-Object { $_ -match "Fatal|Assertion failed|Unhandled Exception|Error:|EXCEPTION_" } | Select-Object -Last 6
    return ($Lines -join " / ")
}

function Make-Report {
    try {
        $Out = & $Python (Join-Path $AgentDir "make_report.py") $RunDir 2>&1
        Say "報告書を更新：$(Join-Path $RunDir 'report.html')"
    } catch { Say "報告書を作れませんでした：$_" "Yellow" }
    if (-not $NoPublish) { Publish-Report }
}

# リモートで見られるように、報告書を GitHub の別ブランチ（agent-reports）へ送る（Claude は使わない）。
# 手元の作業（main・作業中のファイル・インデックス）には触れない：一時的なインデックスでコミットを作って、そのブランチへ push するだけ。
# 見るとき：GitHub のリポジトリ → ブランチ agent-reports → LATEST.md（または <日時>/report.md）。スマホの GitHub アプリでも表のまま読める
function Publish-Report {
    $Branch = "agent-reports"
    $Index = Join-Path $RunDir ".publish-index"
    $OldIndex = $env:GIT_INDEX_FILE
    try {
        Push-Location $Root
        git fetch -q origin $Branch 2>$null
        $Parent = (git rev-parse -q --verify "refs/remotes/origin/$Branch" 2>$null)
        $env:GIT_INDEX_FILE = $Index
        if ($Parent) { git read-tree $Parent.Trim() } else { git read-tree --empty }
        $RunName = Split-Path $RunDir -Leaf
        $Files = @(@{ Src = (Join-Path $RunDir "report.md"); Dst = "$RunName/report.md" }, @{ Src = (Join-Path $RunDir "report.html"); Dst = "$RunName/report.html" },
                   @{ Src = (Join-Path $RunDir "report.md"); Dst = "LATEST.md" })
        foreach ($P in $PersonaList) { $Files += @{ Src = (Join-Path $RunDir "$P\report.md"); Dst = "$RunName/$P.md" } }
        foreach ($F in $Files) {
            if (Test-Path $F.Src) {
                $Hash = (git hash-object -w $F.Src 2>$null).Trim()
                git update-index --add --cacheinfo "100644,$Hash,$($F.Dst)"
            }
        }
        $Tree = (git write-tree).Trim()
        $Message = "AI playtest report $RunName $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
        $Commit = if ($Parent) { (git commit-tree $Tree -p $Parent.Trim() -m $Message).Trim() } else { (git commit-tree $Tree -m $Message).Trim() }
        git push -q origin "${Commit}:refs/heads/$Branch" 2>$null
        if ($LASTEXITCODE -eq 0) { Say "報告書を GitHub のブランチ $Branch に送りました（LATEST.md）" } else { Say "報告書を GitHub に送れませんでした（ネットワーク・ログインを確かめてください）" "Yellow" }
    } catch { Say "報告書を GitHub に送れませんでした：$_" "Yellow" }
    finally {
        if ($OldIndex) { $env:GIT_INDEX_FILE = $OldIndex } else { Remove-Item Env:GIT_INDEX_FILE -ErrorAction SilentlyContinue }
        Remove-Item $Index -Force -ErrorAction SilentlyContinue
        Pop-Location
    }
}

# ---------------------------------------------------------------- 見張る（落ちた・固まったら起動し直す）
$i = 0
foreach ($P in $PersonaList) {
    $State[$P] = @{ Launch = 0; Index = $i; Done = $false }
    Start-Pair $P $i
    $i++
    Start-Sleep -Seconds 4
}
$NextReport = (Get-Date).AddMinutes(15)
try {
    while ($true) {
        Start-Sleep -Seconds 5
        $AllDone = $true
        foreach ($P in $PersonaList) {
            $S = $State[$P]
            if ($S.Done) { continue }
            $AllDone = $false
            if ($S.Agent.HasExited) {
                $Code = $S.Agent.ExitCode
                if ((Get-Date) -ge $Deadline.AddSeconds(-5) -or $Code -eq 0) {
                    Say "$P は時間になったので終わり" "Green"
                    Start-Sleep -Seconds 3
                    Stop-Pair $P
                    $S.Done = $true
                    continue
                }
                # AI が「つながらない・切れた・止まった」で戻った：ゲームが落ちたか固まった
                $GameDead = $S.Game.HasExited
                $Errors = Get-LogErrors $S.GameLog
                $Kind = if ($GameDead) { "crash" } else { "hang" }
                Say "$P：ゲームが$(if ($GameDead) { '落ちた' } else { '応答しない' })ので起動し直す $Errors" "Yellow"
                Write-Crash $P $Kind ("launch {0}: {1}" -f $S.Launch, $Errors)
                Stop-Pair $P
                Start-Sleep -Seconds 5
                Start-Pair $P $S.Index
            }
            elseif ($S.Game.HasExited -and ((Get-Date) - $S.Started).TotalSeconds -gt 30) {
                # ゲームだけ落ちた（AI はすぐ気づいて戻ってくる。来なければこちらで止める）
                Start-Sleep -Seconds 10
                if (-not $S.Agent.HasExited) { try { $S.Agent.Kill() } catch {} }
            }
        }
        if ($AllDone) { break }
        if ((Get-Date) -ge $NextReport) {
            Make-Report
            $NextReport = (Get-Date).AddMinutes(15)
        }
        if ((Get-Date) -gt $Deadline.AddMinutes(3)) {
            Say "時間を過ぎても終わらないので止める" "Yellow"
            foreach ($P in $PersonaList) { Stop-Pair $P; $State[$P].Done = $true }
            break
        }
    }
}
finally {
    foreach ($P in $PersonaList) { Stop-Pair $P }
    Make-Report
    Say "おわり。報告書：$(Join-Path $RunDir 'report.html')（Markdown は report.md）" "Cyan"
    if ($Open) { Start-Process (Join-Path $RunDir "report.html") }
}
