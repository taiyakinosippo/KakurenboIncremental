# かせぐんぼ SNS 運用

「かせぐんぼ」（隠れる側のかくれんぼ × インクリメンタル）を世に出すための作業場所。
Claude が文案・予定・素材を作り、**投稿・アカウント操作はすべて開発者本人が行う**（Claude はアカウント作成・ログイン・投稿をしない）。

## 今の状況（2026-10-16）

- X：**[@kasegunbo](https://x.com/kasegunbo)**（表示名「かせぐんぼ🏮開発中」）。自己紹介・アイコン・ヘッダーは [profile.md](profile.md) の案
- 投稿済み：01 はじめまして（10/15・固定）・1a 連打と音：仕組み（10/16）・1b 試して直した（10/17）→ 記録は [posted/log.md](posted/log.md)
- **次に出すもの**：[calendar.md](calendar.md) の表で「投稿済み」の次の行（2026-10-18 に実際の日付で組み直した）
- スマホ用の投稿ボード（本文のコピー・画像の保存）：https://claude.ai/artifact/VMNTjf3C5MPrWYxKxCKZzz
- itch.io・Steam のページはまだ（文だけ [store-pages.md](store-pages.md)）

## 決めたこと（Claude が守る）

- 口調：**です・ます基本のフレンドリー**、絵文字 1〜2 個 → [voice.md](voice.md)
- ハッシュタグ：毎回 `#かせぐんぼ #AIゲーム開発 #ClaudeCode` ＋場面ごとに 1 つまで。土曜は `#スーパー開発デー`（第 2 土曜は `#スーパーゲ制デー`）→ 根拠は [research.md](research.md)
- 時間：平日 21 時ごろ・土曜 10〜12 時
- 「完全AI開発」＝人は頼むだけ・コードは書かない。**3D の見た目は Fab のアセットなので「絵も AI が作った」とは書かない**
- 序盤は**システム紹介**が主役：1 システムを 3 本（仕組み → 試して直した → 今の形）で 1 週間 → [system-series.md](system-series.md)。「試して直した」は `docs/History.md`・`docs/GameDesign.md` に残っている調整だけを使う（作り話・うその日付はしない）
- 連載「かせぐんぼができるまで」と AI 開発の話は週 1 回（日曜）→ [history-series.md](history-series.md)（開発者の答え＝意図・面白さの考えもここ）
- 決まっていないこと（発売日・値段・機種）は約束しない

## 流れ

```
Claude: drafts/ に下書き（文・画像の指定・投稿日時）
   ↓
あなた: 読んで OK / 直してほしい所を言う
   ↓
あなた: X に投稿（コピペ＋画像）→ URL を Claude に送る
   ↓
Claude: 下書きを posted/ へ移し、posted/log.md に記録（日時・URL・表示回数）→ 次の下書きに生かす
```

- 本文は X の上限（280。日本語・絵文字は 1 字 = 2）に収める
- 画像は `media/`。撮り方・撮り直し方は [shotlist.md](shotlist.md)（ゲームの自動テスト `KakuAutoTest <シナリオ>` をパッケージ版で動かすと撮れる）
- アイコン・ヘッダー・連載のカードは `art/*.html` を `art/Render.ps1` で PNG にする

## 別のパソコン・スマホで続けるとき

- **別のパソコン**：`git pull` して、Claude に「SNS の続きをして」と頼む。Claude はまずこのファイルと [calendar.md](calendar.md)・[posted/log.md](posted/log.md) を読む
- **スマホ**：投稿ボード（Artifact）を開いて、本文をコピー・画像を長押しで保存して X に投稿する。投稿した URL は、あとでどこかの Claude に送れば記録する
- 投稿ボード：https://claude.ai/artifact/VMNTjf3C5MPrWYxKxCKZzz （元は `SNS/board/`。下書きや予定を変えたら `python SNS/board/build.py` → Artifact ツールで `url` にこの URL を渡して read してから、`index.html` を `root` = `SNS/board`・`files` = `img/*.jpg` で出し直す）

## フォルダ

| パス | 中身 |
|---|---|
| `profile.md` | X の名前・自己紹介・画像の案 |
| `store-pages.md` | itch.io / Steam のページの文 |
| `voice.md` | 口調・投稿の柱・ハッシュタグ・やらないこと |
| `research.md` | ハッシュタグと時間帯のリサーチ |
| `calendar.md` | 投稿予定（次に出すもの） |
| `system-series.md` | システム紹介シリーズの計画 |
| `history-series.md` | 連載「かせぐんぼができるまで」の計画・開発者の答え |
| `shotlist.md` | 画像の一覧と撮り方 |
| `drafts/` | 投稿前の下書き（`system/` システム紹介・`history/` 連載） |
| `posted/` | 投稿済み（`log.md` に記録） |
| `media/` | 投稿に使う画像 |
| `art/` | アイコン・ヘッダー・カードの元の HTML と `Render.ps1` |
| `board/` | スマホ用の投稿ボード（Artifact のページ） |

## 決まっていないこと

- 発売・体験版の予定（いつ・どこで・無料か有料か）→ 決まったら投稿の「行き先」が作れる
- 開発者名を出すか（個人名 / サークル名 / 名無し）
- 英語でも投稿するか
