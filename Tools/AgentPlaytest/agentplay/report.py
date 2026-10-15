"""記録（rounds.jsonl・events.jsonl・bugs.jsonl・学習の保存）から報告書を作る。

  <記録のフォルダ>/report.md      まとめ（3 つの性格を並べる）と、性格ごとの詳しい報告
  <記録のフォルダ>/report.html    同じ内容をグラフ付きで（ブラウザで開く。外のファイルは読まない）
  <記録のフォルダ>/<性格>/report.md
Claude を使わずに、ゲームを遊び終わったら（と途中で 15 分ごとに）Tools/AgentPlaytest.ps1 が作る。
"""

import html
import json
import math
import os
import time
from collections import Counter, defaultdict

PERSONA_JA = {"stay": "慎重型（その場から動かない）", "near": "慎重型（近くのお宝だけ取る）", "greedy": "強欲型（お宝をほぼ全部取る）"}
PERSONA_ORDER = ["stay", "near", "greedy"]
ONI_TYPE_JA = {"Balanced": "標準鬼", "Scout": "スピード鬼", "Breaker": "パワー鬼", "Careful": "慎重鬼", "Treasure": "宝物鬼", "Detector": "探知鬼"}
STATE_JA = {"Wander": "うろうろ", "Investigate": "音を調べに来た", "Chase": "追いかけ", "Inspect": "囲いを調べに来た", "Attack": "壁を壊していた", "Stunned": "動けない"}
FEATURE_JA = {
    "bias": "（基準）", "gate_far": "鬼の門から遠い", "cover8": "まわりがふさがっている", "tall8": "まわりに背の高い物", "low8": "まわりに低い物（しゃがんで隠れる）",
    "open_view": "見通しが良い（見られやすい）", "dead_end": "行き止まり", "ring_cost": "囲むのに要る壁が多い", "local_room": "まわりが広い",
    "central": "館のまんなか", "ring_x_breaker": "囲む壁の多さ × 壁を壊す鬼", "view_x_sight": "見通し × 目のいい鬼",
    "gate_x_fast": "門から遠い × 速い鬼", "low_x_steps": "低い物の陰 × 足音に敏感な鬼", "central_x_treasure": "まんなか × お宝好きの鬼",
    "start_far": "スタート（中央）から遠い",
}
TAG_JA = {
    "hears_mash": "連打の音を聞きつける", "ignores_mash": "連打を気にしない", "hears_steps": "足音に敏感", "fast": "足が速い", "slow": "足が遅い",
    "wide_sight": "視界が広い・遠くまで見る", "short_sight": "目は近め", "breaks_walls": "壁を壊す", "weak_breaker": "壁を壊すのは苦手",
    "searches_enclosed": "囲んだ場所も調べる", "ignores_enclosed": "囲んだ場所は調べない", "summons": "仲間を呼ぶ", "stationary": "動かない見張り",
    "treasure_lover": "お宝のまわりを回る", "systematic": "手分けしてしらみつぶし", "chases": "見つけると追いかける",
    "advice:stay": "（対策）動かずに", "advice:sneak": "（対策）しのび足", "advice:less_mash": "（対策）音・連打を控える", "advice:cover": "（対策）陰に回る・視線を切る",
    "advice:relocate": "（対策）見終わった場所へ移る", "advice:spread_walls": "（対策）壁を離して置く",
}
TEMPLATE_JA = {"open": "囲わない", "door_far": "囲って門の反対に出入り口", "sealed": "全部閉じる", "double": "二重の囲い", "decoy_far": "囲わず罠・おとり"}
ACTION_JA = {"MASH": "連打", "QUIET": "静かにしゃがむ", "FLEE": "逃げる", "RETURN": "拠点へ戻る", "FETCH": "お宝を取りに行く", "SMOKE": "煙幕", "ITEM": "道具",
             "EXPLORE": "お宝を探して歩く"}
BUG_JA = {
    "ui-truncated": "文が枠に入らず「…」で切れた", "ui-overlap": "文どうしが重なった", "ui-offscreen": "文が画面の外にはみ出した",
    "stuck-walk": "歩けるはずの所で引っかかって進めない", "player-out": "自分が館の外・床の下へ出た", "oni-out": "鬼が館の外・床の下へ出た",
    "oni-stuck": "鬼が歩くはずなのに 20 秒以上動かない", "caught-far": "鬼が離れているのに見つかった", "time-overrun": "時間が 0 になってもかくれんぼが終わらない",
    "bad-coins": "コインの数がおかしい", "bad-stock": "在庫の数がおかしい", "cursor-miss": "マウスのカーソルで、そのマスを指せない",
    "crash": "ゲームが落ちた", "hang": "ゲームが固まった（応答が無い）", "agent-error": "AI の側のエラー（AI の不具合）",
}


def read_jsonl(path):
    out = []
    try:
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    try:
                        out.append(json.loads(line))
                    except ValueError:
                        pass
    except OSError:
        pass
    return out


def read_json(path, default=None):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def hm(sec):
    sec = int(sec or 0)
    return f"{sec // 3600}:{(sec % 3600) // 60:02d}:{sec % 60:02d}"


def pct(a, b):
    return f"{(100.0 * a / b):.0f}%" if b else "-"


