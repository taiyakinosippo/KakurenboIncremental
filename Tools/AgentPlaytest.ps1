# テストプレイ AI（Tools/AgentPlaytest）で、パッケージ化したゲーム（Packaged/Windows の .exe）をひたすら遊ばせる。
# Claude は使わない（AI は手元の Python。学習もこのパソコンの中だけ）。
#
#   powershell -ExecutionPolicy Bypass -File Tools\AgentPlaytest.ps1                   # 3 つの性格を同時に 8 時間（既定）
#   powershell -ExecutionPolicy Bypass -File Tools\AgentPlaytest.ps1 -Hours 2          # 2 時間
#   powershell -ExecutionPolicy Bypass -File Tools\AgentPlaytest.ps1 -Until 07:30      # その時刻まで
#   powershell -ExecutionPolicy Bypass -File Tools\AgentPlaytest.ps1 -Personas near     # 1 つだけ（stay / near / greedy / builder / nowall をカンマで）
#   powershell -ExecutionPolicy Bypass -File Tools\AgentPlaytest.ps1 -Fresh            # セーブと学習を消して、はじめから
#   -Speed 2（ゲームの時間の速さ。既定 2。1 なら人と同じ）・-Watch（ウィンドウを大きく）・-Sound（音を出す）・-Open（終わったら報告書を開く）
#   既定は軽くして動かす（コマ数の上限・画質を一番低く・ゲームと AI の優先度を「通常以下」。パソコンのほかの作業を邪魔しない）。
#   -Full で軽くしない（-MaxFPS 40 で上限を変える。既定 30。5 つ同時で PC 全体の CPU が 7 割くらい）
#   Claude のセッションから起動するときは explorer.exe 経由で（Claude のアプリが更新・再起動すると、アプリから起動したゲームと AI も一緒に止まるため）
#   -NoPublish（報告書を GitHub のブランチ agent-reports へ送らない。既定では 15 分ごとに <PC の名前>/README.md を更新するので、外出先でも GitHub で見られる）
#   記録は始めた日時のフォルダ（KakurenboIncremental/Saved/AgentPlaytest/<日時>/）。学習とゲームのセーブは前回の続き。-Continue で前回のフォルダに書き足す
#   8 時間が終わって 5 分キーボード・マウスにさわらなければ、同じデータ（学習・セーブ・記録のフォルダ）のまま、もう 8 時間続ける（何回でも）。
#   報告書は 1 周目が report.html・report.md、2 周目からは report_2周目.html のように周の番号を付けて足す。-NoRepeat で 1 回で終わる・-IdleMinutes 10 で待つ時間を変える
#
# ダブルクリックで動かすときは Tools\AgentPlaytest.bat（8 時間・3 つの性格）。
# 記録と報告書：KakurenboIncremental/Saved/AgentPlaytest/<始めた日時>/report.html（と report.md）。15 分ごとと最後に作り直す。
# 学習（続けて上達する）：KakurenboIncremental/Saved/AgentPlaytest/brains/brain_<性格>.json
# ゲームのセーブは専用のスロット（Agent_<性格>）。あなたのセーブ・設定は読み書きしない。
param(
    [double]$Hours = 8,
    [string]$Until = "",
    [string]$Personas = "stay,near,greedy,builder,nowall",
    [double]$Speed = 2,
    [switch]$Fresh,
    [switch]$Watch,
    [switch]$Sound,
    [switch]$Open,
    [switch]$NoPublish,
    [switch]$NewRun,
    [switch]$Continue,
    [switch]$NoRepeat,
    [double]$IdleMinutes = 5,
    [switch]$Full,
    [int]$MaxFPS = 30,
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
# 記録は始めた日時のフォルダに書く（ユーザーの希望）。-Continue のときだけ前回のフォルダに書き足す
$Latest = Get-ChildItem $OutRoot -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^\d{8}-\d{4}$' } | Sort-Object Name | Select-Object -Last 1
if ($Latest -and $Continue -and -not $Fresh -and -not $NewRun) {
    $RunDir = $Latest.FullName
} else {
    $RunDir = Join-Path $OutRoot (Get-Date -Format "yyyyMMdd-HHmm")
}
New-Item -ItemType Directory -Force $RunDir | Out-Null
$LauncherLog = Join-Path $RunDir "launcher.log"
function Say([string]$Message, [string]$Color = "Gray") {
    $Line = "[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $Message
    Write-Host $Line -ForegroundColor $Color
    Add-Content -Path $LauncherLog -Value $Line -Encoding UTF8
}
Say "$(if ($Latest -and $RunDir -eq $Latest.FullName) { '前回の記録の続き：' } else { '' })テストプレイ AI：$($PersonaList -join ', ')・$($Deadline.ToString('MM/dd HH:mm')) まで・速さ ×$Speed$(if ($Full) { '' } else { '・軽くして動かす' })・記録 $RunDir" "Cyan"
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
    if ($S.Launch -eq 0) {
        # 前回の記録の続きなら、前の起動のログを上書きしないよう番号も続きから
        $S.Launch = @(Get-ChildItem (Join-Path $Dir "game_*.log") -ErrorAction SilentlyContinue).Count
    }
    $S.Launch++
    $GameLog = Join-Path $Dir ("game_{0:D2}.log" -f $S.Launch)
    $X = ($Index % 3) * ($W + 8)
    $Y = [int]([math]::Floor($Index / 3)) * ($Hh + 40)
    $GameArgs = @("-windowed", "-ResX=$W", "-ResY=$Hh", "-WinX=$X", "-WinY=$Y", "-KakuNoSave", "-KakuSaveSlot=Agent_$P",
        "-KakuAgentPort=$Port", "-KakuAgentSpeed=$Speed", "-KakuAgentShots=`"$(Join-Path $Dir 'shots')`"", "-log", "-abslog=`"$GameLog`"", "-unattended")
    if (-not $Sound) { $GameArgs += "-NoSound" }
    if (-not $Full) {
        $GameArgs += "-KakuAgentLight"
        if ($MaxFPS -gt 0) { $GameArgs += "-KakuAgentMaxFPS=$MaxFPS" }
    }
    $S.Game = Start-Process -FilePath $Exe -ArgumentList $GameArgs -PassThru
    if (-not $Full) {
        # 優先度を「通常以下」に：パソコンでほかの作業をしているときは、そちらを先に動かす
        try { $S.Game.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal } catch {}
    }
    $AgentArgs = @("`"$(Join-Path $AgentDir 'run_agent.py')`"", "--persona", $P, "--port", "$Port", "--dir", "`"$RunDir`"",
        "--deadline", "$DeadlineUnix", "--speed", "$Speed")
    $S.Agent = Start-Process -FilePath $Python -ArgumentList $AgentArgs -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $Dir ("agent_out_{0:D2}.txt" -f $S.Launch)) -RedirectStandardError (Join-Path $Dir ("agent_err_{0:D2}.txt" -f $S.Launch))
    if (-not $Full) { try { $S.Agent.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal } catch {} }
    $null = $S.Agent.Handle # 終わったときの戻り値（ExitCode）を読めるように、ハンドルを持っておく
    $S.GameLog = $GameLog
    $S.Started = Get-Date
    $S.Lowered = $false
    $S.TitlePid = $null
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

# パッケージの .exe は本体（Binaries の .exe）を別に起動するので、本体の優先度も「通常以下」にする（起動して 1 分の間だけ探す）
function Set-LowPriority([string]$P) {
    $S = $State[$P]
    if ($Full -or $S.Lowered -or ((Get-Date) - $S.Started).TotalSeconds -gt 60) { return }
    Get-CimInstance Win32_Process -Filter "Name LIKE 'KakurenboIncremental%'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -match "KakuAgentPort=$($BasePort + $S.Index)\b" } | ForEach-Object {
            try { (Get-Process -Id $_.ProcessId -ErrorAction Stop).PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal; if ($_.ExecutablePath -match "Binaries") { $S.Lowered = $true } } catch {}
        }
}

