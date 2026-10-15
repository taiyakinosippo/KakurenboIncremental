"""ゲームのテキストの窓口（TCP）とのやり取り。

届いた文字を「観測（Obs）」の形に分けるだけで、意味は解釈しない（解釈は reader.py）。
TEL（記録用の本当の値）は Obs.tel に分けて入れる。AI の判断（agent.py の方針）では使わず、記録（recorder.py）だけが使う。
"""

import re
import socket
import time


class Obs:
    """1 回分の観測。セクションが無かったものは None / 空"""

    def __init__(self):
        self.seq = 0
        self.t = 0.0  # ゲームの時間（秒。止まっている間は進まない）
        self.rt = 0.0  # 実時間（秒）
        self.phase = ""
        self.menu = ""
        self.acks = []  # (id, 結果の文字)
        self.screen = []  # (メニューの文か, 大きさ, 文)
        self.buttons = []  # (id "ShopItem.3", ボタンの文字)
        self.labels = []  # (名前, x, y)
        self.self_ = {}
        self.walk = {}
        self.gauges = {}  # 名前 -> {left, max}
        self.see_onis = []  # dict(name, x, y, vx, vy, dist, cue)
        self.see_treasures = []  # (x, y, dist)
        self.hear = []  # dict(name, dir, loud, muffled)
        self.map_size = None
        self.map_rows = None  # 送られてきたときだけ
        self.placed = None  # 送られてきたときだけ {"walls": [...], "items": [...]}
        self.tel = {}  # 記録用（判断には使わない）
        self.tel_onis = []
        self.tel_gadgets = {}
        self.tel_roundend = None
        self.bugs = []

    def texts(self, menu_only=False):
        return [t for (m, s, t) in self.screen if m or not menu_only]

    def button(self, prefix):
        """id が prefix で始まるボタン（最初の 1 つ）"""
        for bid, label in self.buttons:
            if bid == prefix or bid.startswith(prefix):
                return bid, label
        return None


_NUM = re.compile(r"^-?\d+(\.\d+)?$")


def _kv(tokens):
    d = {}
    for tok in tokens:
        if "=" not in tok:
            continue
        k, v = tok.split("=", 1)
        if "," in v and all(_NUM.match(p) for p in v.split(",")):
            d[k] = tuple(float(p) for p in v.split(","))
        elif _NUM.match(v):
            d[k] = float(v) if "." in v else int(v)
        else:
            d[k] = v
    return d