class PersonaData:
    def __init__(self, run_dir, persona):
        self.persona = persona
        d = os.path.join(run_dir, persona)
        self.rounds = read_jsonl(os.path.join(d, "rounds.jsonl"))
        self.events = read_jsonl(os.path.join(d, "events.jsonl"))
        self.bugs = read_jsonl(os.path.join(d, "bugs.jsonl"))
        self.bug_counts = read_json(os.path.join(d, "bug_counts.json"), {}) or {}
        self.crashes = read_jsonl(os.path.join(d, "crashes.jsonl"))
        self.brain = read_json(os.path.join(os.path.dirname(os.path.abspath(run_dir)), "brains", f"brain_{persona}.json"), {}) or {}
        self.exists = bool(self.rounds or self.events)

    # ---- まとめの数
    def summary(self):
        R = self.rounds
        clears = sum(1 for r in R if r.get("cleared_true") or r.get("cleared"))
        max_stage = max([r.get("stage") or 0 for r in R] + [e["data"]["stage"] for e in self.events if e["type"] == "stage-reached"] or [0])
        prestiges = max([r.get("prestige") or 0 for r in R] + [e["data"].get("prestige", 0) for e in self.events if e["type"] == "prestige-tel"] or [0])
        played = max([r.get("elapsed", 0) for r in R] + [e.get("elapsed", 0) for e in self.events] or [0])
        tr_got = sum(r.get("treasures_got") or 0 for r in R)
        tr_all = sum(r.get("treasures_total") or 0 for r in R)
        surv = [min(1.0, (r.get("survived") or 0) / max(1, r.get("limit") or 1)) for r in R]
        return {"rounds": len(R), "clears": clears, "max_stage": max_stage, "prestiges": prestiges, "played": played,
                "treasures": (tr_got, tr_all), "survival": sum(surv) / len(surv) if surv else 0.0,
                "bugs": len(self.bugs), "crashes": len(self.crashes)}

    def laps(self):
        laps = defaultdict(list)
        for r in self.rounds:
            laps[r.get("prestige") or 0].append(r)
        return dict(sorted(laps.items()))

    def stage_table(self, rounds):
        st = defaultdict(lambda: {"n": 0, "clear": 0, "surv": 0.0, "by": Counter(), "state": Counter(), "map": None, "onis": None,
                                  "first_clear": None, "first_clear_round": None, "first_seen": None, "max_streak": 0, "streak": 0,
                                  "treasure": [0, 0], "at_base": 0, "moving": 0})
        for i, r in enumerate(rounds):
            s = st[r.get("stage") or 0]
            s["n"] += 1
            s["map"] = r.get("map")
            s["onis"] = r.get("onis_true")
            if s["first_seen"] is None:
                s["first_seen"] = r.get("elapsed")
            s["surv"] += min(1.0, (r.get("survived") or 0) / max(1, r.get("limit") or 1))
            s["treasure"][0] += r.get("treasures_got") or 0
            s["treasure"][1] += r.get("treasures_total") or 0
            if r.get("cleared_true") or r.get("cleared"):
                s["clear"] += 1
                s["streak"] = 0
                if s["first_clear"] is None:
                    s["first_clear"] = r.get("elapsed")
                    s["first_clear_round"] = r.get("round")
            else:
                s["streak"] += 1
                s["max_streak"] = max(s["max_streak"], s["streak"])
                if r.get("caught_by_true"):
                    s["by"][ONI_TYPE_JA.get(r["caught_by_true"], r["caught_by_true"])] += 1
                if r.get("caught_state_true"):
                    s["state"][STATE_JA.get(r["caught_state_true"], r["caught_state_true"])] += 1
                acts = r.get("actions") or {}
                moving = (acts.get("FLEE", 0) + acts.get("FETCH", 0) + acts.get("RETURN", 0))
                s["moving" if moving > 0 else "at_base"] += 1
        return dict(sorted(st.items()))


# ---------------------------------------------------------------- Markdown

