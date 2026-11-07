# 採用した効果音の候補を、ゲームが読む KakurenboIncremental/Data/Sounds/<音>.wav にする。
#   Tools\SoundGen\.venv\Scripts\python.exe -I Tools\SoundGen\ExportForGame.py Buy=Buy_s13 OniStep=OniStep_s13 ...
#   Tools\SoundGen\.venv\Scripts\python.exe -I Tools\SoundGen\ExportForGame.py --adopted   # docs/Sounds.md の表で採用した効果音を全部
#   Tools\SoundGen\.venv\Scripts\python.exe -I Tools\SoundGen\ExportForGame.py --check     # 書き出した音とプログラムの音の聞こえる大きさの差を見るだけ
# 引数は <音の名前（EKakurenboSfx）>=<SourceArt/SoundGen の候補の名前>。
#   ・ゲームの効果音と同じ形（モノラル・44100Hz・16bit）にする。左右の振り分け・こもりはゲームが付ける
#   ・大きさは今のプログラムの音（_Current/<音>.wav）と「耳に聞こえる大きさ」をそろえる（音どうしの大きさの釣り合いを変えない）。
#     聞こえる大きさ＝人の耳の感じ方に合わせた重み（K 特性。ITU-R BS.1770：低い音は小さく、高い音は少し大きく数える）を付けて、
#     0.1 秒ごとの大きさのいちばん大きい所（短い音も長い音も同じ物差し）。2026-10-19 まで使っていた RMS（波の平均）だと、
#     低い音の多い AI の音は数字が同じでも小さく聞こえ、最大 ±7dB ずれていた（ユーザー「音はモノによって聞こえにくい」）
#   ・そろえると割れる音は、とがった所だけ柔らかくつぶす（soft_limit）。それでも足りない分だけ小さめになる
#   ・頭とお尻の無音は切る（trim）
import sys
from pathlib import Path

import re

import numpy as np
import soundfile as sf
from scipy.signal import lfilter

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


def k_weight(mono, rate=RATE):
    """人の耳の感じ方の重み（ITU-R BS.1770 の K 特性：1.5kHz から上を +4dB・38Hz より下を切る）"""
    def biquad_shelf(gain_db, q, fc):
        a = 10 ** (gain_db / 40)
        w = 2 * np.pi * fc / rate
        alpha, c, sa = np.sin(w) / (2 * q), np.cos(w), np.sqrt(a)
        b = [a * ((a + 1) + (a - 1) * c + 2 * sa * alpha), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - 2 * sa * alpha)]
        d = [(a + 1) - (a - 1) * c + 2 * sa * alpha, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - 2 * sa * alpha]
        return b, d

    def biquad_highpass(q, fc):
        w = 2 * np.pi * fc / rate
        alpha, c = np.sin(w) / (2 * q), np.cos(w)
        return [(1 + c) / 2, -(1 + c), (1 + c) / 2], [1 + alpha, -2 * c, 1 - alpha]

    x = lfilter(*biquad_shelf(4.0, 1 / np.sqrt(2), 1500.0), mono.astype(np.float64))
    return lfilter(*biquad_highpass(0.5, 38.0), x)


def loudness(mono, rate=RATE):
    """聞こえる大きさ（dB）：K 特性を付けて、0.1 秒ずつ（0.01 秒ずらし）の平均の 2 乗のいちばん大きい所"""
    x = k_weight(mono, rate) ** 2
    n, hop = int(0.1 * rate), int(0.01 * rate)
    total = np.concatenate([[0.0], np.cumsum(np.concatenate([x, np.zeros(n)]))])
    windows = (total[n::hop] - total[: len(total) - n : hop][: len(total[n::hop])]) / n
    return -0.691 + 10 * np.log10(float(windows.max()) + 1e-12)


def soft_limit(mono, knee=0.7, ceiling=0.98):
    """knee を超えたとがった所だけ、ceiling に向かって柔らかくつぶす（そろえた大きさで割れないように）"""
    over = np.abs(mono) > knee
    span = ceiling - knee
    out = mono.copy()
    out[over] = np.sign(mono[over]) * (knee + span * np.tanh((np.abs(mono[over]) - knee) / span))
    return out


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
    target = loudness(load_mono(current)) if current.exists() else -16.0
    # 聞こえる大きさをそろえる。とがった所をつぶすと少し小さくなるので、2 回そろえ直す
    for _ in range(3):
        mono = soft_limit(mono * 10 ** ((target - loudness(mono)) / 20)).astype(np.float32)
    fade = min(int(0.01 * RATE), len(mono))
    mono[-fade:] *= np.linspace(1.0, 0.0, fade, dtype=np.float32)
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / f"{name}.wav"
    sf.write(str(path), mono, RATE, subtype="PCM_16")
    print(f"[ExportForGame] {path.relative_to(ROOT)} <- {pick}（{len(mono) / RATE:.2f} 秒・聞こえる大きさ {loudness(mono):.1f}dB"
          f"（プログラムの音 {target:.1f}dB）・ピーク {float(np.abs(mono).max()):.2f}）")


def adopted():
    """docs/Sounds.md の表で採用した効果音：{音の名前: 候補の名前}（BGM・今のまま・— は除く）"""
    picks = {}
    for line in (ROOT / "docs" / "Sounds.md").read_text(encoding="utf-8").splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        m = re.search(r"\*\*([A-Za-z]\w*_s\d+)\*\*", cells[3]) if len(cells) >= 4 else None
        if m and not cells[0].startswith("Music_"):
            picks[cells[0]] = m.group(1)
    return picks


def check():
    """書き出した音（Data/Sounds）とプログラムの音の聞こえる大きさの差。±1.5dB を超えたら印"""
    for path in sorted(OUT.glob("*.wav")):
        current = SRC / "_Current" / path.name
        if path.stem.startswith("Music_") or not current.exists():
            continue
        diff = loudness(load_mono(path)) - loudness(load_mono(current))
        print(f"{path.stem:16s} {diff:+5.1f}dB{'  ← ずれている' if abs(diff) > 1.5 else ''}")


def main():
    if "--check" in sys.argv:
        check()
        return 0
    if "--adopted" in sys.argv:
        for name, pick in adopted().items():
            export(name, pick)
        return 0
    for arg in sys.argv[1:]:
        name, pick = arg.split("=", 1)
        export(name, pick)
    return 0


if __name__ == "__main__":
    sys.exit(main())
