"""バランスを推論する AI（ユーザーの希望）。テストプレイ AI の記録を全部読み、ステージ・転生を重ねるとどうなるかを推論する。

遊ぶ AI（agent.py）と違い、これは作る人のための分析なので、記録用の本当の値（TEL：ステージ・転生の回数・鬼の種類・見つけた鬼・稼いだコイン…）も使う。
ゲームの仕様（Data/*.csv）は読まない（遊んだ結果だけから推論する）。

  - 3 つの性格（stay / near / greedy）の記録を全部まとめる（記録のフォルダは全部。同じラウンドを 2 回数えない）
  - ステージごとの特徴と傾向：挑戦・クリア率・隠れていた割合・出てきた鬼・見つけた鬼・追われた割合・壊された壁・稼ぎ・お宝
  - 難しさ：クリア率から「1 回クリアするのに何回挑戦が要るか」。全体の傾き（ロジスティック回帰）からの外れで、急に難しい・簡単すぎるステージ
  - Level カーブ：クリア率 = σ(a + b・ステージ + c・転生の回数 + 性格)。ステージ 30 まで・転生 10 回まで外へ伸ばして推論
  - 転生による強化幅：同じステージで転生の回数が 1 違うとクリア率・隠れていた割合がどれだけ変わるか、何ステージぶんの難しさに当たるか、周ごとの進む速さ
  - 置いた物（壁・宝石の道具）とクリア率、お宝・宝石の集まり方
標準ライブラリだけ。python Tools/AgentPlaytest/analyze.py で作る（起動スクリプトが 8 時間ごとに作る）。
"""

import glob
import json
import math
import os
import time

PERSONAS = ["stay", "near", "greedy", "builder", "nowall"]
PERSONA_JA = {"stay": "慎重型（動かない）", "near": "慎重型（近くのお宝）", "greedy": "強欲型", "builder": "建築大好き", "nowall": "壁なし"}
ONI_JA = {"Balanced": "標準鬼", "Scout": "スピード鬼", "Breaker": "パワー鬼", "Careful": "慎重鬼", "Treasure": "宝物鬼", "Detector": "探知鬼"}


# ---------------------------------------------------------------- 読み込み
def _jsonl(path):
    out = []
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    out.append(json.loads(line))
                except ValueError:
                    pass
    return out


def load_all(root):
    """root（Saved/AgentPlaytest）の下の記録のフォルダを全部読む。ラウンドは (性格, 時刻, 何ラウンド目か) で同じ物を除く"""
    rounds, events = [], []
    seen = set()
    for run in sorted(glob.glob(os.path.join(root, "[0-9]" * 8 + "-" + "[0-9]" * 4))):
        for p in PERSONAS:
            for r in _jsonl(os.path.join(run, p, "rounds.jsonl")):
                key = (p, r.get("time"), r.get("round_total"), r.get("stage"))
                if key in seen:
                    continue
                seen.add(key)
                r["_persona"] = p
                r["_run"] = os.path.basename(run)
                rounds.append(r)
            for e in _jsonl(os.path.join(run, p, "events.jsonl")):
                e["_persona"] = p
                e["_run"] = os.path.basename(run)
                events.append(e)
    rounds = [r for r in rounds if isinstance(r.get("stage"), int) and r["stage"] >= 1]
    return rounds, events


# ---------------------------------------------------------------- 小さな計算
def _inv(m):
    n = len(m)
    a = [list(row) + [1.0 if i == j else 0.0 for j in range(n)] for i, row in enumerate(m)]
    for c in range(n):
        piv = max(range(c, n), key=lambda r: abs(a[r][c]))
        if abs(a[piv][c]) < 1e-12:
            a[piv][c] = 1e-12
        a[c], a[piv] = a[piv], a[c]
        d = a[c][c]
        a[c] = [v / d for v in a[c]]
        for r in range(n):
            if r != c and a[r][c] != 0.0:
                f = a[r][c]
                a[r] = [v - f * w for v, w in zip(a[r], a[c])]
    return [row[n:] for row in a]


def sigmoid(z):
    return 1.0 / (1.0 + math.exp(-max(-30.0, min(30.0, z))))


