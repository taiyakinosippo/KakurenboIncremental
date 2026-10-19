"""テストプレイ AI の本体。パートごとの手順（タイトル・購入・設置・結果）と、かくれんぼ中の動き（Q 学習）。

考え方（ユーザーの指定）：
  1. まずステージ全体（上から見た見取り図）を見渡し、隠れやすい場所を見つける
  2. そこを拠点にして（壁で囲む・罠を置く）、長く隠れる・逃げる
  3. 性格は 5 つ：動かない慎重型（stay）・近くのお宝だけ取りに行く慎重型（near）・お宝をほぼ全部取りに行く強欲型（greedy）・
     建築大好き（builder：壁と罠を優先して買い、鬼を足止めして稼ぐ）・壁なし（nowall：壁を一切置かない・買わない。煙幕ダッシュで逃げる）
  4. 初めての館のかくれんぼはマップの中央から始める（見渡して隠れ場所を見つける）。隠れ場所までたどり着けたことのある館は、隠れ場所のそばから始める。
     隠れ場所に着くまでは、お宝を優先して取りつつ、鬼の足音から遠ざかる（どの性格も）
  4b. 登りたい所（2 段の柱の上）には、となりに 1 段の踏み台を置いて階段にする（踏み台はクライミング壁を優先して買う・置く）
  5. 鬼の足音が近いときは連打をやめる。お宝の音がしたら、その方角・距離にお宝があると見当をつける（音の聞き分けは学ぶ）
  5b. 床の輪（クリックの輪・足音の輪）：自分の足元に出る輪の見た目・大きさと、輪の中の鬼が気づいて来るかを学ぶ。
      「連打したら輪が鬼に届くか」を Q 学習の状態に入れ（連打するかは Q 学習が決める）、歩く輪が鬼に届くと分かったらしのび足にする
  6. 宝石のお店の道具は、まだ試していない物から買って必ず使う（使った結果を報告書で比べる）
  7. 転生のお店は性格ごとの優先度（左が先）：動かない＝よじ登る・壁の硬さ・お金アップ / 近くのお宝＝煙幕・よじ登る・お宝・壁の硬さ /
     強欲＝煙幕・お宝・お金アップ。名前の言葉で当てはめる

判断に使うのは画面の文・ボタン・見えている物・聞こえた音・見取り図だけ（Obs.tel は記録用で、ここでは読まない）。
"""

import math
import re
import time

from . import reader
from .brain import ACTIONS, CTX_TAGS, TEMPLATES, template_features
from .insights import InsightTracker
from .world import DIR_VEC, World

# prestige：転生のお店の優先度（ユーザーの指定。左が先）。商品の名前に含まれる言葉で当てはめる（同じ組の中も左が先）
# travel_treasure：隠れ場所に着くまでに、お宝を取りに寄り道してよい道のり（マス）
# 中央から隠れ場所まで歩くので、出入り口の無い「全部閉じる」囲いはどの性格も使わない（中に入れない）
PERSONAS = {
    "stay": {"ja": "慎重型（その場から動かない）", "move": False, "fetch_radius": 0, "treasure_bonus": 0.0, "coin_weight": 1.0,
             "templates": ["open", "door_far", "double", "decoy_far"], "travel_treasure": 8, "wall_lean": True,
             "prestige": [["よじ登る", "登"], ["硬さ"], ["お金アップ"]]},
    "near": {"ja": "慎重型（近くのお宝だけ取る）", "move": True, "fetch_radius": 8, "treasure_bonus": 1.0, "coin_weight": 1.0,
             "templates": ["open", "door_far", "double", "decoy_far"], "travel_treasure": 12, "wall_lean": True,
             "prestige": [["煙幕ダッシュ", "煙幕"], ["よじ登る", "登"], ["お宝"], ["硬さ"]]},
    "greedy": {"ja": "強欲型（お宝をほぼ全部取る）", "move": True, "fetch_radius": 999, "treasure_bonus": 3.0, "coin_weight": 0.5,
               "templates": ["open", "door_far", "double", "decoy_far"], "travel_treasure": 30, "wall_lean": True,
               "prestige": [["煙幕ダッシュ", "煙幕"], ["お宝"], ["お金アップ"]]},
    # 建築大好き（ユーザーの指定）：壁や罠を優先して買い、鬼を足止めして稼ぐ。囲わない型は使わない。転生は壁登り・壁の硬さ・お金アップ
    "builder": {"ja": "建築大好き（壁と罠で足止め）", "move": False, "fetch_radius": 0, "treasure_bonus": 0.0, "coin_weight": 1.0,
                "templates": ["door_far", "double", "decoy_far"], "travel_treasure": 6, "build": True,
                "prestige": [["よじ登る", "登"], ["硬さ"], ["お金アップ"]]},
    # 壁なし（ユーザーの指定）：壁を一切置かない（買わない・壁の補強も買わない）。罠と宝石の道具は使う。転生はダッシュ系（煙幕ダッシュ・煙幕の数）を先に
    "nowall": {"ja": "壁なし（ダッシュで逃げる）", "move": True, "fetch_radius": 8, "treasure_bonus": 1.0, "coin_weight": 1.0,
               "templates": ["open"], "travel_treasure": 12, "no_walls": True,
               "prestige": [["煙幕ダッシュ", "ダッシュ"], ["煙幕の数", "煙幕"], ["お宝"]]},
}


class Cmd:
    def __init__(self, text, timeout=4.0):
        self.text = text
        self.timeout = timeout


class Wait:
    def __init__(self, seconds):
        self.seconds = seconds


