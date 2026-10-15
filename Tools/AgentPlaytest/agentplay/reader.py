"""文を読む力（ゲームの仕様ではなく、ふつうの日本語の読み方）。

AI が持っている知識は「一般的なゲームの言葉の辞書」だけ：壁・音・速い・壊す・弱い…といった言葉が何を意味しそうか。
ゲームの数値・ルールは持たない。画面の文・ボタンの文字・チュートリアルを読んで、
  - 数（コイン・残り時間・お宝の数）
  - お店の商品（名前・値段・説明・持っている数）
  - 鬼の特徴（チュートリアルの文から「壁を壊す」「足音に敏感」「〇〇に弱い」などの印）
を取り出す。印は学習の「最初の見当（事前知識）」になり、遊んだ結果で上書きされる。
"""

import re

_SUFFIX = {"K": 1e3, "M": 1e6, "B": 1e9, "T": 1e12, "Qa": 1e15, "Qi": 1e18}


def parse_number(s):
    """「1,234」「1.23K」「2.50M」「1.20e21」を数にする（読めなければ None）"""
    if s is None:
        return None
    s = s.strip().replace(",", "")
    m = re.match(r"^(-?\d+(?:\.\d+)?)(e-?\d+)?(Qa|Qi|K|M|B|T)?", s)
    if not m:
        return None
    v = float(m.group(1) + (m.group(2) or ""))
    if m.group(3):
        v *= _SUFFIX[m.group(3)]
    return v


_NUMTOKEN = r"(-?\d[\d,]*(?:\.\d+)?(?:e-?\d+)?(?:Qa|Qi|K|M|B|T)?)"


def read_status(obs):
    """画面のいつもの数（コイン・宝石・ステージ・残り時間・お宝）。見つからない値は None"""
    st = {"coins": None, "gems": None, "stage": None, "round_coins": None, "time_left": None, "countdown": None,
          "treasure_got": None, "treasure_total": None, "oni_states": [], "chips": [], "daruma": 0, "usable_name": None}
    in_hide = obs.phase == "Hide"
    for is_menu, size, text in obs.screen:
        if is_menu:
            continue
        t = text.strip()
        m = re.match(r"^コイン\s+" + _NUMTOKEN + r"$", t)
        if m:
            st["coins"] = parse_number(m.group(1))
            continue
        m = re.match(r"^宝石\s+(\d+)$", t)
        if m:
            st["gems"] = int(m.group(1))
            continue
        m = re.search(r"ステージ\s*(\d+)", t)
        if m and st["stage"] is None:
            st["stage"] = int(m.group(1))
        m = re.match(r"^このラウンド\s*\+" + _NUMTOKEN, t)
        if m:
            st["round_coins"] = parse_number(m.group(1))
            continue
        m = re.match(r"^お宝\s*(\d+)\s*/\s*(\d+)$", t)
        if m:
            st["treasure_got"], st["treasure_total"] = int(m.group(1)), int(m.group(2))
            continue
        if in_hide and re.match(r"^\d+$", t):
            # 数だけの大きな文字：上の数字は残り時間、真ん中のとても大きい数字は始まるまでの数え
            if size >= 80:
                st["countdown"] = int(t)
            elif 26 <= size <= 44 and st["time_left"] is None:
                st["time_left"] = int(t)
            continue
        # かくれんぼ中にキーで使える道具（「[G] 〇〇 ×1」の形の文）
        m = re.match(r"^\[([^\]]+)\]\s*(.+?)\s*×(\d+)$", t) if in_hide else None
        if m:
            st["daruma"] = int(m.group(3))
            st["usable_name"] = m.group(2).strip()
        if size in (17, 18) and ("壁の中" in t or "消音壁の中" in t or "しゃがんで" in t or "真っ暗" in t or "ゴールドラッシュ" in t):
            st["chips"].append(t)
    return st


def read_oni_list(obs):
    """かくれんぼ中の左上の鬼の一覧（名前と様子のことば）。名前の行の次が様子"""
    names = []
    lines = [(s, t) for (m, s, t) in obs.screen if not m]
    for i, (size, text) in enumerate(lines):
        if size == 18 and text.endswith("鬼") and len(text) <= 8 and i + 1 < len(lines) and lines[i + 1][0] == 18:
            names.append((text, lines[i + 1][1]))
    return names


# ---------------------------------------------------------------- 言葉の辞書（意味の見当）