def logistic_fit(xs, ys, l2=0.5, iters=30):
    """ロジスティック回帰（ニュートン法・弱い L2）。xs は先頭に 1 を含む特徴の列"""
    d = len(xs[0])
    w = [0.0] * d
    for _ in range(iters):
        g = [0.0] * d
        h = [[0.0] * d for _ in range(d)]
        for x, y in zip(xs, ys):
            p = sigmoid(sum(a * b for a, b in zip(w, x)))
            for i in range(d):
                g[i] += (y - p) * x[i]
                for j in range(d):
                    h[i][j] += p * (1 - p) * x[i] * x[j]
        for i in range(1, d):
            g[i] -= l2 * w[i]
            h[i][i] += l2
        hi = _inv(h)
        step = [sum(hi[i][j] * g[j] for j in range(d)) for i in range(d)]
        w = [a + b for a, b in zip(w, step)]
        if max(abs(s) for s in step) < 1e-6:
            break
    # 係数のばらつき（標準誤差）
    h = [[0.0] * d for _ in range(d)]
    for x in xs:
        p = sigmoid(sum(a * b for a, b in zip(w, x)))
        for i in range(d):
            for j in range(d):
                h[i][j] += p * (1 - p) * x[i] * x[j]
    for i in range(1, d):
        h[i][i] += l2
    cov = _inv(h)
    se = [math.sqrt(max(0.0, cov[i][i])) for i in range(d)]
    return w, se


def linear_fit(xs, ys):
    """最小二乗（xs は先頭に 1 を含む）"""
    d = len(xs[0])
    xtx = [[sum(x[i] * x[j] for x in xs) + (1e-6 if i == j else 0.0) for j in range(d)] for i in range(d)]
    xty = [sum(x[i] * y for x, y in zip(xs, ys)) for i in range(d)]
    inv = _inv(xtx)
    return [sum(inv[i][j] * xty[j] for j in range(d)) for i in range(d)]


def smooth_rate(k, n, prior=0.3, strength=2.0):
    return (k + prior * strength) / (n + strength)


def pct(x):
    return "-" if x is None else f"{x * 100:.0f}%"


def fmt_num(v):
    if v is None:
        return "-"
    a = abs(v)
    for unit, div in (("兆", 1e12), ("億", 1e8), ("万", 1e4)):
        if a >= div:
            return f"{v / div:.1f}{unit}"
    return f"{v:.0f}"


def _features(stage, prestige, persona):
    return [1.0, float(stage), float(min(prestige, 12)), 1.0 if persona == "near" else 0.0, 1.0 if persona == "greedy" else 0.0,
            1.0 if persona == "builder" else 0.0, 1.0 if persona == "nowall" else 0.0]


