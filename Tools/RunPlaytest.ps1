# テストプレイの自動プレイヤー（KakuPlaytest）を、決めた時刻まで動かし続ける。
#   記録: KakurenboIncremental/Saved/Playtest/（rounds.csv・events.log・shots/*.png）
#   セーブは専用のスロット（Playtest）なので、ユーザーのセーブ・設定は読み書きしない
#   ゲームが落ちたら起動し直し、タイトルの「つづきから」で続きを遊ぶ。-Fresh で記録とセーブを消して始める
param([string]$Until = "08:00", [int]$Shots = 220, [string]$Persona = "Beginner", [int]$LastGain = 0, [switch]$Fresh, [switch]$Sound)

. (Join-Path $PSScriptRoot "EnginePath.ps1") # $Engine に UE 5.8 のインストール先が入る
$Project = Join-Path $PSScriptRoot "..\KakurenboIncremental\KakurenboIncremental.uproject" | Resolve-Path
$Saved = Join-Path (Split-Path $Project) "Saved"
$Dir = Join-Path $Saved "Playtest"

if ($Fresh) {
    Remove-Item $Dir -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $Saved "SaveGames\Playtest.sav") -Force -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force (Join-Path $Dir "shots") | Out-Null

$H, $M = $Until.Split(":")
$Deadline = (Get-Date).Date.AddHours([int]$H).AddMinutes([int]$M)
if ($Deadline -lt (Get-Date)) { $Deadline = $Deadline.AddDays(1) }

$Launch = 0
while ((Get-Date) -lt $Deadline.AddMinutes(-1)) {
    $Launch++
    Add-Content (Join-Path $Dir "events.log") "$(Get-Date -Format HH:mm:ss) === launch $Launch ==="
    $GameArgs = @("`"$Project`"", "-game", "-windowed", "-ResX=1280", "-ResY=720", "-KakuNoSave", "-KakuSaveSlot=Playtest",
        "-ExecCmds=`"KakuPlaytest Until=$Until Shots=$Shots Persona=$Persona LastGain=$LastGain`"", "-log", "-abslog=`"$Dir\game_$(Get-Date -Format HHmmss).log`"")
    if (-not $Sound) { $GameArgs += "-NoSound" }
    $Proc = Start-Process -FilePath "$Engine\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList $GameArgs -PassThru
    # 止まった（events.log が 6 分書かれない。起動時の ZenServer 待ちで固まることがある）ら終わらせて起動し直す
    $Events = Join-Path $Dir "events.log"
    while (-not $Proc.WaitForExit(30000)) {
        if (((Get-Date) - (Get-Item $Events).LastWriteTime).TotalMinutes -gt 6) {
            Add-Content $Events "$(Get-Date -Format HH:mm:ss) === no progress for 6 minutes: restarting ==="
            $Proc.Kill()
            Get-Process zenserver -ErrorAction SilentlyContinue | Stop-Process -Force
            break
        }
    }
    Add-Content (Join-Path $Dir "events.log") "$(Get-Date -Format HH:mm:ss) === game exited (code $($Proc.ExitCode)) ==="
    Start-Sleep -Seconds 5
}
