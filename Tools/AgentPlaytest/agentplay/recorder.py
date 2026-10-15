"""記録係。報告書のために、ラウンド・出来事・不具合をファイルに書く。

ここだけが TEL（ゲームの本当の値：ステージ・転生の回数・見つけた鬼の種類…）を読む。AI の判断には渡さない。
  <記録のフォルダ>/<性格>/rounds.jsonl   1 ラウンド 1 行
  <記録のフォルダ>/<性格>/events.jsonl   お店・設置・転生・初めて読んだ鬼・ステージ到達・起動 など
  <記録のフォルダ>/<性格>/bugs.jsonl     見つけたおかしなこと（同じものは 1 行。回数を数える）
"""

import json
import os
import time


class Recorder:
    def __init__(self, folder, persona):
        self.dir = os.path.join(folder, persona)
        os.makedirs(self.dir, exist_ok=True)
        self.persona = persona
        meta_path = os.path.join(folder, "run.json")
        try:
            with open(meta_path, "r", encoding="utf-8") as f:
                self.meta = json.load(f)
        except (OSError, ValueError):
            self.meta = {"started": time.time()}
            with open(meta_path, "w", encoding="utf-8") as f:
                json.dump(self.meta, f)
        self.run_start = self.meta["started"]
        self.tel = {}
        self.tel_onis = []
        self.hide_tel = {}
        self.hide_onis = []
        self.last_roundend = None
        self.prestige = None
        self.gadgets = {}
        self.bug_counts = {}
        self.reached = set()
        self._load_state()

    def _path(self, name):
        return os.path.join(self.dir, name)

    def _append(self, name, obj):
        with open(self._path(name), "a", encoding="utf-8") as f:
            f.write(json.dumps(obj, ensure_ascii=False) + "\n")

    def _load_state(self):
        # 前の起動までの最高ステージ・転生回数（起動し直しても「初めて到達」を二重に数えない）
        try:
            with open(self._path("events.jsonl"), "r", encoding="utf-8") as f:
                for line in f:
                    try:
                        e = json.loads(line)
                    except ValueError:
                        continue
                    if e.get("type") == "stage-reached":
                        self.reached.add((e["data"]["prestige"], e["data"]["stage"]))
        except OSError:
            pass
        try:
            with open(self._path("bugs.jsonl"), "r", encoding="utf-8") as f:
                for line in f:
                    try:
                        b = json.loads(line)
                        self.bug_counts[b["key"]] = b.get("count", 1)
                    except (ValueError, KeyError):
                        continue
        except OSError:
            pass
        try:
            with open(self._path("bug_counts.json"), "r", encoding="utf-8") as f:
                self.bug_counts.update(json.load(f))
        except (OSError, ValueError):
            pass

    def elapsed(self):
        return time.time() - self.run_start

    def event(self, kind, data):
        self._append("events.jsonl", {"time": time.strftime("%Y-%m-%d %H:%M:%S"), "elapsed": round(self.elapsed(), 1), "type": kind, "data": data})

    def observe(self, o):
        if o.tel:
            self.tel = o.tel
            stage = o.tel.get("stage")
            prestige = o.tel.get("prestige")
            if stage is not None and prestige is not None and (prestige, stage) not in self.reached:
                self.reached.add((prestige, stage))
                self.event("stage-reached", {"stage": stage, "prestige": prestige, "map": o.tel.get("map"), "coins": o.tel.get("coins")})
            if self.prestige is not None and prestige is not None and prestige > self.prestige:
                self.event("prestige-tel", {"prestige": prestige, "pp": o.tel.get("pp"), "tpp": o.tel.get("tpp")})
            self.prestige = prestige
            if o.tel.get("phase") == "Hide":
                self.hide_tel = dict(o.tel)
                if o.tel_onis:
                    self.hide_onis = list(o.tel_onis)
                # 宝石のお店の道具の効き目（本当の値）
                g = self.gadgets
                g["dark"] = max(g.get("dark", 0), int(o.tel.get("dark", 0) or 0))
                g["goldrush"] = max(g.get("goldrush", 0), int(o.tel_gadgets.get("goldrush", 0) or 0))
                g["frozen_max"] = max(g.get("frozen_max", 0), int(o.tel_gadgets.get("frozen", 0) or 0))
                for oni in o.tel_onis:
                    cue = oni.get("cue")
                    if cue in ("petting", "doll", "frozen"):
                        g[cue + "_obs"] = g.get(cue + "_obs", 0) + 1
        if o.tel_roundend:
            self.last_roundend = o.tel_roundend

    def bug(self, line, o):
        kind = line.split(" ", 1)[0]
        # 同じ不具合（種類と中身）は 1 行にまとめて数える。shot= の違いは同じものとみなす
        key = line.split(" shot=")[0]
        self.bug_counts[key] = self.bug_counts.get(key, 0) + 1
        if self.bug_counts[key] == 1:
            self._append("bugs.jsonl", {"key": key, "kind": kind, "line": line, "count": 1, "time": time.strftime("%Y-%m-%d %H:%M:%S"),
                                        "elapsed": round(self.elapsed(), 1), "phase": o.phase, "stage": self.tel.get("stage"),
                                        "prestige": self.tel.get("prestige"), "map": self.tel.get("map")})

    def round_end(self, data):
        t = self.hide_tel or self.tel
        rec = dict(data)
        rec.update({
            "time": time.strftime("%Y-%m-%d %H:%M:%S"), "elapsed": round(self.elapsed(), 1),
            "stage": t.get("stage"), "prestige": t.get("prestige"), "map": t.get("map"), "limit_true": t.get("limit"),
            "onis_true": sorted(o.get("type", "?") for o in self.hide_onis), "roundend": self.last_roundend,
            "cleared_true": (self.last_roundend or "").startswith("cleared"),
            "earned": self.tel.get("earned"), "mash_coins": self.tel.get("mashcoins"), "time_coins": self.tel.get("timecoins"),
            "clear_reward": self.tel.get("clearreward"), "treasures_true": f"{self.tel.get('treasures')}", "gems": self.tel.get("gems"),
            "walls_broken": self.tel.get("broken"), "traps_triggered": self.tel.get("traps"), "summons": self.tel.get("summons"),
            "coins_after": self.tel.get("coins"), "gadgets_true": dict(self.gadgets),
        })
        self.gadgets = {}
        rb = self.last_roundend or ""
        if rb.startswith("caught"):
            parts = dict(p.split("=", 1) for p in rb.split()[1:] if "=" in p)
            rec["caught_by_true"] = parts.get("by")
            rec["caught_state_true"] = parts.get("state")
            rec["caught_dist_true"] = parts.get("dist")
        self._append("rounds.jsonl", rec)
        self.last_roundend = None
        self.hide_onis = []
        self.save_bug_counts()

    def save_bug_counts(self):
        # 回数は別のファイルにまとめて書く（bugs.jsonl は初めて見たときの 1 行だけ）
        with open(self._path("bug_counts.json"), "w", encoding="utf-8") as f:
            json.dump(self.bug_counts, f, ensure_ascii=False)