# ---------------------------------------------------------------- 分析
def analyze(root):
    rounds, events = load_all(root)
    A = {"rounds": rounds, "events": events, "made": time.time()}
    if not rounds:
        return A
    # ---- ステージごと
    by_stage = {}
    for r in rounds:
        s = r["stage"]
        b = by_stage.setdefault(s, {"n": 0, "clear": 0, "surv": 0.0, "chased": 0, "broken": 0.0, "earned": [], "tgot": 0, "ttot": 0,
                                    "onis": {}, "catchers": {}, "maps": {}, "personas": {}, "prestiges": {}, "walls_placed": 0,
                                    "gems": 0.0, "limit": []})
        b["n"] += 1
        c = bool(r.get("cleared_true"))
        b["clear"] += c
        lim = r.get("limit_true") or r.get("limit") or 30
        b["limit"].append(lim)
        b["surv"] += min(1.0, (r.get("survived") or 0) / max(1.0, lim))
        b["chased"] += 1 if r.get("chased") else 0
        try:
            b["broken"] += float(r.get("walls_broken") or 0)
        except (TypeError, ValueError):
            pass
        if r.get("earned") is not None:
            try:
                b["earned"].append(float(r["earned"]))
            except (TypeError, ValueError):
                pass
        b["tgot"] += r.get("treasures_got") or 0
        b["ttot"] += r.get("treasures_total") or 0
        for t in set(r.get("onis_true") or []):
            b["onis"][t] = b["onis"].get(t, 0) + 1
        if r.get("caught_by_true"):
            b["catchers"][r["caught_by_true"]] = b["catchers"].get(r["caught_by_true"], 0) + 1
        if r.get("map"):
            b["maps"][r["map"]] = b["maps"].get(r["map"], 0) + 1
        pp = b["personas"].setdefault(r["_persona"], [0, 0])
        pp[0] += 1
        pp[1] += c
        k = r.get("prestige") or 0
        pk = b["prestiges"].setdefault(k, [0, 0])
        pk[0] += 1
        pk[1] += c
    A["by_stage"] = by_stage
    # ---- ロジスティック回帰：クリア率 = σ(a + b・ステージ + c・転生 + 性格)
    xs = [_features(r["stage"], r.get("prestige") or 0, r["_persona"]) for r in rounds]
    ys = [1.0 if r.get("cleared_true") else 0.0 for r in rounds]
    w, se = logistic_fit(xs, ys)
    A["logit"] = (w, se)
    # 隠れていた割合の線形の傾き
    sv = [min(1.0, (r.get("survived") or 0) / max(1.0, r.get("limit_true") or r.get("limit") or 30)) for r in rounds]
    A["surv_lin"] = linear_fit(xs, sv)
    # 稼ぎ：log10(1 ラウンドに稼いだコイン) をステージ・転生で
    ex, ey = [], []
    for r in rounds:
        try:
            e = float(r.get("earned") or 0)
        except (TypeError, ValueError):
            continue
        if e > 0:
            ex.append(_features(r["stage"], r.get("prestige") or 0, r["_persona"]))
            ey.append(math.log10(e))
    A["earn_lin"] = linear_fit(ex, ey) if len(ex) >= 10 else None
    # ---- 転生：同じステージ・同じ性格で、転生の回数が k と k+1 のクリア率の差
    pairs = []
    cell = {}
    for r in rounds:
        key = (r["_persona"], r["stage"], r.get("prestige") or 0)
        c = cell.setdefault(key, [0, 0, 0.0])
        c[0] += 1
        c[1] += 1 if r.get("cleared_true") else 0
        c[2] += min(1.0, (r.get("survived") or 0) / max(1.0, r.get("limit_true") or 30))
    for (p, s, k), c in cell.items():
        nxt = cell.get((p, s, k + 1))
        if nxt and c[0] >= 2 and nxt[0] >= 2:
            pairs.append((k, s, p, nxt[1] / nxt[0] - c[1] / c[0], nxt[2] / nxt[0] - c[2] / c[0], min(c[0], nxt[0])))
    A["prestige_pairs"] = pairs
    # ---- 周（転生の回数）ごとの進む速さ：その周で各ステージに着くまでの遊んだ時間（分）とラウンド数
    laps = {}
    for p in PERSONAS:
        rs = [r for r in rounds if r["_persona"] == p]
        rs.sort(key=lambda r: (r["_run"], r.get("round_total") or 0))
        start = {}
        for i, r in enumerate(rs):
            k = r.get("prestige") or 0
            if k not in start:
                start[k] = (r.get("elapsed") or 0, i)
            L = laps.setdefault((p, k), {"best": 0, "reach": {}})
            s = r["stage"]
            L["best"] = max(L["best"], s + (1 if r.get("cleared_true") else 0))
            if s not in L["reach"]:
                L["reach"][s] = ((r.get("elapsed") or 0) - start[k][0], i - start[k][1])
    A["laps"] = laps
    pres_ev = [e for e in events if e.get("type") == "prestige"]
    A["prestige_events"] = pres_ev
    pt_bought = {}
    for e in events:
        if e.get("type") == "shop":
            for n in e["data"].get("prestige_bought", []):
                pt_bought[n] = pt_bought.get(n, 0) + 1
    A["pt_bought"] = pt_bought
    # ---- 置いた物とクリア率
    walls = {}
    for e in events:
        if e.get("type") == "build":
            log = e["data"].get("log") or {}
            st = e["data"].get("stage")
            # placed は今回新しく置いた数（前のラウンドの壁が残っていれば 0 になる）。standing は立っている壁（古い記録には無い）
            walls.setdefault(e["_persona"], []).append((st, log.get("placed", 0), log.get("standing")))
    A["walls"] = walls
    tmpl = {}
    for r in rounds:
        t = (r.get("plan") or {}).get("template") or "-"
        x = tmpl.setdefault((r["_persona"], t), [0, 0])
        x[0] += 1
        x[1] += 1 if r.get("cleared_true") else 0
    A["templates"] = tmpl
    gem = {}
    for r in rounds:
        for g in r.get("gem_items") or []:
            x = gem.setdefault(g, [0, 0])
            x[0] += 1
            x[1] += 1 if r.get("cleared_true") else 0
    A["gem_use"] = gem
    gem_buys = {}
    for e in events:
        if e.get("type") == "shop":
            for n in e["data"].get("gem_bought", []):
                gem_buys[n] = gem_buys.get(n, 0) + 1
    A["gem_buys"] = gem_buys
    # ---- 鬼の種類ごと
    onis = {}
    for r in rounds:
        for t in set(r.get("onis_true") or []):
            o = onis.setdefault(t, {"rounds": 0, "first": 999, "caught": 0, "clear": 0})
            o["rounds"] += 1
            o["first"] = min(o["first"], r["stage"])
            o["clear"] += 1 if r.get("cleared_true") else 0
        if r.get("caught_by_true"):
            onis.setdefault(r["caught_by_true"], {"rounds": 0, "first": 999, "caught": 0, "clear": 0})["caught"] += 1
    A["onis"] = onis
    return A


