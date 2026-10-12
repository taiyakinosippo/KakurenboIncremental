# -*- coding: utf-8 -*-
"""
館の間取り（KakurenboIncremental/Data/Maps/*.txt）を作る（M14：本物の館らしく）。

  python Tools/GenerateMansion.py            # 全部の館を作り直す
  python Tools/GenerateMansion.py Parlor     # 1 つだけ
  python Tools/GenerateMansion.py --preview  # 書き出さずに画面に出す

館の作り:
  - 東の真ん中の赤い門（鬼の出入り口）の前が「玄関ホール」。そこから西へ廊下（2 マス幅）がのびる
  - 広い館は、廊下から北と南へも廊下が分かれる
  - 廊下とホールの残りの場所を部屋に分ける。部屋どうし・廊下との間は部屋の壁（#）。出入り口は 1〜2 マス
  - 部屋には役割（居間・寝室・台所・浴室…）があり、家具はその部屋の決まりで置く（壁ぎわに本棚、真ん中に食卓と椅子…）
  - 床の種類（[floor] の文字。Data/FloorKinds.csv）も部屋ごと（台所・浴室はタイル、玄関ホールは石…）
  - 全部の床へ、門から歩いて行けることを確かめる。家具の量は館ごとの目標（床のマスに対する割合）に合わせる

家具の文字は Data/Furniture.csv の行の名前。手で直してもよい（このスクリプトで作り直すと上書きされる）。
"""
import os
import random
import sys
import zlib
from collections import deque

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP_DIR = os.path.join(ROOT, "KakurenboIncremental", "Data", "Maps")

WALL = "#"
FLOOR = "."

# 家具の文字（Furniture.csv の行の名前。FName は大文字と小文字を区別しないので、大文字・数字・記号だけを使う）
#   背の高さは隠れ方に効く：高い（立ったまま隠れられる）・低い（しゃがめば隠れられる）
TALL = set("BSKOXF3L2")   # 本棚・棚・大時計・衣装だんす・食器棚・暖炉・柱・望遠鏡・植木
NAMES = {
    "B": "本棚", "S": "棚", "T": "テーブル", "A": "ひじかけ椅子", "F": "暖炉", "G": "地球儀", "K": "大時計",
    "P": "つぼ", "R": "蓄音機", "L": "望遠鏡", "U": "トランクの山", "W": "まき", "C": "じゅうたん",
    "D": "ベッド", "O": "衣装だんす", "N": "たんす", "E": "机", "H": "椅子", "I": "食卓", "J": "ソファ",
    "Q": "調理台", "V": "かまど", "X": "食器棚", "Y": "浴槽", "Z": "洗面台", "M": "樽", "1": "宝箱",
    "2": "植木", "3": "柱", "4": "長いす", "5": "小さな台",
}
WALKABLE = set("C")  # じゅうたんは上を歩ける（家具の量に数えない）

# 部屋の種類：日本語の名前・床の種類
ROOM_TYPES = {
    "entrance": ("玄関ホール", "m"),
    "corridor": ("廊下", "p"),
    "parlor": ("居間", "w"),
    "study": ("書斎", "w"),
    "library": ("書庫", "p"),
    "dining": ("食堂", "w"),
    "kitchen": ("台所", "t"),
    "bedroom": ("寝室", "w"),
    "nursery": ("子ども部屋", "w"),
    "bath": ("浴室", "b"),
    "storeroom": ("物置", "s"),
    "guest": ("客間", "w"),
    "cellar": ("貯蔵庫", "s"),
    "conservatory": ("温室", "t"),
    "chapel": ("礼拝堂", "s"),
    "clock": ("時計の間", "p"),
    "observatory": ("天文室", "p"),
    "ballroom": ("舞踏室", "p"),
    "vault": ("宝物庫", "s"),
    "hall": ("大広間", "p"),
    "throne": ("主の間", "p"),
    "gallery": ("画廊", "p"),
    "attic": ("屋根裏部屋", "w"),
    "pantry": ("食料庫", "s"),
}

