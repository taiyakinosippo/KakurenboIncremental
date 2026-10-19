# 採用した効果音の候補を、ゲームが読む KakurenboIncremental/Data/Sounds/<音>.wav にする。
#   Tools\SoundGen\.venv\Scripts\python.exe -I Tools\SoundGen\ExportForGame.py Buy=Buy_s13 OniStep=OniStep_s13 ...
# 引数は <音の名前（EKakurenboSfx）>=<SourceArt/SoundGen の候補の名前>。
#   ・ゲームの効果音と同じ形（モノラル・44100Hz・16bit）にする。左右の振り分け・こもりはゲームが付ける
#   ・大きさは今のプログラムの音（_Current/<音>.wav）の鳴っている部分の RMS にそろえる（音どうしの大きさの釣り合いを変えない）
#   ・頭とお尻の無音は切る（trim）
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRC = ROOT / "KakurenboIncremental" / "SourceArt" / "SoundGen"
OUT = ROOT / "KakurenboIncremental" / "Data" / "Sounds"
RATE = 44100  # KakurenboSynth::DefaultSampleRate


def load_mono(path):
    audio, rate = sf.read(str(path), dtype="float32", always_2d=True)
    mono = audio.mean(axis=1)
    if rate != RATE:
        # 線形補間で周波数を変える（効果音なのでこれで十分）
        n = int(round(len(mono) * RATE / rate))
        mono = np.interp(np.linspace(0, len(mono) - 1, n), np.arange(len(mono)), mono).astype(np.float32)
    return mono


def active_rms(mono):
    """鳴っている部分（いちばん大きいところから -40dB 以内）の RMS"""
    peak = float(np.abs(mono).max())
    loud = mono[np.abs(mono) > peak * 0.01]
    return float(np.sqrt((loud ** 2).mean())) if len(loud) else 0.0


def trim(mono):
    """頭とお尻の無音を切る（AI の音は鳴り始めが遅れることがあり、押してから鳴るまでがもたつく）。
    頭は最大の -34dB を超える 5ms 前から、お尻は -50dB を超える最後の所の 20ms 後まで"""
    peak = float(np.abs(mono).max())
    if peak <= 0:
        return mono
    loud = np.nonzero(np.abs(mono) > peak * 0.02)[0]
    audible = np.nonzero(np.abs(mono) > peak * 0.003)[0]
    start = max(int(loud[0]) - int(0.005 * RATE), 0)
    end = min(int(audible[-1]) + int(0.02 * RATE), len(mono))
    return mono[start:end].copy()


def export(name, pick):
    mono = trim(load_mono(SRC / name / f"{pick}.wav"))
    current = SRC / "_Current" / f"{name}.wav"
    target = active_rms(load_mono(current)) if current.exists() else 0.25
    mono *= target / max(active_rms(mono), 1e-6)
    peak = float(np.abs(mono).max())
    if peak > 0.98:
        mono *= 0.98 / peak  # 割れないように（そろえきれなかった分は小さめになる）
    fade = min(int(0.01 * RATE), len(mono))
    mono[-fade:] *= np.linspace(1.0, 0.0, fade, dtype=np.float32)
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / f"{name}.wav"
    sf.write(str(path), mono, RATE, subtype="PCM_16")
    print(f"[ExportForGame] {path.relative_to(ROOT)} <- {pick}（{len(mono) / RATE:.2f} 秒・RMS {target:.3f}・ピーク {float(np.abs(mono).max()):.2f}）")


def main():
    for arg in sys.argv[1:]:
        name, pick = arg.split("=", 1)
        export(name, pick)
    return 0


if __name__ == "__main__":
    sys.exit(main())
