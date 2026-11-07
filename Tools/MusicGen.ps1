# BGM を作る AI（ACE-Step 1.5。MIT・作った曲は商用で使える）で BGM の候補を作る。初めてのパソコンでは環境も作る（何も聞かずに進む）。
#   powershell -ExecutionPolicy Bypass -File Tools\MusicGen.ps1 -Setup        # 環境とモデルだけ（Tools/MusicGen/ACE-Step-1.5。約 7GB ＋ モデル約 10GB）
#   powershell -ExecutionPolicy Bypass -File Tools\MusicGen.ps1               # Tools/MusicGen/Music.csv の全部を作る（1 曲につき 3 候補）
#   powershell -ExecutionPolicy Bypass -File Tools\MusicGen.ps1 Music_Hide    # 名前を選んで作る（ほかの引数は Tools/MusicGen/GenerateMusic.py を参照）
# 候補は KakurenboIncremental/SourceArt/MusicGen/<名前>/。一覧は同じフォルダの index.html。
# 必要なもの: git と uv（https://docs.astral.sh/uv/ 。無ければ入れる）。Python は uv が用意する。Hugging Face のログインは要らない。
param([switch]$Setup, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
# git・uv・Python は進み具合やログを stderr に出す。Windows PowerShell 5.1 は Stop だとそれをエラーで止めてしまうので、終了コードで調べる
$ErrorActionPreference = "Continue"
$Dir = Join-Path $PSScriptRoot "MusicGen"
$Ace = Join-Path $Dir "ACE-Step-1.5"
$Python = Join-Path $Ace ".venv\Scripts\python.exe"
# 動作を確かめた ACE-Step のコミット（新しくするときはここを変えて -Setup）
$AceCommit = "ca1e85fe9430179831e6bc6be790c332190a3866"

function Test-Ready {
    if (-not (Test-Path $Python)) { return $false }
    & $Python -c "import torch, acestep.inference" 2>$null
    return $LASTEXITCODE -eq 0
}

if ($Setup -or -not (Test-Ready)) {
    if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
        Write-Host "[MusicGen] uv を入れます"
        powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"
        $env:Path = "$env:USERPROFILE\.local\bin;$env:Path"
    }
    if (-not (Test-Path (Join-Path $Ace ".git"))) {
        Write-Host "[MusicGen] ACE-Step 1.5 を取ってきます"
        git clone https://github.com/ACE-Step/ACE-Step-1.5.git $Ace
        if ($LASTEXITCODE -ne 0) { throw "ACE-Step を取ってこられませんでした" }
    }
    git -C $Ace fetch --depth 1 origin $AceCommit 2>$null
    git -C $Ace checkout -q $AceCommit
    if ($LASTEXITCODE -ne 0) { throw "ACE-Step のコミット $AceCommit に切り替えられませんでした" }
    Write-Host "[MusicGen] Python の環境を作ります（初回だけ。PyTorch など約 7GB）"
    Push-Location $Ace
    try { uv sync --python 3.12 } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw "uv sync に失敗しました（上のエラーを見てください）" }
    if (-not (Test-Ready)) { throw "環境を作れませんでした（上のエラーを見てください）" }
    Write-Host "[MusicGen] 環境ができました: $Ace\.venv"
    if ($Setup) {
        & $Python (Join-Path $Dir "GenerateMusic.py") --download
        exit $LASTEXITCODE
    }
}

& $Python (Join-Path $Dir "GenerateMusic.py") @Rest
exit $LASTEXITCODE