# 館ごとの設定：行の名前・名前・大きさ・目玉の部屋・家具の量（床のマスに対する割合）・ほかに置きたい部屋（先頭から）
# 転生の壁の館（ステージ 8・13・18・23）は家具を少なめ
MAPS = [
    ("Parlor", "居間", 16, "parlor", 0.22, ["study", "bath", "storeroom"]),
    ("Study", "書斎", 17, "study", 0.22, ["library", "bedroom", "bath"]),
    ("Nursery", "子ども部屋", 18, "nursery", 0.22, ["bedroom", "bath", "storeroom", "study"]),
    ("Library", "書庫", 19, "library", 0.23, ["study", "storeroom", "bath", "bedroom"]),
    ("Dining", "食堂", 20, "dining", 0.22, ["kitchen", "pantry", "parlor", "bath"]),
    ("Storeroom", "物置", 21, "storeroom", 0.23, ["storeroom", "kitchen", "pantry", "bedroom", "bath"]),
    ("Rooms", "客間", 22, "guest", 0.22, ["guest", "bedroom", "bath", "parlor", "study"]),
    ("Cellar", "地下室", 23, "cellar", 0.15, ["storeroom", "pantry", "cellar", "kitchen", "bath"]),
    ("Gallery", "回廊", 24, "gallery", 0.21, ["parlor", "study", "bedroom", "bath", "library"]),
    ("Hall", "大広間", 25, "hall", 0.21, ["dining", "kitchen", "parlor", "bath", "storeroom"]),
    ("Conservatory", "温室", 26, "conservatory", 0.21, ["parlor", "dining", "kitchen", "bedroom", "bath"]),
    ("Archive", "文書庫", 27, "library", 0.22, ["library", "study", "storeroom", "bedroom", "bath"]),
    ("Attic", "屋根裏", 28, "attic", 0.15, ["storeroom", "attic", "bedroom", "storeroom", "bath"]),
    ("Chapel", "礼拝堂", 29, "chapel", 0.21, ["study", "library", "bedroom", "guest", "bath", "storeroom"]),
    ("ClockHall", "時計の間", 30, "clock", 0.21, ["parlor", "study", "dining", "kitchen", "bedroom", "bath"]),
    ("GuestWing", "離れ", 31, "guest", 0.21, ["guest", "guest", "bedroom", "bath", "bath", "parlor", "storeroom"]),
    ("GreatLibrary", "大書庫", 32, "library", 0.22, ["library", "study", "study", "storeroom", "bedroom", "bath"]),
    ("Labyrinth", "迷いの間", 33, "gallery", 0.15, ["storeroom", "cellar", "bedroom", "study", "bath", "storeroom"]),
    ("Ballroom", "舞踏室", 34, "ballroom", 0.20, ["dining", "kitchen", "parlor", "guest", "bath", "storeroom"]),
    ("Throne", "主の間", 35, "throne", 0.20, ["bedroom", "study", "library", "dining", "kitchen", "bath"]),
    ("Observatory", "天文台", 36, "observatory", 0.20, ["library", "study", "bedroom", "storeroom", "bath", "parlor"]),
    ("Banquet", "大食堂", 37, "dining", 0.21, ["kitchen", "pantry", "kitchen", "parlor", "guest", "bath"]),
    ("Vault", "宝物庫", 38, "vault", 0.15, ["vault", "storeroom", "cellar", "study", "bath", "storeroom"]),
    ("Cloister", "中庭の回廊", 39, "conservatory", 0.20, ["chapel", "library", "dining", "bedroom", "guest", "bath"]),
    ("Crown", "王の間", 40, "throne", 0.20, ["hall", "dining", "kitchen", "bedroom", "library", "bath"]),
]


class Room:
    def __init__(self, x0, y0, x1, y1, kind="room"):
        self.x0, self.y0, self.x1, self.y1 = x0, y0, x1, y1  # 中の床（端を含む）
        self.kind = kind
        self.doors = []  # 出入り口のマス

    @property
    def w(self):
        return self.x1 - self.x0 + 1

    @property
    def h(self):
        return self.y1 - self.y0 + 1

    @property
    def area(self):
        return self.w * self.h

    def cells(self):
        for y in range(self.y0, self.y1 + 1):
            for x in range(self.x0, self.x1 + 1):
                yield (x, y)

    def contains(self, x, y):
        return self.x0 <= x <= self.x1 and self.y0 <= y <= self.y1


