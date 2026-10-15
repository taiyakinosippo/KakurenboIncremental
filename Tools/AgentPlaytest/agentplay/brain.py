"""学習する部分（強化学習）。得点は「長く隠れられたか」（隠れた秒数 ÷ 制限時間。逃げ切りで上乗せ）。

  - 隠れ場所の良さ      ：特徴（world.SPOT_FEATURES）の線形モデル。ベイズ線形回帰＋トンプソン抽出で「試しつつ良い所を選ぶ」
  - 囲い方（設計の型）  ：型ごとに同じく線形モデル（文脈＝そのステージの鬼の印）
  - かくれんぼ中の動き  ：表形式の Q 学習（状態＝鬼の近さ・追われているか・拠点からの距離…、行動＝連打・静かに・逃げる…）
  - お店の商品          ：買った後のラウンドの得点で商品ごとの良さを更新（多腕バンディット）
  - 転生のタイミング    ：同じステージで何回つまずいたら転生するか（多腕バンディット）
  - 音の危なさ          ：聞こえた音のすぐ後に鬼が近くに見えたか（音ごとの確率）
  - 鬼の特徴            ：文から読んだ印（事前知識）と、遊んで見た速さ・壁をこわした回数・見つかった回数
すべて brain_<性格>.json に保存し、次の起動で続きから学ぶ。
"""

import json
import math
import os
import random

from .world import SPOT_FEATURES


# ---------------------------------------------------------------- 小さな行列の計算（標準ライブラリだけ）

def _inverse(m):
    n = len(m)
    a = [row[:] + [1.0 if i == j else 0.0 for j in range(n)] for i, row in enumerate(m)]
    for col in range(n):
        piv = max(range(col, n), key=lambda r: abs(a[r][col]))
        a[col], a[piv] = a[piv], a[col]
        p = a[col][col]
        if abs(p) < 1e-12:
            p = 1e-12
        a[col] = [v / p for v in a[col]]
        for r in range(n):
            if r != col and a[r][col] != 0.0:
                f = a[r][col]
                a[r] = [vr - f * vc for vr, vc in zip(a[r], a[col])]
    return [row[n:] for row in a]


def _cholesky(m):
    n = len(m)
    L = [[0.0] * n for _ in range(n)]
    for i in range(n):
        for j in range(i + 1):
            s = sum(L[i][k] * L[j][k] for k in range(j))
            if i == j:
                L[i][j] = math.sqrt(max(m[i][i] - s, 1e-9))
            else:
                L[i][j] = (m[i][j] - s) / L[j][j]
    return L


class BayesLinear:
    """y ≈ w・x のベイズ線形回帰。sample() で w を 1 つ引く（トンプソン抽出）"""

    def __init__(self, d, prior_var=0.5, noise=0.08, prior_mean=None):
        self.d = d
        self.noise = noise
        self.A = [[(1.0 / prior_var if i == j else 0.0) for j in range(d)] for i in range(d)]
        pm = prior_mean or [0.0] * d
        self.b = [pm[i] / prior_var for i in range(d)]
        self.n = 0
        self._cache = None

    def update(self, x, y, weight=1.0):
        for i in range(self.d):
            xi = x[i] * weight / self.noise
            if xi == 0.0:
                continue
            row = self.A[i]
            for j in range(self.d):
                row[j] += xi * x[j]
            self.b[i] += xi * y
        self.n += 1
        self._cache = None

    def _post(self):
        if self._cache is None:
            cov = _inverse(self.A)
            mean = [sum(cov[i][j] * self.b[j] for j in range(self.d)) for i in range(self.d)]
            self._cache = (mean, _cholesky(cov))
        return self._cache

    def mean(self):
        return self._post()[0]

    def sample(self, rng, scale=1.0):
        mean, L = self._post()
        z = [rng.gauss(0.0, 1.0) for _ in range(self.d)]
        return [mean[i] + scale * sum(L[i][k] * z[k] for k in range(i + 1)) for i in range(self.d)]

    @staticmethod
    def predict(w, x):
        return sum(a * b for a, b in zip(w, x))

    def to_json(self):
        return {"d": self.d, "noise": self.noise, "A": self.A, "b": self.b, "n": self.n}

    @classmethod
    def from_json(cls, j):
        m = cls(j["d"], noise=j.get("noise", 0.08))
        m.A, m.b, m.n = j["A"], j["b"], j.get("n", 0)
        return m