def persona_markdown(pd):
    s = pd.summary()
    L = []
    L.append(f"## {PERSONA_JA[pd.persona]}（{pd.persona}）")
    L.append("")
    if not pd.exists:
        L.append("（まだ記録がありません）")
        return "\n".join(L)
    L.append(f"- 遊んだ時間 **{hm(s['played'])}**・ラウンド **{s['rounds']}**・逃げ切り **{s['clears']}**（{pct(s['clears'], s['rounds'])}）")
    L.append(f"- 最高ステージ **{s['max_stage']}**・転生 **{s['prestiges']} 回**・隠れていられた割合の平均 **{s['survival'] * 100:.0f}%**")
    L.append(f"- お宝 {s['treasures'][0]} / {s['treasures'][1]} 個・見つけたおかしなこと {s['bugs']} 種類・ゲームが落ちた／固まった {s['crashes']} 回")
    L.append("")

    # ---- どこで詰まったか
    L.append("### どこで詰まったか")
    L.append("")
    stuck = []
    for lap, rounds in pd.laps().items():
        for stage, t in pd.stage_table(rounds).items():
            fails = t["n"] - t["clear"]
            if fails >= 4 or t["max_streak"] >= 3:
                stuck.append((fails, lap, stage, t))
    if not stuck:
        L.append("大きく詰まったステージはまだありません（同じステージで 4 回以上見つかった所を「詰まり」として数えます）。")
    else:
        stuck.sort(key=lambda p: -p[0])
        L.append("| 周 | ステージ | 館 | 鬼 | 挑戦 | 逃げ切り | 続けて失敗（最大） | 見つけた鬼 | そのときの鬼 | 拠点にいた／動いていた |")
        L.append("|---|---|---|---|---|---|---|---|---|---|")
        for fails, lap, stage, t in stuck[:12]:
            onis = "・".join(ONI_TYPE_JA.get(o, o) for o in (t["onis"] or []))
            by = "、".join(f"{k} {v}" for k, v in t["by"].most_common(3)) or "-"
            stt = "、".join(f"{k} {v}" for k, v in t["state"].most_common(2)) or "-"
            L.append(f"| {lap + 1} | {stage} | {t['map']} | {onis} | {t['n']} | {t['clear']} | {t['max_streak']} | {by} | {stt} | {t['at_base']} / {t['moving']} |")
    L.append("")

    # ---- ステージ別（全部の周を合わせて）とクリアまでの時間
    L.append("### ステージごとの結果とクリアまでにかかった時間")
    L.append("")
    L.append("「初クリア」は記録を始めてからの時間（転生した周も含めて、そのステージを初めて逃げ切ったとき）。")
    L.append("")
    L.append("| ステージ | 館 | 鬼 | 挑戦 | 逃げ切り | 率 | 隠れていた割合 | 初クリア | そのときのラウンド | お宝 |")
    L.append("|---|---|---|---|---|---|---|---|---|---|")
    for stage, t in pd.stage_table(pd.rounds).items():
        onis = "・".join(ONI_TYPE_JA.get(o, o) for o in (t["onis"] or []))
        L.append(f"| {stage} | {t['map']} | {onis} | {t['n']} | {t['clear']} | {pct(t['clear'], t['n'])} | {t['surv'] / max(1, t['n']) * 100:.0f}% | "
                 f"{hm(t['first_clear']) if t['first_clear'] is not None else '-'} | {t['first_clear_round'] or '-'} | {t['treasure'][0]}/{t['treasure'][1]} |")
    last_stage = max(pd.stage_table(pd.rounds) or {0: None})
    cleared_25 = any((r.get("stage") or 0) >= 25 and (r.get("cleared_true") or r.get("cleared")) for r in pd.rounds)
    L.append("")
    L.append(f"- ステージ表の最後（25）を逃げ切ったか：**{'はい' if cleared_25 else 'まだ'}**（最高はステージ {last_stage}）")
    L.append("")

    # ---- 転生
    L.append("### 転生と、転生後どうだったか")
    L.append("")
    pres = [e for e in pd.events if e["type"] == "prestige"]
    laps = pd.laps()
    if not pres and len(laps) <= 1:
        L.append("まだ転生していません。")
    else:
        L.append("| 回 | いつ | ステージ | もらったポイント | その前に続けて失敗 | 決めていた回数 | この周の最高 | 前の周の最高 | この周の長さ |")
        L.append("|---|---|---|---|---|---|---|---|---|")
        for i, e in enumerate(pres):
            d = e["data"]
            L.append(f"| {i + 1} | {hm(e['elapsed'])} | {d.get('stage')} | {d.get('points')} | {d.get('fail_streak')} | {d.get('arm')} | {d.get('lap_best')} | {d.get('prev_lap_best')} | {d.get('lap_minutes')} 分 |")
        L.append("")
        L.append("周ごとの進み方（その周で各ステージに初めて着くまでの時間。転生後に速く進めているか）：")
        L.append("")
        reach = defaultdict(dict)
        for e in pd.events:
            if e["type"] == "stage-reached":
                reach[e["data"]["prestige"]][e["data"]["stage"]] = e["elapsed"]
        stages = sorted({s for m in reach.values() for s in m})
        if stages:
            L.append("| 周 | " + " | ".join(f"st{s}" for s in stages[:26]) + " |")
            L.append("|---|" + "---|" * len(stages[:26]))
            for lap in sorted(reach):
                start = min(reach[lap].values())
                cells = []
                for s in stages[:26]:
                    v = reach[lap].get(s)
                    cells.append(f"{(v - start) / 60:.0f}分" if v is not None else "")
                L.append(f"| {lap + 1} | " + " | ".join(cells) + " |")
        L.append("")
        for lap, rounds in laps.items():
            cl = sum(1 for r in rounds if r.get("cleared_true") or r.get("cleared"))
            best = max(r.get("stage") or 0 for r in rounds)
            L.append(f"- {lap + 1} 周目：{len(rounds)} ラウンド・逃げ切り {cl}・最高ステージ {best}")
    L.append("")
    arms = (pd.brain.get("prestige") or {})
    if arms:
        L.append("転生のタイミングの学習（同じステージで何回続けて失敗したら転生するか → その周でどれだけ先へ進めたか）： " +
                 "、".join(f"{k} 回 → {v['mean']:+.1f}（{v['n']} 回）" for k, v in arms.items() if v.get("n")))
        L.append("")

    # ---- 中央から隠れ場所へ
    trav = [r for r in pd.rounds if r.get("phase_at_end") is not None]
    if trav:
        L.append("### 中央から隠れ場所へ（お宝を優先しつつ、足音から遠ざかる）")
        L.append("")
        reached = Counter(r.get("reached") or "着く前に終わった" for r in trav)
        reached_ja = {"arrived": "着いた", "timeout": "時間切れ（制限時間の 4 割）", "nopath": "行けなかった（道が無い）"}
        times = [r["travel_time"] for r in trav if r.get("travel_time") is not None]
        caught_travel = sum(1 for r in trav if r.get("phase_at_end") == "travel" and not (r.get("cleared_true") or r.get("cleared")))
        caught = sum(1 for r in trav if not (r.get("cleared_true") or r.get("cleared")))
        L.append(f"- 隠れ場所に：" + "、".join(f"{reached_ja.get(k, k)} {v}" for k, v in reached.most_common()))
        if times:
            L.append(f"- 着くまでの時間（ゲームの秒）：平均 {sum(times) / len(times):.1f} 秒")
        L.append(f"- 歩いている途中で見つかった：{caught_travel} / 見つかった {caught}")
        L.append(f"- 歩いている途中に取ったお宝：{sum(r.get('travel_treasures') or 0 for r in trav)}・足音から遠ざかった回数：{sum(r.get('travel_flee') or 0 for r in trav)}"
                 f"・隠れ場所を変えた回数：{sum(r.get('base_switches') or 0 for r in trav)}")
        L.append("")

    # ---- 宝石のお店
    L.append("### 宝石のお店の道具を使った結果")
    L.append("")
    gem_rounds = [r for r in pd.rounds if r.get("gem_items")]
    gem_buys = Counter(n for e in pd.events if e["type"] == "shop" for n in e["data"].get("gem_bought", []))
    if not gem_rounds and not gem_buys:
        L.append("まだ宝石のお店の道具を使っていません（宝石が足りない・お宝が取れていない）。")
    else:
        # 同じステージで道具を使わなかったラウンドを基準にする
        base_by_stage = defaultdict(list)
        for r in pd.rounds:
            if not r.get("gem_items"):
                base_by_stage[r.get("stage")].append(r)
        L.append("| 道具 | 買った | 使ったラウンド | 逃げ切り（使った） | 逃げ切り（同じステージで使わなかった） | 隠れていた割合（使った / 使わなかった） | 本当に起きたこと | AI に見えた効き目 |")
        L.append("|---|---|---|---|---|---|---|---|")
        names = sorted(set(gem_buys) | {n for r in gem_rounds for n in r["gem_items"]})
        for name in names:
            used = [r for r in gem_rounds if name in r["gem_items"]]
            base = [b for r in used for b in base_by_stage.get(r.get("stage"), [])]
            cl_u = sum(1 for r in used if r.get("cleared_true") or r.get("cleared"))
            cl_b = sum(1 for r in base if r.get("cleared_true") or r.get("cleared"))
            sv = lambda rs: sum(min(1.0, (r.get("survived") or 0) / max(1, r.get("limit") or 1)) for r in rs) / max(1, len(rs)) * 100
            truth = Counter()
            seen = Counter()
            for r in used:
                for k, v in (r.get("gadgets_true") or {}).items():
                    if v:
                        truth[k] += 1
                for k, v in (r.get("effects") or {}).items():
                    if v:
                        seen[k] += 1
            truth_ja = {"dark": "館が真っ暗", "goldrush": "ゴールドラッシュ中", "frozen_max": "鬼が止まった", "petting_obs": "鬼が猫をなでた",
                        "doll_obs": "鬼が人形を追った", "frozen_obs": "止まった鬼"}
            seen_ja = {"dark": "真っ暗の表示", "gold": "ゴールドラッシュの表示", "onis_frozen": "止まった鬼", "cat_petting": "猫をなでる鬼",
                       "doll_chased": "人形を追う鬼", "item_used": "道具を使った"}
            L.append(f"| {name} | {gem_buys.get(name, 0)} | {len(used)} | {pct(cl_u, len(used))} | {pct(cl_b, len(base))} | {sv(used):.0f}% / {sv(base):.0f}% | "
                     + ("、".join(f"{truth_ja.get(k, k)} {v}" for k, v in truth.most_common()) or "-") + " | "
                     + ("、".join(f"{seen_ja.get(k, k)} {v}" for k, v in seen.most_common()) or "-") + " |")
        L.append("")
        L.append("（数はラウンドの数。ゴールドラッシュ・真っ暗などは 1 ラウンドで壊れる。宝石と宝石の道具は転生するとなくなる。宝石が少ないうちはラウンドも少ないので目安）")
    L.append("")

    # ---- 転生のお店
    pbuys = Counter(n for e in pd.events if e["type"] == "shop" for n in e["data"].get("prestige_bought", []))
    if pbuys:
        L.append("### 転生のお店で買った物（性格ごとの優先度で）")
        L.append("")
        L.append("、".join(f"{k} {v} 回" for k, v in pbuys.most_common()))
        L.append("")

    # ---- 見つかり方
    L.append("### 見つかり方")
    L.append("")
    caught = [r for r in pd.rounds if not (r.get("cleared_true") or r.get("cleared"))]
    if caught:
        by = Counter(ONI_TYPE_JA.get(r.get("caught_by_true"), r.get("caught_by_true") or "?") for r in caught)
        st_ = Counter(STATE_JA.get(r.get("caught_state_true"), r.get("caught_state_true") or "?") for r in caught)
        chased = sum(1 for r in caught if r.get("chased"))
        flee = [r for r in pd.rounds if (r.get("flee") or 0) > 0]
        esc = sum(1 for r in flee if r.get("cleared_true") or r.get("cleared"))
        early = sum(1 for r in caught if (r.get("survived") or 0) < 5)
        L.append(f"- 見つけた鬼：" + "、".join(f"{k} {v}" for k, v in by.most_common()))
        L.append(f"- そのときの鬼の様子：" + "、".join(f"{k} {v}" for k, v in st_.most_common()))
        L.append(f"- 追われてから見つかった：{chased} / {len(caught)}・逃げる行動をしたラウンドで逃げ切れた：{esc} / {len(flee)}")
        L.append(f"- 始まって 5 秒以内に見つかった：{early} 回")
        seen_ok = sum(1 for r in caught if r.get("caught_by_seen") and ONI_TYPE_JA.get(r.get("caught_by_true")) == r.get("caught_by_seen"))
        L.append(f"- AI が「この鬼に見つかった」と見て分かっていた：{seen_ok} / {len(caught)}")
    L.append("")

    # ---- 学習の様子
    L.append("### 学習の様子（強化学習）")
    L.append("")
    hist = [r.get("score", 0) for r in pd.rounds]
    if len(hist) >= 10:
        n = max(5, len(hist) // 5)
        first, last = hist[:n], hist[-n:]
        L.append(f"- 得点（隠れていた割合＋逃げ切り 0.5）の平均：最初の {n} ラウンド {sum(first) / n:.2f} → 最後の {n} ラウンド {sum(last) / n:.2f}"
                 "（後半はステージが難しくなるので、同じステージどうしの比べは下の表）")
        first_a, last_a = Counter(), Counter()
        for r in pd.rounds[:n]:
            first_a.update(r.get("actions") or {})
        for r in pd.rounds[-n:]:
            last_a.update(r.get("actions") or {})
        fa, la = sum(first_a.values()) or 1, sum(last_a.values()) or 1
        L.append("- かくれんぼ中の行動の割合（最初 → 最後）：" + "、".join(
            f"{ACTION_JA[a]} {first_a[a] * 100 // fa}%→{last_a[a] * 100 // la}%" for a in ACTION_JA if first_a[a] or last_a[a]))
        # 同じステージの最初と最後の挑戦
        by_stage = defaultdict(list)
        for r in pd.rounds:
            by_stage[r.get("stage")].append(r.get("score", 0))
        rows = [(s, v) for s, v in by_stage.items() if len(v) >= 6]
        if rows:
            L.append("")
            L.append("| ステージ | 挑戦 | 最初の 3 回の得点 | 最後の 3 回の得点 |")
            L.append("|---|---|---|---|")
            for s, v in sorted(rows, key=lambda p: p[0] or 0)[:15]:
                L.append(f"| {s} | {len(v)} | {sum(v[:3]) / 3:.2f} | {sum(v[-3:]) / 3:.2f} |")
    L.append("")
    spot = pd.brain.get("spot")
    if spot:
        mean = _blr_mean(spot)
        if mean:
            from .world import SPOT_FEATURES
            pairs = sorted(zip(SPOT_FEATURES, mean), key=lambda p: -abs(p[1]))
            L.append("隠れ場所の選び方として学んだこと（＋は隠れやすい・－は見つかりやすい。ラウンド " + str(spot.get("n", 0)) + " 回ぶん）：")
            L.append("")
            for k, v in pairs:
                if k == "bias":
                    continue
                L.append(f"- {FEATURE_JA.get(k, k)}：{v:+.2f}")
            L.append("")
    tm = pd.brain.get("templates") or {}
    if tm:
        L.append("囲い方の型ごとの良さ（鬼の印を平均にしたときの見込みの得点・試した回数）： " + "、".join(
            f"{TEMPLATE_JA.get(k, k)} {(_blr_mean(v) or [0])[0]:.2f}（{v.get('n', 0)}）" for k, v in tm.items()))
        L.append("")

    # ---- 鬼の特徴（読んだこと・見たこと）
    L.append("### AI が文章から読み取った鬼の特徴と、遊んで分かったこと")
    L.append("")
    onis = {k: v for k, v in (pd.brain.get("onis") or {}).items() if not k.startswith("_")}
    if onis:
        L.append("| 鬼 | 文から読んだ特徴 | 星（速さ・目・耳・壁） | 見た回数 | 平均の速さ（マス/秒） | 壁をこわしていた | 動けなかった | 追ってきた | この鬼に見つかった |")
        L.append("|---|---|---|---|---|---|---|---|---|")
        for name, p in sorted(onis.items()):
            tags = "、".join(TAG_JA.get(t, t.replace("weak_to:", "") + "に弱い" if t.startswith("weak_to:") else t) for t in p.get("text_tags", []))
            rt = p.get("ratings") or {}
            stars = "・".join(str(v) for v in rt.values()) or "-"
            seen = p.get("seen", 0)
            spd = p["speed_sum"] / p["speed_n"] if p.get("speed_n") else 0.0
            L.append(f"| {name} | {tags or '-'} | {stars} | {seen} | {spd:.1f} | {pct(p.get('hitting', 0), seen)} | {pct(p.get('stuck', 0), seen)} | {pct(p.get('chasing', 0), seen)} | {p.get('caught_me', 0)} |")
    L.append("")
    snd = pd.brain.get("sounds") or {}
    if snd:
        agg = defaultdict(lambda: [0, 0])
        for k, (n, hit) in snd.items():
            if k.startswith("_"):
                continue
            name = k.split(":")[0]
            agg[name][0] += n
            agg[name][1] += hit
        bn, bh = snd.get("_base", [0, 0])
        base = (bh + 1.0) / (bn + 2.0)
        top = sorted(agg.items(), key=lambda p: -p[1][0])[:10]
        L.append(f"聞こえた音の危なさ（その音のあと 3 秒以内に鬼が 6 マス以内に見えた割合と、ふだん（{base * 100:.0f}%）の何倍か。"
                 "AI は音の名前の意味を知らず、この倍率が 1.2 以上の音を「鬼が来る音」として用心する）： " +
                 "、".join(f"{k} {pct(h, n)}・×{((h + 1.0) / (n + 2.0)) / max(0.02, base):.1f}（{n}）" for k, (n, h) in top))
        L.append("")

    # ---- 音の聞き分け（鬼の足音・お宝の音）
    sid = pd.brain.get("sound_id") or {}
    if sid:
        cal = pd.brain.get("loudcal") or {}
        L.append("### 音の聞き分け（鬼の足音・お宝の音）")
        L.append("")
        L.append("AI は音の名前の意味を知らない。音がした方向にそのとき鬼・お宝が見えていた割合で「鬼の足音らしい」「お宝の音らしい」を自分で決め、"
                 "見えていた物までの距離で「この大きさなら何マス先か」も学ぶ。鬼の足音らしい音が 6 マス以内（または見えている鬼が 6 マス以内）なら連打をやめ、"
                 "お宝の音らしい音がしたら、その方向と距離の所にお宝があると見当をつける。")
        L.append("")
        L.append("| 音 | 聞いた回数 | その方向に鬼（ふだんの何倍） | その方向にお宝（ふだんの何倍） | その方向に自分の道具 | 鳴る間隔（同じ方角） | 鬼らしさ（＋）・お宝らしさ（－） | AI の判断 | 大きさ → 距離（マス。学んだもの） |")
        L.append("|---|---|---|---|---|---|---|---|---|")
        try:
            from .brain import Brain
            judge = Brain.__new__(Brain)
            judge.sound_id, judge.sounds = sid, pd.brain.get("sounds") or {}
            judge.sound_rhythm = pd.brain.get("sound_rhythm") or {}
        except Exception:
            judge = None
        rhythm = pd.brain.get("sound_rhythm") or {}
        for name, rec in sorted(((k, v) for k, v in sid.items() if not k.startswith("_")), key=lambda p: -p[1][0])[:12]:
            rec = list(rec) + [0] * (4 - len(rec))
            n = max(1, rec[0])
            lifts = judge.sound_lifts(name) if judge else None
            kind = judge.sound_kind(name) if judge else None
            kind_ja = {"oni": "鬼の足音・声らしい", "treasure": "お宝の音らしい"}.get(kind, "わからない")
            lo = f"{rec[1] * 100 // n}%（×{lifts[0]:.1f}）" if lifts else f"{rec[1] * 100 // n}%"
            lt = f"{rec[2] * 100 // n}%（×{lifts[1]:.1f}）" if lifts else f"{rec[2] * 100 // n}%"
            dist = "、".join(f"{int(k.split(':')[1]) / 10:.1f}→{v[1] / v[0]:.1f}" for k, v in sorted(cal.items()) if k.startswith(name + ":") and v[0] >= 8)
            rh = rhythm.get(name)
            pattern = (f"{rh[1] / rh[0]:.2f} 秒（" + {"steps": "細かく続く", "sparse": "ときどき", None: "わからない"}[judge.sound_pattern(name) if judge else None] + "）") if rh and rh[0] else "-"
            score = judge.sound_score(name) if judge else None
            L.append(f"| {name} | {rec[0]} | {lo} | {lt} | {rec[3] * 100 // n}% | {pattern} | {score:+.2f} |" if score is not None else
                     f"| {name} | {rec[0]} | {lo} | {lt} | {rec[3] * 100 // n}% | {pattern} | - |")
            L[-1] += f" {kind_ja} | {dist or '-'} |"
        stopped = sum(r.get("mash_stopped", 0) for r in pd.rounds)
        sfetch = sum(r.get("sound_fetch", 0) for r in pd.rounds)
        fetches = [e["data"] for e in pd.events if e["type"] == "fetch"]
        snd = [f for f in fetches if f.get("from_sound")]
        L.append("")
        L.append(f"- 鬼が近くて連打をやめた回数：{stopped}（{sum(1 for r in pd.rounds if r.get('mash_stopped'))} ラウンド）")
        L.append(f"- お宝の音を頼りに取りに行った回数：{sfetch}（着いた {sum(1 for f in snd if f.get('result') == 'arrived')}・道が無い {sum(1 for f in snd if f.get('result') == 'nopath')}）")
        L.append("")

    # ---- 読んだ文
    texts = pd.brain.get("read_texts") or {}
    if texts:
        hints = [t for t, where in texts.items() if "[" in t and "]" in t and len(t) >= 12 and not where.startswith("Shop")][:12]
        L.append(f"AI がこれまでに読んだ文：{len(texts)} 種類（画面に出た文のうち、数字だけでないもの）。操作の案内・ヒントの例：")
        L.append("")
        for t in hints:
            L.append(f"- `{t[:120]}`")
        L.append("")

    # ---- お店
    items = pd.brain.get("items") or {}
    if items:
        L.append("### お店で学んだこと")
        L.append("")
        L.append("| 商品 | 買った回数 | 買った後のラウンドの良さ（基準との差） |")
        L.append("|---|---|---|")
        for name, rec in sorted(items.items(), key=lambda p: -p[1].get("mean", 0)):
            L.append(f"| {name} | {rec.get('bought', 0)} | {rec.get('mean', 0):+.3f}（{rec.get('n', 0)} 回） |")
        L.append("")

    # ---- 不具合
    L.append("### バグ・おかしなところ")
    L.append("")
    L.append(bug_markdown(pd))
    L.append("")
    return "\n".join(L)


def _blr_mean(j):
    try:
        from .brain import BayesLinear
        return BayesLinear.from_json(j).mean()
    except Exception:
        return None


def bug_markdown(pd):
    L = []
    groups = defaultdict(list)
    for b in pd.bugs:
        groups[b.get("kind", "?")].append(b)
    for c in pd.crashes:
        groups[c.get("kind", "crash")].append({"line": c.get("detail", ""), "key": c.get("detail", ""), "time": c.get("time"), "stage": c.get("stage")})
    errs = [e for e in pd.events if e["type"] == "agent-error"]
    for e in errs:
        groups["agent-error"].append({"line": e["data"].get("error"), "key": e["data"].get("error"), "time": e.get("time"), "phase": e["data"].get("phase")})
    if not groups:
        return "見つかりませんでした。"
    for kind, lst in sorted(groups.items(), key=lambda p: -len(p[1])):
        total = sum(pd.bug_counts.get(b.get("key"), 1) for b in lst)
        L.append(f"- **{BUG_JA.get(kind, kind)}**（{kind}）：{len(lst)} 種類・のべ {total} 回")
        for b in lst[:6]:
            line = (b.get("line") or "")[:220]
            L.append(f"  - {b.get('time', '')} ステージ {b.get('stage', '-')}：`{line}`")
        if len(lst) > 6:
            L.append(f"  - ほか {len(lst) - 6} 種類（{pd.persona}/bugs.jsonl）")
    return "\n".join(L)


def overview_markdown(datas, run_dir):
    L = ["# テストプレイ AI の報告（かせぐんぼ）", ""]
    meta = read_json(os.path.join(run_dir, "run.json"), {}) or {}
    started = meta.get("started")
    L.append(f"記録のフォルダ：`{run_dir}`")
    if started:
        L.append(f"開始：{time.strftime('%Y-%m-%d %H:%M', time.localtime(started))}　この報告を作った時刻：{time.strftime('%Y-%m-%d %H:%M')}")
    L.append("")
    L.append("AI はゲームの仕様（CSV・設計書・鬼の数値）を一切知らず、画面の文・ボタン・見えている物・聞こえた音・上から見た館の形だけで遊ぶ。"
             "長く隠れられるほど得点が高い強化学習（隠れ場所・囲い方はベイズ線形モデル＋トンプソン抽出、かくれんぼ中の動きは Q 学習、お店と転生のタイミングは多腕バンディット）で上達する。"
             "まず上から見た館全体を見渡して隠れやすい場所を探し、そこを拠点に壁で囲み、鬼の様子を見て連打・静かにする・逃げる・お宝を取るを選ぶ。")
    L.append("")
    L.append("| | " + " | ".join(PERSONA_JA[d.persona] for d in datas) + " |")
    L.append("|---|" + "---|" * len(datas))
    sums = [d.summary() for d in datas]
    rows = [("遊んだ時間", [hm(s["played"]) for s in sums]), ("ラウンド", [s["rounds"] for s in sums]),
            ("逃げ切り", [f"{s['clears']}（{pct(s['clears'], s['rounds'])}）" for s in sums]),
            ("最高ステージ", [s["max_stage"] for s in sums]), ("転生", [f"{s['prestiges']} 回" for s in sums]),
            ("隠れていた割合の平均", [f"{s['survival'] * 100:.0f}%" for s in sums]),
            ("お宝", [f"{s['treasures'][0]} / {s['treasures'][1]}" for s in sums]),
            ("おかしなこと", [f"{s['bugs']} 種類" for s in sums]), ("落ちた・固まった", [s["crashes"] for s in sums])]
    for name, vals in rows:
        L.append(f"| {name} | " + " | ".join(str(v) for v in vals) + " |")
    L.append("")
    # 3 つの性格を合わせた、詰まりやすいステージ
    agg = defaultdict(lambda: [0, 0])
    for d in datas:
        for stage, t in d.stage_table(d.rounds).items():
            agg[stage][0] += t["n"]
            agg[stage][1] += t["clear"]
    hard = sorted(((n - c) / max(1, n), s, n, c) for s, (n, c) in agg.items() if n >= 3)
    hard.reverse()
    if hard:
        L.append("3 つの性格を合わせて、逃げ切りにくかったステージ（失敗の割合が高い順）： " +
                 "、".join(f"ステージ {s}（{c}/{n}）" for (_, s, n, c) in hard[:6]))
        L.append("")
    return "\n".join(L)


# ---------------------------------------------------------------- HTML（グラフ付き）

def _svg_line(series, width=720, height=260, ylabel="", ymax=None, step=False):
    """series: [(名前, 色の変数, [(x, y)])]"""
    pts = [p for _, _, s in series for p in s]
    if not pts:
        return "<p class='muted'>まだ記録がありません</p>"
    x0, x1 = min(p[0] for p in pts), max(p[0] for p in pts)
    y1 = ymax if ymax is not None else max(1e-6, max(p[1] for p in pts))
    pad_l, pad_b, pad_t, pad_r = 44, 28, 12, 12
    W, H = width - pad_l - pad_r, height - pad_t - pad_b
    sx = lambda x: pad_l + (x - x0) / max(1e-9, x1 - x0) * W
    sy = lambda y: pad_t + H - y / y1 * H
    out = [f"<svg viewBox='0 0 {width} {height}' role='img' aria-label='{html.escape(ylabel)}' class='chart'>"]
    for i in range(5):
        y = y1 * i / 4
        out.append(f"<line x1='{pad_l}' x2='{pad_l + W}' y1='{sy(y):.1f}' y2='{sy(y):.1f}' class='grid'/>")
        out.append(f"<text x='{pad_l - 6}' y='{sy(y) + 4:.1f}' class='tick' text-anchor='end'>{y:.0f}" + ("" if y1 > 4 else "") + "</text>")
    for name, color, s in series:
        if not s:
            continue
        d = []
        prev = None
        for (x, y) in s:
            if step and prev is not None:
                d.append(f"L{sx(x):.1f},{sy(prev):.1f}")
            d.append(("M" if not d else "L") + f"{sx(x):.1f},{sy(y):.1f}")
            prev = y
        out.append(f"<path d='{' '.join(d)}' fill='none' stroke='var({color})' stroke-width='2'/>")
    out.append(f"<text x='{pad_l}' y='{height - 6}' class='tick'>0</text><text x='{pad_l + W}' y='{height - 6}' class='tick' text-anchor='end'>{hm(x1)}</text>")
    out.append("</svg>")
    legend = "".join(f"<span class='key'><i style='background:var({c})'></i>{html.escape(n)}</span>" for n, c, s in series if s)
    return f"<div class='legend'>{legend}</div>" + "".join(out)


def _md_to_html(md):
    """この報告書で使う Markdown だけ（見出し・表・箇条書き・太字・コード）を HTML にする"""
    import re
    lines = md.split("\n")
    out = []
    i = 0
    in_list = False
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("|") and i + 1 < len(lines) and re.match(r"^\|[-|\s]+\|$", lines[i + 1]):
            head = [c.strip() for c in ln.strip("|").split("|")]
            out.append("<div class='tablewrap'><table><thead><tr>" + "".join(f"<th>{_inline(c)}</th>" for c in head) + "</tr></thead><tbody>")
            i += 2
            while i < len(lines) and lines[i].startswith("|"):
                cells = [c.strip() for c in lines[i].strip("|").split("|")]
                out.append("<tr>" + "".join(f"<td>{_inline(c)}</td>" for c in cells) + "</tr>")
                i += 1
            out.append("</tbody></table></div>")
            continue
        if ln.startswith("- ") or ln.startswith("  - "):
            if not in_list:
                out.append("<ul>")
                in_list = True
            cls = " class='sub'" if ln.startswith("  - ") else ""
            out.append(f"<li{cls}>{_inline(ln.strip()[2:])}</li>")
            i += 1
            continue
        if in_list:
            out.append("</ul>")
            in_list = False
        if ln.startswith("### "):
            out.append(f"<h3>{_inline(ln[4:])}</h3>")
        elif ln.startswith("## "):
            out.append(f"<h2>{_inline(ln[3:])}</h2>")
        elif ln.startswith("# "):
            out.append(f"<h1>{_inline(ln[2:])}</h1>")
        elif ln.strip():
            out.append(f"<p>{_inline(ln)}</p>")
        i += 1
    if in_list:
        out.append("</ul>")
    return "\n".join(out)


def _inline(s):
    import re
    s = html.escape(s)
    s = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"`(.+?)`", r"<code>\1</code>", s)
    return s


