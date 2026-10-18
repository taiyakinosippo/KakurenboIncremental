"""スマホ用の投稿ボード（Artifact のページ）を作る。

calendar.md の表の順に、下書き（drafts/・posted/）の本文・返信・画像を並べた board/index.html と、
画像を縮めた board/img/*.jpg を作る。公開は Artifact ツールで index.html を、files に img/ の JPEG を付けて出す。
    python SNS/board/build.py
"""

import html
import re
from pathlib import Path

from PIL import Image

SNS = Path(__file__).resolve().parent.parent
BOARD = SNS / "board"
IMG = BOARD / "img"


def weight(text):
    """X の文字数（日本語・絵文字は 2、英数字と一部の記号は 1）"""
    n = 0
    for c in text:
        o = ord(c)
        n += 1 if o <= 0x10FF or 0x2000 <= o <= 0x200D or 0x2010 <= o <= 0x201F or 0x2032 <= o <= 0x2037 else 2
    return n


def block(md, heading):
    m = re.search(r"## " + re.escape(heading) + r"[^\n]*\n\n```\n(.*?)```", md, re.S)
    return m.group(1).rstrip("\n") if m else ""


def read_post(path):
    md = path.read_text(encoding="utf-8")
    title = md.splitlines()[0].lstrip("# ").strip()
    img_line = re.search(r"- 画像：(.*)", md)
    images = re.findall(r"media/([\w/.-]+\.png)", img_line.group(1)) if img_line else []
    state = re.search(r"- 状態：(.*)", md)
    return {
        "title": title,
        "body": block(md, "本文"),
        "reply": block(md, "返信"),
        "images": images,
        "posted": bool(state and "投稿済み" in state.group(1)),
    }


def rows():
    """calendar.md の表の行（日・曜日・リンク先・シリーズ）"""
    out = []
    for line in (SNS / "calendar.md").read_text(encoding="utf-8").splitlines():
        if not line.startswith("| ") or line.startswith("| 日") or line.startswith("|---"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if len(cells) < 5:
            continue
        link = re.search(r"\[([^\]]+)\]\(([^)]+\.md)\)", cells[2])
        out.append({"day": cells[0], "dow": cells[1], "series": cells[3],
                    "label": link.group(1) if link else cells[2], "path": link.group(2) if link else None,
                    "note": cells[4]})
    return out


def jpeg(name):
    """画像を幅 1280 までの JPEG にして img/ に置き、公開するときのパスを返す"""
    src = SNS / "media" / name
    if not src.exists():
        return None
    dst = IMG / (Path(name).stem + ".jpg")
    dst.parent.mkdir(parents=True, exist_ok=True)
    if not dst.exists() or dst.stat().st_mtime < src.stat().st_mtime:
        im = Image.open(src).convert("RGB")
        if im.width > 1280:
            im = im.resize((1280, round(im.height * 1280 / im.width)))
        im.save(dst, "JPEG", quality=86)
    return "img/" + dst.name


def card(r, idx):
    e = html.escape
    head = f'<div class="when"><span class="day">{e(r["day"])}</span>' + (f'<span class="dow">{e(r["dow"])}</span>' if r["dow"] not in ("", "-") else "") + f'<span class="series">{e(r["series"])}</span></div>'
    if not r["path"]:
        return f'<article class="post todo">{head}<h2>{e(r["label"])}</h2><p class="note">まだ下書きがありません（{e(r["note"])}）</p></article>'
    p = read_post(SNS / r["path"])
    parts = [f'<article class="post{" done" if p["posted"] else ""}" id="p{idx}">', head, f'<h2>{e(p["title"])}</h2>']
    if p["posted"]:
        parts.append('<p class="badge">投稿済み</p>')
    for kind, text in (("本文", p["body"]), ("返信（スレッドの 2 つ目）", p["reply"])):
        if not text:
            continue
        tid = f"t{idx}{'r' if kind != '本文' else ''}"
        parts.append(f'<div class="textbox"><div class="tb-head"><span>{kind}</span><span class="count">{weight(text)} / 280</span>'
                     f'<button type="button" class="copy" data-target="{tid}">コピー</button></div>'
                     f'<pre id="{tid}">{e(text)}</pre></div>')
    imgs = [(n, jpeg(n)) for n in p["images"]]
    shown = [(n, j) for n, j in imgs if j]
    if shown:
        parts.append('<div class="imgs">' + "".join(f'<figure><img src="{e(j)}" alt="{e(n)}" loading="lazy"><figcaption>{e(n)}</figcaption></figure>' for n, j in shown) + "</div>")
    missing = [n for n, j in imgs if not j]
    if missing or not imgs:
        parts.append(f'<p class="note">画像：{e(r["note"])}</p>')
    parts.append("</article>")
    return "".join(parts)


def main():
    cards = "\n".join(card(r, i) for i, r in enumerate(rows()))
    page = (BOARD / "template.html").read_text(encoding="utf-8").replace("<!--CARDS-->", cards)
    (BOARD / "index.html").write_text(page, encoding="utf-8")
    print("board/index.html", len(list(IMG.glob("*.jpg"))), "images")


if __name__ == "__main__":
    main()
