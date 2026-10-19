# 作った音の候補を AI（CLAP：音と文の近さを測るモデル laion/clap-htsat-unfused・約 600MB）で採点し、おすすめの順に並べる。
#   Tools\SoundGen\.venv\Scripts\python.exe -I Tools\SoundGen\RankCandidates.py                # 全部
#   Tools\SoundGen\.venv\Scripts\python.exe -I Tools\SoundGen\RankCandidates.py --min-seed 21 Mash OniStepScout
# 結果は KakurenboIncremental/SourceArt/SoundGen/_ranking.json（MakeWebPage.py が「AI の予想」の印に使う）。
#
# 点数 = ユーザーの好みの近さ − 耳で聞く前にわかる問題（頭の無音・音割れ・「1 回の足音」なのに何回も鳴る・お尻のノイズ）。
# 好みは docs/Sounds.md の「採用」から学ぶ：選ばれた候補と選ばれなかった候補の CLAP の特徴の差（向き）に近い候補ほど高い。
# 2026-10-19 に確かめた当たり具合（15 個の採用を 1 つずつ隠して当てる）：1 位が選ばれた音 4/15（でたらめなら 2.5/15）・平均 2.9 位/6。
# 説明文との近さだけだと平均 3.6 位/6（でたらめと同じ）だったので使わない（表示だけ）。人の耳の代わりではない。決めるのはユーザー。
import argparse
import csv
import json
import os
import re
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRC = ROOT / "KakurenboIncremental" / "SourceArt" / "SoundGen"
MUSIC_SRC = ROOT / "KakurenboIncremental" / "SourceArt" / "MusicGen"
OUT = SRC / "_ranking.json"
MODEL_ID = "laion/clap-htsat-unfused"
CLAP_RATE = 48000
GAME_STYLE = "cute cartoon video game sound effect"
REAL_STYLE = "realistic field recording of a real sound"

# この数字より大きいシード値の候補は、まだユーザーが聞いて決めていないので好みの学習に使わない（決めたら上げる）
JUDGED_BELOW = 31

os.environ.setdefault("HF_HUB_DISABLE_SYMLINKS_WARNING", "1")
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def load(path, rate=CLAP_RATE, max_seconds=None):
    audio, sr = sf.read(str(path), dtype="float32", always_2d=True)
    mono = audio.mean(axis=1)
    if max_seconds:
        mono = mono[: int(max_seconds * sr)]
    if sr != rate:
        n = int(round(len(mono) * rate / sr))
        mono = np.interp(np.linspace(0, len(mono) - 1, n), np.arange(len(mono)), mono).astype(np.float32)
    return mono


def checks(mono, rate):
    """耳で聞く前にわかる問題：頭の無音（秒）・割れている割合・音の数（立ち上がりの数）・お尻のノイズ（鳴っている所との比）"""
    peak = float(np.abs(mono).max()) or 1e-9
    loud = np.nonzero(np.abs(mono) > peak * 0.05)[0]
    lead = loud[0] / rate if len(loud) else 0.0
    clip = float((np.abs(mono) > 0.99).mean())
    # 10ms ごとの大きさ。前より 2.5 倍以上・最大の 25% 以上に上がった所を「音の立ち上がり」と数える（50ms は数えない）
    hop = rate // 100
    env = np.array([np.sqrt((mono[i:i + hop] ** 2).mean()) for i in range(0, len(mono) - hop, hop)]) + 1e-9
    top = env.max()
    onsets, last = 0, -999
    for i in range(1, len(env)):
        if env[i] > top * 0.25 and env[i] > env[max(i - 3, 0)] * 2.5 and i - last > 5:
            onsets += 1
            last = i
    tail = env[int(len(env) * 0.9):]
    active = env[env > top * 0.1]
    tail_ratio = float(tail.mean() / active.mean()) if len(tail) and len(active) else 0.0
    return lead, clip, max(onsets, 1), tail_ratio