CSS = """
:root{--bg:#f7f6f3;--panel:#ffffff;--ink:#1f1d1a;--muted:#6b665e;--line:#e3dfd7;--accent:#7a4bc9;
--s1:#2f6fb5;--s2:#c0682b;--s3:#3c9a5f;--gridc:#e9e5dd}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){--bg:#17161a;--panel:#211f25;--ink:#ece9e4;--muted:#a39d94;--line:#36323b;
--accent:#b393f0;--s1:#6ea8e8;--s2:#e8955a;--s3:#6cc78f;--gridc:#2e2b33}}
:root[data-theme="dark"]{--bg:#17161a;--panel:#211f25;--ink:#ece9e4;--muted:#a39d94;--line:#36323b;--accent:#b393f0;--s1:#6ea8e8;--s2:#e8955a;--s3:#6cc78f;--gridc:#2e2b33}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.65 system-ui,-apple-system,"Segoe UI","Hiragino Sans","Yu Gothic UI",sans-serif}
main{max-width:1080px;margin:0 auto;padding:24px 16px 64px}
h1{font-size:26px;margin:8px 0 12px}h2{font-size:21px;margin:40px 0 8px;padding-top:12px;border-top:1px solid var(--line)}h3{font-size:16px;margin:24px 0 6px;color:var(--accent)}
.card{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:14px 16px;margin:12px 0}
.tablewrap{overflow-x:auto}table{border-collapse:collapse;width:100%;min-width:560px;font-size:13px;margin:8px 0}th,td{border-bottom:1px solid var(--line);padding:5px 8px;text-align:left;vertical-align:top}
td:first-child,th:first-child{white-space:nowrap}
th{color:var(--muted);font-weight:600;white-space:nowrap}code{font-size:12px;word-break:break-all}.muted{color:var(--muted)}
.chart{width:100%;height:auto}.chart .grid{stroke:var(--gridc)}.chart .tick{fill:var(--muted);font-size:11px}
.legend{display:flex;gap:14px;flex-wrap:wrap;font-size:13px;color:var(--muted)}.key i{display:inline-block;width:12px;height:3px;margin-right:6px;vertical-align:middle}
li.sub{margin-left:18px;list-style:circle;color:var(--muted)}
"""


