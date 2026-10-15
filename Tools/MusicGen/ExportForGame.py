# 採用した BGM の候補を、ゲームが読む KakurenboIncremental/Data/Sounds/<曲>.wav にする（くり返してもつながるように）。
#   Tools\MusicGen\ACE-Step-1.5\.venv\Scripts\python.exe -I Tools\MusicGen\ExportForGame.py Music_Hide=Music_Hide_s2 Music_Calm=Music_Calm_s2 ...
# 引数は <ゲームの曲の名前>=<SourceArt/MusicGen の候補の名前>。
#   ・AI の曲は終わり方（フェードアウト・最後の音）があるので、終わりの数秒を捨てて、拍子（Music.csv の BPM の 1 小節）の切れ目で切る
#   ・切った先の少し（CrossfadeBars 小節）を曲の頭に重ねる（クロスフェード）ので、最後から最初へつなぎ目なく戻る
#   ・音の大きさを今のプログラムの BGM（RMS 0.085 くらい）にそろえる（効果音・足音を聞き取る邪魔をしないように）
import csv
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRC = ROOT / "KakurenboIncremental" / "SourceArt" / "MusicGen"
OUT = ROOT / "KakurenboIncremental" / "Data" / "Sounds"
TARGET_RMS = 0.085      # 今のプログラムの BGM と同じくらい
TAIL_SECONDS = 6.0      # 鳴り終わる前のこれだけは使わない（曲の終わり方・フェードアウト）
CROSSFADE_BARS = 1      # 頭に重ねる長さ（小節）


def export(game_name, pick, bpm):
    audio, rate = sf.read(str(SRC / game_name / f"{pick}.wav"), dtype="float32", always_2d=True)
    bar = int(round(4 * 60.0 / bpm * rate))
    fade = bar * CROSSFADE_BARS
    # 曲が鳴り終わる場所（0.1 秒ごとの大きさが、全体の真ん中の大きさの 1 割を最後に超えたところ）。AI の曲は最後の数秒が無音のことがある
    hop = rate // 10
    level = np.sqrt(np.add.reduceat((audio ** 2).mean(axis=1), np.arange(0, len(audio), hop)) / hop)
    loud = np.where(level > np.median(level) * 0.1)[0]
    music_end = min(len(audio), (int(loud[-1]) + 1) * hop) if len(loud) else len(audio)
    usable = music_end - int(TAIL_SECONDS * rate) - fade
    loop_len = (usable // bar) * bar
    if loop_len < bar * 4:
        raise SystemExit(f"{pick}: 短すぎてくり返しを作れません")
    loop = audio[:loop_len].copy()
    # 切った後ろの続き（loop_len〜+fade）を、頭（0〜fade）に重ねる。音の大きさが変わらない（等パワー）の重ね方
    t = np.linspace(0.0, 1.0, fade, dtype=np.float32)[:, None]
    loop[:fade] = audio[:fade] * np.sin(t * np.pi / 2) + audio[loop_len:loop_len + fade] * np.cos(t * np.pi / 2)
    rms = float(np.sqrt((loop ** 2).mean()))
    loop *= TARGET_RMS / max(rms, 1e-6)
    peak = float(np.abs(loop).max())
    if peak > 0.98:
        loop *= 0.98 / peak  # 割れないように
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / f"{game_name}.wav"
    sf.write(str(path), loop, rate, subtype="PCM_16")
    print(f"[ExportForGame] {path.relative_to(ROOT)} <- {pick}（{loop_len / rate:.1f} 秒・{loop_len // bar} 小節・{bpm} BPM）")


def main():
    with open(HERE / "Music.csv", encoding="utf-8") as f:
        bpms = {row["Name"]: int(row["BPM"]) for row in csv.DictReader(f) if row.get("Name")}
    for arg in sys.argv[1:]:
        game_name, pick = arg.split("=", 1)
        export(game_name, pick, bpms[game_name])
    return 0


if __name__ == "__main__":
    sys.exit(main())
