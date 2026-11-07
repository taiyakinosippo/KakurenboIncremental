# GitHub 側で履歴を書き換えたとき（間違えて入れた大きなファイルを履歴から消した など）に、このパソコンの main を GitHub に合わせる。
# AutoBuild.ps1 が pull の前（と git pull 後のフック）で呼ぶので、ふだんは自分で動かさなくてよい。
#
#   powershell -ExecutionPolicy Bypass -File Tools\RepairHistory.ps1
#
# 何をするか：
#   このパソコンにだけあるコミットが、全部「GitHub にある書き換え後のコミットと同じもの」（作者・日時・題名が同じ）なら、
#   それは書き換え前の古いコミットなので、main を GitHub の main に合わせる（git reset --hard）。
#   古いコミット（と消したファイル）はもう要らないので、場所も空ける（git gc・git lfs prune）。
#   このパソコンで作ったまだ push していないコミットがある・コミットしていない変更があるときは、何もしないで知らせる（消さない）。
#
# 戻り値（終了コード）：0 何もしなかった / 1 直した / 2 直せなかった（知らせただけ）
param(
    [string]$Root = (Split-Path $PSScriptRoot -Parent),
    [switch]$NoFetch,
    [switch]$NoCleanup
)

function Say([string]$Message, [string]$Color = "Gray") {
    Write-Host ("[RepairHistory] " + $Message) -ForegroundColor $Color
}

function Invoke-Git([string[]]$GitArgs) {
    $Out = & git -C $Root @GitArgs 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    return $Out
}

$Upstream = Invoke-Git @("rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}")
if (-not $Upstream) { exit 0 } # main が GitHub とつながっていない
$Upstream = ($Upstream | Out-String).Trim()

if (-not $NoFetch) { & git -C $Root fetch --quiet 2>$null | Out-Null }

# git pull が古いコミットと書き換え後のコミットをマージしようとして、ぶつかって止まっている（CONFLICT）なら、そのマージはやめる。
# （マージをやめると pull の前の状態に戻る。下で古いコミットだけと分かれば GitHub に合わせる）
$GitDir = (Invoke-Git @("rev-parse", "--absolute-git-dir") | Out-String).Trim()
$MergeHeadFile = Join-Path $GitDir "MERGE_HEAD"
if ($GitDir -and (Test-Path $MergeHeadFile)) {
    $MergeHead = (Get-Content $MergeHeadFile -TotalCount 1).Trim()
    $UpstreamHash = (Invoke-Git @("rev-parse", $Upstream) | Out-String).Trim()
    $OldOnly = @(Invoke-Git @("log", "--no-merges", "--format=%at%x09%ae%x09%s", "$Upstream..HEAD") | Where-Object { $_ })
    $UpKeys = @{}
    Invoke-Git @("log", "--no-merges", "--format=%at%x09%ae%x09%s", "-n", "500", $Upstream) | Where-Object { $_ } | ForEach-Object { $UpKeys[$_] = $true }
    $AllRewritten = $OldOnly.Count -gt 0 -and @($OldOnly | Where-Object { -not $UpKeys.ContainsKey($_) }).Count -eq 0
    # マージしようとしたのが GitHub のコミット（その後さらに push されていれば、その前のもの・同じ作者・日時・題名のもの）か
    & git -C $Root merge-base --is-ancestor $MergeHead $UpstreamHash 2>$null
    $FromUpstream = ($LASTEXITCODE -eq 0)
    if (-not $FromUpstream) {
        $MergeKey = (Invoke-Git @("log", "-1", "--format=%at%x09%ae%x09%s", $MergeHead) | Out-String).Trim()
        $FromUpstream = $MergeKey -and $UpKeys.ContainsKey($MergeKey)
    }
    if ($FromUpstream -and $AllRewritten) {
        Say "書き換え前の古いコミットと GitHub のコミットのマージが途中で止まっているので、やめます" "Cyan"
        & git -C $Root merge --abort 2>$null | Out-Null
    }
}

# このパソコンにだけあるコミット（マージのコミットは除く）
$LocalOnly = @(Invoke-Git @("log", "--no-merges", "--format=%H%x09%at%x09%ae%x09%s", "$Upstream..HEAD") | Where-Object { $_ })
if ($LocalOnly.Count -eq 0) { exit 0 }

# GitHub の main にあるコミットの「作者の日時・メール・題名」
$Known = @{}
Invoke-Git @("log", "--no-merges", "--format=%at%x09%ae%x09%s", "-n", "500", $Upstream) | Where-Object { $_ } | ForEach-Object { $Known[$_] = $true }

$Rewritten = @()
$Own = @()
foreach ($Line in $LocalOnly) {
    $Parts = $Line -split "`t", 2
    if ($Known.ContainsKey($Parts[1])) { $Rewritten += $Line } else { $Own += $Line }
}
if ($Rewritten.Count -eq 0) { exit 0 } # ふつうの「まだ push していないコミット」だけ

if ($Own.Count -gt 0) {
    Say "GitHub の履歴が書き換えられていますが、このパソコンにだけあるコミットもあるので、自動では直しません：" "Yellow"
    $Own | ForEach-Object { Say ("  " + ($_ -split "`t")[3]) "Yellow" }
    Say "Claude に「GitHub の書き換え後の main に、このコミットを載せ直して」と頼んでください" "Yellow"
    exit 2
}

$Dirty = @(Invoke-Git @("status", "--porcelain", "--untracked-files=no") | Where-Object { $_ })
if ($Dirty.Count -gt 0) {
    Say "GitHub の履歴が書き換えられていますが、コミットしていない変更があるので、自動では直しません（変更をコミットか元に戻してから、もう一度）" "Yellow"
    exit 2
}

Say "GitHub の履歴が書き換えられていたので、このパソコンの main を GitHub に合わせます（古いコミット $($Rewritten.Count) 個は GitHub に同じものがあります）" "Cyan"
& git -C $Root reset --quiet --hard $Upstream
if ($LASTEXITCODE -ne 0) {
    Say "git reset に失敗しました" "Red"
    exit 2
}
Say ("main を " + (Invoke-Git @("log", "--oneline", "-1")) + " に合わせました") "Cyan"

if (-not $NoCleanup) {
    # 書き換え前の古いコミットと、それにしか無かったファイルを消して場所を空ける
    Say "古いコミットの分の場所を空けます（少し時間がかかります）"
    & git -C $Root reflog expire --expire-unreachable=now --all 2>$null | Out-Null
    & git -C $Root gc --prune=now --quiet 2>$null | Out-Null
    & git -C $Root lfs prune 2>$null | Out-Null
}
exit 1
