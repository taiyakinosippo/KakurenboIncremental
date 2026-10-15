"""AI が攻略で大事なことに「気づいた」瞬間を見つけて記録する（ユーザーの希望：どのくらいで気づくか知りたい）。

学んだ中身（brain）をラウンドの終わりごとに見て、はっきりしてきたことを 1 回だけ書く（遊んだ時間・周・ステージ・何ラウンド目か付き）。
前に気づいたことと逆になったら「考えが変わった」として、もう一度書く。
"""

from .report import FEATURE_JA, TEMPLATE_JA
from .world import SPOT_FEATURES

ACTION_KEYS = ["MASH", "QUIET", "FLEE", "RETURN", "FETCH", "SMOKE", "ITEM", "EXPLORE"]


def _opposite(key):
    """「+」と「-」、「oni」と「treasure」などの逆の気づきの key"""
    for a, b in (("+", "-"), (":oni", ":treasure"), ("good", "bad")):
        if key.endswith(a):
            return key[: -len(a)] + b
        if key.endswith(b):
            return key[: -len(b)] + a
    return None


class InsightTracker:
    def __init__(self, brain, recorder):
        self.brain = brain
        self.rec = recorder

    def note(self, key, text):
        known = self.brain.insights
        if key in known:
            return
        opp = _opposite(key)
        if opp and opp in known:
            text = "（考えが変わった）" + text
        known[key] = round(self.rec.elapsed(), 1)
        self.rec.insight(key, text)

    def check(self):
        b = self.brain
        # ---- 音の聞き分け
        for name in list(b.sound_id):
            if name.startswith("_"):
                continue
            kind = b.sound_kind(name)
            lifts = b.sound_lifts(name)
            if kind == "oni" and lifts:
                self.note(f"sound:{name}:oni", f"「{name}」の音は鬼の足音・声だと聞き分けた（その方角に鬼がいたのは、ふだんの {lifts[0]:.1f} 倍）。近いときは連打をやめる")
            elif kind == "treasure" and lifts:
                self.note(f"sound:{name}:treasure", f"「{name}」の音はお宝の音だと聞き分けた（その方角にお宝があったのは、ふだんの {lifts[1]:.1f} 倍）。音のする方へ取りに行ける")
        # ---- 隠れ場所の選び方
        if b.spot.n >= 20:
            for feat, w in zip(SPOT_FEATURES, b.spot.mean()):
                if feat == "bias" or abs(w) < 0.15:
                    continue
                sign = "+" if w > 0 else "-"
                self.note(f"spot:{feat}{sign}", f"隠れ場所は「{FEATURE_JA.get(feat, feat)}」だと{'隠れやすい' if w > 0 else '見つかりやすい'}（重み {w:+.2f}）")
        # ---- 囲い方
        stats = {k: v for k, v in b.template_stats.items() if v[0] >= 5}
        if len(stats) >= 2:
            ranked = sorted(stats.items(), key=lambda p: -p[1][1] / p[1][0])
            (k1, (n1, s1)), (k2, (n2, s2)) = ranked[0], ranked[1]
            if s1 / n1 - s2 / n2 >= 0.08:
                self.note(f"tmpl:{k1}", f"囲い方は「{TEMPLATE_JA.get(k1, k1)}」が一番良い（得点の平均 {s1 / n1:.2f}、次の「{TEMPLATE_JA.get(k2, k2)}」は {s2 / n2:.2f}）")
        # ---- かくれんぼ中の動き（Q の値を、鬼の近さ・追われているかでまとめて比べる）
        def avg_q(prefixes, action):
            i = ACTION_KEYS.index(action)
            tot, n = 0.0, 0
            for s, vals in b.qtable.q.items():
                if any(s.startswith(p) for p in prefixes) and i < len(vals) and b.qtable.n.get(s, [0] * 8)[i] >= 3:
                    tot += vals[i] * b.qtable.n[s][i]
                    n += b.qtable.n[s][i]
            return (tot / n, n) if n else (None, 0)
        for prefixes, label in ((("T2", "T3"), "鬼が近いとき"), (("T0",), "鬼の気配が無いとき")):
            qm, nm = avg_q(prefixes, "MASH")
            qq, nq = avg_q(prefixes, "QUIET")
            if qm is not None and qq is not None and nm >= 60 and nq >= 60:
                if qq - qm >= 0.15:
                    self.note(f"q:{label}:quietgood", f"{label}は、連打より静かにしゃがむ方が長く隠れられる")
                elif qm - qq >= 0.15:
                    self.note(f"q:{label}:mashgood", f"{label}は、静かにするより連打して稼いでも大丈夫")
        qf, nf = avg_q(("T1C1", "T2C1", "T3C1"), "FLEE")
        qs, ns = avg_q(("T1C1", "T2C1", "T3C1"), "QUIET")
        if qf is not None and qs is not None and nf >= 30 and ns >= 30:
            if qf - qs >= 0.15:
                self.note("q:chased:fleegood", "追われたら、その場でしゃがむより逃げる方が良い")
            elif qs - qf >= 0.15:
                self.note("q:chased:fleebad", "追われても、逃げるよりその場でしゃがむ方が良い")
        # ---- 鬼（見て分かったこと）
        onis = {k: v for k, v in b.onis.items() if not k.startswith("_")}
        total_caught = sum(v.get("caught_me", 0) for v in onis.values())
        for name, p in onis.items():
            seen = p.get("seen", 0)
            if seen >= 80 and p.get("hitting", 0) / seen >= 0.08:
                self.note(f"oni:{name}:breaks", f"「{name}」は壁を壊しに来る（見ていた間の {p['hitting'] * 100 // seen}% は壁をこわしていた）")
            if p.get("speed_n", 0) >= 80:
                spd = p["speed_sum"] / p["speed_n"]
                if spd >= 5.0:
                    self.note(f"oni:{name}:fast", f"「{name}」は足が速い（平均 {spd:.1f} マス/秒）")
            met = sum(1 for v in onis.values() if v.get("rounds", 0) >= 3)  # 3 ラウンド以上会った鬼の種類（1 種類なら比べられない）
            if met >= 2 and total_caught >= 8 and p.get("caught_me", 0) >= 5 and p["caught_me"] / total_caught >= 0.4:
                self.note(f"oni:{name}:catcher", f"「{name}」に一番よく見つかる（見つかったうちの {p['caught_me'] * 100 // total_caught}%）")
        # ---- お店の商品
        for name, rec in b.items.items():
            if rec.get("n", 0) >= 8:
                if rec["mean"] >= 0.08:
                    self.note(f"item:{name}:good", f"「{name}」を買った後のラウンドは隠れやすい（基準より {rec['mean']:+.2f}）")
                elif rec["mean"] <= -0.08:
                    self.note(f"item:{name}:bad", f"「{name}」を買っても隠れやすくならない（基準より {rec['mean']:+.2f}）")
        # ---- 宝石のお店の道具
        hist = b.history[-300:]
        overall = sum(hist) / len(hist) if hist else None
        for name, rec in b.gem_trials.items():
            if rec.get("rounds", 0) >= 3 and overall is not None:
                avg = rec["score_sum"] / rec["rounds"]
                if avg - overall >= 0.1:
                    self.note(f"gem:{name}:good", f"宝石の道具「{name}」を使うと隠れやすい（得点の平均 {avg:.2f}、ふだん {overall:.2f}）")
                elif overall - avg >= 0.1:
                    self.note(f"gem:{name}:bad", f"宝石の道具「{name}」はあまり効かない（得点の平均 {avg:.2f}、ふだん {overall:.2f}）")
        # ---- 転生のタイミング
        arms = {k: v for k, v in b.prestige.items() if v.get("n", 0) >= 2}
        if len(arms) >= 2:
            best = max(arms, key=lambda k: arms[k]["mean"])
            self.note(f"prestige:{best}", f"転生は、同じステージで {best} 回続けて見つかったらするのが良い（その周で先へ進めた）")
