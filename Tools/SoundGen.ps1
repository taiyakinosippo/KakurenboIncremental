# 音を作る AI（Stable Audio Open 1.0）で効果音・BGM の候補を作る。初めてのパソコンでは環境も作る（何も聞かずに進む）。
#   powershell -ExecutionPolicy Bypass -File Tools\SoundGen.ps1 -Setup         # 環境だけ作る（Tools/SoundGen/.venv。PyTorch など約 3GB）
#   powershell -ExecutionPolicy Bypass -File Tools\SoundGen.ps1 -Current       # 今の音（プログラムで作った音）を WAV に書き出す（ビルド済みのこと）
#   powershell -ExecutionPolicy Bypass -File Tools\SoundGen.ps1                # Tools/SoundGen/Sounds.csv の全部を作る（1 つにつき 3 候補。約 1 時間）
#   powershell -ExecutionPolicy Bypass -File Tools\SoundGen.ps1 Mash OniStep   # 名前を選んで作る（ほかの引数は Tools/SoundGen/GenerateSounds.py を参照）
# 候補は KakurenboIncremental/SourceArt/SoundGen/<名前>/（Git には入れない）。一覧は同じフォルダの index.html（今の音と聞き比べられる）。
# モデルを使うには、パソコンごとに 1 回だけユーザーが Hugging Face にログインする（規約への同意はアカウントに 1 回）:
#   Tools\SoundGen\.venv\Scripts\hf.exe auth login
param([switch]$Setup, [switch]$Current, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
$Dir = Join-Path $PSScriptRoot "SoundGen"
$Venv = Join-Path $Dir ".venv"
$Python = Join-Path $Venv "Scripts\python.exe"

function Test-Ready {
    if (-not (Test-Path $Python)) { return $false }
    & $Python -c "import torch, diffusers, soundfile, torchsde" 2>$null
    return $LASTEXITCODE -eq 0
}

if (-not (Test-Ready)) {
    Write-Host "[SoundGen] Python の環境を作ります（初回だけ。約 3GB をダウンロード）"
    if (-not (Test-Path $Python)) {
        # Python 3.12（py ランチャー）を優先。無ければ PATH の python
        $UsePy = $false
        if (Get-Command py -ErrorAction SilentlyContinue) {
            & py -3.12 -c "import sys" 2>$null
            $UsePy = $LASTEXITCODE -eq 0
        }
        if ($UsePy) { & py -3.12 -m venv $Venv } else { & python -m venv $Venv }
        if ($LASTEXITCODE -ne 0) { throw "venv を作れませんでした（Python 3.10〜3.12 を入れてください）" }
    }
    & $Python -m pip install --upgrade pip -q
    # GPU（CUDA）版の PyTorch。NVIDIA の GPU が無いパソコンでも動く（CPU でとても遅い）
    & $Python -m pip install torch==2.7.1 torchaudio==2.7.1 --index-url https://download.pytorch.org/whl/cu126 -q
    & $Python -m pip install -r (Join-Path $Dir "requirements.txt") -q
    if (-not (Test-Ready)) { throw "環境を作れませんでした（上のエラーを見てください）" }
    Write-Host "[SoundGen] 環境ができました: $Venv"
}

if ($Current) {
    # 単体テストの仕組みで、プログラムで作っている今の効果音・BGM を SourceArt/SoundGen/_Current に WAV で書き出す
    & (Join-Path $PSScriptRoot "RunUnitTests.ps1") -Filter "KakuTools.ExportSynthSounds"
    & $Python (Join-Path $Dir "GenerateSounds.py") --index-only
    exit $LASTEXITCODE
}
if ($Setup) {
    & $Python (Join-Path $Dir "GenerateSounds.py") --check
    exit 0
}

& $Python (Join-Path $Dir "GenerateSounds.py") @Rest
exit $LASTEXITCODE