def read_adoptions():
    """docs/Sounds.md の表の「採用」：{音の名前: 選んだ候補の名前（選ばなかった・今のままは None）}"""
    adopted = {}
    for line in (ROOT / "docs" / "Sounds.md").read_text(encoding="utf-8").splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 4 or not re.fullmatch(r"[A-Za-z]\w*", cells[0]) or cells[0].startswith("Music_"):
            continue
        m = re.search(r"\*\*([A-Za-z]\w*_s\d+)\*\*", cells[3])
        adopted[cells[0]] = m.group(1) if m else None
    return adopted


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("names", nargs="*", help="Sounds.csv の Name（省略で全部。Music_ で始まる名前は BGM の候補も）")
    parser.add_argument("--min-seed", type=int, default=0, help="このシード値より前の候補は採点しない（説明文を変える前の候補）")
    parser.add_argument("--cpu", action="store_true", help="GPU を使わない（音を作る AI と同時に動かすとき）")
    args = parser.parse_args()

    with open(HERE / "Sounds.csv", encoding="utf-8") as f:
        table = {row["Name"]: row for row in csv.DictReader(f) if row.get("Name")}
    with open(ROOT / "Tools" / "MusicGen" / "Music.csv", encoding="utf-8") as f:
        music = {row["Name"]: row for row in csv.DictReader(f) if row.get("Name")}
    names = args.names or list(dict.fromkeys(list(table) + list(music)))

    import torch
    from transformers import ClapModel, ClapProcessor

    device = "cuda" if torch.cuda.is_available() and not args.cpu else "cpu"
    model = ClapModel.from_pretrained(MODEL_ID).to(device).eval()
    processor = ClapProcessor.from_pretrained(MODEL_ID)

    def unwrap(e):
        # transformers 5 からは出力の入れ物（pooler_output が投影した 512 次元）で返る
        return e if torch.is_tensor(e) else e.pooler_output

    def embed_text(texts):
        inputs = processor(text=texts, return_tensors="pt", padding=True).to(device)
        with torch.no_grad():
            e = unwrap(model.get_text_features(**inputs))
        return torch.nn.functional.normalize(e, dim=-1)

    def embed_audio(clips):
        inputs = processor(audio=clips, sampling_rate=CLAP_RATE, return_tensors="pt").to(device)
        with torch.no_grad():
            e = unwrap(model.get_audio_features(**inputs))
        return torch.nn.functional.normalize(e, dim=-1)

    styles = embed_text([GAME_STYLE, REAL_STYLE])

    # 好みの向き：選ばれた候補の平均 − 選ばれなかった候補の平均
    chosen, others = [], []
    for name, pick in read_adoptions().items():
        for path in sorted((SRC / name).glob("*.wav")) if (SRC / name).exists() else []:
            if int(path.stem.rsplit("_s", 1)[-1]) >= JUDGED_BELOW:
                continue
            e = embed_audio([load(path, max_seconds=10)])[0]
            (chosen if path.stem == pick else others).append(e)
    taste = (torch.stack(chosen).mean(0) - torch.stack(others).mean(0)) if chosen and others else None
    print(f"[Rank] 好みを {len(chosen)} 個の採用と {len(others)} 個の選ばれなかった候補から学んだ")
    ranking = json.loads(OUT.read_text(encoding="utf-8")) if OUT.exists() else {}
    for name in names:
        is_music = name in music
        prompt = (music.get(name) or table.get(name) or {}).get("Prompt", "")
        files = []
        if (SRC / name).exists():
            files += [("", p) for p in (SRC / name).glob("*.wav")]
        if is_music and (MUSIC_SRC / name).exists():
            files += [("MusicGen:", p) for p in (MUSIC_SRC / name).glob("*.wav")]
        files = [(pre, p) for pre, p in files if int(p.stem.rsplit("_s", 1)[-1]) >= args.min_seed and (not is_music or pre)]
        if not files or not prompt:
            continue
        text = embed_text([prompt])
        rows = []
        for pre, path in files:
            # 曲は 10 秒ずつ 3 か所の平均（CLAP は 10 秒まで）
            if is_music:
                full = load(path)
                n = len(full)
                clips = [full[int(n * t): int(n * t) + 10 * CLAP_RATE] for t in (0.1, 0.4, 0.7)]
                a = embed_audio(clips).mean(dim=0, keepdim=True)
                a = torch.nn.functional.normalize(a, dim=-1)
                lead, clip, onsets, tail = 0.0, float((np.abs(full) > 0.99).mean()), 1, 0.0
            else:
                mono = load(path, max_seconds=10)
                a = embed_audio([mono])
                lead, clip, onsets, tail = checks(mono, CLAP_RATE)
            sim = float((a @ text.T).item())
            style = float((a @ styles.T)[0, 0] - (a @ styles.T)[0, 1])
            pref = float(a[0] @ taste) if taste is not None and not is_music else sim
            notes = []
            penalty = 0.0  # 好みの点（同じ音の候補の中で平均 0・ばらつき 1 にそろえる）から引く
            if not is_music:
                if lead > 0.05:
                    penalty += 0.5 + min(lead, 0.5)
                    notes.append(f"頭の無音 {lead:.2f} 秒")
                single = prompt.lower().startswith(("single", "tiny", "short", "quick"))
                if single and onsets > 2:
                    penalty += min(0.25 * (onsets - 2), 1.0)
                    notes.append(f"音が {onsets} 回")
                if tail > 0.3:
                    penalty += 0.5
                    notes.append("お尻にノイズ")
            if clip > 0.001:
                penalty += 0.5
                notes.append("音割れ")
            rows.append({"id": pre + path.stem, "pref": pref, "penalty": penalty, "text": round(sim, 4), "style": round(style, 4),
                         "lead": round(lead, 3), "onsets": onsets, "notes": notes})
        prefs = np.array([r["pref"] for r in rows])
        spread = prefs.std() or 1.0
        for r in rows:
            r["score"] = round(float((r.pop("pref") - prefs.mean()) / spread - r.pop("penalty")), 3)
        rows.sort(key=lambda r: -r["score"])
        # 前の採点を残しつつ、今回採点した候補を上書きする
        keep = [r for r in ranking.get(name, []) if r["id"] not in {x["id"] for x in rows}]
        ranking[name] = sorted(keep + rows, key=lambda r: -r["score"])
        best = rows[0]
        print(f"[Rank] {name}: 1 位 {best['id']}（{best['score']:+.2f}・文 {best['text']:.3f}・ゲームらしさ {best['style']:+.3f}）"
              + (f" 注意: {', '.join(best['notes'])}" if best["notes"] else ""))
    OUT.write_text(json.dumps(ranking, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"[Rank] {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
