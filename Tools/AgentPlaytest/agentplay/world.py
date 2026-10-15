"""見取り図（MAP の文字）から、歩ける所・見通し・隠れやすさの特徴を計算する。

文字の意味は窓口の凡例そのまま：. 床 / g 赤い床（置けない） / 1 低い家具 / 2 背の高さくらい / 3 2 段ぶん / # 部屋の壁。
「隠れやすさ」はここでは数えるだけで、どれが大事かは学習（brain.py）が決める。
"""

import math
from collections import deque

DIRS4 = ((1, 0), (-1, 0), (0, 1), (0, -1))
DIRS8 = ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1))
DIR_VEC = {"E": (1, 0), "SE": (1, 1), "S": (0, 1), "SW": (-1, 1), "W": (-1, 0), "NW": (-1, -1), "N": (0, -1), "NE": (1, -1)}

SPOT_FEATURES = ["bias", "gate_far", "cover8", "tall8", "low8", "open_view", "dead_end", "ring_cost", "local_room", "central",
                 "ring_x_breaker", "view_x_sight", "gate_x_fast", "low_x_steps", "central_x_treasure", "start_far"]


class World:
    def __init__(self, rows):
        self.h = len(rows)
        self.w = max(len(r) for r in rows) if rows else 0
        self.g = [r.ljust(self.w, "#") for r in rows]
        self.walls = {}  # (x, y) -> 今の段数
        self.plans = {}  # (x, y) -> 設計図の段数
        self.items = {}  # (x, y) -> (名前, 向き, 今あるか)
        self._cache = {}

    def key(self):
        return "\n".join(self.g)

    def set_placed(self, placed):
        self.walls = {(x, y): h for (x, y, h, plan, name) in placed["walls"] if h > 0}
        self.plans = {(x, y): plan for (x, y, h, plan, name) in placed["walls"]}
        self.items = {(x, y): (name, d, live) for (x, y, name, d, live) in placed["items"]}

    # ---- マスの性質
    def inside(self, c):
        return 0 <= c[0] < self.w and 0 <= c[1] < self.h

    def glyph(self, c):
        return self.g[c[1]][c[0]] if self.inside(c) else "#"

    def is_floor(self, c):
        return self.glyph(c) in ".g"

    def placeable(self, c):
        return self.glyph(c) == "."

    def walkable(self, c):
        return self.is_floor(c) and self.walls.get(c, 0) == 0

    def blocks_view(self, c):
        """立っている人の目をさえぎるか（背の高さ以上の家具・部屋の壁・置いた壁）"""
        gl = self.glyph(c)
        return gl in "23#" or self.walls.get(c, 0) > 0

    def tall(self, c):
        return self.glyph(c) in "3#"

    def low(self, c):
        return self.glyph(c) in "12"

    def floors(self):
        return [(x, y) for y in range(self.h) for x in range(self.w) if self.is_floor((x, y))]

    # ---- 距離・見通し
    def bfs(self, starts, passable=None, limit=None):
        passable = passable or self.walkable
        dist = {}
        q = deque()
        for s in starts:
            if self.inside(s):
                dist[s] = 0
                q.append(s)
        while q:
            c = q.popleft()
            d = dist[c]
            if limit is not None and d >= limit:
                continue
            for dx, dy in DIRS4:
                n = (c[0] + dx, c[1] + dy)
                if n not in dist and self.inside(n) and passable(n):
                    dist[n] = d + 1
                    q.append(n)
        return dist

    def path(self, a, b, passable=None):
        """4 方向の最短経路（a を含まず b を含む）。無ければ None"""
        passable = passable or self.walkable
        prev = {a: None}
        q = deque([a])
        while q:
            c = q.popleft()
            if c == b:
                break
            for dx, dy in DIRS4:
                n = (c[0] + dx, c[1] + dy)
                if n not in prev and self.inside(n) and (passable(n) or n == b):
                    prev[n] = c
                    q.append(n)
        if b not in prev:
            return None
        out = []
        c = b
        while c != a:
            out.append(c)
            c = prev[c]
        return list(reversed(out))

    def los(self, a, b):
        """a と b の間に目をさえぎる物が無いか（両端は数えない）"""
        x0, y0 = a
        x1, y1 = b
        steps = int(max(abs(x1 - x0), abs(y1 - y0)) * 2)
        for i in range(1, steps):
            t = i / steps
            c = (int(round(x0 + (x1 - x0) * t)), int(round(y0 + (y1 - y0) * t)))
            if c != a and c != b and self.blocks_view(c):
                return False
        return True

    def exposure(self, c, radius=7):
        """c が見える床の数（まわり radius マス以内）"""
        n = 0
        for dy in range(-radius, radius + 1):
            for dx in range(-radius, radius + 1):
                if dx * dx + dy * dy > radius * radius or (dx == 0 and dy == 0):
                    continue
                t = (c[0] + dx, c[1] + dy)
                if self.is_floor(t) and self.los(c, t):
                    n += 1
        return n

    def ring(self, c, r=1):
        """c から距離 r（チェビシェフ）の床のマス（置ける所だけ）"""
        out = []
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if max(abs(dx), abs(dy)) == r:
                    n = (c[0] + dx, c[1] + dy)
                    if self.is_floor(n):
                        out.append(n)
        return out

    # ---- 隠れ場所の特徴
    def spot_features(self, c, gate_dist, diameter, ctx):
        """隠れ場所の候補 c の特徴（0〜1 くらい）。ctx は鬼の印（文から読んだもの・学んだもの）"""
        nb8 = [(c[0] + dx, c[1] + dy) for dx, dy in DIRS8]
        cover = sum(1 for n in nb8 if not self.is_floor(n)) / 8.0
        tall = sum(1 for n in nb8 if self.tall(n)) / 8.0
        low = sum(1 for n in nb8 if self.low(n)) / 8.0
        key = ("exp", c)
        if key not in self._cache:
            self._cache[key] = self.exposure(c)
        view = min(1.0, self._cache[key] / 80.0)
        open4 = sum(1 for dx, dy in DIRS4 if self.is_floor((c[0] + dx, c[1] + dy)))
        dead = 1.0 if open4 <= 1 else 0.0
        ring = len(self.ring(c, 1)) / 8.0
        key = ("room", c)
        if key not in self._cache:
            self._cache[key] = len(self.bfs([c], self.is_floor, limit=4))
        room = min(1.0, self._cache[key] / 40.0)
        gd = gate_dist.get(c, diameter)
        gate_far = min(1.0, gd / max(1.0, diameter))
        central = ctx.get("_central", {}).get(c, 0.5)
        f = {
            "bias": 1.0, "gate_far": gate_far, "cover8": cover, "tall8": tall, "low8": low, "open_view": view, "dead_end": dead,
            "ring_cost": ring, "local_room": room, "central": central,
            "ring_x_breaker": ring * ctx.get("breaks_walls", 0.0),
            "view_x_sight": view * ctx.get("wide_sight", 0.0),
            "gate_x_fast": gate_far * ctx.get("fast", 0.0),
            "low_x_steps": low * ctx.get("hears_steps", 0.0),
            "central_x_treasure": central * ctx.get("treasure_lover", 0.0),
            # スタート（マップの中央）からの道のり：遠いほど歩いている間に見つかりやすいかもしれない
            "start_far": min(1.0, ctx.get("_start_dist", {}).get(c, diameter) / max(1.0, diameter)),
        }
        return [f[k] for k in SPOT_FEATURES]

    def centrality(self, sample=60):
        """床のマスごとの「館のまんなか度」（ほかの床への平均距離が短いほど 1 に近い）"""
        floors = self.floors()
        if not floors:
            return {}
        step = max(1, len(floors) // sample)
        srcs = floors[::step]
        total = {c: 0 for c in floors}
        for s in srcs:
            d = self.bfs([s], self.is_floor)
            for c in floors:
                total[c] += d.get(c, self.w + self.h)
        vals = {c: total[c] / len(srcs) for c in floors}
        lo, hi = min(vals.values()), max(vals.values())
        return {c: 1.0 - (v - lo) / max(1e-6, hi - lo) for c, v in vals.items()}

    def flee_target(self, me, threat, max_steps=10):
        """逃げ先：threat（鬼の場所）から遠く・見通しの切れる所で、近いマス"""
        dist = self.bfs([me], limit=max_steps)
        best, best_score = None, -1e9
        for c, d in dist.items():
            if d == 0:
                continue
            away = math.dist(c, threat)
            hidden = 0.0 if self.los((int(round(threat[0])), int(round(threat[1]))), c) else 3.0
            open4 = sum(1 for dx, dy in DIRS4 if self.walkable((c[0] + dx, c[1] + dy)))
            score = away + hidden - 0.25 * d - (1.5 if open4 <= 1 else 0.0)
            if score > best_score:
                best, best_score = c, score
        return best


def dir_to_vec(name):
    return DIR_VEC.get(name, (0, 0))