def build_html(datas, run_dir, md_all):
    colors = {"stay": "--s1", "near": "--s2", "greedy": "--s3"}
    stage_series = []
    score_series = []
    for d in datas:
        pts = [(r.get("elapsed", 0), r.get("stage") or 0) for r in d.rounds]
        stage_series.append((PERSONA_JA[d.persona], colors[d.persona], pts))
        sc = [r.get("score", 0) for r in d.rounds]
        ma = []
        for i in range(len(sc)):
            w = sc[max(0, i - 19):i + 1]
            ma.append((d.rounds[i].get("elapsed", 0), sum(w) / len(w) * 100))
        score_series.append((PERSONA_JA[d.persona], colors[d.persona], ma))
    charts = ("<div class='card'><h3>到達ステージ（横は記録を始めてからの時間。転生すると 1 に戻る）</h3>" + _svg_line(stage_series, step=True, ylabel="ステージ") + "</div>"
              "<div class='card'><h3>得点の移動平均（直近 20 ラウンド。100 = 毎回逃げ切り＋0.5 の半分）</h3>" + _svg_line(score_series, ymax=150, ylabel="得点") + "</div>")
    body = _md_to_html(md_all)
    # 最初の表の後にグラフを入れる
    idx = body.find("</table></div>")
    if idx >= 0:
        idx += len("</table></div>")
        body = body[:idx] + charts + body[idx:]
    else:
        body = charts + body
    return ("<!doctype html><html lang='ja'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            f"<title>テストプレイ AI 報告</title><style>{CSS}</style></head><body><main>{body}</main></body></html>")


def generate(run_dir, personas=None):
    personas = personas or [p for p in PERSONA_ORDER if os.path.isdir(os.path.join(run_dir, p))]
    datas = [PersonaData(run_dir, p) for p in personas]
    parts = [overview_markdown(datas, run_dir)]
    for d in datas:
        md = persona_markdown(d)
        with open(os.path.join(run_dir, d.persona, "report.md"), "w", encoding="utf-8") as f:
            f.write(md + "\n")
        parts.append(md)
    md_all = "\n\n".join(parts) + "\n"
    with open(os.path.join(run_dir, "report.md"), "w", encoding="utf-8") as f:
        f.write(md_all)
    with open(os.path.join(run_dir, "report.html"), "w", encoding="utf-8") as f:
        f.write(build_html(datas, run_dir, md_all))
    return os.path.join(run_dir, "report.html")
