# UE 5.8 のインストール先を探す（パソコンによって場所が違ってもよいように）。他のスクリプトから . で読み込んで使う。
#   1. 環境変数 UE_ROOT（例: D:\Epic Games\UE_5.8）
#   2. Epic Games Launcher の記録（C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat）
#   3. 既定の場所（C:\Program Files\Epic Games\UE_5.8）
function Get-KakurenboEnginePath {
    $Candidates = @()
    if ($env:UE_ROOT) { $Candidates += $env:UE_ROOT }
    $Dat = Join-Path $env:ProgramData "Epic\UnrealEngineLauncher\LauncherInstalled.dat"
    if (Test-Path $Dat) {
        try {
            $List = (Get-Content $Dat -Raw | ConvertFrom-Json).InstallationList
            $Candidates += @($List | Where-Object { $_.AppName -eq "UE_5.8" } | ForEach-Object { $_.InstallLocation })
        } catch {}
    }
    $Candidates += "C:\Program Files\Epic Games\UE_5.8"
    foreach ($Path in $Candidates) {
        # 存在しないドライブ（D: が無いなど）を指していてもエラーにしない
        if ($Path -and [System.IO.File]::Exists([System.IO.Path]::Combine($Path, "Engine\Binaries\Win64\UnrealEditor.exe"))) { return $Path }
    }
    throw "UE 5.8 が見つかりません。Epic Games Launcher でインストールするか、環境変数 UE_ROOT にインストール先（UE_5.8 フォルダ）を設定してください"
}
$Engine = Get-KakurenboEnginePath