# どのウィンドウがどの性格か分かるように、ゲームとログのウィンドウの題名に性格を入れる（タスクバーにもその名前で出る）
Add-Type -Namespace KakuAgent -Name Title -MemberDefinition @'
delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
[DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
[DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hWnd, System.Text.StringBuilder s, int n);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder s, int n);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool SetWindowText(IntPtr hWnd, string s);
// pid のゲームの窓（UnrealWindow）とログの窓（ConsoleWindowClass）の題名を変える。ゲームの窓を見つけたら true
public static bool Apply(uint pid, string game, string log) {
    bool found = false;
    EnumWindows((h, l) => {
        uint p; GetWindowThreadProcessId(h, out p);
        if (p != pid) return true;
        var c = new System.Text.StringBuilder(64); GetClassName(h, c, 64);
        string want = c.ToString() == "UnrealWindow" ? game : c.ToString() == "ConsoleWindowClass" ? log : null;
        if (want == null) return true;
        if (want == game) found = true;
        var t = new System.Text.StringBuilder(256); GetWindowText(h, t, 256);
        if (t.ToString() != want) SetWindowText(h, want);
        return true;
    }, IntPtr.Zero);
    return found;
}
'@
$PersonaLabels = @{ stay = "動かない慎重型"; near = "近くのお宝だけ"; greedy = "強欲型"; builder = "建築大好き"; nowall = "壁なし" }
function Set-Titles([string]$P) {
    $S = $State[$P]
    if (-not $S.TitlePid) {
        # パッケージの .exe は本体（Binaries の .exe）を別に起動する。窓を持つのは本体
        $Proc = Get-CimInstance Win32_Process -Filter "Name LIKE 'KakurenboIncremental%'" -ErrorAction SilentlyContinue |
            Where-Object { $_.ExecutablePath -match "Binaries" -and $_.CommandLine -match "KakuAgentPort=$($BasePort + $S.Index)\b" } | Select-Object -First 1
        if (-not $Proc) { return }
        $S.TitlePid = [uint32]$Proc.ProcessId
    }
    $Label = if ($PersonaLabels.ContainsKey($P)) { "$P（$($PersonaLabels[$P])）" } else { $P }
    try { [KakuAgent.Title]::Apply($S.TitlePid, "かせぐんぼ - $Label", "ログ - $Label") | Out-Null } catch {}
}