class Agent:
    # 転生（ユーザーの指定 2026-10-18：転生が多すぎるので人に近づける）：前の周の最高ステージ（-1 まで）に届いてから、
    # 隠れ場所を見つけて 12 回見つかったら転生。届く前は転生しない（同じステージで 40 回続けて見つかったら行き詰まりとして転生）
    PRESTIGE_AFTER_FOUND = 12
    PRESTIGE_STUCK = 40
    PRESTIGE_BEST_MARGIN = 1  # 前の周の最高からこれだけ手前でも「届いた」とみる（前の周がたまたま良かったとき用）

    def __init__(self, persona, link, brain, recorder, speed=1.0, log=print):
        self.persona = persona
        self.P = PERSONAS[persona]
        self.link = link
        self.brain = brain
        self.rec = recorder
        self.rng = brain.rng
        self.speed = speed
        self.log = log
        self.obs = None
        self.acks = {}
        self.routine = None
        self.routine_key = None
        self.waiting = None  # ("cmd", id, deadline) / ("wait", until)
        self.send_value = None
        # 見取り図
        self.map_rows = None
        self.terrain = {}  # 見取り図の文字 -> 壁を置いていない World（隠れ場所の特徴を計算する）
        self.placed = {"walls": [], "items": []}
        # 進み具合（自分で数えたもの）
        self.fail_streak = {}
        self.found_spot = set()  # 隠れ場所を見つけた（たどり着けた）ステージ（転生まで）
        self.fails_after_found = {}  # ステージ -> 隠れ場所を見つけてから見つかった回数（逃げ切ったら 0）
        self.stage_seen = None
        self.prestige_arm = None
        self.lap_best = 0
        self.prev_lap_best = brain.prev_lap_best  # 起動し直しても前の周の最高を覚えておく（転生の目安）
        self.lap_started = time.time()
        self.lap_prestige_items = []
        self.base_texts = []
        self.read_count = 0
        # 1 ラウンドの記録
        self.round = None
        self.round_no = 0
        self.plan = None
        self.shop_basket = []
        self.stage_baseline = {}
        self.next_credit = []  # 前のラウンドで買ったお金の強化（次のラウンドの勝ちやすさも見る）
        self.coin_scale = {}
        self.hear_events = []
        self.word_events = []
        self.slot_roles = {}  # 置く物の名前 -> "wall" / "item"（置いて確かめた）
        self.can_climb = False  # この起動で、かくれんぼ中の案内（操作の欄・ヒント）に「登る」が出た（使える）
        self.insights = InsightTracker(brain, recorder)  # 攻略で大事なことに気づいた瞬間を記録する

    # ------------------------------------------------------------ 送る・待つ
    def send(self, text):
        cid = self.link.send(text)
        return cid

    def run(self, deadline):
        last_save = time.time()
        while time.time() < deadline:
            obs_list = self.link.poll()
            if not self.link.connected():
                return "disconnected"
            if not obs_list:
                if time.time() - self.link.last_recv > 90:
                    return "hang"
                time.sleep(0.01)  # 観測は 0.1 秒ごとなので、待つ間は CPU を使わない
                continue
            for o in obs_list:
                self.ingest(o)
            self.obs = obs_list[-1]
            try:
                self.step()
            except Exception as e:  # 1 つの判断の失敗で止まらない（記録して続ける）
                self.rec.event("agent-error", {"error": repr(e)[:300], "phase": self.obs.phase})
                self.routine = None
            if time.time() - last_save > 120:
                self.brain.save()
                last_save = time.time()
        return "deadline"

    def ingest(self, o):
        for aid, res in o.acks:
            self.acks[aid] = res
        for b in o.bugs:
            self.rec.bug(b, o)
        self.rec.observe(o)  # 記録用（本当の値を含む）
        if o.map_rows is not None and o.map_rows:
            self.map_rows = o.map_rows
        if o.placed is not None:
            self.placed = o.placed
        # 初めて読んだ文（報告書の「AI が読んだ文」）
        if o.screen and self.read_count < 4000:
            for m, s, t in o.screen:
                if len(t) >= 6 and not re.match(r"^[\d\s,./+×:%-]+$", t) and t not in self.brain.read_texts:
                    self.brain.read_texts[t] = o.phase + ("/menu" if m else "")
                    self.read_count += 1
        if o.phase == "Hide" and o.menu == "None":
            self.learn_abilities(o)
            self.learn_from_senses(o)

    def learn_abilities(self, o):
        """かくれんぼ中の案内の文（操作の欄・はじめてのヒント）から、自分の体でできることと使い方を読む。
        取ったときに出るヒントを読めば、使い方がすぐ分かる（学習を待たない）。「解放」と書いてある文はまだ使えない物"""
        for m, s, t in o.screen:
            if m or "解放" in t:
                continue
            tags = reader.ability_tags(t)
            if not tags:
                continue
            if "climb" in tags:
                self.can_climb = True
            for tag in tags:
                if tag not in self.brain.abilities:
                    self.brain.abilities[tag] = t
                    self.rec.event("ability", {"tag": tag, "text": t})
                    self.log(f"使い方を読んだ：{tag}（{t}）")

    def climb_use(self, use):
        """よじ登るを use（"escape" 逃げる / "out_back" 囲いから出て戻る）に使ってよいか：今使えて、使い方を読んだ"""
        return self.can_climb and ("climb:" + use) in self.brain.abilities

    def climb_flag(self, use):
        """goto に付ける「登ってよい」の印"""
        return " climb" if self.climb_use(use) else ""

    def flee_plan(self, w, me, tp):
        """逃げ先と goto の印。追われたら段を越えて逃げると読んでいれば、段の向こうも逃げ先にする"""
        if self.climb_use("escape"):
            if self.round is not None:
                self.round["climb_flee"] = self.round.get("climb_flee", 0) + 1
            return w.flee_target(me, tp, over=self.climbable(w)), " climb"
        return w.flee_target(me, tp), ""

    def climbable(self, w, tiers=1):
        """登って通れるマスも含めた「通れる」（置いた tiers 段までの壁は越えられる。家具は窓口の道に入らないので数えない）"""
        return lambda c: w.walkable(c) or (w.is_floor(c) and 0 < w.walls.get(c, 0) <= tiers)

    def step(self):
        o = self.obs
        if o.phase in ("", "Loading"):
            return
        key = ("menu", o.menu) if o.menu not in ("None", "") else ("phase", o.phase)
        if key != self.routine_key or self.routine is None:
            self.routine_key = key
            self.waiting = None
            self.send_value = None
            if key[0] == "menu":
                self.routine = self.r_menu(o.menu)
            else:
                self.routine = {"Title": self.r_title, "Shop": self.r_shop, "Build": self.r_build, "Hide": self.r_hide,
                                "Result": self.r_result}.get(o.phase, self.r_idle)()
            if o.menu in ("None", ""):
                self.base_texts = o.texts()
        # 待っているもの
        if self.waiting:
            kind = self.waiting[0]
            if kind == "cmd":
                cid, until = self.waiting[1], self.waiting[2]
                if cid in self.acks:
                    self.send_value = self.acks.pop(cid)
                elif time.time() < until:
                    return
                else:
                    self.send_value = "timeout"
            elif kind == "wait":
                if time.time() < self.waiting[1]:
                    return
                self.send_value = None
            self.waiting = None
        try:
            item = self.routine.send(self.send_value)
        except StopIteration:
            self.routine = self.r_idle()
            return
        self.send_value = None
        if isinstance(item, Cmd):
            cid = self.send(item.text)
            self.waiting = ("cmd", cid, time.time() + item.timeout)
        elif isinstance(item, Wait):
            self.waiting = ("wait", time.time() + item.seconds / max(0.5, min(self.speed, 2.0)) ** 0.5)

    def r_idle(self):
        while True:
            yield None

    # ------------------------------------------------------------ メニュー・タイトル・結果
    def r_menu(self, menu):
        if menu == "OniIntro":
            while True:
                yield Wait(1.3)
                page = reader.read_oni_page(self.obs, self.base_texts)
                if page:
                    self.learn_oni_page(page)
                if self.obs.button("OniPage.1"):
                    yield Cmd("click OniPage.1")
                yield Wait(0.4)
        while True:
            yield Wait(0.5)
            for bid in ("ConfirmYes.0" if menu == "ConfirmNewGame" else "ConfirmNo.0", "MenuItem.0", "Back.0"):
                if self.obs.button(bid):
                    yield Cmd("click " + bid)
                    break
            yield Wait(1.0)

    def r_title(self):
        yield Wait(1.5)
        cont = self.obs.button("TitleItem.0")
        if cont and "セーブなし" not in cont[1]:
            yield Cmd("click TitleItem.0")
            self.rec.event("title", {"choice": "continue"})
        else:
            yield Cmd("click TitleItem.1")
            self.rec.event("title", {"choice": "new"})
        while True:
            yield Wait(3.0)
            # まだタイトルなら（押せていない）もう一度
            if self.obs.phase == "Title":
                self.routine_key = None

    def r_result(self):
        yield Wait(0.8)
        self.finish_round()
        yield Wait(0.6)
        for _ in range(20):
            yield Cmd("click NextPhase.0")
            yield Wait(1.5)

    # ------------------------------------------------------------ 鬼の紹介を読む
    def learn_oni_page(self, page):
        p = self.brain.oni(page["name"])
        new_tags = set(p.get("text_tags", [])) | set(page["tags"])
        if sorted(new_tags) != sorted(p.get("text_tags", [])) or not p.get("lines"):
            p["text_tags"] = sorted(new_tags)
            p["ratings"] = page["ratings"]
            p["lines"] = page["lines"][:12]
            self.rec.event("read-oni", {"name": page["name"], "tags": sorted(new_tags), "ratings": page["ratings"]})

    # ------------------------------------------------------------ 見取り図
    def current_world(self, with_walls=True):
        if not self.map_rows:
            return None
        # 見取り図・置いた物が届き直すまでは同じ物を使う（かくれんぼ中は毎回呼ぶので）
        key = (id(self.map_rows), id(self.placed), with_walls)
        if getattr(self, "_world_key", None) == key:
            return self._world
        w = World(self.map_rows)
        if with_walls:
            w.set_placed(self.placed)
        self._world_key, self._world = key, w
        return w

    def terrain_world(self):
        key = "\n".join(self.map_rows or [])
        if key not in self.terrain:
            w = World(self.map_rows)
            w.centrality_map = w.centrality()
            self.terrain = {key: w}  # 館は 1 つずつしか覚えない（メモリ）
        return self.terrain[key]

    def gate_cells(self, w):
        """鬼の出てくる所の見当：「鬼の門」と書いてある場所の近くの赤い床。無ければ名前の場所"""
        gates = [(x, y) for (x, y) in ((xx, yy) for yy in range(w.h) for xx in range(w.w)) if w.glyph((x, y)) == "g"]
        label = next(((x, y) for (t, x, y) in (self.obs.labels if self.obs else []) if "門" in t), None)
        mem = self.brain.onis.get("_spawn")
        if mem and mem.get("n", 0) >= 5:
            label = (mem["x"] / mem["n"], mem["y"] / mem["n"])
        if gates:
            return gates
        if label:
            c = (min(w.w - 1, max(0, int(round(label[0])))), min(w.h - 1, max(0, int(round(label[1])))))
            return [c]
        return [(w.w - 1, w.h // 2)]

    # ------------------------------------------------------------ 計画（隠れ場所・囲い方）
    def stage_context(self):
        names = reader.read_stage_onis(self.obs)
        if not names:
            names = {n: 1 for (n, s) in reader.read_oni_list(self.obs)}
        ctx = self.brain.oni_context(names.keys())
        ctx["_names"] = names
        return ctx

    def make_plan(self, stock_walls):
        w = self.terrain_world()
        if not w:
            return None
        st = reader.read_status(self.obs)
        stage = st["stage"] or 1
        ctx = self.stage_context()
        ctx["_central"] = getattr(w, "centrality_map", {})
        gates = self.gate_cells(w)
        gate_dist = w.bfs(gates, w.is_floor)
        diameter = max(gate_dist.values()) if gate_dist else w.w + w.h
        # スタートはマップの中央（そこから隠れ場所まで歩く）
        center = min((c for c in w.floors() if w.placeable(c)), key=lambda c: (c[0] - (w.w - 1) / 2.0) ** 2 + (c[1] - (w.h - 1) / 2.0) ** 2, default=None)
        ctx["_start_dist"] = w.bfs([center], w.is_floor) if center else {}
        weights = self.brain.spot.sample(self.rng, scale=1.0)
        mapkey = str(hash("\n".join(self.map_rows)) & 0xffffffff)
        scored = []
        for c in w.floors():
            if not w.placeable(c) or c not in gate_dist:
                continue
            f = w.spot_features(c, gate_dist, diameter, ctx)
            v = self.brain.spot.predict(weights, f)
            mem = self.brain.spot_memory.get(f"{mapkey}:{c[0]},{c[1]}")
            if mem and mem[0] >= 2:
                v = 0.5 * v + 0.5 * (mem[1] / mem[0])  # この館のこの場所で実際にどうだったか
            scored.append((v, c, f))
        if not scored:
            return None
        scored.sort(key=lambda p: -p[0])
        best_v, best, best_f = scored[0]
        alts = [(c, f, v) for (v, c, f) in scored[1:6]]
        # 囲い方の型（性格で使える型だけ）
        stage_norm = min(1.0, stage / 25.0)
        tf = template_features(ctx, stage_norm, min(1.0, stock_walls / 40.0))
        tmpl_scores = {}
        for k in self.P["templates"]:
            wt = self.brain.templates[k].sample(self.rng, scale=1.0)
            tmpl_scores[k] = self.brain.templates[k].predict(wt, tf)
            if self.P.get("wall_lean") and k != "open":
                tmpl_scores[k] += 0.06  # ユーザーの希望：建築大好き・壁なし以外も、壁を気持ち積極的に置く
        # 柱の上に乗る型：よじ登れて、案内の文で「鬼は 2 段の壁に飛び乗れない」と読んだときだけ（柱 2 段＋踏み台 1 段＝壁 3 個）
        if self.climb_use("perch") and not self.P.get("no_walls") and stock_walls >= 3:
            wt = self.brain.templates["perch"].sample(self.rng, scale=1.0)
            tmpl_scores["perch"] = self.brain.templates["perch"].predict(wt, tf) + (0.06 if self.P.get("wall_lean") else 0.0)
        template = max(tmpl_scores, key=tmpl_scores.get)
        plan = {"base": best, "features": best_f, "value": best_v, "template": template, "tfeat": tf, "mapkey": mapkey,
                "ctx": {k: round(v, 2) for k, v in ctx.items() if not k.startswith("_")}, "onis": dict(ctx.get("_names", {})),
                "gates": gates[:6], "stage": stage, "alts": alts}
        plan.update(self.layout_for(w, best, template, gates))
        if template == "perch" and not plan.get("pillar"):
            # 踏み台を置ける所が無かった：柱の型はやめて、次に良い型で（柱の型の学習に混ぜない）
            tmpl_scores.pop("perch")
            plan["template"] = max(tmpl_scores, key=tmpl_scores.get)
            plan.update(self.layout_for(w, best, plan["template"], gates))
        return plan

    def layout_for(self, w, base, template, gates):
        """型から、壁を置くマス（段ごと）と出入り口を決める"""
        gate_dist = w.bfs(gates, w.is_floor)
        if template == "perch":
            # 隠れ場所に 2 段の柱を立て、となりに 1 段の踏み台（階段）。踏み台は鬼の門から遠い側で、その先に立てる床がある所
            cands = []
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                step = (base[0] + dx, base[1] + dy)
                stand = (base[0] + 2 * dx, base[1] + 2 * dy)
                if w.placeable(step) and w.is_floor(stand):
                    cands.append((gate_dist.get(step, 0), step))
            if cands:
                return {"ring1": [], "ring2": [], "door": None, "door2": None, "pillar": tuple(base), "step": max(cands)[1]}
            return {"ring1": [], "ring2": [], "door": None, "door2": None}
        inner = [c for c in w.ring(base, 1) if w.placeable(c)]
        door = None
        if template in ("door_far", "double"):
            cands = []
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (base[0] + dx, base[1] + dy)
                out = (base[0] + 2 * dx, base[1] + 2 * dy)
                if w.is_floor(n) and w.is_floor(out):
                    cands.append((gate_dist.get(n, 0), n))
            if cands:
                door = max(cands)[1]
            else:
                # 出入り口にできる所が無い：閉じ込められないよう、ここでは囲わない
                return {"ring1": [], "ring2": [], "door": None, "door2": None}
        ring1 = [c for c in inner if c != door] if template in ("door_far", "sealed", "double") else []
        ring2 = []
        door2 = None
        if template == "double":
            outer = [c for c in w.ring(base, 2) if w.placeable(c)]
            if door:
                # 外の出入り口は内の出入り口から一番遠い所（中をぐるっと回る通路）
                far = [c for c in outer if any(w.is_floor((c[0] + dx, c[1] + dy)) and max(abs(c[0] + dx - base[0]), abs(c[1] + dy - base[1])) >= 3
                                               for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
                if far:
                    door2 = max(far, key=lambda c: math.dist(c, door))
            # 外の出入り口が作れなければ、外の囲いは置かない（閉じ込められないように）
            ring2 = [c for c in outer if c != door2] if door2 else []
        return {"ring1": ring1, "ring2": ring2, "door": door, "door2": door2}

    # ------------------------------------------------------------ 購入パート
    def r_shop(self):
        yield Wait(0.8)
        st = reader.read_status(self.obs)
        stage = st["stage"] or 1
        self.stage_seen = stage
        self.lap_best = max(self.lap_best, stage)
        self.shop_basket = []

        # ---- 転生（ユーザーの指定）：前の周の最高ステージ近くまで来て、このステージで隠れ場所を見つけて（たどり着けて）から 12 回見つかったら。
        #      前の周の最高に届かないうち・隠れ場所をまだ見つけていないうちは転生しない（40 回続けて見つかったら、行き詰まりとして転生する）
        pb = self.obs.button("Prestige.0")
        if pb and "pt" in pb[1] and "+" in pb[1]:
            streak = self.fail_streak.get(stage, 0)
            after_found = self.fails_after_found.get(stage, 0)
            near_best = stage >= self.prev_lap_best - self.PRESTIGE_BEST_MARGIN
            if (near_best and after_found >= self.PRESTIGE_AFTER_FOUND) or streak >= self.PRESTIGE_STUCK:
                pts = re.search(r"\+\s*(\d+)\s*pt", pb[1])
                yield Cmd("click Prestige.0")
                yield Wait(0.3)
                yield Cmd("click Prestige.0")
                yield Wait(2.0)
                st2 = reader.read_status(self.obs)
                if (st2["stage"] or stage) < stage:
                    self.on_prestige(stage, int(pts.group(1)) if pts else 0, streak)
                    self.routine_key = None  # 転生後の購入パートを最初から
                    return

        # ---- 転生のお店・宝石のお店（ポイント・宝石があれば）
        tabs = reader.read_shop_tabs(self.obs)
        for tab_id, currency in (("ShopTab.1", "pt"), ("ShopTab.2", "宝石")):
            t = tabs.get(tab_id)
            if t and t["count"]:
                yield Cmd("click " + tab_id)
                yield Wait(0.4)
                for _ in range(25):
                    items = [i for i in reader.read_shop_items(self.obs) if i["available"] and i["currency"] == currency]
                    have = (reader.read_shop_tabs(self.obs).get(tab_id) or {}).get("count") or 0
                    if currency == "pt":
                        choice = self.pick_prestige_item(items, have)
                    else:
                        choice = self.pick_gem_item(items, have)
                    if not choice:
                        break
                    yield Cmd("click " + choice["id"])
                    yield Wait(0.25)
                    self.bought(choice, "prestige" if currency == "pt" else "gem")
                yield Cmd("click ShopTab.0")
                yield Wait(0.4)

        # ---- コインのお店：壊れた分の補充 → 計画に要る壁 → 強化・罠
        if self.obs.button("RefillAll.0"):
            yield Cmd("click RefillAll.0")
            yield Wait(0.3)
        if self.obs.button("BuyAmount.0"):
            yield Cmd("click BuyAmount.0")
            yield Wait(0.2)
        items = reader.read_shop_items(self.obs)
        walls_owned = sum((i["stock"] or 0) for i in items if "blocker" in i["roles"])
        placed_walls = sum(h for (x, y, h, plan, name) in self.placed.get("walls", []))
        self.plan = self.make_plan(walls_owned + placed_walls)
        need = 0
        if self.plan:
            tiers = self.want_tiers(stage)
            need = len(self.plan["ring1"]) * tiers + len(self.plan["ring2"]) * max(1, tiers - 1)
            if self.plan.get("pillar"):
                tiers, need = 2, 3  # 柱 2 段＋踏み台 1 段
            self.plan["tiers"] = tiers
        have_total = walls_owned + placed_walls
        buys = 0
        # 柱の型：階段の 1 段目（踏み台）にする「登る壁」を、ほかの壁より先に 1 個（持っていなければ）
        if self.plan and self.plan.get("pillar"):
            st = reader.read_status(self.obs)
            ladders = [i for i in reader.read_shop_items(self.obs) if {"blocker", "climb"} <= i["roles"] and i["available"]
                       and (i["stock"] or 0) < 1 and (i["cost"] or 1e18) <= (st["coins"] or 0)]
            if ladders:
                yield Cmd("click " + ladders[0]["id"])
                yield Wait(0.25)
                self.bought(dict(ladders[0], required=True), "coin")
                self.plan["ladder_bought"] = True
                have_total += 1
                buys += 1
        while have_total < need and buys < 60:
            st = reader.read_status(self.obs)
            coins = st["coins"] or 0
            walls = [i for i in reader.read_shop_items(self.obs) if "blocker" in i["roles"] and i["available"] and (i["cost"] or 1e18) <= coins]
            if not walls:
                break
            wall = max(walls, key=lambda i: self.brain.item_value(i["name"], i["roles"], 0.3 - 0.0001 * (i["cost"] or 0)))
            yield Cmd("click " + wall["id"])
            yield Wait(0.25)
            # 計画の囲いに要る分（壊れた分の買い直し）は選んで買った物ではないので、良さの学習に入れない
            # （壊された＝手ごわいラウンドの後に多く買うので、入れると壁が損に見える）
            self.bought(dict(wall, required=True), "coin")
            have_total += 1
            buys += 1
        # 残りのコインで、良さそうな物から（買えるだけ。少しはとっておく）
        for _ in range(40):
            st = reader.read_status(self.obs)
            coins = st["coins"] or 0
            items = [i for i in reader.read_shop_items(self.obs) if i["available"] and i["currency"] == "コイン" and (i["cost"] or 1e18) <= coins * 0.9]
            choice = self.pick_item(items, coins, special=False)
            if not choice:
                break
            yield Cmd("click " + choice["id"])
            yield Wait(0.25)
            self.bought(choice, "coin")
        self.rec.event("shop", {"stage": stage, "basket": [b["name"] for b in self.shop_basket],
                                "prestige_bought": [b["name"] for b in self.shop_basket if b.get("currency") == "pt"],
                                "gem_bought": [b["name"] for b in self.shop_basket if b.get("currency") == "宝石"],
                                "plan": self.plan_summary()})
        for _ in range(20):
            yield Cmd("click NextPhase.0")
            yield Wait(1.5)

    def want_tiers(self, stage):
        if self.P.get("wall_lean"):
            base = 1 if stage <= 1 else 2 if stage <= 5 else 3  # 壁を気持ち早く高く
        else:
            base = 1 if stage <= 2 else 2 if stage <= 7 else 3
        return base + 1 if self.P.get("build") else base  # 建築大好きは 1 段高く積む

    def pick_prestige_item(self, items, points):
        """転生のお店：性格ごとの優先度（左が先）で、今のポイントで買える一番左の物。優先度の物が全部最大なら学んだ良さで"""
        order = self.P["prestige"]
        listed = False
        for group in order:
            for word in group:
                for i in items:
                    if word in i["name"]:
                        listed = True
                        if i["cost"] is not None and i["cost"] <= points:
                            return i
        if listed:
            return None  # 優先度の物はまだ買えない（ポイントをためる）
        return self.pick_item(items, points, special=True)

    GEM_TRY = 3  # 宝石の道具は、まずどれもこの回数ずつ使ってみる

    def pick_gem_item(self, items, gems):
        """宝石のお店：まず試した回数の少ない道具から買う（同じなら安い物）。
        どれもある程度試したら、使ったラウンドの勝ちやすさ（お金の道具は次のラウンドも）の高い物を優先"""
        trials = self.brain.gem_trials
        afford = [i for i in items if i["cost"] is not None and i["cost"] <= gems]
        if not afford:
            return None
        untried = [i for i in afford if trials.get(i["name"], {}).get("rounds", 0) < self.GEM_TRY]
        if untried:
            return min(untried, key=lambda i: (trials.get(i["name"], {}).get("rounds", 0) + (i["stock"] or 0) * 3, i["cost"]))

        def val(i):
            n, lift = self.brain.win_lift(i["name"], i["roles"])
            return lift + 0.15 / math.sqrt(max(1, n)) - 0.1 * (i["stock"] or 0)  # 持っている物は少し後回し
        return max(afford, key=val)

    def item_prior(self, item):
        roles = item["roles"]
        p = 0.0
        if "income_mash" in roles:
            p += 0.5 * self.P["coin_weight"]
        if "income_time" in roles:
            p += 0.3
        if "toughen" in roles:
            p += 0.4
        if "blocker" in roles:
            p += 0.25 if self.P.get("wall_lean") else 0.1
        if "stun" in roles or "lure" in roles or "decoy_runner" in roles or "one_way" in roles or "darkness" in roles:
            p += 0.15
        if "smoke" in roles:
            p += 0.3 if self.P["move"] else -0.2
        if "climb" in roles:
            p += 0.2 if self.P["move"] else -0.2
        if "treasure" in roles:
            p += 0.1 + 0.2 * self.P["treasure_bonus"]
        if "gold" in roles:
            p += 0.3
        if "freeze_item" in roles:
            p += 0.2
        if self.P.get("build"):
            # 建築大好き：壁・壁の補強・足止めの罠を優先して買う
            if roles & {"blocker", "toughen", "stun", "lure", "one_way", "decoy_runner", "darkness"}:
                p += 0.8
        return p

    def pick_item(self, items, budget, special):
        best, best_v = None, 0.05
        for i in items:
            if {"blocker", "climb"} <= i["roles"] and i.get("stock") is not None and i["available"]:
                self.can_climb = True  # 置ける「登る壁」が買える＝もう登れる（起動してまだかくれんぼの案内を見ていなくても分かる）
            if i["cost"] is None or i["cost"] > budget:
                continue
            if self.P.get("no_walls") and i["roles"] & {"blocker", "toughen"}:
                continue  # 壁なし：壁も壁の補強も買わない
            v = self.brain.item_value(i["name"], i["roles"], self.item_prior(i))
            v -= 0.02 * math.log10(max(1.0, i["cost"]))  # 同じくらいなら安い方
            if self.wants_ladder(i):
                v += 0.3  # 柱に登る階段の 1 段目（踏み台）に使う。2 個までは先に買う
            if v > best_v:
                best, best_v = i, v
        return best

    def wants_ladder(self, item):
        """この商品を階段の踏み台として欲しいか：置ける壁ですばやく登れる物（説明に「登」）で、柱に乗る型を使える（読んだ）とき。持っているのが 2 個未満"""
        if not ({"blocker", "climb"} <= item["roles"]) or item.get("stock") is None or item["stock"] >= 2:
            return False
        # 置ける「登る壁」が売られている＝もう登れる（お店に出るのは登れるようになってから）。使い方は前に読んだもの
        return "climb:perch" in self.brain.abilities and not self.P.get("no_walls")

    def bought(self, item, shop):
        self.shop_basket.append(item)
        self.brain.note_bought(item["name"], item["roles"])
        if shop == "prestige":
            self.lap_prestige_items.append(item["name"])
        if shop == "gem":
            rec = self.brain.gem_trials.setdefault(item["name"], {"bought": 0, "rounds": 0, "score_sum": 0.0, "clears": 0, "stage_sum": 0, "effects": {}})
            rec["bought"] += 1

    def on_prestige(self, stage, points, streak):
        reward = (self.lap_best - self.prev_lap_best) + 0.5
        for name in self.lap_prestige_items:
            rec = self.brain.items.get(name)
            if rec:
                self.brain.note_item(name, set(rec.get("roles", [])), reward / 3.0)
        # なぜ転生したか（報告書の「何回で転生」の欄）：隠れ場所を見つけてから 12 回 / 行き詰まり（続けて 40 回）
        found = stage in self.found_spot
        if streak >= self.PRESTIGE_STUCK:
            self.prestige_arm = f"行き詰まり{streak}回" if found else f"見つけられず{streak}回"
        else:
            self.prestige_arm = f"見つけて{self.fails_after_found.get(stage, 0)}回"
        self.rec.event("prestige", {"stage": stage, "points": points, "fail_streak": streak, "arm": self.prestige_arm,
                                    "found_spot": found, "fails_after_found": self.fails_after_found.get(stage, 0),
                                    "lap_best": self.lap_best, "prev_lap_best": self.prev_lap_best,
                                    "lap_minutes": round((time.time() - self.lap_started) / 60.0, 1)})
        self.prev_lap_best = self.lap_best
        self.brain.prev_lap_best = self.lap_best
        self.lap_best = 1
        self.lap_started = time.time()
        self.lap_prestige_items = []
        self.fail_streak = {}
        self.found_spot = set()
        self.fails_after_found = {}
        self.prestige_arm = None
        self.brain.save()

    def plan_summary(self):
        if not self.plan:
            return None
        p = self.plan
        return {"base": p["base"], "template": p["template"], "tiers": p.get("tiers"), "ring1": len(p["ring1"]), "ring2": len(p["ring2"]),
                "door": p["door"], "value": round(p["value"], 3), "ctx": p["ctx"], "onis": p["onis"],
                "step": p.get("step"), "start": p.get("start"), "start_from": p.get("start_from")}

    # ------------------------------------------------------------ 設置パート
    def r_build(self):
        yield Wait(0.8)
        if not self.map_rows:
            yield Cmd("map")
            yield Wait(0.5)
        slots = reader.read_build_slots(self.obs)
        wall_slots = [s for s in slots if self.slot_role(s) == "wall"]
        stock_walls = sum(s["stock"] for s in wall_slots)
        placed_walls = sum(h for (x, y, h, plan, name) in self.placed.get("walls", []))
        if not self.plan or self.plan.get("mapkey") != str(hash("\n".join(self.map_rows or [])) & 0xffffffff):
            self.plan = self.make_plan(stock_walls + placed_walls)
            if self.plan:
                self.plan["tiers"] = 2 if self.plan.get("pillar") else self.want_tiers(self.plan["stage"])
        plan = self.plan
        if not plan:
            yield Cmd("click NextPhase.0")
            return
        base = plan["base"]
        wanted = set(plan["ring1"]) | set(plan["ring2"]) | {c for c in (plan.get("pillar"), plan.get("step")) if c}
        # 前の隠れ場所の壁が残っていて、今の計画と違う所が多ければ全部回収してから置き直す（柱の型は隠れ場所に壁があってよい）
        current = {(x, y) for (x, y, h, p, n) in self.placed.get("walls", [])}
        blocks_door = any(d and tuple(d) in current for d in (plan.get("door"), plan.get("door2"))) or (tuple(base) in current and not plan.get("pillar"))
        if current and (len(current - wanted) > max(2, len(current) // 3) or blocks_door):
            yield Cmd("click ClearAll.0")
            yield Wait(0.2)
            yield Cmd("click ClearAll.0")
            yield Wait(0.6)
            self.rec.event("clear-all", {"old": len(current)})
        build_log = {"setstart": [], "placed": 0, "failed": 0, "items": []}
        # 壁：1 段目を全部 → 2 段目 …（置く物は「壁」とわかっている欄から、在庫のある順）
        tiers = plan.get("tiers", 1)
        layers = [(1, plan["ring1"] + plan["ring2"])]
        for t in range(2, tiers + 1):
            layers.append((t, plan["ring1"] + (plan["ring2"] if t <= tiers - 1 else [])))
        if plan.get("pillar"):
            # 柱（2 段）と踏み台（1 段）：踏み台は階段の 1 段目なのでクライミング壁を先に使う
            layers = [(1, [plan["pillar"], plan["step"]]), (2, [plan["pillar"]])]
        selected = None
        for tier, cells in layers:
            for c in cells:
                h = {(x, y): hh for (x, y, hh, p, n) in self.placed.get("walls", [])}.get(c, 0)
                if h >= tier:
                    continue
                slot = self.pick_wall_slot(reader.read_build_slots(self.obs), tier, prefer_climb=(c == plan.get("step")))
                if not slot:
                    break
                if slot["id"] != selected:
                    yield Cmd("click " + slot["id"])
                    yield Wait(0.15)
                    selected = slot["id"]
                res = yield Cmd(f"place {c[0]} {c[1]}", timeout=6)
                if res and res.startswith("ok"):
                    build_log["placed"] += 1
                    self.slot_roles.setdefault(slot["name"], "wall")
                    if c == plan.get("step"):
                        build_log["step_wall"] = slot["name"]  # 階段の踏み台に使った壁
                else:
                    build_log["failed"] += 1
        # 道具（罠・宝石の道具）：役割の見当で置く場所を決める
        w = self.current_world()
        gates = self.gate_cells(w)
        for slot in reader.read_build_slots(self.obs):
            if self.slot_role(slot) == "wall" or slot["stock"] <= 0:
                continue
            for _ in range(min(slot["stock"], 8 if self.P.get("build") else 3)):
                w = self.current_world()
                cell = self.item_cell(w, slot, plan, gates)
                if not cell:
                    break
                if slot["id"] != selected:
                    yield Cmd("click " + slot["id"])
                    yield Wait(0.15)
                    selected = slot["id"]
                res = yield Cmd(f"place {cell[0]} {cell[1]}", timeout=6)
                build_log["items"].append({"name": slot["name"], "cell": cell, "result": res})
                if not (res and res.startswith("ok")):
                    break
                yield Wait(0.2)
                # 向きのある道具（のれん）：外向き（拠点から出入り口の向き）にそろえる
                if "one_way" in slot["roles"] and plan.get("door") and cell == plan["door"]:
                    want = {(1, 0): 0, (0, 1): 1, (-1, 0): 2, (0, -1): 3}.get((plan["door"][0] - base[0], plan["door"][1] - base[1]))
                    for _ in range(4):
                        cur = next((d for (x, y, n, d, live) in self.placed.get("items", []) if (x, y) == cell), None)
                        if cur is None or cur == want:
                            break
                        yield Cmd(f"place {cell[0]} {cell[1]}", timeout=6)
                        yield Wait(0.2)
        # スタート（ユーザーの指定）：初めての館はマップの中央から（歩いて見渡し、隠れ場所を見つける）。
        # 隠れ場所までたどり着けたことのある館は、隠れ場所のそば（だめなら中央）。置けなければ近い順に試す
        spot = self.brain.start_spots.get(plan["mapkey"])
        plan["start_from"] = "spot" if spot else "center"
        cands = (self.cells_near(plan, plan["base"])[:4] if spot else []) + self.center_cells(plan)[:6]
        for c in cands:
            res = yield Cmd(f"setstart {c[0]} {c[1]}", timeout=6)
            build_log["setstart"].append([list(c), res])
            if res and res.startswith("ok"):
                plan["start"] = c
                if spot and math.dist(c, plan["base"]) > 3.0:
                    plan["start_from"] = "center"  # 隠れ場所のそばに置けなかった
                break
        # 立っている壁（前のラウンドから残っている分も。placed は今回新しく置いた数）
        walls_now = self.placed.get("walls", [])
        build_log["standing"] = sum(1 for (x, y, h, p, n) in walls_now if h > 0)
        build_log["blocks"] = sum(h for (x, y, h, p, n) in walls_now)
        plan["build"] = build_log
        self.rec.event("build", {"stage": plan["stage"], "plan": self.plan_summary(), "log": build_log})
        if self.rng.random() < 0.05 or self.round_no < 2:
            yield Cmd(f"shot build_{self.persona}_s{plan['stage']}")
        for _ in range(20):
            yield Cmd("click NextPhase.0")
            yield Wait(1.5)

    def center_cells(self, plan):
        """マップの中央に近い順の、スタートにできそうなマス（置いた壁・囲いの予定・赤い床・道具の所は除く）"""
        w = self.current_world()
        if not w:
            return []
        return self.cells_near(plan, ((w.w - 1) / 2.0, (w.h - 1) / 2.0))

    def cells_near(self, plan, center):
        """center に近い順の、スタートにできそうなマス（置いた壁・囲い・柱と踏み台の予定・赤い床・道具の所は除く）。
        隠れ場所のそばなら、囲いの中（隠れ場所そのもの）が先に来る"""
        w = self.current_world()
        if not w:
            return []
        avoid = set(plan.get("ring1", [])) | set(plan.get("ring2", [])) | set(w.items) | {c for c in (plan.get("pillar"), plan.get("step")) if c}
        cells = [c for c in w.floors() if w.placeable(c) and w.walls.get(c, 0) == 0 and w.plans.get(c, 0) == 0 and c not in avoid]
        return sorted(cells, key=lambda c: (c[0] - center[0]) ** 2 + (c[1] - center[1]) ** 2)

    def slot_role(self, slot):
        known = self.slot_roles.get(slot["name"])
        if known:
            return known
        return "wall" if "blocker" in slot["roles"] else "item"

    def full_roles(self, slot):
        """置く物の役割：欄の文の言葉と、お店で説明を読んだときの役割（欄には説明が無いので、名前で引く）"""
        return set(slot["roles"]) | set(self.brain.items.get(slot["name"], {}).get("roles", []))

    def pick_wall_slot(self, slots, tier, prefer_climb=False):
        walls = [s for s in slots if self.slot_role(s) == "wall" and s["stock"] > 0]
        climbs = [s for s in walls if "climb" in self.full_roles(s)]
        if prefer_climb and climbs:
            return climbs[0]  # 階段の踏み台：すばやく登れる壁（説明に「登」）を先に。無ければふつうの壁
        # ふつうの段は登る壁以外から（登る壁は踏み台に取っておく。ほかに無ければ使う）
        cands = [s for s in walls if s not in climbs] or climbs
        if not cands:
            return None
        # 1 段目は音を消しそうな壁を先に（持っていれば）、上の段は安い壁から（在庫の多い物）
        if tier == 1:
            quiet = [s for s in cands if "quiet" in self.full_roles(s)]
            if quiet:
                return quiet[0]
        return max(cands, key=lambda s: s["stock"])

    def item_cell(self, w, slot, plan, gates):
        roles = slot["roles"]
        base = plan["base"]
        walk = w.bfs([base], w.is_floor)  # 拠点からの道のり（自分の壁は数えない）
        gdist = w.bfs(gates, w.is_floor)
        free = [c for c in walk if w.placeable(c) and c not in w.items and c != base and w.walls.get(c, 0) == 0]
        if not free:
            return None
        if "one_way" in roles and plan.get("door") and plan["door"] not in w.items and w.placeable(plan["door"]):
            return plan["door"]
        if "stun" in roles:
            cands = [c for c in free if 2 <= walk[c] <= 4 and gdist.get(c, 99) < gdist.get(base, 0)]
        elif "lure" in roles:
            cands = [c for c in free if 7 <= walk[c] <= 14 and gdist.get(c, 99) < gdist.get(base, 0)]
        elif "decoy_runner" in roles:
            cands = [c for c in free if 4 <= walk[c] <= 9]
        elif "darkness" in roles or "gold" in roles:
            cands = [c for c in free if 2 <= walk[c] <= 4]
        else:
            cands = free
        if not cands:
            cands = free
        return self.rng.choice(cands)

    # ------------------------------------------------------------ かくれんぼ
    def new_round(self):
        st = reader.read_status(self.obs)
        self.round_no += 1
        me = self.obs.self_.get("pos", (0.0, 0.0))
        base = tuple(self.plan["base"]) if self.plan and self.plan.get("mapkey") == str(hash("\n".join(self.map_rows or [])) & 0xffffffff) else (int(round(me[0])), int(round(me[1])))
        self.round = {"no": self.round_no, "stage": st["stage"], "base": base, "plan": self.plan_summary(), "t0": None, "t_last": self.obs.t,
                      "limit": None, "time_left": None, "coins0": None, "coins_last": 0.0, "treasures_last": 0, "treasures_total": None,
                      "actions": {a: 0 for a in ACTIONS}, "decisions": 0, "state": None, "action": None, "reward": 0.0, "score": 0.0,
                      "treasure_known": {}, "chased_ever": False, "flee_count": 0, "smoke_used": 0, "last_seen": [], "goto": None,
                      "words": {}, "mash_on": False, "crouch": None, "items_used": 0, "explore_target": None,
                      "first_seen_oni": None, "real_start": time.time(), "finished": False,
                      # 中央から隠れ場所へ歩いている間（travel）と、着いてから（hide）
                      "phase": "travel", "travel_time": None, "travel_treasures": 0, "travel_flee": 0, "base_switches": 0,
                      "base_nopath": 0, "reached": None, "caught_while": None, "effects": {}, "gem_items": []}

    def r_hide(self):
        # チュートリアルが閉じて戻ってきたときは、同じラウンドの続き
        if self.round is None or self.round.get("finished"):
            self.new_round()
        while True:
            yield None
            self.hide_tick()

    def hide_tick(self):
        o = self.obs
        R = self.round
        st = reader.read_status(o)
        if R is None:
            return
        if st["countdown"] is not None:
            # 「もういいかい」の間（鬼はまだ出てこない）：もう隠れ場所へ歩き出す（連打はまだできない）
            if R["phase"] == "travel" and o.t - R.get("travel_decided", -99.0) >= 0.3:
                _s, info = self.hide_state(st)
                self.keep_action()
                self.travel_tick(info)
            return
        if R["t0"] is None:
            R["t0"] = o.t
            R["limit"] = st["time_left"] or 30
            R["coins0"] = st["round_coins"] or 0.0
            R["coins_last"] = R["coins0"]
            R["treasures_total"] = st["treasure_total"]
            # このラウンドに置いてある宝石のお店の道具（使った結果を比べるため）
            R["gem_items"] = sorted({name for (x, y, name, d, live) in self.placed.get("items", []) if name in self.brain.gem_trials})
        if st["time_left"] is not None:
            R["time_left"] = st["time_left"]
        R["t_obs"] = o.t
        # ---- このあいだの得点（毎回たまっていく）
        dt = o.t - R["t_last"]
        R["t_last"] = o.t
        coins = st["round_coins"] if st["round_coins"] is not None else R["coins_last"]
        stage = R["stage"] or 1
        scale = self.coin_scale.get(stage, 0.0) or 50.0
        got = st["treasure_got"] or 0
        r = dt / max(1.0, R["limit"]) * 10.0
        r += self.P["coin_weight"] * max(0.0, coins - R["coins_last"]) / scale
        r += self.P["treasure_bonus"] * max(0, got - R["treasures_last"])
        if R["phase"] == "travel" and got > R["treasures_last"]:
            R["travel_treasures"] += got - R["treasures_last"]
        R["coins_last"] = coins
        R["treasures_last"] = got
        if self.on_perch():
            R["perch_time"] = R.get("perch_time", 0.0) + dt  # 柱の上にいた秒数
        R["score"] += r
        R["reward"] += r
        self.keep_action()
        # ---- 次の判断をするか：止まっている行動は 0.4 秒ごと。歩く行動（逃げる・戻る・お宝）は、着くか危なくなるまで続ける
        s, info = self.hide_state(st)
        self.note_gem_effects(st)
        if info["steps_near"] and R["mash_on"]:
            # 鬼の足音が近い（目で見えた鬼が近い）：判断を待たずにすぐ連打をやめる（ユーザーの希望）
            self.set_mash(False)
            R["mash_stopped"] = R.get("mash_stopped", 0) + 1
            if R["mash_stopped"] <= 3:
                self.rec.event("mash-stop", {"dist": round(info["steps_d"], 1), "seen": any(e["dist"] <= 6.0 for e in o.see_onis)})
        # 道具（だるまさんがころんだ）：追われている・すぐそばに見えている鬼がいれば使ってみる（宝石のお店の道具を試す）
        if info["daruma"] > 0 and o.t - R.get("item_at", -99.0) > 2.0:
            vis = [e for e in o.see_onis if e["cue"] not in ("frozen", "petting", "stuck")]
            close = [e for e in vis if e["dist"] <= 6.0 or e["cue"] == "red-eyes"]
            if close:
                e = min(close, key=lambda e: e["dist"])
                self.send(f"face {int(round(e['x']))} {int(round(e['y']))}")
                self.send("key UseItem tap")
                R["items_used"] += 1
                R["item_at"] = o.t
                R["effects"]["item_used"] = R["effects"].get("item_used", 0) + 1
                name = st.get("usable_name") or "道具"
                if name not in R["gem_items"]:
                    R["gem_items"].append(name)
        if R["phase"] == "travel":
            self.travel_tick(info)
            return
        if not self.decision_due(info):
            return
        allowed = self.allowed_actions(info)
        if R["state"] is not None and R["action"] is not None:
            self.brain.qtable.update(R["state"], R["action"], R["reward"], s, allowed)
        R["reward"] = 0.0
        a = self.brain.qtable.choose(s, allowed, self.rng)
        R["state"], R["action"] = s, a
        R["decided_at"] = o.t
        R["decided_threat"] = info["threat"]
        R["decided_chased"] = info["chased"]
        R["decisions"] += 1
        R["actions"][ACTIONS[a]] += 1
        if ACTIONS[a] in ("MASH", "QUIET"):
            rh = R.setdefault("ring_choice", {})
            k = f"H{info['ring_h']}:{ACTIONS[a]}"
            rh[k] = rh.get(k, 0) + 1
        R["info"] = info
        self.do_action(ACTIONS[a], info)

    def decision_due(self, info):
        R = self.round
        if R["action"] is None:
            return True
        name = ACTIONS[R["action"]]
        since = self.obs.t - R.get("decided_at", -99.0)
        if name == "MASH" and info["steps_near"]:
            return True  # 連打をやめたので、次にどうするか決め直す
        if name not in ("FLEE", "RETURN", "FETCH", "EXPLORE"):
            return since >= 0.4
        if name == "EXPLORE" and info["target"]:
            return True  # 歩いていてお宝が見えた
        if since < 0.4:
            return False
        if R["goto"] is None:
            return True  # 着いた・引っかかった・道が無い
        if info["chased"] and not R.get("decided_chased"):
            return True  # 歩いている途中で追われ始めた
        if name != "FLEE" and info["threat"] > R.get("decided_threat", 0) and info["threat"] >= 2:
            return True  # 鬼が近づいてきた
        return since > (6.0 if name == "FLEE" else 15.0)

    # ------------------------------------------------------------ 中央から隠れ場所へ（どの性格も：お宝を優先しつつ、鬼の足音から遠ざかる）
    def travel_tick(self, info):
        o = self.obs
        R = self.round
        if o.t - R.get("travel_decided", -99.0) < 0.3:
            return
        R["travel_decided"] = o.t
        me, base = info["me"], R["base"]
        w = self.current_world()
        self.set_mash(False)
        # 隠れ場所へ行けなかった（道が無い・引っかかる）：別の候補へ。それも無ければここに隠れる
        last = R.pop("last_walk_result", None)
        if last and last[0] == "base" and last[1] in ("nopath", "stuck"):
            R["base_nopath"] += 1
            if R["base_nopath"] >= 2:
                alt = self.alt_hiding_spot(me, info["threat_pos"] or (-100.0, -100.0)) if w else None
                if alt and alt != base and R["base_switches"] < 3:
                    R["base"], base = alt, alt
                    R["base_switches"] += 1
                    R["base_nopath"] = 0
                else:
                    R["base"] = info["mec"]
                    self.start_hiding("nopath")
                    return
        perch_base = bool((self.plan or {}).get("pillar")) and tuple(base) == tuple(self.plan["pillar"])
        if (self.on_perch() if perch_base else math.dist(me, base) < 0.8):
            self.start_hiding("arrived")  # 柱の型は柱の上に乗ってから
            return
        if (R["t0"] is not None and o.t - R["t0"] > 0.4 * R["limit"]) or not w:
            # 時間がかかりすぎた：ここから先は性格ごとの隠れ方で（動かない性格はその場を隠れ場所にする）
            if not self.P["move"]:
                R["base"] = info["mec"]
            self.start_hiding("timeout")
            return
        # 鬼が近い・追われている：足音（見えた鬼）から遠ざかる。隠れ場所が鬼の近くなら、別の隠れ場所にする
        if info["chased"] or info["threat"] >= 2:
            tp = info["threat_pos"] or (me[0] + 3.0, me[1])
            if math.dist(base, tp) < 6.0:
                alt = self.alt_hiding_spot(me, tp)
                if alt and alt != base:
                    R["base"] = alt
                    R["base_switches"] += 1
            target, climb = self.flee_plan(w, info["mec"], tp)
            if target and R["goto"] != target:
                self.send(f"goto {target[0]} {target[1]}{climb}")
                R["goto"], R["goto_at"], R["goto_kind"] = target, o.t, "flee"
                R["travel_flee"] += 1
            return
        if R.get("goto_kind") == "flee" and R["goto"] is not None:
            return  # 逃げ終わるまで
        sneak = " sneak" if self.sneak_needed(info) else ""
        # お宝を優先（見えた・覚えている・音の手がかり。鬼の近くの物は取らない）
        t = self.travel_treasure(me, info)
        if t:
            if R["goto"] != t:
                self.send(f"goto {t[0]} {t[1]}{sneak}{self.climb_flag('out_back')}")
                R["goto"], R["goto_at"], R["goto_kind"] = t, o.t, "treasure"
                R["fetch_from_sound"] = t not in R["treasure_known"]
            return
        goal, climb = self.base_goal(base)
        if R["goto"] != goal:
            self.send(f"goto {goal[0]} {goal[1]}{sneak}{climb}")
            R["goto"], R["goto_at"], R["goto_kind"] = goal, o.t, "base"

    def on_perch(self):
        """柱の上（2 段の高さ）にいるか"""
        p = self.plan or {}
        return bool(p.get("pillar")) and float(self.obs.self_.get("h") or 0) >= 1.5

    def base_goal(self, base):
        """隠れ場所へ行くときの行き先と goto の印。柱の型は階段を 1 段ずつ：床にいるなら踏み台へ、踏み台に乗ったら柱へ"""
        p = self.plan or {}
        if p.get("pillar") and tuple(base) == tuple(p["pillar"]) and self.can_climb:
            if float(self.obs.self_.get("h") or 0) < 0.5:
                return tuple(p["step"]), " climb"
            return tuple(base), " climb"
        return base, self.climb_flag("out_back")

    def travel_treasure(self, me, info):
        R = self.round
        w = self.current_world()
        threats = list(info.get("threat_points") or [])
        # 始まってすぐは、鬼の門（鬼が出てくる所）のまわりも鬼がいるものとして避ける
        gates = (self.plan or {}).get("gates") or []
        if gates and (R["t0"] is None or self.obs.t - R["t0"] < 8.0):
            threats.append((sum(g[0] for g in gates) / len(gates), sum(g[1] for g in gates) / len(gates)))
        # 鬼がそばにいる・鬼の足音が 10 マス以内に聞こえる（鬼が出てきた後）ときは寄り道しない。
        # お宝のまわりを回ると読んだ鬼がいるステージでは、お宝のまわりをもっと避ける
        if any(math.dist(me, p) < 5.0 for p in threats) or (R["t0"] is not None and info["threat_d"] <= 10.0):
            return None
        keep_away = 6.0 + 3.0 * (self.plan or {}).get("ctx", {}).get("treasure_lover", 0.0)
        reach = w.bfs([(int(round(me[0])), int(round(me[1])))], w.walkable, limit=self.P["travel_treasure"])
        cands = []
        for c in list(R["treasure_known"]) + list(R.get("treasure_suspect", {})):
            if c in reach and all(math.dist(c, p) >= keep_away for p in threats):
                cands.append((reach[c] + (0 if c in R["treasure_known"] else 3), c))
        return min(cands)[1] if cands else None

    def alt_hiding_spot(self, me, threat):
        """鬼から離れた、歩いて行ける別の隠れ場所（見渡したときの候補から）"""
        w = self.current_world()
        reach = w.bfs([(int(round(me[0])), int(round(me[1])))], w.walkable)
        cands = [self.round["base"]] + [c for (c, f, v) in ((self.plan or {}).get("alts") or [])]
        cands = [tuple(c) for c in cands if tuple(c) in reach]
        if not cands:
            return None
        return max(cands, key=lambda c: math.dist(c, threat) - 0.15 * reach[c])

    def start_hiding(self, reason):
        o = self.obs
        R = self.round
        R["phase"] = "hide"
        R["reached"] = reason
        R["travel_time"] = round(o.t - R["t0"], 1) if R["t0"] is not None else 0.0  # 「もういいかい」の間に着いたら 0 秒
        R["reward"] = 0.0  # Q 学習は隠れ場所に着いてから
        R["state"], R["action"] = None, None
        if R["goto"] is not None:
            self.send("stop")
            R["goto"] = None
        self.set_crouch(1)
        self.rec.event("reached-hiding", {"reason": reason, "seconds": R["travel_time"], "base": R["base"],
                                          "treasures": R["treasures_last"], "flee": R["travel_flee"], "switches": R["base_switches"]})

    def note_gem_effects(self, st):
        """宝石のお店の道具の効き目として見えたもの（画面の文・鬼の様子）を数える"""
        o = self.obs
        R = self.round
        eff = R["effects"]
        words = [w for (n, w) in reader.read_oni_list(o)]
        for key, cond in (("cat_petting", any("猫" in w for w in words) or any(e["cue"] == "petting" for e in o.see_onis)),
                          ("doll_chased", any("人形" in w for w in words)),
                          ("onis_frozen", any("止まって" in w or "ピタッ" in w for w in words) or any(e["cue"] == "frozen" for e in o.see_onis)),
                          ("dark", any("真っ暗" in c for c in st["chips"])),
                          ("gold", any("ゴールド" in c for c in st["chips"]))):
            if cond:
                eff[key] = eff.get(key, 0) + 1

    def hide_state(self, st):
        o = self.obs
        R = self.round
        me = o.self_.get("pos", (0.0, 0.0))
        mec = (int(round(me[0])), int(round(me[1])))
        # 見えている鬼と、危ない音
        threat_d = 99.0
        threat_pos = None
        chased = False
        for e in o.see_onis:
            if e["cue"] in ("frozen", "petting", "stuck"):
                continue
            if e["dist"] < threat_d:
                threat_d, threat_pos = e["dist"], (e["x"], e["y"])
            if e["cue"] == "red-eyes":
                chased = True
        # 目で見える鬼が近い、または鬼の足音らしい音が近い → 連打をやめる（ユーザーの希望）
        steps_near = any(e["dist"] <= 6.0 and e["cue"] not in ("frozen", "petting", "stuck") for e in o.see_onis)
        steps_d = 99.0
        # 鬼がいそうな所（見えた鬼・鬼の足音らしい音の見当）。お宝を取りに寄るか決めるのに使う
        threat_points = [(e["x"], e["y"]) for e in o.see_onis if e["cue"] not in ("frozen", "petting", "stuck")]
        suspects = R.setdefault("treasure_suspect", {})
        w = self.current_world()
        for hr in o.hear:
            if hr["dir"] in ("-", "here"):
                continue
            kind = self.brain.sound_kind(hr["name"])
            lift = self.brain.sound_lift(hr["name"], hr["loud"])
            ang, _half = self.hear_angle(hr)
            v = (math.cos(math.radians(ang)), math.sin(math.radians(ang))) if ang is not None else (0.0, 0.0)
            est = self.brain.loud_dist(hr["name"], hr["loud"])
            if kind == "treasure":
                # お宝の音らしい：方向と大きさから、お宝のありそうなマスを覚える（見えたお宝より弱い手がかり）
                if w:
                    c = (int(round(me[0] + v[0] * est)), int(round(me[1] + v[1] * est))) if ang is not None else mec
                    c = (min(w.w - 1, max(0, c[0])), min(w.h - 1, max(0, c[1])))
                    if not w.walkable(c):
                        near = [n for n in w.bfs([c], w.is_floor, limit=2) if w.walkable(n)]
                        c = min(near, key=lambda n: math.dist(n, me)) if near else None
                    if c:
                        suspects[c] = (o.t, hr["loud"])
                continue
            # 鬼の足音らしい音・ふだんより鬼が来やすい音を危ない音として数える（知らない音は大きいときだけ用心する）
            if not (kind == "oni" or lift >= 1.2 or (kind is None and lift == 1.0 and hr["loud"] >= 0.3)):
                continue
            if kind == "oni" and est < steps_d:
                steps_d = est
            threat_points.append((me[0] + v[0] * est, me[1] + v[1] * est))
            if est < threat_d:
                threat_d, threat_pos = est, (me[0] + v[0] * est, me[1] + v[1] * est)
        if steps_d <= 6.0:
            steps_near = True
        # 古い手がかり（6 秒より前）は忘れる
        for c in [c for c, (t, l) in suspects.items() if o.t - t > 6.0]:
            suspects.pop(c, None)
        for name, word in reader.read_oni_list(o):
            if self.brain.word_danger(word) > 0.35 and ("追" in word):
                chased = True
            R["words"][word] = R["words"].get(word, 0) + 1
        if chased:
            R["chased_ever"] = True
        threat = 0 if threat_d > 14 else 1 if threat_d > 7 else 2 if threat_d > 3.5 else 3
        base = R["base"]
        bd = math.dist(me, base)
        at_base = 0 if bd < 0.8 else 1 if bd < 3.5 else 2
        enc = 0
        if self.plan and self.plan.get("ring1"):
            have = {(x, y) for (x, y, h, p, n) in self.placed.get("walls", []) if h > 0}
            enc = 1 if all(c in have for c in self.plan["ring1"]) else 0
        # お宝（見えたもの・覚えているもの）
        for (x, y, d) in o.see_treasures:
            R["treasure_known"][(int(round(x)), int(round(y)))] = o.t
        if st["treasure_got"] is not None and st["treasure_got"] > R.get("treasures_seen_got", 0):
            R["treasures_seen_got"] = st["treasure_got"]
            # 取ったお宝は覚えている物から消す（一番近い物）
            if R["treasure_known"]:
                near = min(R["treasure_known"], key=lambda c: math.dist(c, me))
                if math.dist(near, me) < 2.0:
                    R["treasure_known"].pop(near, None)
        target = self.treasure_target(me, base)
        smoke = 1 if (o.gauges.get("煙幕", {}).get("left", 0) or 0) > 0 else 0
        limit = R["limit"] or 30
        tl = R["time_left"] or limit
        tphase = 0 if tl > limit * 0.66 else 1 if tl > limit * 0.33 else 2
        # 床の輪：連打したら輪が鬼に届くか。人と同じくらいの見積もりにする（ユーザーの希望）：
        #   見えている鬼だけ、輪の大きさを目分量（±15%）でくらべる。壁の向こうの鬼（足音・さっき見た所の見当）は、
        #   輪が壁を越えてどこまで届くか人には分かりにくいので「分からない」（H3）にして、どうするかは経験（Q 学習）で覚える
        #   0＝まだ分からない・鬼の見当が無い / 1＝届かない / 2＝届く（見えている鬼） / 3＝壁の向こうで届くかもしれない
        vis = [(e["x"], e["y"]) for e in o.see_onis if e["cue"] not in ("frozen", "petting", "stuck")]
        hidden = list(threat_points[len(vis):]) + [(x, y) for (t, x, y) in getattr(self, "_recent_oni_pos", []) if 0.15 < o.t - t <= 2.0]
        vis_d = min((math.dist(me, p) for p in vis), default=99.0)
        hidden_d = min((math.dist(me, p) for p in hidden), default=99.0)
        oni_d = min(vis_d, hidden_d)
        reach = self.ring_reach_cells("mash", me)
        if reach is None or oni_d >= 99.0:
            ring_h = 0
        else:
            seen_reach = reach * (1.0 + self.rng.gauss(0.0, 0.15))  # 目分量
            ring_h = 2 if vis_d <= seen_reach else 3 if hidden_d <= reach * 1.5 else 1
        s = f"T{threat}C{1 if chased else 0}B{at_base}E{enc}R{1 if target else 0}S{smoke}P{tphase}H{ring_h}"
        remaining = (st["treasure_total"] or 0) - (st["treasure_got"] or 0)
        explore_ok = self.P["move"] and self.P["fetch_radius"] > 0 and not target and threat <= 1 and not chased and remaining > 0
        info = {"me": me, "mec": mec, "threat": threat, "threat_d": threat_d, "threat_pos": threat_pos, "chased": chased,
                "at_base": at_base, "target": target, "smoke": smoke, "daruma": st["daruma"], "explore_ok": explore_ok,
                "ring_h": ring_h, "oni_d": oni_d, "vis_d": vis_d, "steps_near": steps_near, "steps_d": min(steps_d, threat_d if steps_near else 99.0), "threat_points": threat_points}
        return s, info

    @staticmethod
    def hear_angle(hr):
        """聞こえた音の方角（度。0 = E・90 = S）と、その確からしさの幅（±度）。15 度刻みがあればそれ、無ければ 8 方位"""
        if hr.get("deg") is not None:
            return float(hr["deg"]), 20.0
        v = DIR_VEC.get(hr.get("dir"))
        if not v:
            return None, None
        return math.degrees(math.atan2(v[1], v[0])), 30.0

    @staticmethod
    def seen_in_dir(me, hr_or_dir, points, half_angle=None):
        """音の方角（±幅）に、points のうち一番近い物までの距離（無ければ None）。hr_or_dir は聞こえた音か、方角（度）"""
        if isinstance(hr_or_dir, dict):
            want, half = Agent.hear_angle(hr_or_dir)
        else:
            want, half = hr_or_dir, 20.0
        if half_angle is not None:
            half = half_angle
        if want is None or not points:
            return None
        best = None
        for (x, y) in points:
            dx, dy = x - me[0], y - me[1]
            d = math.hypot(dx, dy)
            if d < 0.5:
                continue
            diff = abs((math.degrees(math.atan2(dy, dx)) - want + 180.0) % 360.0 - 180.0)
            if diff <= half and (best is None or d < best):
                best = d
        return best

    def treasure_target(self, me, base):
        R = self.round
        radius = self.P["fetch_radius"]
        if radius <= 0:
            return None
        w = self.current_world()
        if not w:
            return None
        # 囲いから登って一度外に出て戻ってこられると読んでいれば、囲いの外のお宝も取りに行く先にする
        # 2 段の壁を越えられるかは試して分かる（行けなかった所はこのラウンドは選ばない）
        passable = self.climbable(w, 2) if self.climb_use("out_back") else w.walkable
        dist_base = w.bfs([base], passable, limit=radius if radius < 999 else None)
        for c in R.get("unreachable", ()):
            dist_base.pop(c, None)
        known = [c for c in R["treasure_known"] if c in dist_base]
        if known:
            R["target_from_sound"] = False
            return min(known, key=lambda c: math.dist(c, me))
        # 見えていなくても、お宝の音らしい音がした所（性格の範囲の中）。大きく聞こえた所から
        suspects = [(l, c) for c, (t, l) in R.get("treasure_suspect", {}).items() if c in dist_base]
        if suspects:
            R["target_from_sound"] = True
            return max(suspects)[1]
        return None

    def allowed_actions(self, info):
        idx = {a: i for i, a in enumerate(ACTIONS)}
        # 鬼の足音が近いときは連打しない（ユーザーの希望のきまり。学習より先に効く）
        allowed = [idx["QUIET"]] if info.get("steps_near") else [idx["MASH"], idx["QUIET"]]
        if info["daruma"] > 0 and info["threat"] >= 1:
            allowed.append(idx["ITEM"])
        if not self.P["move"]:
            return allowed
        # 柱の上（鬼が飛び乗れないと読んだ高さ）にいる間は、降りて逃げない
        perched = self.on_perch()
        if (info["threat"] >= 1 or info["chased"]) and not perched:
            allowed.append(idx["FLEE"])
        if info["at_base"] > 0:
            allowed.append(idx["RETURN"])
        if info["target"]:
            allowed.append(idx["FETCH"])
        elif info.get("explore_ok"):
            allowed.append(idx["EXPLORE"])
        if info["smoke"] and info["chased"] and not perched:
            allowed.append(idx["SMOKE"])
        return allowed

    def explore_target(self, me, base):
        """お宝を探して歩く先：この館で今回まだ歩いていない所（お宝の性格の範囲で、近い方）"""
        w = self.current_world()
        R = self.round
        if not w:
            return None
        start = (int(round(me[0])), int(round(me[1])))
        passable = self.climbable(w) if self.climb_use("out_back") else w.walkable
        reach = w.bfs([start], passable, limit=30)
        if self.P["fetch_radius"] < 999:
            around = w.bfs([tuple(base)], passable, limit=self.P["fetch_radius"])
            cands = [c for c in reach if c in around]
        else:
            cands = list(reach)
        if len(cands) > 300:
            cands = self.rng.sample(cands, 300)
        visited = list(R.get("visited", set()))[-250:]
        best, best_s = None, -1e9
        for c in cands:
            md = min((abs(c[0] - v[0]) + abs(c[1] - v[1]) for v in visited), default=8)
            s = min(md, 8) - 0.08 * reach[c]
            if s > best_s:
                best, best_s = c, s
        return best if best_s > 1.5 else None

    def do_action(self, a, info):
        R = self.round
        me = info["mec"]
        R["goto_kind"] = a
        if a in ("MASH", "QUIET", "ITEM"):
            if R["goto"] is not None:
                self.send("stop")
                R["goto"] = None
            self.set_crouch(1)
            self.set_mash(a == "MASH")
            if a == "ITEM":
                vis = [e for e in self.obs.see_onis if e["cue"] not in ("frozen",)]
                if vis:
                    e = min(vis, key=lambda e: e["dist"])
                    self.send(f"face {int(round(e['x']))} {int(round(e['y']))}")
                self.send("key UseItem tap")
                R["items_used"] += 1
            return
        self.set_mash(False)
        if a in ("FLEE", "SMOKE"):
            w = self.current_world()
            tp = info["threat_pos"] or (me[0] + 5, me[1])
            target, climb = self.flee_plan(w, me, tp) if w else (None, "")
            if a == "SMOKE":
                self.send("key Dash tap")
                R["smoke_used"] += 1
            R["flee_count"] += 1
            self.set_crouch(0)
            if target and R["goto"] != target:
                self.send(f"goto {target[0]} {target[1]}{climb}")
                R["goto"] = target
                R["goto_at"] = self.obs.t
            return
        if a == "RETURN":
            b, climb = self.base_goal(R["base"])
            if R["goto"] != b:
                self.send(f"goto {b[0]} {b[1]} sneak{climb}")
                R["goto"] = b
                R["goto_at"] = self.obs.t
            return
        if a in ("FETCH", "EXPLORE"):
            t = info["target"] if a == "FETCH" else self.explore_target(info["me"], R["base"])
            if a == "FETCH":
                R["fetch_from_sound"] = bool(R.get("target_from_sound"))
                if R["fetch_from_sound"]:
                    R["sound_fetch"] = R.get("sound_fetch", 0) + 1
            if t and R["goto"] != t:
                sneak = " sneak" if self.sneak_needed(info) else ""
                self.send(f"goto {t[0]} {t[1]}{sneak}{self.climb_flag('out_back')}")
                R["goto"] = t
                R["goto_at"] = self.obs.t

    def keep_action(self):
        """判断と判断の間：歩き終わったら止まる（goto を送った直後の観測はまだ前の歩きの様子なので 0.3 秒待つ）"""
        R = self.round
        if R["goto"] is not None and self.obs.t - R.get("goto_at", -99.0) > 0.3 and self.obs.walk.get("state") in ("arrived", "stuck", "nopath", "idle"):
            state = self.obs.walk.get("state")
            kind = R.get("goto_kind")
            if R["phase"] == "travel":
                R["last_walk_result"] = (kind, state)
            if (R["phase"] == "travel" and kind == "treasure") or (R["phase"] == "hide" and R["action"] is not None and ACTIONS[R["action"]] == "FETCH"):
                self.rec.event("fetch", {"goal": R["goto"], "result": state, "at": self.obs.self_.get("pos"), "phase": R["phase"],
                                         "from_sound": bool(R.get("fetch_from_sound")), "got": R["treasures_last"]})
            R["goto_kind"] = None
            if R["goto"] in R["treasure_known"]:
                R["treasure_known"].pop(R["goto"], None)
            R.get("treasure_suspect", {}).pop(R["goto"], None)  # 音の手がかりの所に着いた（無ければ次の手がかり）
            if state == "stuck":
                self.rec.event("walk-stuck", {"at": self.obs.self_.get("pos"), "goal": R["goto"]})
            if state == "nopath":
                R.setdefault("unreachable", set()).add(tuple(R["goto"]))
            R["goto"] = None

    # ------------------------------------------------------------ 床の輪
    def my_mode(self, o):
        """今の自分の様子（輪がどの様子のときに出るかを数えるため）"""
        me = o.self_
        if me.get("climbing"):
            return "climb"
        if me.get("dashing"):
            return "dash"
        if o.walk.get("state") == "moving":
            return "sneak" if me.get("crouch") else "walk"
        if (o.walk.get("mash") or 0) > 0:
            return "mash"
        return "still"

    def ring_reach_cells(self, mode, me):
        """その様子のときの自分の輪が届く距離（マス。学んだ「鬼が気づく範囲」の倍率込み）。分からなければ None。
        このラウンドに同じ場所（1 マス以内・20 秒以内）で見た輪があればその大きさ（囲いの中では小さくなる）、無ければふだんの大きさ"""
        R = self.round
        rec = (R or {}).get("my_rings", {}).get(mode)
        if rec and self.obs.t - rec[0] <= 20.0 and math.dist(rec[1], me) <= 1.0:
            look, r = rec[2], rec[3]
        else:
            mr = self.brain.mode_ring(mode)
            if not mr:
                return None
            look, r = mr
        return r * self.brain.ring_reach(look) + 0.5

    def sneak_needed(self, info):
        """しのび足にするか。歩く輪が届いた鬼は気づくと学んだら、輪が鬼に届くかで決める（届かなければ速く歩く）。
        まだ学んでいなければ、鬼が近いとき（見えた・足音が 14 マス以内）"""
        mr = self.brain.mode_ring("walk")
        reach = self.ring_reach_cells("walk", info["me"]) if mr and self.brain.ring_reveals(mr[0]) else None
        if reach is None:
            return info["threat"] >= 1
        # 輪で決めるのは見えている鬼だけ（目分量）。壁の向こうの鬼は届くか分からないので、近ければ用心する（前のきまり）
        sneak = info.get("vis_d", 99.0) <= reach * (1.0 + self.rng.gauss(0.0, 0.15)) or info["chased"] or info["threat"] >= 2
        if self.round is not None and sneak != (info["threat"] >= 1):
            k = "ring_sneak" if sneak else "ring_walk"  # 輪で判断して、前のきまりと違う歩き方にした回数
            self.round[k] = self.round.get(k, 0) + 1
        return sneak

    def learn_rings(self, o, me, now):
        """床の輪から学ぶ：どの見た目の輪が自分の足元に出るか（そのときの自分の様子・大きさ）と、
        輪が出たときに見えていた鬼がその後 3 秒以内に反応したか（輪の中の鬼と外の鬼をくらべる）。
        反応＝左上の一覧のその鬼の様子のことばが変わった（新しいことばが出た）・輪の方へ向きを変えた・追ってきた。ことばの意味は使わない"""
        words_now = {}
        for name, word in reader.read_oni_list(o):
            words_now.setdefault(name.replace(" ", "_"), set()).add(word)
        def heading(e, c):
            """鬼の進む向きが c の方を向いている度合い（-1〜1。止まっていれば None）"""
            sp = math.hypot(e["vx"], e["vy"])
            dx, dy = c[0] - e["x"], c[1] - e["y"]
            d = math.hypot(dx, dy)
            if sp < 0.3 or d < 0.5:
                return None
            return (e["vx"] * dx + e["vy"] * dy) / (sp * d)
        keep = []
        for p in getattr(self, "_ring_pending", []):
            if not p["reacted"]:
                for e in o.see_onis:
                    if e["name"] != p["name"]:
                        continue
                    h = heading(e, p["c"])
                    if e["cue"] == "red-eyes" or (h is not None and p["h0"] < 0.5 and h >= 0.8 and h - p["h0"] >= 0.5):
                        p["reacted"] = True
                if p["w0"] is not None and words_now.get(p["name"], set()) - p["w0"]:
                    p["reacted"] = True
            if now - p["t"] >= 3.0:
                self.brain.note_ring_reaction(p["look"], p["ratio"], p["reacted"])
            else:
                keep.append(p)
        mode = self.my_mode(o)
        last = getattr(self, "_ring_last", {})
        for rg in o.see_rings:
            look = self.brain.ring_look(rg["color"], rg["w"])
            mine = rg["dist"] <= 1.0
            self.brain.note_ring(look, mine, mode, rg["r"])
            if mine and self.round is not None:
                self.round.setdefault("my_rings", {})[mode] = (now, (me[0], me[1]), look, rg["r"])
            # 輪が出たときに見えていた鬼（同じ見た目の輪は 1 秒に 1 回まで。もう追ってきている・止まっている鬼は数えない）
            key = (look, int(rg["x"]), int(rg["y"]))
            if rg["r"] < 0.3 or now - last.get(key, -99.0) < 1.0:
                continue
            last[key] = now
            c = (rg["x"], rg["y"])
            for e in o.see_onis:
                if e["cue"] != "-":
                    continue
                ratio = math.dist((e["x"], e["y"]), c) / rg["r"]
                h0 = heading(e, c)
                h0 = 0.0 if h0 is None else h0
                w0 = words_now.get(e["name"])
                if ratio <= 4.0:
                    keep.append({"t": now, "look": look, "name": e["name"], "c": c, "h0": h0, "w0": set(w0) if w0 else None,
                                 "ratio": ratio, "reacted": False})
        self._ring_last = {k: t for k, t in last.items() if now - t < 5.0}
        self._ring_pending = keep[-200:]

    def set_mash(self, on):
        R = self.round
        if R["mash_on"] != on:
            self.send(f"mash {6 if on else 0}")
            R["mash_on"] = on

    def set_crouch(self, v):
        R = self.round
        if R["crouch"] != v:
            self.send(f"crouch {v}")
            R["crouch"] = v

    # ------------------------------------------------------------ 感覚から学ぶ（音の危なさ・鬼の特徴）
    def learn_from_senses(self, o):
        now = o.t
        me = o.self_.get("pos", (0.0, 0.0))
        # 見えたお宝と歩いた所は、「もういいかい」の間も覚えておく
        if self.round is not None:
            for (x, y, d) in o.see_treasures:
                self.round["treasure_known"][(int(round(x)), int(round(y)))] = now
            self.round.setdefault("visited", set()).add((int(round(me[0])), int(round(me[1]))))
        self.learn_rings(o, me, now)
        near_oni = any(e["dist"] < 6 and e["cue"] not in ("frozen", "petting", "stuck") for e in o.see_onis)
        # 聞き分けのための「その方向にあった物」：見えている鬼（と 1 秒以内に見た鬼）・見えている／覚えているお宝・自分の置いた道具
        recent = getattr(self, "_recent_oni_pos", [])
        recent = [(t, x, y) for (t, x, y) in recent if now - t <= 1.0] + [(now, e["x"], e["y"]) for e in o.see_onis]
        self._recent_oni_pos = recent[-40:]
        oni_pts = [(x, y) for (t, x, y) in recent]
        seen_tr = [(x, y) for (x, y, d) in o.see_treasures]
        tr_pts = seen_tr + (list(self.round["treasure_known"].keys()) if self.round is not None else [])
        item_pts = [(x, y) for (x, y, name, d, live) in self.placed.get("items", [])]
        last_heard = getattr(self, "_last_heard", {})
        for hr in o.hear:
            if hr["dir"] not in ("-", "here"):
                self.hear_events.append([now, hr["name"], hr["loud"], hr["dir"], False])
                od = self.seen_in_dir(me, hr, oni_pts)
                td = self.seen_in_dir(me, hr, tr_pts)
                idist = self.seen_in_dir(me, hr, item_pts)
                # 大きさ → 距離は、今見えている物（その方向にそれしか無いとき）の距離で学ぶ
                vo = self.seen_in_dir(me, hr, [(e["x"], e["y"]) for e in o.see_onis])
                vt = self.seen_in_dir(me, hr, seen_tr)
                calib = vo if (vo is not None and vt is None) else vt if (vt is not None and vo is None) else None
                self.brain.note_sound_source(hr["name"], hr["loud"], od, td, idist, calib)
                # 鳴る間隔：同じ方角から同じ音が続けて聞こえるまでの秒数（足音のように細かく続くか、ときどき鳴るか）
                key = (hr["name"], hr.get("deg", hr["dir"]))
                prev = last_heard.get(key)
                if prev is not None and 0.05 < now - prev <= 3.0:
                    self.brain.note_rhythm(hr["name"], now - prev)
                last_heard[key] = now
        self._last_heard = {k: t for k, t in last_heard.items() if now - t <= 3.0}
        if now - getattr(self, "_last_dir_base", -1.0) >= 0.5:
            self._last_dir_base = now
            angles = [a * 15.0 for a in range(24)]
            self.brain.note_direction_base(sum(1 for a in angles if self.seen_in_dir(me, a, oni_pts) is not None) / 24.0,
                                           sum(1 for a in angles if self.seen_in_dir(me, a, tr_pts) is not None) / 24.0,
                                           sum(1 for a in angles if self.seen_in_dir(me, a, item_pts) is not None) / 24.0)
        # くらべるための「ふだん」（音に関係なく、0.5 秒ごとに同じ数え方をする）
        if now - getattr(self, "_last_base_ev", -1.0) >= 0.5:
            self._last_base_ev = now
            self.hear_events.append([now, "_base", 0.0, "-", False])
        if near_oni:
            for ev in self.hear_events:
                if now - ev[0] <= 3.0:
                    ev[4] = True
        keep = []
        for ev in self.hear_events:
            if now - ev[0] > 3.0:
                if ev[1] == "_base":
                    rec = self.brain.sounds.setdefault("_base", [0, 0])
                    rec[0] += 1
                    rec[1] += 1 if ev[4] else 0
                else:
                    self.brain.note_sound(ev[1], ev[2], ev[4])
            else:
                keep.append(ev)
        self.hear_events = keep[-400:]
        # 鬼を見たこと（速さ・様子）。音の大きさと距離の対応も（見えている鬼の向きから同じ向きの足音）
        for e in o.see_onis:
            p = self.brain.oni(e["name"])
            p["seen"] += 1
            sp = math.hypot(e["vx"], e["vy"])
            p["speed_sum"] += sp
            p["speed_n"] += 1
            p["max_speed"] = max(p["max_speed"], sp)
            cue = e["cue"]
            if cue == "hitting":
                p["hitting"] += 1
            elif cue == "stuck":
                p["stuck"] += 1
            elif cue == "frozen":
                p["frozen"] += 1
            elif cue == "petting":
                p["petting"] += 1
            elif cue == "red-eyes":
                p["chasing"] += 1
            if self.round is not None:
                self.round["last_seen"].append((now, e["name"], e["dist"]))
                self.round["last_seen"] = self.round["last_seen"][-20:]
                if self.round.get("first_seen_oni") is None:
                    self.round["first_seen_oni"] = (e["x"], e["y"])
                    sp_mem = self.brain.onis.setdefault("_spawn", {"n": 0, "x": 0.0, "y": 0.0})
                    if self.round.get("t0") is not None and now - self.round["t0"] < 4.0:
                        sp_mem["n"] += 1
                        sp_mem["x"] += e["x"]
                        sp_mem["y"] += e["y"]
        if self.round is not None:
            for name, word in reader.read_oni_list(o):
                self.word_events.append([now, word])
            self.word_events = [ev for ev in self.word_events if now - ev[0] <= 5.0][-300:]

    # ------------------------------------------------------------ ラウンドの終わり
    def finish_round(self):
        R = self.round
        o = self.obs
        if R is None or R.get("finished"):
            return
        R["finished"] = True
        outcome = reader.result_outcome(o)
        cleared = bool(outcome)
        # 隠れていた秒数：鬼が出てきてから最後に見たかくれんぼの画面まで（ゲームの時間。止まっていた間は数えない）
        survived = 0.0
        if R["t0"] is not None:
            survived = max(0.0, R.get("t_obs", R["t0"]) - R["t0"])
        limit = R["limit"] or 30
        if cleared:
            survived = limit
        frac = min(1.0, survived / max(1.0, limit))
        terminal = 3.0 if cleared else -3.0
        if R["state"] is not None and R["action"] is not None:
            self.brain.qtable.update(R["state"], R["action"], R["reward"] + terminal, None, [], terminal=True)
        R["score"] += terminal
        score = frac + (0.5 if cleared else 0.0)  # 得点：隠れていられた割合（逃げ切りで +0.5）
        stage = R["stage"] or 1
        # 学習：隠れ場所・囲い方
        plan = R.get("plan")
        if self.plan and plan and self.plan.get("features"):
            self.brain.spot.update(self.plan["features"], frac)
            key = f"{self.plan['mapkey']}:{self.plan['base'][0]},{self.plan['base'][1]}"
            mem = self.brain.spot_memory.setdefault(key, [0, 0.0])
            mem[0] += 1
            mem[1] += frac
            self.brain.templates[self.plan["template"]].update(self.plan["tfeat"], frac)
            # 隠れ場所までたどり着けた館：次からはスタートを隠れ場所のそばに（着いた所を覚える）
            if R.get("reached") == "arrived":
                ss = self.brain.start_spots.setdefault(self.plan["mapkey"], {"cell": None, "n": 0, "score": 0.0})
                ss["cell"] = list(R["base"])
                ss["n"] += 1
                ss["score"] += frac
            ts = self.brain.template_stats.setdefault(self.plan["template"], [0, 0.0])
            ts[0] += 1
            ts[1] += score
        # 商品：このラウンドの前に買った物（基準より良かったか）
        base_score = self.stage_baseline.get(stage, score)
        adv = score - base_score
        self.stage_baseline[stage] = 0.8 * base_score + 0.2 * score
        for item in self.shop_basket:
            if not item.get("required"):
                self.brain.note_item(item["name"], item["roles"], adv)
        # 勝ちやすさ：買った物・使った宝石の道具は今回の、前のラウンドで買ったお金の強化（ゴールドラッシュも）は次回の分として
        win_lift = self.brain.clear_baseline(stage, cleared)
        for name in self.next_credit:
            self.brain.note_item_win(name, "next", win_lift)
        names = {item["name"] for item in self.shop_basket if item.get("currency") != "pt" and not item.get("required")} | set(R["gem_items"])
        for name in names:
            self.brain.note_item_win(name, "now", win_lift)
        self.next_credit = [n for n in names if self.brain.is_income(self.brain.items.get(n, {}).get("roles", []))]
        self.shop_basket = []
        # お金の目安（得点の正規化に使う）
        earned = max(0.0, (R["coins_last"] or 0) - (R["coins0"] or 0))
        self.coin_scale[stage] = 0.7 * self.coin_scale.get(stage, earned or 50.0) + 0.3 * max(earned, 10.0)
        # 見つかった：直前に近くに見えた鬼（自分が見たもの）と、そのとき出ていた様子のことば
        caught_by_seen = None
        if not cleared:
            recent = [(n, d) for (t, n, d) in R["last_seen"] if o.t - t < 3.0]
            if recent:
                caught_by_seen = min(recent, key=lambda p: p[1])[0]
                self.brain.oni(caught_by_seen)["caught_me"] += 1
        for word in {w for (t, w) in self.word_events}:
            self.brain.note_word(word, not cleared)
        for name in ((R.get("plan") or {}).get("onis") or {}):
            self.brain.oni(name)["rounds"] += 1
        # つまずき（同じステージで続けて見つかった数・隠れ場所を見つけてから見つかった数）
        if R.get("reached") == "arrived":
            self.found_spot.add(stage)
        if cleared:
            self.fail_streak[stage] = 0
            self.fails_after_found[stage] = 0
            self.lap_best = max(self.lap_best, stage + 1)
        else:
            self.fail_streak[stage] = self.fail_streak.get(stage, 0) + 1
            if stage in self.found_spot:
                self.fails_after_found[stage] = self.fails_after_found.get(stage, 0) + 1
        # 宝石のお店の道具：使ったラウンドの結果と、見えた効き目
        for name in R["gem_items"]:
            rec = self.brain.gem_trials.setdefault(name, {"bought": 0, "rounds": 0, "score_sum": 0.0, "clears": 0, "stage_sum": 0, "effects": {}})
            rec["rounds"] += 1
            rec["score_sum"] += score
            rec["clears"] += 1 if cleared else 0
            rec["stage_sum"] += stage
            for k, v in R["effects"].items():
                rec["effects"][k] = rec["effects"].get(k, 0) + v
        self.brain.episodes += 1
        self.brain.history.append(round(score, 3))
        self.rec.round_end({
            "phase_at_end": R["phase"], "travel_time": R["travel_time"], "reached": R["reached"], "travel_treasures": R["travel_treasures"],
            "travel_flee": R["travel_flee"], "base_switches": R["base_switches"], "base_final": R["base"],
            "start": (self.plan or {}).get("start"), "gem_items": R["gem_items"], "effects": R["effects"],
            "round": R["no"], "persona": self.persona, "stage_read": stage, "cleared": cleared, "survived": round(survived, 1),
            "limit": limit, "score": round(score, 3), "q_return": round(R["score"], 2), "plan": plan,
            "actions": R["actions"], "decisions": R["decisions"], "chased": R["chased_ever"], "flee": R["flee_count"],
            "smoke": R["smoke_used"], "climb_flee": R.get("climb_flee", 0), "perch_time": round(R.get("perch_time", 0.0), 1), "climb_known": sorted(k for k in self.brain.abilities if self.can_climb), "items_used": R["items_used"], "caught_by_seen": caught_by_seen,
            "mash_stopped": R.get("mash_stopped", 0), "ring_choice": R.get("ring_choice", {}), "ring_sneak": R.get("ring_sneak", 0), "ring_walk": R.get("ring_walk", 0),
            "sound_fetch": R.get("sound_fetch", 0),
            "treasures_got": R["treasures_last"], "treasures_total": R["treasures_total"], "round_coins": round(earned, 1),
            "fail_streak": self.fail_streak.get(stage, 0), "found_spot": stage in self.found_spot,
            "fails_after_found": self.fails_after_found.get(stage, 0), "prestige_arm": self.prestige_arm, "real_seconds": round(time.time() - R["real_start"], 1),
        })
        try:
            self.insights.check()
        except Exception as e:  # 気づきの記録の失敗で遊ぶのを止めない
            self.rec.event("agent-error", {"error": "insights: " + repr(e)[:200], "phase": "Result"})
        self.brain.save()
        self.round = None
