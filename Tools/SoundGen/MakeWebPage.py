# 作った音の候補と今の音を、どこでも聞ける 1 枚のページにする（claude.ai の Artifact として公開する用）。
#   Tools\SoundGen\.venv\Scripts\python.exe Tools\SoundGen\MakeWebPage.py
# KakurenboIncremental/SourceArt/SoundGen/_web/ に index.html（WebPage.html に音の一覧を差し込んだもの）と audio/*.mp3 を作る。
# WAV のままだと大きい（60MB）ので MP3 にする。公開は Claude が Artifact ツールで _web/index.html と audio/ を送る
import csv
import json
import shutil
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRC = ROOT / "KakurenboIncremental" / "SourceArt" / "SoundGen"
OUT = SRC / "_web"
BARS = 40  # 波形の棒の数

# 一覧の分け方（ゲームの中でどこで鳴るか）
GROUPS = [
    ("お金とお店", ["Mash", "Buy", "BuyFail", "Place", "PickUp", "Prestige", "Gem"]),
    ("ラウンドの流れ", ["CountdownBeep", "RoundStart", "TimeTick", "Alert", "Caught", "Clear", "Daruma"]),
    ("プレイヤー", ["PlayerStep", "PlayerClimb", "Dash", "Smoke", "Heartbeat"]),
    ("鬼", ["OniStep", "OniNotice", "Summon", "BlockHit", "BlockBreak"]),
    ("お宝・罠・道具", ["Treasure", "TreasureSparkle", "TrapSticky", "DecoyPing", "DecoyBreak", "Poof", "Meow"]),
    ("BGM", ["Music_Calm", "Music_Hide"]),
]


def peaks(audio):
    mono = np.abs(audio.mean(axis=1) if audio.ndim > 1 else audio)
    if len(mono) == 0:
        return [0] * BARS
    chunks = np.array_split(mono, BARS)
    values = np.array([float(c.max()) if len(c) else 0.0 for c in chunks])
    top = values.max() or 1.0
    return [round(v / top, 2) for v in values]


def to_mp3(wav, mp3):
    audio, rate = sf.read(str(wav), dtype="float32")
    mp3.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(mp3), audio, rate, format="MP3", compression_level=0.4)
    return len(audio) / rate, peaks(audio)


def main():
    with open(HERE / "Sounds.csv", encoding="utf-8") as f:
        table = {row["Name"]: row for row in csv.DictReader(f) if row.get("Name")}
    shutil.rmtree(OUT / "audio", ignore_errors=True)  # 前に作った MP3（消えた候補を残さない）
    OUT.mkdir(parents=True, exist_ok=True)
    groups = []
    for title, names in GROUPS:
        items = []
        for name in names:
            row = table.get(name, {})
            clips = []
            current = SRC / "_Current" / f"{name}.wav"
            if current.exists():
                seconds, bars = to_mp3(current, OUT / "audio" / "current" / f"{name}.mp3")
                clips.append({"id": f"{name}_now", "label": "今の音", "src": f"audio/current/{name}.mp3", "sec": round(seconds, 2), "bars": bars, "now": True})
            for wav in sorted((SRC / name).glob("*.wav"), key=lambda p: int(p.stem.rsplit("_s", 1)[-1])):
                seconds, bars = to_mp3(wav, OUT / "audio" / name / f"{wav.stem}.mp3")
                clips.append({"id": wav.stem, "label": "s" + wav.stem.rsplit("_s", 1)[-1], "src": f"audio/{name}/{wav.stem}.mp3", "sec": round(seconds, 2), "bars": bars})
            items.append({"name": name, "desc": row.get("Desc", ""), "prompt": row.get("Prompt", ""), "clips": clips})
        groups.append({"title": title, "items": items})
    page = (HERE / "WebPage.html").read_text(encoding="utf-8")
    page = page.replace("/*SOUND_DATA*/[]", json.dumps(groups, ensure_ascii=False))
    (OUT / "index.html").write_text(page, encoding="utf-8")
    files = list((OUT / "audio").rglob("*.mp3"))
    size = sum(p.stat().st_size for p in files) / 1024 ** 2
    print(f"[MakeWebPage] {OUT / 'index.html'}  MP3 {len(files)} 個・{size:.1f}MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