function Get-LogErrors([string]$Path) {
    if (-not (Test-Path $Path)) { return "" }
    $Lines = Get-Content $Path -Tail 400 -ErrorAction SilentlyContinue | Where-Object { $_ -match "Fatal|Assertion failed|Unhandled Exception|Error:|EXCEPTION_" } | Select-Object -Last 6
    return ($Lines -join " / ")
}

# バランスを推論する AI（analyze.py）：記録を全部読み、新しいファイル analysis/balance_<日時>.md・.html を作る（1 時間ごとと 8 時間の終わり）
function Make-Analysis {
    try {
        $Out = & $Python (Join-Path $AgentDir "analyze.py") $OutRoot 2>&1
        Say "バランスの推論を作りました：$Out"
    } catch { Say "バランスの推論を作れませんでした：$_" "Yellow" }
}

function Make-Report {
    try {
        $Out = & $Python (Join-Path $AgentDir "make_report.py") $RunDir $ReportName $ReportLabel 2>&1
        Say "報告書を更新：$(Join-Path $RunDir "$ReportName.html")"
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
        # PC ごとのフォルダ（PC の名前）の同じファイルを上書きする（新しく作らない。前の版は Git の履歴に残る）：
        #   <PC の名前>/README.md（報告書。GitHub でフォルダを開くとそのまま出る）・report.html・<性格>.md
        $RunName = Split-Path $RunDir -Leaf
        # PC の名前は日本語でもそのまま（GitHub のフォルダ名に使えない記号と空白だけ _ にする。前は英数字以外を全部 _ にしていて、日本語の名前が _______ になった）
        $Pc = ($env:COMPUTERNAME -replace '[\\/:*?"<>|\s]', '_')
        git rm -r -q --cached --ignore-unmatch "$Pc" 2>$null | Out-Null
        # 続けて遊んだ 2 周目からは、その周の報告書（report_2周目 など）を送る（GitHub のファイルの名前は同じまま上書き）
        $Files = @(@{ Src = (Join-Path $RunDir "$ReportName.md"); Dst = "$Pc/README.md" }, @{ Src = (Join-Path $RunDir "$ReportName.html"); Dst = "$Pc/report.html" })
        foreach ($P in $PersonaList) { $Files += @{ Src = (Join-Path $RunDir "$P\$ReportName.md"); Dst = "$Pc/$P.md" } }
        # バランスの推論：最新は balance.md・balance.html、作ったものは全部 analysis/ に残す（新しく作るたびに足していく）
        $AnalysisDir = Join-Path $OutRoot "analysis"
        $Files += @{ Src = (Join-Path $AnalysisDir "latest.md"); Dst = "$Pc/balance.md" }
        $Files += @{ Src = (Join-Path $AnalysisDir "latest.html"); Dst = "$Pc/balance.html" }
        foreach ($F in @(Get-ChildItem (Join-Path $AnalysisDir "balance_*.md") -ErrorAction SilentlyContinue)) { $Files += @{ Src = $F.FullName; Dst = "$Pc/analysis/$($F.Name)" } }
        foreach ($F in $Files) {
            if (Test-Path $F.Src) {
                $Hash = (git hash-object -w $F.Src 2>$null).Trim()
                git update-index --add --cacheinfo "100644,$Hash,$($F.Dst)"
            }
        }
        $Tree = (git write-tree).Trim()
        $Message = "AI playtest report $Pc $RunName $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
        $Commit = if ($Parent) { (git commit-tree $Tree -p $Parent.Trim() -m $Message).Trim() } else { (git commit-tree $Tree -m $Message).Trim() }
        git push -q origin "${Commit}:refs/heads/$Branch" 2>$null
        if ($LASTEXITCODE -eq 0) { Say "報告書を GitHub のブランチ $Branch に送りました（$Pc/README.md を更新）" } else { Say "報告書を GitHub に送れませんでした（ネットワーク・ログイン・ほかの PC と同時に送った、など。次の回にもう一度送る）" "Yellow" }
    } catch { Say "報告書を GitHub に送れませんでした：$_" "Yellow" }
    finally {
        if ($OldIndex) { $env:GIT_INDEX_FILE = $OldIndex } else { Remove-Item Env:GIT_INDEX_FILE -ErrorAction SilentlyContinue }
        Remove-Item $Index -Force -ErrorAction SilentlyContinue
        Pop-Location
    }
}