def predict_clear(w, stage, prestige, persona):
    return sigmoid(sum(a * b for a, b in zip(w, _features(stage, prestige, persona))))


# ---------------------------------------------------------------- 文にする
def markdown(A, root):
    L = ["# バランスの推論（かせぐんぼ）", ""]
    L.append(f"作った時刻：{time.strftime('%Y-%m-%d %H:%M', time.localtime(A['made']))}　記録：`{root}` の全部のフォルダ")
    L.append("")
    rounds = A["rounds"]
    if not rounds:
        L.append("まだ記録がありません。")
        return "\n".join(L) + "\n"
    L.append("テストプレイ AI（3 つの性格）の記録を全部まとめ、ステージ・転生を重ねるとどうなるかを推論する。"
             "遊ぶ AI と違って記録用の本当の値（ステージ・転生の回数・鬼の種類・稼ぎ）も使う。ゲームの仕様（CSV）は読まず、遊んだ結果だけから推論する。"
             "AI は人より上手な所も下手な所もあるので、数字は目安（何回遊んだかも一緒に書く）。")
    L.append("")
    # ---- データの量
    L.append("## データの量")
    L.append("")
    L.append("| 性格 | ラウンド | 遊んだ時間 | 転生の回数（最大） | 一番先のステージ |")
    L.append("|---|---|---|---|---|")
    for p in PERSONAS:
        rs = [r for r in rounds if r["_persona"] == p]
        if not rs:
            continue
        hours = sum(max(r.get("elapsed") or 0 for r in rs if r["_run"] == run) for run in {r["_run"] for r in rs}) / 3600.0
        L.append(f"| {PERSONA_JA[p]} | {len(rs)} | {hours:.1f} 時間 | {max(r.get('prestige') or 0 for r in rs)} | {max(r['stage'] for r in rs)} |")
    L.append("")
    w, se = A["logit"]
    a, b, c, dn, dg, db, dw = w
    # ---- 推論のまとめ（先頭に）
    L.append("## 推論のまとめ")
    L.append("")
    for line in conclusions(A):
        L.append(f"- {line}")
    L.append("")
    # ---- Level カーブ
    L.append("## Level カーブ（全体の傾き）")
    L.append("")
    L.append(f"クリア率 = σ({a:+.2f} {b:+.3f}×ステージ {c:+.3f}×転生の回数 ＋ 性格（近くのお宝 {dn:+.2f}・強欲 {dg:+.2f}・建築大好き {db:+.2f}・壁なし {dw:+.2f}））"
             f"　※ σ はロジスティック関数。係数のばらつき（±1 標準誤差）：ステージ ±{se[1]:.3f}・転生 ±{se[2]:.3f}")
    L.append("")
    L.append("ステージ・転生の回数ごとのクリア率の推論（慎重型（動かない）。太字は遊んだことのある組み合わせ。カッコは 1 回クリアするのに要る挑戦の回数の目安）：")
    L.append("")
    stages = [1, 3, 5, 8, 10, 13, 15, 18, 20, 23, 25, 28, 30]
    seen = {(r["stage"], r.get("prestige") or 0) for r in rounds}
    L.append("| 転生 \\ ステージ | " + " | ".join(str(s) for s in stages) + " |")
    L.append("|---|" + "---|" * len(stages))
    for k in (0, 1, 2, 3, 5, 8, 10):
        cells = []
        for s in stages:
            pr = predict_clear(w, s, k, "stay")
            t = f"{pr * 100:.0f}%（{min(99.0, 1.0 / max(pr, 1e-3)):.0f}）"
            cells.append(f"**{t}**" if (s, k) in seen else t)
        L.append(f"| {k} 回 | " + " | ".join(cells) + " |")
    L.append("")
    # ---- ステージごとの特徴と傾向・難しさ
    L.append("## ステージごとの特徴と傾向・難しさ")
    L.append("")
    L.append("難しさ＝1 回クリアするのに要る挑戦の回数（クリア率の逆数。少ないときは 30% を見込みに混ぜてならす）。"
             "傾きとの差＝全体の傾き（上の式。そのステージで遊んだときの転生の回数・性格で）から推論したクリア率との差。−10 ポイントより低ければ「急に難しい」、＋15 より高ければ「簡単」。")
    L.append("")
    L.append("| ステージ | 挑戦 | クリア率 | 難しさ | 傾きとの差 | 隠れていた割合 | 追われた | 壊された壁 | 1 ラウンドの稼ぎ | お宝を取った割合 | 出てきた鬼 | よく見つけた鬼 | 性格ごとのクリア率 |")
    L.append("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    spikes = []
    for s in sorted(A["by_stage"]):
        B = A["by_stage"][s]
        n = B["n"]
        rate = smooth_rate(B["clear"], n)
        rs = [r for r in rounds if r["stage"] == s]
        pred = sum(predict_clear(w, s, r.get("prestige") or 0, r["_persona"]) for r in rs) / n
        diff = B["clear"] / n - pred
        tag = ""
        if n >= 6 and diff <= -0.10:
            tag = "（急に難しい）"
            spikes.append((s, diff, n, "hard"))
        elif n >= 6 and diff >= 0.15:
            tag = "（簡単）"
            spikes.append((s, diff, n, "easy"))
        oni = "・".join(f"{ONI_JA.get(t, t)}" for t, _ in sorted(B["onis"].items(), key=lambda p: -p[1])[:4]) or "-"
        catcher = max(B["catchers"].items(), key=lambda p: p[1])[0] if B["catchers"] else None
        earned = sorted(B["earned"])[len(B["earned"]) // 2] if B["earned"] else None
        per = "・".join(f"{'w' if p == 'nowall' else p[0]}{pr[1] * 100 // max(1, pr[0])}%" for p, pr in ((q, B["personas"][q]) for q in PERSONAS if q in B["personas"]))
        L.append(f"| {s} | {n} | {pct(B['clear'] / n)} | {1.0 / rate:.1f} 回 | {diff * 100:+.0f}{tag} | {pct(B['surv'] / n)} | {pct(B['chased'] / n)} | "
                 f"{B['broken'] / n:.1f} | {fmt_num(earned)} | {pct(B['tgot'] / B['ttot']) if B['ttot'] else '-'} | {oni} | "
                 f"{ONI_JA.get(catcher, catcher) if catcher else '-'} | {per} |")
    A["spikes"] = spikes
    L.append("")
    L.append("性格の記号：s＝慎重型（動かない）・n＝慎重型（近くのお宝）・g＝強欲型・b＝建築大好き・w＝壁なし")
    L.append("")
    # ---- 転生
    L.append("## 転生による強化幅")
    L.append("")
    if c:
        per_stage = c / -b if b < 0 else None
        L.append(f"- 全体の傾きでは、転生 1 回でクリアのしやすさ（対数オッズ）が {c:+.3f}。"
                 + (f"ステージが 1 進むと {b:+.3f} なので、**転生 1 回 ≒ {per_stage:.2f} ステージぶん楽になる**。" if per_stage is not None else ""))
    pairs = A["prestige_pairs"]
    if pairs:
        tot = sum(p[5] for p in pairs)
        dclear = sum(p[3] * p[5] for p in pairs) / tot
        dsurv = sum(p[4] * p[5] for p in pairs) / tot
        L.append(f"- 同じ性格・同じステージで転生の回数が 1 違うラウンドをくらべると（{len(pairs)} 組）：クリア率 {dclear * 100:+.1f} ポイント・隠れていた割合 {dsurv * 100:+.1f} ポイント")
    L.append("")
    L.append("周ごとの進み方（その周で着いた一番先のステージ・ステージ 5 / 10 / 15 / 20 / 25 に着くまでのラウンド数）：")
    L.append("")
    L.append("| 性格 | 周（転生の回数） | 一番先 | 5 | 10 | 15 | 20 | 25 |")
    L.append("|---|---|---|---|---|---|---|---|")
    for (p, k), Lp in sorted(A["laps"].items()):
        cols = []
        for s in (5, 10, 15, 20, 25):
            v = Lp["reach"].get(s)
            cols.append(f"{v[1]}" if v else "-")
        L.append(f"| {PERSONA_JA[p]} | {k} 回 | {Lp['best']} | " + " | ".join(cols) + " |")
    L.append("")
    if A["pt_bought"]:
        L.append("転生のお店で買った物：" + "、".join(f"{n} {v}" for n, v in sorted(A["pt_bought"].items(), key=lambda p: -p[1])))
        L.append("")
    # ---- 置いた物
    L.append("## 置いた物とクリア率")
    L.append("")
    for p, lst in A["walls"].items():
        zero = sum(1 for (s, n, sd) in lst if not n)
        line = (f"- {PERSONA_JA[p]}：設置パート {len(lst)} 回のうち新しく壁を置かなかったのは {zero} 回（{zero * 100 // max(1, len(lst))}%）・"
                f"新しく置いた数の平均 {sum(n for s, n, sd in lst) / max(1, len(lst)):.1f} 個")
        sl = [sd for (s, n, sd) in lst if sd is not None]
        if sl:
            sz = sum(1 for sd in sl if not sd)
            line += f"／立っている壁が 0 個だったのは {sz} 回（{sz * 100 // len(sl)}%・記録のある {len(sl)} 回のうち）・平均 {sum(sl) / len(sl):.1f} 個"
        L.append(line)
    L.append("")
    L.append("（新しく置いた数は、前のラウンドの壁が残っていると 0 になる。壁を使っているかは「立っている壁」で見る）")
    L.append("")
    L.append("| 性格 | 囲い方 | ラウンド | クリア率 |")
    L.append("|---|---|---|---|")
    TJ = {"open": "壁を置かない", "door_far": "囲んで出入り口 1 つ", "double": "二重の囲い", "decoy_far": "囲わず罠", "sealed": "全部閉じる"}
    for (p, t), (n, k) in sorted(A["templates"].items()):
        if n >= 5:
            L.append(f"| {PERSONA_JA[p]} | {TJ.get(t, t)} | {n} | {pct(k / n)} |")
    L.append("")
    if A["gem_buys"] or A["gem_use"]:
        L.append("宝石のお店の道具：買った回数 " + ("、".join(f"{n} {v}" for n, v in A["gem_buys"].items()) or "0")
                 + "／使ったラウンドのクリア率 " + ("、".join(f"{n} {v[1] * 100 // max(1, v[0])}%（{v[0]}）" for n, v in A["gem_use"].items()) or "-"))
    else:
        L.append("宝石のお店の道具：まだ使ったラウンドが無い（統計が取れない）")
    L.append("")
    # ---- 鬼
    L.append("## 鬼の種類ごと")
    L.append("")
    L.append("| 鬼 | 初めて出たステージ | 出たラウンド | そのラウンドのクリア率 | 見つけた回数 |")
    L.append("|---|---|---|---|---|")
    for t, o in sorted(A["onis"].items(), key=lambda p: p[1]["first"]):
        if o["rounds"]:
            L.append(f"| {ONI_JA.get(t, t)} | {o['first']} | {o['rounds']} | {pct(o['clear'] / o['rounds'])} | {o['caught']} |")
    L.append("")
    return "\n".join(L) + "\n"


def conclusions(A):
    """推論した結論を文にする（数字の裏付け付き）"""
    out = []
    w, se = A["logit"]
    a, b, c, dn, dg, db, dw = w
    rounds = A["rounds"]
    # 難しくなる速さ
    if b < 0:
        half = None
        for s in range(1, 61):
            if predict_clear(w, s, 0, "stay") < 0.5:
                half = s
                break
        out.append(f"ステージが 1 進むごとにクリアしにくくなる（対数オッズ {b:+.3f}／ステージ）。転生 0 回だと、クリア率が 50% を切るのは "
                   + (f"ステージ {half} ごろ" if half else "ステージ 60 より先") + "。")
    elif b >= 0:
        out.append(f"ステージが進んでもクリア率が下がっていない（対数オッズ {b:+.3f}／ステージ）。後半が簡単すぎるか、転生の強化・お金の伸びが強すぎる見込み"
                   "（ただし先のステージに着くのは強くなった後なので、傾きは甘く出やすい）。")
    # 転生でどこまで行けるか
    if c > 0 and b < 0:
        need = {}
        for target in (15, 20, 25, 30):
            k = (-(a + b * target)) / c
            need[target] = k
        txt = "・".join(f"ステージ {t} は {max(0.0, k):.1f} 回" for t, k in need.items())
        out.append(f"転生 1 回は約 {c / -b:.2f} ステージぶん楽になる。クリア率 50% にするのに要る転生の回数の推論：{txt}。"
                   + ("転生を重ねても最後のステージまで届きにくい（強化幅が小さい）。" if need[25] > 15 else "")
                   + ("転生の強化が強く、少ない転生で最後まで行ける。" if need[25] < 2 else ""))
    elif c <= 0:
        out.append(f"転生の回数が増えてもクリア率が上がっていない（対数オッズ {c:+.3f}／回）。転生のお店の強化が今の遊び方に効いていない見込み。")
    # 急に難しいステージ
    spikes = A.get("spikes") or []
    hard = [s for s in spikes if s[3] == "hard"]
    easy = [s for s in spikes if s[3] == "easy"]
    if hard:
        out.append("全体の傾きより急に難しいステージ：" + "、".join(f"{s}（{d * 100:+.0f} ポイント・{n} 回）" for s, d, n, _ in sorted(hard, key=lambda x: x[1])[:5]))
    if easy:
        out.append("全体の傾きより簡単なステージ：" + "、".join(f"{s}（{d * 100:+.0f} ポイント・{n} 回）" for s, d, n, _ in sorted(easy, key=lambda x: -x[1])[:5]))
    # 性格
    out.append(f"性格の差（慎重型（動かない）とくらべた対数オッズ）：近くのお宝 {dn:+.2f}・強欲 {dg:+.2f}・建築大好き {db:+.2f}・壁なし {dw:+.2f}。"
               + ("お宝を取りに動くほど見つかりやすい。" if dg < -0.3 else "お宝を取りに動いても不利になっていない。"))
    # 稼ぎ
    el = A.get("earn_lin")
    if el:
        out.append(f"1 ラウンドの稼ぎはステージ 1 つで ×{10 ** el[1]:.2f}、転生 1 回で ×{10 ** el[2]:.2f}（中央値の傾き）。")
    # 壁
    walls = A["walls"]
    alls = [sd for lst in walls.values() for (s, n, sd) in lst if sd is not None]
    allw = alls or [n for lst in walls.values() for (s, n, sd) in lst]
    if allw:
        zero = sum(1 for n in allw if not n) / len(allw)
        t = A["templates"]
        def rate(name):
            n = sum(v[0] for (p, k), v in t.items() if k == name)
            k = sum(v[1] for (p, kk), v in t.items() if kk == name)
            return (k / n, n) if n else (None, 0)
        ro, no = rate("open")
        rw = [rate(x) for x in ("door_far", "double")]
        rw_rate = sum(r * n for r, n in rw if r is not None) / max(1, sum(n for r, n in rw if r is not None))
        what = "壁が 1 つも立っていない" if alls else "新しく壁を置いていない（前のラウンドの壁が残っている分を含まない）"
        out.append(f"設置パートの {zero * 100:.0f}% で{what}。壁を置かない囲い方のクリア率 {pct(ro)}・壁で囲む囲い方 {rw_rate * 100:.0f}%。"
                   + ("壁で囲んでも有利になっておらず、壁を強くする転生の強化（壁の硬さ）の意味が薄い。" if ro is not None and ro >= rw_rate else "壁で囲むと有利。"))
    # お宝・宝石
    tg = sum(r.get("treasures_got") or 0 for r in rounds)
    tt = sum(r.get("treasures_total") or 0 for r in rounds)
    if tt:
        out.append(f"お宝を取った割合は全体で {tg * 100 // tt}%。"
                   + ("お宝（宝石）は取りに行きにくく、宝石のお店の道具を買う数がたまりにくい。" if tg / tt < 0.3 else ""))
    if not A["gem_use"]:
        out.append("宝石のお店の道具を使ったラウンドがまだ無いので、道具の効き目は推論できない。")
    return out


def html(md):
    """Markdown を簡単な HTML にする（表・見出し・箇条書きだけ）"""
    import html as H
    out = ["<!doctype html><html lang=ja><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>",
           "<title>バランスの推論</title><style>body{font-family:system-ui,sans-serif;max-width:1100px;margin:0 auto;padding:16px;line-height:1.6;background:#fff;color:#222}"
           "table{border-collapse:collapse;display:block;overflow-x:auto;font-size:13px}td,th{border:1px solid #ccc;padding:3px 6px;white-space:nowrap}"
           "th{background:#f3f3f3}@media(prefers-color-scheme:dark){body{background:#181818;color:#ddd}th{background:#2a2a2a}td,th{border-color:#444}}</style><body>"]
    in_table = False
    in_list = False
    for line in md.split("\n"):
        esc = H.escape(line)
        esc = __import__("re").sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", esc)
        esc = __import__("re").sub(r"`(.+?)`", r"<code>\1</code>", esc)
        if line.startswith("|"):
            if set(line.replace("|", "").strip()) <= {"-"}:
                continue
            cells = [c.strip() for c in esc.strip("|").split("|")]
            tag = "th" if not in_table else "td"
            if not in_table:
                out.append("<table>")
                in_table = True
            out.append("<tr>" + "".join(f"<{tag}>{c}</{tag}>" for c in cells) + "</tr>")
            continue
        if in_table:
            out.append("</table>")
            in_table = False
        if line.startswith("- "):
            if not in_list:
                out.append("<ul>")
                in_list = True
            out.append(f"<li>{esc[2:]}</li>")
            continue
        if in_list:
            out.append("</ul>")
            in_list = False
        if line.startswith("## "):
            out.append(f"<h2>{esc[3:]}</h2>")
        elif line.startswith("# "):
            out.append(f"<h1>{esc[2:]}</h1>")
        elif line.strip():
            out.append(f"<p>{esc}</p>")
    if in_table:
        out.append("</table>")
    if in_list:
        out.append("</ul>")
    return "\n".join(out) + "</body></html>\n"


def generate(root):
    """root/analysis/balance_<日時>.md・.html を新しく作る（前の分析は残す）。latest.md・latest.html も同じ中身で上書き"""
    A = analyze(root)
    md = markdown(A, root)
    out_dir = os.path.join(root, "analysis")
    os.makedirs(out_dir, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M")
    paths = []
    for name in (f"balance_{stamp}", "latest"):
        for ext, body in (("md", md), ("html", html(md))):
            path = os.path.join(out_dir, f"{name}.{ext}")
            with open(path, "w", encoding="utf-8") as f:
                f.write(body)
            paths.append(path)
    return paths[0]