def text_tags(text):
    """文から意味の印を取り出す（一般的な言葉の読み方）"""
    tags = set()
    t = text
    neg = any(w in t for w in ("気にしない", "聞かない", "鈍", "苦手", "しない", "見破る"))
    if "連打" in t and any(w in t for w in ("聞こえ", "調べに来", "寄って", "振り向")) and not ("連打" in t and neg and "気にしない" in t):
        tags.add("hears_mash")
    if "連打" in t and any(w in t for w in ("気にしない", "聞かない")):
        tags.add("ignores_mash")
    if "足音" in t and any(w in t for w in ("敏感", "調べに来", "遠くから")):
        tags.add("hears_steps")
    if any(w in t for w in ("足が速", "速い", "走り回")):
        tags.add("fast")
    if "遅い" in t:
        tags.add("slow")
    if any(w in t for w in ("視界が広", "遠くまで見張", "首を回")):
        tags.add("wide_sight")
    if "目は近め" in t or "近くしか見えない" in t:
        tags.add("short_sight")
    if "壊" in t or "こわ" in t:
        if "苦手" in t or "弱い" in t and "壊す力" in t:
            tags.add("weak_breaker")
        elif "まとめて壊す" in t or "壊しに" in t or "壊して" in t or "じゃまな壁は壊す" in t:
            tags.add("breaks_walls")
    if "囲んだ場所" in t:
        if "調べない" in t:
            tags.add("ignores_enclosed")
        elif "調べ" in t or "壊して" in t or "後回し" in t:
            tags.add("searches_enclosed")
    if "呼び寄せ" in t or "仲間を呼" in t:
        tags.add("summons")
    if "動かない" in t or "見張り" in t:
        tags.add("stationary")
    if "お宝" in t and any(w in t for w in ("大好き", "まわりを回", "見回", "向かって")):
        tags.add("treasure_lover")
    if "手分け" in t or "しらみつぶし" in t or "調べた" in t:
        tags.add("systematic")
    if "見つけると追いかけ" in t or "追いかけてくる" in t:
        tags.add("chases")
    for m in re.finditer(r"([^\s、。（）「」]+?)に弱い", t):
        tags.add("weak_to:" + m.group(1))
    # 対策の文（どうするとよいか）
    if t.startswith("対策"):
        if "動かずに" in t:
            tags.add("advice:stay")
        if "しのび足" in t:
            tags.add("advice:sneak")
        if "連打は控えめ" in t or "音を出しすぎない" in t or "近くでは連打しない" in t:
            tags.add("advice:less_mash")
        if "陰" in t or "視線を切る" in t:
            tags.add("advice:cover")
        if "移って" in t or "隠れ直す" in t:
            tags.add("advice:relocate")
        if "離して置く" in t:
            tags.add("advice:spread_walls")
    return tags


def item_role_prior(name, desc):
    """お店の商品・置く物の役割の見当（名前と説明の言葉から）"""
    t = name + " " + desc
    roles = set()
    if "壁" in name and "補強" not in name:
        roles.add("blocker")
    if "補強" in name or "耐久" in t and "Lv" in t:
        roles.add("toughen")
    if "連打" in name and ("強化" in name or "お金" in name):
        roles.add("income_mash")
    if "時間" in name and ("強化" in name or "お金" in name):
        roles.add("income_time")
    if "踏んだ" in t or "動けない" in t:
        roles.add("stun")
    if "音を出して" in t or "呼ぶ" in t or "呼び寄せ" in t or "鳴く" in t or "猫" in name:
        roles.add("lure")
    if "逃げる" in t or "人形" in name or "身代わり" in name:
        roles.add("decoy_runner")
    if "暗" in t:
        roles.add("darkness")
    if "一方通行" in t or "のれん" in name or "矢印" in t:
        roles.add("one_way")
    if "連打のコイン" in t or "ゴールド" in name:
        roles.add("gold")
    if "音" in t and ("−" in t or "消" in t):
        roles.add("quiet")
    if "登" in t:
        roles.add("climb")
    if "煙" in t:
        roles.add("smoke")
    if "硬さ" in name or "硬さ" in t:
        roles.add("toughen")
    if "お宝" in name or "宝石" in name and "お宝" in t:
        roles.add("treasure")
    if "だるま" in name:
        roles.add("freeze_item")
    return roles


# ---------------------------------------------------------------- お店・置く物