# ---------------------------------------------------------------- 遊んでいる間はスリープしない
# Windows の電源の設定は変えない。このスクリプトが動いている間だけ「使用中」と伝え、終わったら（Ctrl+C・ウィンドウを閉じた後も）元に戻る。
# ふたを閉じる・手動でスリープにしたときは止まる
Add-Type -Namespace KakuAgent -Name Power -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint esFlags);'
$EsContinuous = [uint32]2147483648 # ES_CONTINUOUS
$EsSystem = [uint32]1              # ES_SYSTEM_REQUIRED
[KakuAgent.Power]::SetThreadExecutionState($EsContinuous -bor $EsSystem) | Out-Null
Say "遊んでいる間はスリープしないようにしました（終わったら元に戻ります）"

# ---------------------------------------------------------------- 最後にキーボード・マウスにさわってからの時間（続けて遊ぶか決める）
Add-Type -Namespace KakuAgent -Name Idle -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
[DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LASTINPUTINFO plii);
public static uint IdleMs() { LASTINPUTINFO i = new LASTINPUTINFO(); i.cbSize = (uint)Marshal.SizeOf(i); GetLastInputInfo(ref i); return unchecked((uint)Environment.TickCount - i.dwTime); }
'@

# 8 時間が終わった後、$IdleMinutes 分のあいだキーボード・マウスにさわらなければ $true（続けて遊ぶ）。さわったら $false（終わる）
function Wait-Idle {
    $End = Get-Date
    Say "$IdleMinutes 分のあいだキーボード・マウスにさわらなければ、同じデータのまま続けてもう $([math]::Round($BlockSpan.TotalHours, 2)) 時間遊びます（さわると終わります）" "Cyan"
    while (((Get-Date) - $End).TotalMinutes -lt $IdleMinutes) {
        Start-Sleep -Seconds 2
        $Since = ((Get-Date) - $End).TotalMilliseconds
        if ([KakuAgent.Idle]::IdleMs() -lt $Since - 1500) { return $false } # 終わった後にさわった
    }
    return $true
}