def parse_obs(text):
    o = Obs()
    section = None
    for line in text.split("\n"):
        if line.startswith("> "):
            body = line[2:]
            if section == "SCREEN":
                m = re.match(r"([sm])(\d+) (.*)$", body)
                if m:
                    o.screen.append((m.group(1) == "m", int(m.group(2)), m.group(3)))
            elif section == "BUTTONS":
                bid, _, label = body.partition(" | ")
                o.buttons.append((bid.strip(), label.strip()))
            elif section == "LABELS":
                name, _, pos = body.rpartition(" @ ")
                try:
                    x, y = (float(p) for p in pos.split(","))
                    o.labels.append((name, x, y))
                except ValueError:
                    pass
            elif section == "MAP":
                o.map_rows.append(body)
            elif section == "PLACED":
                parts = body.split()
                if len(parts) >= 2:
                    kind = parts[0]
                    x, y = (int(p) for p in parts[1].split(","))
                    kv = _kv(parts[2:])
                    if kind == "wall":
                        o.placed["walls"].append((x, y, int(kv.get("h", 0)), int(kv.get("plan", 0)), str(kv.get("name", "-"))))
                    elif kind == "item":
                        o.placed["items"].append((x, y, str(kv.get("name", "-")), int(kv.get("dir", -1)), int(kv.get("live", 1))))
            continue
        section = None
        if not line:
            continue
        head, _, rest = line.partition(" ")
        if head == "#OBS":
            parts = rest.split()
            o.seq = int(parts[0])
            kv = _kv(parts[1:])
            o.t = float(kv.get("t", 0.0))
            o.rt = float(kv.get("rt", 0.0))
        elif head == "PHASE":
            o.phase = rest.strip()
        elif head == "MENU":
            o.menu = rest.strip()
        elif head == "ACK":
            aid, _, result = rest.partition(" ")
            try:
                o.acks.append((int(aid), result.strip()))
            except ValueError:
                pass
        elif head in ("SCREEN", "BUTTONS", "LABELS"):
            section = head
        elif head == "PLACED":
            section = head
            o.placed = {"walls": [], "items": []}
        elif head == "MAP":
            section = "MAP"
            w, h = (int(p) for p in rest.split()[:2])
            o.map_size = (w, h)
            o.map_rows = []
        elif head == "SELF":
            o.self_ = _kv(rest.split())
        elif head == "WALK":
            o.walk = _kv(rest.split())
        elif head == "GAUGE":
            parts = rest.split()
            if parts:
                o.gauges[parts[0]] = _kv(parts[1:])
        elif head == "SEE":
            parts = rest.split()
            kv = _kv(parts[1:])
            if parts and parts[0] == "oni" and "pos" in kv:
                o.see_onis.append({"name": str(kv.get("name", "?")), "x": kv["pos"][0], "y": kv["pos"][1],
                                   "vx": kv.get("vel", (0.0, 0.0))[0], "vy": kv.get("vel", (0.0, 0.0))[1],
                                   "dist": float(kv.get("dist", 99)), "cue": str(kv.get("cue", "-"))})
            elif parts and parts[0] == "treasure" and "pos" in kv:
                o.see_treasures.append((kv["pos"][0], kv["pos"][1], float(kv.get("dist", 99))))
        elif head == "HEAR":
            parts = rest.split()
            if parts:
                kv = _kv(parts[1:])
                o.hear.append({"name": parts[0], "dir": str(kv.get("dir", "-")), "deg": kv.get("deg"), "loud": float(kv.get("loud", 0)),
                               "muffled": int(kv.get("muffled", 0))})
        elif head == "TEL":
            parts = rest.split()
            if parts and parts[0] == "oni":
                o.tel_onis.append(_kv(parts[1:]))
            elif parts and parts[0] == "gadgets":
                o.tel_gadgets = _kv(parts[1:])
            elif parts and parts[0] == "roundend":
                o.tel_roundend = " ".join(parts[1:])
            else:
                o.tel = _kv(parts)
        elif head == "BUG":
            o.bugs.append(rest.strip())
    return o


class Link:
    """ゲームとの TCP。poll() で届いた観測を全部返す。send() は 1 行の操作を送って番号を返す"""

    def __init__(self, port, host="127.0.0.1"):
        self.host = host
        self.port = port
        self.sock = None
        self.buf = b""
        self.next_id = 1
        self.last_recv = time.time()

    def connect(self, timeout=240.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                self.sock = socket.create_connection((self.host, self.port), timeout=3)
                self.sock.setblocking(False)
                self.buf = b""
                self.last_recv = time.time()
                return True
            except OSError:
                time.sleep(1.0)
        return False

    def connected(self):
        return self.sock is not None

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None

    def poll(self):
        if not self.sock:
            return []
        while True:
            try:
                data = self.sock.recv(1 << 20)
                if not data:
                    self.close()
                    break
                self.buf += data
                self.last_recv = time.time()
            except BlockingIOError:
                break
            except OSError:
                self.close()
                break
        out = []
        while b"#END\n" in self.buf:
            chunk, self.buf = self.buf.split(b"#END\n", 1)
            try:
                out.append(parse_obs(chunk.decode("utf-8", "replace")))
            except Exception:  # 壊れた 1 回分は読み飛ばす
                pass
        return out

    def send(self, command):
        if not self.sock:
            return -1
        cid = self.next_id
        self.next_id += 1
        try:
            self.sock.setblocking(True)
            self.sock.sendall(f"{cid} {command}\n".encode("utf-8"))
        except OSError:
            self.close()
            return -1
        finally:
            if self.sock:
                self.sock.setblocking(False)
        return cid