def read_shop_items(obs):
    """購入パートの商品のボタン（ShopItem.n）を読む"""
    items = []
    for bid, label in obs.buttons:
        if not bid.startswith("ShopItem."):
            continue
        parts = [p.strip() for p in label.split(" / ")]
        head = parts[0] if parts else ""
        m = re.match(r"^\[(\d+)\]\s*(.+)$", head)
        rest_head = m.group(2) if m else head
        level = None
        stock = None
        lm = re.search(r"Lv\.?\s*(\d+)", rest_head)
        if lm:
            level = int(lm.group(1))
        sm = re.search(r"在庫\s*(\d+)", rest_head)
        if sm:
            stock = int(sm.group(1))
        name = re.split(r"\s{2,}|\s+Lv\.|\s+在庫", rest_head)[0].strip()
        cost = None
        currency = None
        available = True
        desc_parts = []
        for p in parts[1:]:
            cm = re.match(r"^(?:×\d+\s+)?" + _NUMTOKEN + r"\s*(コイン|宝石|pt|ポイント)$", p)
            if cm and cost is None:
                cost = parse_number(cm.group(1))
                currency = cm.group(2)
            elif "まだ買えない" in p or "最大" == p or "売り切れ" in p:
                available = False
            else:
                desc_parts.append(p)
        desc = " ".join(desc_parts)
        roles = item_role_prior(name, desc)
        if stock is None:
            roles.discard("blocker")  # 在庫を持てない物（強化）は置く壁ではない
        items.append({"id": bid, "name": name, "level": level, "stock": stock, "cost": cost, "currency": currency,
                      "available": available and cost is not None, "desc": desc, "roles": roles})
    return items


def read_shop_tabs(obs):
    """お店の切り替えのボタン（ShopTab.n）：名前と括弧の中の数（宝石・ポイント）"""
    tabs = {}
    for bid, label in obs.buttons:
        if bid.startswith("ShopTab."):
            m = re.search(r"（\s*(\d+)\s*(pt)?\s*）", label)
            tabs[bid] = {"label": label, "count": int(m.group(1)) if m else None}
    return tabs


def read_build_slots(obs):
    """設置パートの下の欄（BuildSlot.n）：名前と在庫"""
    slots = []
    for bid, label in obs.buttons:
        if not bid.startswith("BuildSlot."):
            continue
        name = None
        stock = 0
        for p in (q.strip() for q in label.split(" / ")):
            m = re.match(r"^\[(\d+)\]\s*(.+)$", p)
            if m and name is None:
                name = m.group(2).strip()
            sm = re.search(r"在庫\s*(\d+)", p)
            if sm:
                stock = int(sm.group(1))
        if name:
            slots.append({"id": bid, "name": name, "stock": stock, "roles": item_role_prior(name, label)})
    return slots


def read_stage_onis(obs):
    """購入・設置パートの「このステージの鬼」の下の名前（「標準鬼 ×2」など）"""
    out = {}
    lines = [t for (m, s, t) in obs.screen if not m]
    for i, t in enumerate(lines):
        if t.startswith("このステージの鬼") and "攻撃力" not in t:
            for u in lines[i + 1:i + 8]:
                for part in re.split(r"[\s　■]+", u):
                    m = re.match(r"^(.+鬼)(?:\s*×(\d+))?$", part)
                    if m:
                        out[m.group(1)] = out.get(m.group(1), 0) + int(m.group(2) or 1)
                if out:
                    break
            break
    return out


def read_oni_page(obs, before_texts):
    """鬼のチュートリアル（メニューの文）：名前・ひとこと・星・特徴の行"""
    menu = [(s, t) for (m, s, t) in obs.screen if m]
    if not menu:
        return None
    candidates = [(s, t) for (s, t) in menu if not re.match(r"^[\d\s/]+$", t) and "新しい鬼" not in t and "図鑑" not in t
                  and "★" not in t and not t.startswith(("わかった", "つぎ", "もどる", "◀"))]
    if not candidates:
        return None
    name = max(candidates, key=lambda p: p[0])[1]
    ratings = {}
    label = None
    for s, t in menu:
        if t in ("足の速さ", "目のよさ", "壁をこわす力") or t.startswith("耳"):
            label = t
        elif label and ("★" in t or "―" in t):
            ratings[label] = t.count("★")
            label = None
    lines = [t for (s, t) in menu if t != name and "★" not in t and t not in ratings and not t.startswith(("耳", "足の速さ", "目のよさ", "壁をこわす力"))
             and not t.startswith(("わかった", "つぎ", "もどる", "◀", "新しい鬼", "鬼の図鑑")) and not re.match(r"^[\d\s/]+$", t)]
    tags = set()
    for t in lines:
        tags |= text_tags(t)
    return {"name": name, "ratings": ratings, "lines": lines, "tags": sorted(tags)}


def result_outcome(obs):
    """結果画面：逃げ切ったか（True / False / わからなければ None）"""
    for m, s, t in obs.screen:
        if "逃げきった" in t or "逃げ切った" in t:
            return True
        if "みーつけた" in t:
            return False
    return None