# ---------------------------------------------------------------- 囲い方の型

TEMPLATES = {
    "open": "壁を置かない（家具の陰にしゃがむ）",
    "door_far": "まわりを壁で囲み、鬼の門と反対側に出入り口を 1 つ",
    "sealed": "まわりを全部壁で閉じる",
    "double": "二重の囲い（内と外で出入り口をずらす）",
    "decoy_far": "囲わずに、罠・おとりを門と隠れ場所の間に置く",
}
CTX_TAGS = ["breaks_walls", "searches_enclosed", "hears_mash", "hears_steps", "fast", "wide_sight", "summons", "treasure_lover", "systematic", "weak_breaker"]


def template_features(ctx, stage_norm, walls_norm):
    return [1.0, stage_norm, walls_norm] + [ctx.get(t, 0.0) for t in CTX_TAGS]


# ---------------------------------------------------------------- かくれんぼ中の動き（Q 学習）

ACTIONS = ["MASH", "QUIET", "FLEE", "RETURN", "FETCH", "SMOKE", "ITEM", "EXPLORE"]
ACTION_JA = {"MASH": "連打", "QUIET": "静かにしゃがむ", "FLEE": "逃げる", "RETURN": "拠点へ戻る", "FETCH": "お宝を取りに行く",
             "SMOKE": "煙幕", "ITEM": "道具を使う", "EXPLORE": "お宝を探して歩く"}


class QTable:
    def __init__(self):
        self.q = {}
        self.n = {}

    def values(self, s):
        if s not in self.q:
            self.q[s] = [0.0] * len(ACTIONS)
            # 最初の見当：連打は少しだけ得（お金が入る）、静かにも悪くない
            self.q[s][0] = 0.05
            self.n[s] = [0] * len(ACTIONS)
        return self.q[s]

    def choose(self, s, allowed, rng, eps_base=0.25):
        vals = self.values(s)
        visits = sum(self.n[s])
        eps = max(0.03, eps_base / math.sqrt(1.0 + visits / 25.0))
        if rng.random() < eps:
            return rng.choice(allowed)
        return max(allowed, key=lambda a: vals[a] + rng.random() * 1e-6)

    def update(self, s, a, r, s2, allowed2, alpha=0.12, gamma=0.97, terminal=False):
        vals = self.values(s)
        self.n[s][a] += 1
        target = r if terminal or s2 is None else r + gamma * max(self.values(s2)[b] for b in allowed2)
        vals[a] += alpha * (target - vals[a])

    def to_json(self):
        return {"q": {k: v for k, v in self.q.items()}, "n": {k: v for k, v in self.n.items()}}

    @classmethod
    def from_json(cls, j):
        t = cls()
        t.q = {k: v for k, v in j.get("q", {}).items()}
        t.n = {k: v for k, v in j.get("n", {}).items()}
        for k in t.q:
            if len(t.q[k]) < len(ACTIONS):
                t.q[k] += [0.0] * (len(ACTIONS) - len(t.q[k]))
                t.n[k] += [0] * (len(ACTIONS) - len(t.n[k]))
        return t


# ---------------------------------------------------------------- まとめ