# ---------------------------------------------------------------- 見張る（落ちた・固まったら起動し直す）。1 周（8 時間）ぶん
function Run-Block {
    $i = 0
    foreach ($P in $PersonaList) {
        if (-not $State.ContainsKey($P)) { $State[$P] = @{ Launch = 0; Index = $i } }
        $State[$P].Done = $false
        Start-Pair $P $i
        $i++
        Start-Sleep -Seconds 4
    }
    $NextReport = (Get-Date).AddMinutes(15)
    $NextAnalysis = (Get-Date).AddMinutes(60)
    while ($true) {
        Start-Sleep -Seconds 5
        foreach ($P in $PersonaList) { Set-LowPriority $P; if (-not $State[$P].Done) { Set-Titles $P } }
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
        if ((Get-Date) -ge $NextAnalysis) {
            Make-Analysis
            $NextAnalysis = (Get-Date).AddMinutes(60)
        }
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

# 1 周目の報告書は report.html、続けて遊んだ 2 周目からは report_2周目.html（同じフォルダに足す。中身はフォルダの最初からの記録）
$Block = 1
$ReportName = "report"
$ReportLabel = ""
$BlockSpan = $Deadline - (Get-Date)
$Reported = $false
try {
    while ($true) {
        $Reported = $false
        Run-Block
        foreach ($P in $PersonaList) { Stop-Pair $P }
        Make-Analysis
        Make-Report
        $Reported = $true
        Say "$Block 周目が終わりました。報告書：$(Join-Path $RunDir "$ReportName.html")" "Cyan"
        if ($NoRepeat -or -not (Wait-Idle)) { break }
        $Block++
        $ReportName = "report_${Block}周目"
        $ReportLabel = "（続けて遊んだ $Block 周目：$(Get-Date -Format 'MM/dd HH:mm') から）"
        $Deadline = (Get-Date) + $BlockSpan
        $DeadlineUnix = [DateTimeOffset]::new($Deadline).ToUnixTimeSeconds()
        Say "さわらなかったので、同じデータのまま $Block 周目を始めます（$($Deadline.ToString('MM/dd HH:mm')) まで・報告書は $ReportName.html）" "Cyan"
    }
}
finally {
    foreach ($P in $PersonaList) { Stop-Pair $P }
    if (-not $Reported) { Make-Report }
    [KakuAgent.Power]::SetThreadExecutionState($EsContinuous) | Out-Null # スリープしてよい状態に戻す
    Say "おわり。報告書：$(Join-Path $RunDir "$ReportName.html")（Markdown は $ReportName.md）" "Cyan"
    if ($Open) { Start-Process (Join-Path $RunDir "$ReportName.html") }
}
