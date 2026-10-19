# 作った音の候補と今の音を、どこでも聞ける 1 枚のページにする（claude.ai の Artifact として公開する用）。
#   Tools\SoundGen\.venv\Scripts\python.exe Tools\SoundGen\MakeWebPage.py
# KakurenboIncremental/SourceArt/SoundGen/_web/ に index.html（WebPage.html に音の一覧を差し込んだもの）と audio/*.mp3 を作る。
# WAV のままだと大きい（60MB）ので MP3 にする。公開は Claude が Artifact ツールで _web/index.html と audio/ を送る
import csv
import json
import re
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
    ("メニュー", ["UIClick", "UIMove", "MenuOpen", "MenuClose"]),
    ("ラウンドの流れ", ["CountdownBeep", "RoundStart", "TimeTick", "Alert", "Caught", "Clear", "Daruma"]),
    ("プレイヤー", ["PlayerStep", "PlayerClimb", "Dash", "Smoke", "Heartbeat"]),
    ("鬼", ["OniStep", "OniStepScout", "OniStepBreaker", "OniStepCareful", "OniStepTreasure", "OniLeap", "OniNotice", "OniIntro", "Summon", "BlockHit", "BlockBreak"]),
    ("お宝・罠・道具", ["Treasure", "TreasureSparkle", "TrapSticky", "DecoyPing", "DecoyBreak", "DollStep", "CurtainPass", "Poof", "Meow"]),
    ("BGM", ["Music_Title", "Music_Calm", "Music_Hide", "Music_HideDeep", "Music_Chase"]),
]
# このシード値から 99 までの候補が「新しい候補」（まだ決めていない。RankCandidates.py の JUDGED_BELOW と同じ。s900 は前の試し）。それより前の候補はたたんで出す
NEW_SEED = 41
# BGM の新しい候補（ACE-Step）：曲ごとのこのシード値から
NEW_MUSIC_SEED = {}
# BGM 専用の AI（ACE-Step 1.5。Tools/MusicGen.ps1）の候補。同じ名前の BGM に「ACE s1」などとして並べる
MUSIC_SRC = ROOT / "KakurenboIncremental" / "SourceArt" / "MusicGen"
MUSIC_CSV = ROOT / "Tools" / "MusicGen" / "Music.csv"


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


def read_adoptions():
    """docs/Sounds.md の表の「採用」：{音の名前: 候補の名前（MusicGen:… を含む）・"（今のまま）"・None}"""
    adopted = {}
    for line in (ROOT / "docs" / "Sounds.md").read_text(encoding="utf-8").splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 4 or not re.fullmatch(r"[A-Za-z]\w*", cells[0]):
            continue
        m = re.search(r"\*\*((?:MusicGen:)?[A-Za-z]\w*_s\d+)\*\*", cells[3])
        adopted[cells[0]] = m.group(1) if m else ("（今のまま）" if "今のまま" in cells[3] else None)
    return adopted


def main():
    adopted = read_adoptions()
    ranking_path = SRC / "_ranking.json"
    ranking = json.loads(ranking_path.read_text(encoding="utf-8")) if ranking_path.exists() else {}
    with open(HERE / "Sounds.csv", encoding="utf-8") as f:
        table = {row["Name"]: row for row in csv.DictReader(f) if row.get("Name")}
    with open(MUSIC_CSV, encoding="utf-8") as f:
        music = {row["Name"]: row for row in csv.DictReader(f) if row.get("Name")}
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
            game = adopted.get(name)
            for wav in sorted((SRC / name).glob("*.wav"), key=lambda p: int(p.stem.rsplit("_s", 1)[-1])):
                seed = int(wav.stem.rsplit("_s", 1)[-1])
                seconds, bars = to_mp3(wav, OUT / "audio" / name / f"{wav.stem}.mp3")
                clips.append({"id": wav.stem, "label": f"s{seed}", "src": f"audio/{name}/{wav.stem}.mp3", "sec": round(seconds, 2), "bars": bars,
                              "fresh": NEW_SEED <= seed < 100 and name not in music, "game": game == wav.stem})
            for wav in sorted((MUSIC_SRC / name).glob("*.wav"), key=lambda p: int(p.stem.rsplit("_s", 1)[-1])):
                seed = int(wav.stem.rsplit("_s", 1)[-1])
                seconds, bars = to_mp3(wav, OUT / "audio" / "MusicGen" / name / f"{wav.stem}.mp3")
                clips.append({"id": "MusicGen:" + wav.stem, "label": f"ACE s{seed}",
                              "src": f"audio/MusicGen/{name}/{wav.stem}.mp3", "sec": round(seconds, 2), "bars": bars,
                              "fresh": seed >= NEW_MUSIC_SEED.get(name, 999), "game": game == "MusicGen:" + wav.stem})
            # 新しい候補があれば、それより前の候補（前に聞いて決めたもの）はたたむ。ゲームに入っている候補はたたまない
            has_fresh = any(c.get("fresh") for c in clips)
            for c in clips:
                c["old"] = has_fresh and not c.get("now") and not c.get("fresh") and not c.get("game")
            if game == "（今のまま）" and clips and clips[0].get("now"):
                clips[0]["game"] = True
            # AI の予想（RankCandidates.py）：新しい候補の中の 1 位に印
            fresh_ids = {c["id"] for c in clips if c.get("fresh")}
            ranked = [r for r in ranking.get(name, []) if r["id"] in fresh_ids]
            notes = {r["id"]: r.get("notes", []) for r in ranked}
            for c in clips:
                if ranked and c["id"] == ranked[0]["id"]:
                    c["ai"] = True
                if notes.get(c["id"]):
                    c["warn"] = "・".join(notes[c["id"]])
            row = music.get(name, row)  # BGM は ACE-Step の説明文を出す
            items.append({"name": name, "desc": row.get("Desc", ""), "prompt": row.get("Prompt", ""), "clips": clips,
                          "game": game, "pending": has_fresh})
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