class Mansion:
    def __init__(self, name, n, seed):
        self.name = name
        self.n = n
        self.rng = random.Random(seed)
        self.grid = [[WALL] * n for _ in range(n)]
        self.floor = [["w"] * n for _ in range(n)]
        self.rooms = []
        self.mid = n // 2
        # 鬼の出入り口の前（何も置かない）：門のマスと、その 1 列手前（GameMode::ApplyCurrentMap と同じ）
        self.keep = set()
        for gy in range(self.mid - 1, self.mid + 2):
            for gx in (n - 1, n - 2):
                for dy in (-1, 0, 1):
                    for dx in (-1, 0):
                        self.keep.add((gx + dx, gy + dy))

    # ---- 基本 ----
    def inside(self, x, y):
        return 0 <= x < self.n and 0 <= y < self.n

    def get(self, x, y):
        return self.grid[y][x] if self.inside(x, y) else WALL

    def carve(self, room):
        for (x, y) in room.cells():
            self.grid[y][x] = FLOOR
        self.rooms.append(room)
        return room

    def room_at(self, x, y):
        for r in self.rooms:
            if r.contains(x, y):
                return r
        return None

    # ---- 1) 区切り：玄関ホール・廊下・部屋 ----
    def build_layout(self, featured):
        n, mid, rng = self.n, self.mid, self.rng
        ew = max(5, min(9, round(n * 0.22)))
        eh = 7 if n < 24 else 9
        hall = Room(n - ew, mid - eh // 2, n - 1, mid + eh // 2, "entrance")
        self.carve(hall)
        # 廊下（2 マス幅）：ホールから西へ。西の端に大きな部屋を置く館もある
        west_room = n >= 20 and rng.random() < 0.5
        cx0 = rng.randint(7, max(7, n // 3)) if west_room else 0
        corridor = Room(cx0, mid - 1, hall.x0 - 1, mid, "corridor")
        self.carve(corridor)
        corridors = [corridor]
        # 広い館は、廊下から北と南へ分かれる廊下
        branch_xs = []
        if n >= 34:
            third = (hall.x0 - cx0) // 3
            branch_xs = [cx0 + third - 1, cx0 + 2 * third - 1]
        elif n >= 26:
            branch_xs = [cx0 + (hall.x0 - cx0) // 2 - 1]
        for bx in branch_xs:
            north = Room(bx, 0, bx + 1, mid - 2, "corridor")
            south = Room(bx, mid + 1, bx + 1, n - 1, "corridor")
            self.carve(north)
            self.carve(south)
            corridors += [north, south]
        # 残りの場所を長方形のかたまりに分けて、部屋にする（かたまりの間は壁 1 マス）
        blocks = []
        if west_room:
            blocks.append((0, 0, cx0 - 2, n - 1))
        # 廊下の北と南（分かれた廊下で東西に区切る）
        segments = []
        start = corridor.x0
        for bx in branch_xs:
            segments.append((start, bx - 2))
            start = bx + 3
        segments.append((start, hall.x0 - 2))
        for (sx0, sx1) in segments:
            if sx1 - sx0 >= 2:
                blocks.append((sx0, 0, sx1, mid - 3))
                blocks.append((sx0, mid + 2, sx1, n - 1))
        # ホールの北と南
        if hall.y0 - 2 >= 2:
            blocks.append((hall.x0, 0, n - 1, hall.y0 - 2))
            blocks.append((hall.x0, hall.y1 + 2, n - 1, n - 1))
        max_room = 7 if n < 22 else 9 if n < 30 else 11
        rooms = []
        # 目玉の部屋：一番大きな部屋が取れるかたまりの、廊下に面した側に大きな部屋を取る（残りを分ける）
        fw_want = max(6, min(16, round(n * 0.5)))
        fh_want = max(5, min(13, round(n * 0.38)))
        big = max(blocks, key=lambda b: min(b[2] - b[0] + 1, fw_want) * min(b[3] - b[1] + 1, fh_want))
        blocks.remove(big)
        bx0, by0, bx1, by1 = big
        bw, bh = bx1 - bx0 + 1, by1 - by0 + 1
        fw = min(bw, fw_want)
        fh = min(bh, fh_want)
        if bw - fw - 1 < 3:
            fw = bw
        if bh - fh - 1 < 3:
            fh = bh
        # 廊下に近い側（北のかたまりなら下、南なら上）に寄せる。東西はどちらかの端
        fy0 = by1 - fh + 1 if by1 < mid else by0
        fx0 = bx0 if rng.random() < 0.5 or fw == bw else bx1 - fw + 1
        feature = Room(fx0, fy0, fx0 + fw - 1, fy0 + fh - 1, featured)
        rooms.append(feature)
        # 残り（横の残りと縦の残り）
        if fw < bw:
            if fx0 == bx0:
                self.split_block(fx0 + fw + 1, by0, bx1, by1, max_room, rooms)
            else:
                self.split_block(bx0, by0, fx0 - 2, by1, max_room, rooms)
        if fh < bh:
            if fy0 == by0:
                self.split_block(fx0, fy0 + fh + 1, fx0 + fw - 1, by1, max_room, rooms)
            else:
                self.split_block(fx0, by0, fx0 + fw - 1, fy0 - 2, max_room, rooms)
        for (x0, y0, x1, y1) in blocks:
            self.split_block(x0, y0, x1, y1, max_room, rooms)
        for r in rooms:
            self.carve(r)
        self.assign_kinds(featured)
        return corridors

    def split_block(self, x0, y0, x1, y1, max_room, out):
        """長方形（端を含む）を部屋に分ける。分ける線は壁 1 マス"""
        w, h = x1 - x0 + 1, y1 - y0 + 1
        if w < 3 or h < 3:
            if w >= 2 and h >= 2:
                out.append(Room(x0, y0, x1, y1))
            return
        rng = self.rng
        split_x = w > max_room and (w >= h or h <= max_room)
        split_y = h > max_room and not split_x
        # 大きすぎなくても、たまに分けて部屋の数を増やす（小さな館以外）
        if not split_x and not split_y and w * h > 40 and rng.random() < 0.45:
            split_x = w >= h
            split_y = not split_x
        if split_x and w >= 7:
            cut = rng.randint(x0 + 3, x1 - 3)
            self.split_block(x0, y0, cut - 1, y1, max_room, out)
            self.split_block(cut + 1, y0, x1, y1, max_room, out)
        elif split_y and h >= 7:
            cut = rng.randint(y0 + 3, y1 - 3)
            self.split_block(x0, y0, x1, cut - 1, max_room, out)
            self.split_block(x0, cut + 1, x1, y1, max_room, out)
        else:
            out.append(Room(x0, y0, x1, y1))

    def assign_kinds(self, featured):
        # 目玉の部屋は build_layout で決めてある。残りは広さで決める（ほかに置きたい部屋を先に）
        plain = [r for r in self.rooms if r.kind == "room"]
        plain.sort(key=lambda r: -r.area)
        wish = list(self.wish)
        small = {"bath", "storeroom", "pantry", "cellar", "vault"}
        for r in plain:
            pick = None
            for k in wish:
                if (k in small) == (r.area <= 16) or not wish:
                    pick = k
                    break
            if pick is None and wish:
                pick = wish[0]
            if pick is None:
                pick = "storeroom" if r.area <= 12 else self.rng.choice(["bedroom", "study", "guest", "parlor", "library", "storeroom"])
            if pick in wish:
                wish.remove(pick)
            r.kind = pick

    # ---- 2) 出入り口 ----
    def connect(self):
        """部屋に出入り口を開けて、全部の床へ門から歩いて行けるようにする"""
        rng = self.rng
        open_kinds = {"entrance", "corridor"}
        connected = set(id(r) for r in self.rooms if r.kind in open_kinds)
        # 廊下とホールはつながっているはず（廊下の東の端がホール、分かれた廊下は廊下に付く）
        pending = [r for r in self.rooms if id(r) not in connected]
        rng.shuffle(pending)
        progress = True
        while pending and progress:
            progress = False
            # 廊下・ホールに面した部屋から
            pending.sort(key=lambda r: 0 if self.neighbors(r, open_kinds) else 1)
            for r in list(pending):
                options = self.door_options(r, lambda o: id(o) in connected)
                if not options:
                    continue
                # 廊下・ホールへの出入り口を優先
                options.sort(key=lambda o: (0 if o[1].kind in open_kinds else 1, -len(o[0])))
                cells, other = options[0]
                self.open_door(r, cells)
                connected.add(id(r))
                pending.remove(r)
                progress = True
        # ときどき、となりの部屋とも出入り口でつなぐ（行き止まりばかりにならないように）
        for r in self.rooms:
            if r.kind in open_kinds or rng.random() > 0.3:
                continue
            options = [o for o in self.door_options(r, lambda o: o.kind not in open_kinds) if not any(c in r.doors for c in o[0])]
            if options:
                self.open_door(r, rng.choice(options)[0])

    def neighbors(self, room, kinds):
        return any(o.kind in kinds for _, o in self.door_options(room, lambda o: True))

    def door_options(self, room, accept):
        """部屋の壁の向こうにある部屋・廊下ごとに、出入り口にできる壁のマスの並び（1〜2 マス）"""
        found = {}
        sides = [
            ((room.x0, room.y0 - 1), (1, 0), (0, -1), room.w),  # 北
            ((room.x0, room.y1 + 1), (1, 0), (0, 1), room.w),   # 南
            ((room.x0 - 1, room.y0), (0, 1), (-1, 0), room.h),  # 西
            ((room.x1 + 1, room.y0), (0, 1), (1, 0), room.h),   # 東
        ]
        for (sx, sy), (ax, ay), (nx, ny), length in sides:
            run = []
            for i in range(length):
                wx, wy = sx + ax * i, sy + ay * i
                ox, oy = wx + nx, wy + ny
                other = self.room_at(ox, oy) if self.inside(ox, oy) else None
                ok = self.inside(wx, wy) and self.get(wx, wy) == WALL and other is not None and other is not room and accept(other)
                # 壁の両端（角）には開けない（出入り口の枠が付けられるように、両側は壁）
                ok = ok and 0 < i < length - 1
                if ok:
                    run.append((wx, wy, other))
                else:
                    if run:
                        self.add_runs(found, run)
                    run = []
            if run:
                self.add_runs(found, run)
        return list(found.values())

    def add_runs(self, found, run):
        # 同じ相手の壁の並びから、真ん中あたりに 2 マス（短ければ 1 マス）
        other = run[0][2]
        if len(run) >= 4:
            i = len(run) // 2 - 1 + self.rng.randint(-1, 1) * (1 if len(run) >= 6 else 0)
            i = max(0, min(len(run) - 2, i))
            cells = [(run[i][0], run[i][1]), (run[i + 1][0], run[i + 1][1])]
        else:
            i = len(run) // 2
            cells = [(run[i][0], run[i][1])]
        key = (id(other), cells[0][0] == cells[-1][0])
        if key not in found or len(found[key][0]) < len(cells):
            found[key] = (cells, other)

    def open_door(self, room, cells):
        for (x, y) in cells:
            self.grid[y][x] = FLOOR
            room.doors.append((x, y))
            self.door_cells.add((x, y))

    # ---- 3) 家具 ----
    def furnish(self, target):
        rng = self.rng
        self.reserved = set(self.keep)
        # 出入り口の前とその奥は空けておく
        for (x, y) in self.door_cells:
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    if abs(dx) + abs(dy) <= 1:
                        self.reserved.add((x + dx, y + dy))
        for room in self.rooms:
            FURNISH.get(room.kind, furnish_generic)(self, room)
        self.balance(target)

    def free(self, x, y):
        return self.inside(x, y) and self.grid[y][x] == FLOOR and (x, y) not in self.reserved

    def can_place(self, x0, y0, w, h):
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                if not self.free(x, y):
                    return False
        return True

    def place(self, ch, x0, y0, w, h, optional=False, check=True):
        if not self.can_place(x0, y0, w, h):
            return False
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                self.grid[y][x] = ch
        if check and ch not in WALKABLE and not self.all_reachable():
            for y in range(y0, y0 + h):
                for x in range(x0, x0 + w):
                    self.grid[y][x] = FLOOR
            return False
        self.placed.append((ch, x0, y0, w, h, optional))
        return True

    def remove(self, item):
        ch, x0, y0, w, h, _ = item
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                self.grid[y][x] = FLOOR
        self.placed.remove(item)

    def all_reachable(self):
        """門の前から、全部の床（じゅうたんの上も）へ歩いて行けるか"""
        start = (self.n - 1, self.mid)
        seen = {start}
        q = deque([start])
        while q:
            x, y = q.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                p = (x + dx, y + dy)
                if p not in seen and self.inside(*p) and (self.grid[p[1]][p[0]] == FLOOR or self.grid[p[1]][p[0]] in WALKABLE):
                    seen.add(p)
                    q.append(p)
        total = sum(1 for y in range(self.n) for x in range(self.n) if self.grid[y][x] == FLOOR or self.grid[y][x] in WALKABLE)
        return len(seen) == total

    def wall_spots(self, room, depth=1, length=1, sides="NSWE"):
        """部屋の壁ぎわに length × depth の家具を置ける場所（向き付き）。窓・出入り口の前は避ける"""
        spots = []
        if "N" in sides:
            for x in range(room.x0, room.x1 - length + 2):
                spots.append((x, room.y0, length, depth, "N"))
        if "S" in sides:
            for x in range(room.x0, room.x1 - length + 2):
                spots.append((x, room.y1 - depth + 1, length, depth, "S"))
        if "W" in sides:
            for y in range(room.y0, room.y1 - length + 2):
                spots.append((room.x0, y, depth, length, "W"))
        if "E" in sides:
            for y in range(room.y0, room.y1 - length + 2):
                spots.append((room.x1 - depth + 1, y, depth, length, "E"))
        self.rng.shuffle(spots)
        return [s for s in spots if self.can_place(s[0], s[1], s[2], s[3])]

    def along_wall(self, room, ch, length=1, depth=1, count=1, sides="NSWE", optional=False):
        placed = 0
        for (x, y, w, h, _) in self.wall_spots(room, depth, length, sides):
            if placed >= count:
                break
            if self.place(ch, x, y, w, h, optional):
                placed += 1
        return placed

    def corners(self, room, ch, count=2, optional=True):
        spots = [(room.x0, room.y0), (room.x1, room.y0), (room.x0, room.y1), (room.x1, room.y1)]
        self.rng.shuffle(spots)
        placed = 0
        for (x, y) in spots:
            if placed >= count:
                break
            if self.place(ch, x, y, 1, 1, optional):
                placed += 1
        return placed

    def center(self, room, ch, w, h, optional=False):
        cx = room.x0 + (room.w - w) // 2
        cy = room.y0 + (room.h - h) // 2
        for (dx, dy) in ((0, 0), (1, 0), (0, 1), (-1, 0), (0, -1), (1, 1), (-1, -1)):
            if self.place(ch, cx + dx, cy + dy, w, h, optional):
                return (cx + dx, cy + dy)
        return None

    def balance(self, target):
        """家具の量を目標に合わせる：少なければ小物（植木・つぼ・トランク…）を足し、多ければ足した小物から減らす"""
        rng = self.rng
        floor_cells = sum(1 for y in range(self.n) for x in range(self.n) if self.grid[y][x] != WALL)

        def amount():
            return sum(w * h for (ch, x, y, w, h, _) in self.placed if ch not in WALKABLE) / floor_cells

        rooms = [r for r in self.rooms if r.kind not in ("corridor",)]
        extras = {
            "entrance": "2P3", "parlor": "2PA5", "study": "2GS", "library": "BS2", "dining": "2PX", "kitchen": "MXQ",
            "bedroom": "N2U", "nursery": "1U5", "bath": "Z5", "storeroom": "UMW1", "guest": "N2A", "cellar": "MW1",
            "conservatory": "2P5", "chapel": "34", "clock": "K2", "observatory": "GB", "ballroom": "32A", "vault": "1U",
            "hall": "32P", "throne": "32P", "gallery": "2P3", "attic": "U1W", "pantry": "MS",
        }
        tries = 0
        while amount() < target - 0.01 and tries < 400:
            tries += 1
            room = rng.choice(rooms)
            ch = rng.choice(extras.get(room.kind, "2P"))
            self.along_wall(room, ch, 1, 1, 1, optional=True)
        # 廊下の壁ぎわにも少しだけ（大時計・植木。2 マス幅の廊下を 1 マスまでしか狭めない）
        tries = 0
        while amount() > target + 0.015 and tries < 400:
            tries += 1
            optional = [p for p in self.placed if p[5]]
            if not optional:
                break
            self.remove(rng.choice(optional))
        return amount()

    # ---- 4) 床の種類 ----
    def paint_floors(self):
        for r in self.rooms:
            kind = ROOM_TYPES.get(r.kind, ("", "w"))[1]
            for (x, y) in r.cells():
                self.floor[y][x] = kind
        # 出入り口は廊下と同じ寄木
        for (x, y) in self.door_cells:
            self.floor[y][x] = "p"

    # ---- 書き出し ----
    def text(self, display, target):
        floor_cells = sum(1 for y in range(self.n) for x in range(self.n) if self.grid[y][x] != WALL)
        furn = sum(1 for y in range(self.n) for x in range(self.n) if self.grid[y][x] not in (WALL, FLOOR) and self.grid[y][x] not in WALKABLE)
        names = []
        for r in self.rooms:
            if r.kind not in ("corridor",):
                names.append(ROOM_TYPES.get(r.kind, (r.kind,))[0])
        lines = [
            "; %s（%d x %d）。家具 %d%%（床のマスに対して）。部屋：%s" % (display, self.n, self.n, round(100 * furn / floor_cells), "・".join(names)),
            "; 生成: Tools/GenerateMansion.py（手で直してよい。作り直すと上書きされる）。右端の真ん中は鬼の出入り口の前なので空けておく",
        ]
        lines += ["".join(row) for row in self.grid]
        lines.append("[floor]")
        lines.append("; 床の種類（Data/FloorKinds.csv の行の名前。壁や家具の下のマスは使わない）")
        lines += ["".join(row) for row in self.floor]
        return "\n".join(lines) + "\n", furn / floor_cells


# ---------------------------------------------------------------- 部屋ごとの家具の置き方

def furnish_generic(m, r):
    m.along_wall(r, "S", 1, 1, 1)
    m.corners(r, "2", 1)


def furnish_entrance(m, r):
    # 玄関ホール：柱・植木・大時計・長いす。真ん中（門からの通り道）は空ける
    m.corners(r, "3", 4, optional=False)
    m.along_wall(r, "K", 1, 1, 1, sides="NS")
    m.along_wall(r, "4", 2, 1, 1, sides="NS")
    m.along_wall(r, "2", 1, 1, 2, sides="NS", optional=True)


def furnish_corridor(m, r):
    # 廊下：ところどころに大時計・植木・小さな台（2 マス幅を 1 マスまで）
    n = max(1, (r.w * r.h) // 18)
    for _ in range(n):
        ch = m.rng.choice("K2P5")
        m.along_wall(r, ch, 1, 1, 1, optional=True)


def furnish_parlor(m, r):
    m.along_wall(r, "F", 2 if r.w >= 6 else 1, 1, 1, sides="NS")
    sofa = m.center(r, "J", 2, 1)
    m.center(r, "C", min(4, r.w - 2), min(3, r.h - 2), optional=True)
    m.along_wall(r, "A", 1, 1, 2)
    m.along_wall(r, "B", 2, 1, 1)
    m.along_wall(r, "R", 1, 1, 1, optional=True)
    m.corners(r, "2", 2)


def furnish_study(m, r):
    m.along_wall(r, "B", min(3, max(1, r.w - 3)), 1, 2)
    desk = m.center(r, "E", 2, 1)
    if desk:
        m.place("H", desk[0], desk[1] + 1, 1, 1, optional=True)
    m.along_wall(r, "G", 1, 1, 1)
    m.along_wall(r, "A", 1, 1, 1, optional=True)
    m.corners(r, "2", 1)


def furnish_library(m, r):
    # 本棚の列（間は 2 マスの通路）と、読書の机
    vertical = r.h >= r.w
    if vertical:
        for x in range(r.x0 + 1, r.x1, 3):
            m.place("B", x, r.y0 + 1, 1, max(1, r.h - 3))
    else:
        for y in range(r.y0 + 1, r.y1, 3):
            m.place("B", r.x0 + 1, y, max(1, r.w - 3), 1)
    m.along_wall(r, "B", 2, 1, 2)
    m.along_wall(r, "T", 2, 1, 1, optional=True)
    m.corners(r, "G", 1)


def furnish_dining(m, r):
    # 食卓（長い）と両側の椅子、壁ぎわに食器棚
    tw = max(2, min(r.w - 4, 6))
    tw -= tw % 2  # 食卓は偶数の長さ（2 マスずつのテーブルを並べる）
    th = 2 if r.h >= 6 else 1
    pos = m.center(r, "I", tw, th)
    if pos:
        x0, y0 = pos
        for x in range(x0, x0 + tw):
            if m.rng.random() < 0.8:
                m.place("H", x, y0 - 1, 1, 1, optional=True)
            if m.rng.random() < 0.8:
                m.place("H", x, y0 + th, 1, 1, optional=True)
    m.along_wall(r, "X", 1, 1, 2)
    m.along_wall(r, "F", 2, 1, 1, sides="WE", optional=True)
    m.corners(r, "2", 2)


def furnish_kitchen(m, r):
    m.along_wall(r, "Q", min(4, max(2, r.w - 2)), 1, 1, sides="N")
    m.along_wall(r, "V", 2, 1, 1)
    m.along_wall(r, "Q", 2, 1, 1)
    m.along_wall(r, "X", 1, 1, 1)
    m.center(r, "T", 2, 1, optional=True)
    m.corners(r, "M", 2)


def furnish_bedroom(m, r, small=False):
    bed_w, bed_h = (1, 2) if small else (2, 2)
    placed = False
    for (x, y, w, h, side) in m.wall_spots(r, 1, 1, "NSWE"):
        # ベッドは頭を壁に付ける
        if side == "N" and m.place("D", x, y, bed_w, bed_h):
            placed = True
        elif side == "S" and m.place("D", x, y - bed_h + 1, bed_w, bed_h):
            placed = True
        elif side == "W" and m.place("D", x, y, bed_h, bed_w):
            placed = True
        elif side == "E" and m.place("D", x - bed_h + 1, y, bed_h, bed_w):
            placed = True
        if placed:
            break
    m.along_wall(r, "O", 1, 1, 1)
    m.along_wall(r, "N", 1, 1, 2)
    m.center(r, "C", min(3, r.w - 2), min(2, r.h - 2), optional=True)
    m.along_wall(r, "A", 1, 1, 1, optional=True)


def furnish_nursery(m, r):
    furnish_bedroom(m, r, small=True)
    m.along_wall(r, "1", 1, 1, 2)
    m.along_wall(r, "S", 1, 1, 1)
    m.center(r, "T", 1, 1, optional=True)


def furnish_bath(m, r):
    m.along_wall(r, "Y", 2, 1, 1)
    m.along_wall(r, "Z", 1, 1, 1)
    m.along_wall(r, "S", 1, 1, 1, optional=True)


def furnish_storeroom(m, r):
    m.along_wall(r, "S", 2, 1, 2)
    m.corners(r, "M", 2)
    m.along_wall(r, "U", 1, 1, 2)
    m.along_wall(r, "1", 1, 1, 1, optional=True)
    m.along_wall(r, "W", 1, 1, 1, optional=True)


def furnish_cellar(m, r):
    for y in range(r.y0 + 1, r.y1, 3):
        m.place("M", r.x0 + 1, y, max(1, min(r.w - 3, 5)), 1)
    m.along_wall(r, "S", 2, 1, 1)
    m.along_wall(r, "W", 1, 1, 2)


def furnish_guest(m, r):
    furnish_bedroom(m, r)
    m.along_wall(r, "T", 1, 1, 1, optional=True)
    m.corners(r, "2", 1)


def furnish_conservatory(m, r):
    for y in range(r.y0 + 1, r.y1, 3):
        for x in range(r.x0 + 1, r.x1, 3):
            m.place(m.rng.choice("22P"), x, y, 1, 1, optional=True)
    m.along_wall(r, "4", 2, 1, 2)
    m.center(r, "T", 2, 1, optional=True)


def furnish_chapel(m, r):
    # 長いすの列（真ん中に通路）と、奥の柱
    aisle = r.x0 + r.w // 2
    for y in range(r.y0 + 2, r.y1, 2):
        m.place("4", r.x0 + 1, y, max(1, aisle - r.x0 - 1), 1)
        m.place("4", aisle + 1, y, max(1, r.x1 - aisle - 1), 1)
    m.corners(r, "3", 4)


def furnish_clock(m, r):
    for y in range(r.y0 + 1, r.y1, 3):
        for x in range(r.x0 + 1, r.x1, 3):
            m.place(m.rng.choice("K3"), x, y, 1, 1)
    m.along_wall(r, "A", 1, 1, 2, optional=True)


def furnish_observatory(m, r):
    m.center(r, "L", 1, 1)
    m.along_wall(r, "B", 2, 1, 2)
    m.along_wall(r, "G", 1, 1, 2)
    m.along_wall(r, "E", 2, 1, 1)


def furnish_ballroom(m, r):
    # 広く空けて、壁ぎわに柱と椅子
    for x in range(r.x0 + 1, r.x1, 4):
        m.place("3", x, r.y0, 1, 1)
        m.place("3", x, r.y1, 1, 1)
    m.along_wall(r, "A", 1, 1, 4, sides="WE")
    m.along_wall(r, "R", 1, 1, 1)
    m.center(r, "C", min(5, r.w - 4), min(3, r.h - 4), optional=True)


def furnish_vault(m, r):
    m.along_wall(r, "1", 1, 1, 4)
    m.along_wall(r, "S", 2, 1, 2)
    m.corners(r, "3", 2)
    m.center(r, "U", 1, 1, optional=True)


def furnish_hall(m, r):
    # 大広間：長い食卓・暖炉・柱
    m.center(r, "I", max(2, min(r.w - 4, 8)), 2)
    m.along_wall(r, "F", 2, 1, 1, sides="NS")
    for x in range(r.x0 + 1, r.x1, 4):
        m.place("3", x, r.y0, 1, 1)
        m.place("3", x, r.y1, 1, 1)
    m.along_wall(r, "H", 1, 1, 4, optional=True)


def furnish_throne(m, r):
    # 主の間：奥に椅子（玉座）とじゅうたん、両側に柱の列
    m.center(r, "C", 2, max(2, r.h - 2), optional=True)
    m.along_wall(r, "A", 1, 1, 1, sides="W")
    for y in range(r.y0 + 1, r.y1, 3):
        m.place("3", r.x0 + 2, y, 1, 1)
        m.place("3", r.x1 - 2, y, 1, 1)
    m.along_wall(r, "F", 2, 1, 1, sides="NS")


def furnish_gallery(m, r):
    # 画廊：展示台と柱（壁の絵は Arena が飾る）
    for x in range(r.x0 + 2, r.x1 - 1, 3):
        m.place(m.rng.choice("5P3"), x, r.y0 + r.h // 2, 1, 1)
    m.along_wall(r, "4", 2, 1, 2, optional=True)


def furnish_attic(m, r):
    m.along_wall(r, "U", 1, 1, 3)
    m.along_wall(r, "1", 1, 1, 2)
    m.along_wall(r, "S", 1, 1, 1)
    m.corners(r, "W", 2)


def furnish_pantry(m, r):
    m.along_wall(r, "S", 2, 1, 2)
    m.corners(r, "M", 2)


FURNISH = {
    "entrance": furnish_entrance, "corridor": furnish_corridor, "parlor": furnish_parlor, "study": furnish_study,
    "library": furnish_library, "dining": furnish_dining, "kitchen": furnish_kitchen, "bedroom": furnish_bedroom,
    "nursery": furnish_nursery, "bath": furnish_bath, "storeroom": furnish_storeroom, "guest": furnish_guest,
    "cellar": furnish_cellar, "conservatory": furnish_conservatory, "chapel": furnish_chapel, "clock": furnish_clock,
    "observatory": furnish_observatory, "ballroom": furnish_ballroom, "vault": furnish_vault, "hall": furnish_hall,
    "throne": furnish_throne, "gallery": furnish_gallery, "attic": furnish_attic, "pantry": furnish_pantry,
}


def generate(name, display, n, featured, target, wish, seed):
    m = Mansion(name, n, seed)
    m.wish = wish
    m.door_cells = set()
    m.placed = []
    m.build_layout(featured)
    m.connect()
    m.paint_floors()
    m.furnish(target)
    return m


def build_one(spec, preview=False):
    name, display, n, featured, target, wish = spec
    best = None
    # うまく行かない（つながらない・家具が目標に届かない）ときは種を変えて作り直す
    for attempt in range(40):
        seed = zlib.crc32(("%s/%d" % (name, attempt)).encode())
        m = generate(name, display, n, featured, target, wish, seed)
        text, amount = m.text(display, target)
        ok = m.all_reachable() and abs(amount - target) < 0.03 and any(r.kind == featured for r in m.rooms)
        if ok:
            best = (text, amount)
            break
        if best is None or abs(amount - target) < abs(best[1] - target):
            best = (text, amount)
    text, amount = best
    if preview:
        print(text)
    else:
        with open(os.path.join(MAP_DIR, name + ".txt"), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    print("%-13s %2dx%-2d furniture %.0f%% (target %.0f%%)" % (name, n, n, amount * 100, target * 100))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    preview = "--preview" in sys.argv
    for spec in MAPS:
        if args and spec[0] not in args:
            continue
        build_one(spec, preview)


if __name__ == "__main__":
    main()