class Brain:
    PRESTIGE_ARMS = [2, 4, 7, 12]

    def __init__(self, path, seed=None):
        self.path = path
        self.rng = random.Random(seed)
        d = len(SPOT_FEATURES)
        # 最初の見当：門から遠い・まわりがふさがっている・見通しが悪い所が良さそう（隠れんぼの一般的な考え）
        prior = [0.0] * d
        prior[SPOT_FEATURES.index("bias")] = 0.3
        prior[SPOT_FEATURES.index("gate_far")] = 0.3
        prior[SPOT_FEATURES.index("cover8")] = 0.2
        prior[SPOT_FEATURES.index("open_view")] = -0.2
        prior[SPOT_FEATURES.index("start_far")] = -0.1  # 遠くまで歩くのは少し危なそう
        self.spot = BayesLinear(d, prior_mean=prior)
        nt = len(template_features({}, 0, 0))
        self.templates = {k: BayesLinear(nt, prior_var=0.3, prior_mean=[0.35] + [0.0] * (nt - 1)) for k in TEMPLATES}
        self.qtable = QTable()
        self.items = {}  # 商品名 -> {n, mean, roles}
        self.prestige = {str(a): {"n": 0, "mean": 0.0} for a in self.PRESTIGE_ARMS}
        self.sounds = {}  # "音:大きさ" -> [聞いた回数, その後すぐ鬼が近くに見えた回数]
        # 音の聞き分け：音の名前 -> [聞いた回数, その方向に鬼が見えていた回数, その方向にお宝が見えていた回数]
        self.sound_id = {}
        # 音の大きさ → 距離（その方向に見えていた物までの距離で学ぶ）："音:大きさの段" -> [回数, 距離の合計]
        self.loudcal = {}
        # 音の鳴り方：音の名前 -> [回数, 同じ方角から続けて聞こえるまでの秒数の合計]
        self.sound_rhythm = {}
        self.words = {}  # 鬼の一覧の様子のことば -> [回数, その後すぐ見つかった回数]
        self.onis = {}  # 鬼の名前 -> 読んだ印・見たこと
        self.spot_memory = {}  # "館の形の印:x,y" -> [回数, 得点の合計]
        # 宝石のお店の道具を使ったラウンドの記録：道具の名前 -> {rounds, score_sum, clears, effects{…}}（報告書と、試す順番に使う）
        self.gem_trials = {}
        self.template_stats = {}  # 囲い方の型 -> [回数, 得点の合計]
        self.insights = {}  # 気づいたこと（key -> 気づいたときの遊んだ秒数）。同じことを 2 回書かない
        self.read_texts = {}  # 初めて読んだ文 -> 回数
        self.episodes = 0
        self.history = []  # ラウンドごとの得点（学習の進み具合のグラフ用。最後の 5000）
        self.load()

    # ---- 保存
    def load(self):
        if not os.path.exists(self.path):
            return
        try:
            with open(self.path, "r", encoding="utf-8") as f:
                j = json.load(f)
        except (OSError, ValueError):
            return
        if "spot" in j and j["spot"].get("d") == len(SPOT_FEATURES):
            self.spot = BayesLinear.from_json(j["spot"])
        for k, v in j.get("templates", {}).items():
            if k in self.templates and v.get("d") == self.templates[k].d:
                self.templates[k] = BayesLinear.from_json(v)
        self.qtable = QTable.from_json(j.get("qtable", {}))
        self.items = j.get("items", {})
        self.prestige.update(j.get("prestige", {}))
        self.sounds = j.get("sounds", {})
        self.sound_id = j.get("sound_id", {})
        self.loudcal = j.get("loudcal", {})
        self.sound_rhythm = j.get("sound_rhythm", {})
        self.words = j.get("words", {})
        self.onis = j.get("onis", {})
        self.spot_memory = j.get("spot_memory", {})
        self.gem_trials = j.get("gem_trials", {})
        self.template_stats = j.get("template_stats", {})
        self.insights = j.get("insights", {})
        self.read_texts = j.get("read_texts", {})
        self.episodes = j.get("episodes", 0)
        self.history = j.get("history", [])

    def save(self):
        j = {"spot": self.spot.to_json(), "templates": {k: v.to_json() for k, v in self.templates.items()},
             "qtable": self.qtable.to_json(), "items": self.items, "prestige": self.prestige, "sounds": self.sounds,
             "sound_id": self.sound_id, "loudcal": self.loudcal, "sound_rhythm": self.sound_rhythm,
             "words": self.words, "onis": self.onis, "spot_memory": self.spot_memory, "gem_trials": self.gem_trials, "template_stats": self.template_stats, "insights": self.insights, "read_texts": self.read_texts,
             "episodes": self.episodes, "history": self.history[-5000:]}
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(j, f, ensure_ascii=False)
        os.replace(tmp, self.path)

    # ---- 鬼の特徴
    def oni(self, name):
        return self.onis.setdefault(name, {"text_tags": [], "ratings": {}, "lines": [], "seen": 0, "speed_sum": 0.0, "speed_n": 0,
                                           "max_speed": 0.0, "hitting": 0, "stuck": 0, "frozen": 0, "petting": 0, "chasing": 0,
                                           "caught_me": 0, "rounds": 0})

    def oni_context(self, names):
        """そのステージの鬼の名前から、印の強さ（0〜1）を作る。文の印 0.6 ＋ 見たこと 0.4"""
        ctx = {}
        for name in names:
            p = self.onis.get(name)
            if not p:
                continue
            tags = set(p.get("text_tags", []))
            for t in CTX_TAGS:
                v = 0.6 if t in tags else 0.0
                if t == "breaks_walls" and p.get("seen", 0) > 20:
                    v += 0.4 * min(1.0, p["hitting"] / max(1, p["seen"]) * 10)
                if t == "fast" and p.get("speed_n", 0) > 10:
                    v += 0.4 * min(1.0, max(0.0, (p["speed_sum"] / p["speed_n"] - 4.0) / 4.0))
                ctx[t] = max(ctx.get(t, 0.0), min(1.0, v))
        return ctx

    # ---- 音・ことばの危なさ
    def sound_danger(self, name, loud):
        key = f"{name}:{min(3, int(loud * 6))}"
        n, hit = self.sounds.get(key, [0, 0])
        return (hit + 1.0) / (n + 2.0)

    def sound_lift(self, name, loud):
        """その音を聞いたあと鬼が近くに来る割合が、ふだん（何も聞いていないとき）の何倍か。よく知らない音は 1"""
        key = f"{name}:{min(3, int(loud * 6))}"
        n, hit = self.sounds.get(key, [0, 0])
        bn, bhit = self.sounds.get("_base", [0, 0])
        if n < 15 or bn < 50:
            return 1.0
        base = (bhit + 1.0) / (bn + 2.0)
        return ((hit + 1.0) / (n + 2.0)) / max(0.02, base)

    def note_sound(self, name, loud, dangerous):
        key = f"{name}:{min(3, int(loud * 6))}"
        rec = self.sounds.setdefault(key, [0, 0])
        rec[0] += 1
        rec[1] += 1 if dangerous else 0

    # ---- 音の聞き分け（ユーザーの希望：鬼の足音とお宝の音を判断する）
    # 音の名前の意味は知らないので、「その音のした方向に、そのとき鬼（お宝）が見えていたか」を数えて聞き分ける
    @staticmethod
    def loud_bucket(loud):
        return min(5, int(loud * 10))

    def note_sound_source(self, name, loud, oni_dist, treasure_dist, item_dist, calib_dist=None):
        """その音がした方向（±30 度）に、鬼・お宝（見えている・覚えている）・自分の置いた道具があったか。calib_dist は見えていた物までの距離"""
        rec = self.sound_id.setdefault(name, [0, 0, 0, 0])
        while len(rec) < 4:
            rec.append(0)
        rec[0] += 1
        rec[1] += 1 if oni_dist is not None else 0
        rec[2] += 1 if treasure_dist is not None else 0
        rec[3] += 1 if item_dist is not None else 0
        if calib_dist is not None:
            cal = self.loudcal.setdefault(f"{name}:{self.loud_bucket(loud)}", [0, 0.0])
            cal[0] += 1
            cal[1] += calib_dist

    def note_rhythm(self, name, interval):
        """同じ方角から同じ音が続けて聞こえるまでの秒数（ゲームの時間）"""
        rec = self.sound_rhythm.setdefault(name, [0, 0.0])
        rec[0] += 1
        rec[1] += interval

    def sound_pattern(self, name):
        """"steps"（足音のように細かく続く）/ "sparse"（ときどき鳴る）/ None（まだわからない）"""
        n, total = self.sound_rhythm.get(name, [0, 0.0])
        if n < 20:
            return None
        mean = total / n
        return "steps" if mean < 0.6 else "sparse" if mean > 0.85 else None

    def note_direction_base(self, oni_frac, treasure_frac, item_frac):
        """くらべるための「ふだん」：でたらめな方向に鬼・お宝・道具がある割合（0.5 秒ごと）"""
        rec = self.sound_id.setdefault("_base", [0, 0.0, 0.0, 0.0])
        rec[0] += 1
        rec[1] += oni_frac
        rec[2] += treasure_frac
        rec[3] += item_frac

    def sound_lifts(self, name):
        """その音の方向に鬼・お宝・道具があった割合が、ふだんの何倍か（データが足りなければ None）"""
        rec = self.sound_id.get(name)
        base = self.sound_id.get("_base")
        if not rec or not base or rec[0] < 30 or base[0] < 40:
            return None
        rec = list(rec) + [0] * (4 - len(rec))
        n = rec[0]
        out = []
        for i in (1, 2, 3):
            b = (base[i] + 0.5) / (base[0] + 1.0)
            out.append(((rec[i] + 0.5) / (n + 1.0)) / max(0.005, b))
        return tuple(out)

    def danger_lift_total(self, name):
        """その音（大きさ全部）の後に鬼が近くに来た割合が、ふだんの何倍か（足りなければ None）"""
        agg_n = sum(v[0] for k, v in self.sounds.items() if k.startswith(name + ":"))
        bn, bh = self.sounds.get("_base", [0, 0])
        if agg_n < 50 or bn < 50:
            return None
        hit = sum(v[1] for k, v in self.sounds.items() if k.startswith(name + ":"))
        return ((hit + 1.0) / (agg_n + 2.0)) / max(0.02, (bh + 1.0) / (bn + 2.0))

    def sound_score(self, name):
        """鬼の音らしさ（＋）とお宝の音らしさ（－）。手がかりは 3 つ：
          方向：その方角にふだんより鬼が多いか・お宝が多いか（見えている・覚えている物と照らし合わせる）
          鳴り方：足音のように細かく続くか、ときどき鳴るか
          危なさ：その音の後に鬼が近くに来やすいか"""
        lifts = self.sound_lifts(name)
        if lifts is None:
            return None
        lo, lt, li = lifts
        score = math.log(max(0.05, lo) / max(0.05, lt))
        pattern = self.sound_pattern(name)
        score += 0.5 if pattern == "steps" else -0.5 if pattern == "sparse" else 0.0
        dl = self.danger_lift_total(name)
        if dl is not None:
            score += max(-0.5, min(0.5, math.log(max(0.05, dl))))
        return score

    def sound_kind(self, name):
        """"oni"（鬼の足音・声らしい）/ "treasure"（お宝の音らしい）/ None（まだわからない・どちらでもない）"""
        score = self.sound_score(name)
        if score is None:
            return None
        lo, lt, li = self.sound_lifts(name)
        if score >= 0.35:
            return "oni"
        # 自分の置いた道具の方から鳴る音（おとりなど）はお宝の音にしない
        if score <= -0.35 and lt >= 1.1 and lt >= li:
            return "treasure"
        return None

    def loud_dist(self, name, loud):
        """その音の大きさなら何マスくらい先か（学んだ値。足りなければ近くの大きさの段・最初の見当）"""
        b = self.loud_bucket(loud)
        for bb in (b, b - 1, b + 1):
            rec = self.loudcal.get(f"{name}:{bb}")
            if rec and rec[0] >= 8:
                return rec[1] / rec[0]
        return {0: 22.0, 1: 14.0, 2: 9.0, 3: 6.0, 4: 4.0, 5: 2.5}[b]

    def word_danger(self, word):
        n, hit = self.words.get(word, [0, 0])
        return (hit + 0.5) / (n + 2.0)

    def note_word(self, word, caught_soon):
        rec = self.words.setdefault(word, [0, 0])
        rec[0] += 1
        rec[1] += 1 if caught_soon else 0

    # ---- 商品
    def item_value(self, name, roles, prior):
        rec = self.items.get(name)
        if not rec or rec["n"] == 0:
            return prior + 0.3  # まだ試していないものは少し試したくなる
        bonus = 0.25 / math.sqrt(rec["n"])
        return 0.5 * prior + rec["mean"] + bonus

    def note_item(self, name, roles, advantage):
        rec = self.items.setdefault(name, {"n": 0, "mean": 0.0, "roles": sorted(roles), "bought": 0})
        rec["n"] += 1
        rec["mean"] += (advantage - rec["mean"]) / min(rec["n"], 30)

    def note_bought(self, name, roles):
        rec = self.items.setdefault(name, {"n": 0, "mean": 0.0, "roles": sorted(roles), "bought": 0})
        rec["bought"] = rec.get("bought", 0) + 1

    # ---- 転生
    def choose_prestige_arm(self):
        untried = [a for a in self.PRESTIGE_ARMS if self.prestige[str(a)]["n"] == 0]
        if untried:
            return self.rng.choice(untried)
        total = sum(v["n"] for v in self.prestige.values())
        return max(self.PRESTIGE_ARMS, key=lambda a: self.prestige[str(a)]["mean"] + math.sqrt(2 * math.log(total + 1) / self.prestige[str(a)]["n"]))

    def note_prestige(self, arm, reward):
        rec = self.prestige[str(arm)]
        rec["n"] += 1
        rec["mean"] += (reward - rec["mean"]) / rec["n"]
