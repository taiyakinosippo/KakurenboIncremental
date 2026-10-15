# 撮ってほしい画像・GIF

撮ったら `media/` に下のファイル名で入れる。撮り方：Windows の **Win + Shift + S**（画像）、**Win + Alt + R**（Xbox Game Bar で録画。mp4）。
GIF にしたい時は mp4 のまま入れれば Claude が変換を手伝う（X は mp4 をそのまま載せられる）。

| ファイル名 | 種類 | 場面 | 使う投稿 | 状態 |
|---|---|---|---|---|
| `title.png` | 画像 | タイトル画面（夜の館をゆっくり回っている所・ロゴ入り） | はじめまして・ヘッダー | できた（1280×720） |
| `renda.mp4` | 動画 5〜10 秒 | かくれんぼ中に連打 → コインが増える → 鬼が「音のほうへ…」とこっちへ来る | ゲームの核 | 未 |
| `oni_gate.png` | 画像 | 連打していると赤い門から鬼が出てくる（「鬼が来る…」） | ゲームの核 | できた |
| `header_game.png` | 画像 3000×1000 | ヘッダー案 2：実際のタイトル画面＋ロゴ（`art/header_game.html`） | プロフィール | できた |
| `crouch.mp4` | 動画 5〜10 秒 | 椅子・テーブルの陰にしゃがむ → 鬼がすぐそばを通り過ぎる | 音で遊ぶ / しゃがむ | 未 |
| `footsteps.mp4` | 動画 10 秒（**音あり**） | 止まって聞いていると、足音が左から近づいて遠ざかる | 音で遊ぶ | 未 |
| `build.png` | 画像 | 設置パート（真上から）。壁で二重に囲った隠れ場所 | インフレ | できた（ステージ 3。壁は少なめ） |
| `shop.png` | 画像 | 購入パートのお店（コイン・宝石・転生のタブ） | インフレ | できた |
| `mitsuketa.png` | 画像 | 見つかったときの「みーつけた」の結果画面 | 開発の裏側 | できた |
| `gem.png` | 画像 | お宝の宝石が光っているところ（近くから） | システム 2a | できた（自動テストで撮影） |
| `gem_walled.png` | 画像 | 壁で囲まれた宝石（光が壁の外に漏れていない。真上から） | システム 2b | 未（Claude が撮れる） |
| `gem_shop.png` | 画像 | 宝石のお店のタブ | システム 2c | できた（自動テストで撮影） |
| `cat.png` / `daruma.png` | 画像 | 迷い猫を鬼がなでている・だるまさんがころんだで鬼が止まった | システム 2c・土曜 | できた（自動テストで撮影） |
| `build_double.png` | 画像 | 壁を二重に置いた隠れ場所（真上から） | 土曜・システム 3 | 未（Claude が撮れる） |
| `icon_koban.png` / `icon_bo.png` / `icon_peek.png` | 画像 800×800 | X のアイコン（案 C：壁のかげからのぞく小判（おすすめ）／案 B：ロゴの「ぼ」／案 A：壁のかげからのぞく目）。`art/` の HTML から `art/Render.ps1` で作る | プロフィール | できた |
| `header.png` | 画像 3000×1000 | X のヘッダー（夜の館＋ロゴ＋「連打で稼ぐ。音で見つかる。」）。同上 | プロフィール | できた |
| `ai_report.png` | 画像 | AI テストプレイの報告書（`report.html`）の「気づいた時間」の表 | AI テストプレイ | Claude が用意できる |

## 2026-10-15 に自動テストで撮った画像（`media/`）

ゲームに入っている自動テスト（Showcase・Look・OniIntro・GemShop・Walls・Climb・Dash・Crouch・Detector・Mood・Gate・Trap・Maps など）をパッケージ版で動かして撮った。
`showcase_shop / showcase_build / showcase_chase / daruma / gem`（ステージ 10）・`oni_lineup / oni_red_eyes / oni_intro / oni_book_1〜6 / oni_detector_summon`・
`gem_shop / prestige_shop / walls_3types / treasure_complete`・`climb / smoke / crouch / darkness / trap_stunned / gate_onis / countdown`・
`map_s01_parlor / map_s03_nursery / map_s04_library / map_s05_dining / map_walls_angle`・`mood_gate / mood_great_hall / mood_bedroom`

撮り直す方法：`Packaged\Windows\KakurenboIncremental.exe -windowed -KakuNoSave -NoSound -ExecCmds="KakuAutoTest <シナリオ>"` → `Packaged/Windows/KakurenboIncremental/Saved/AutoTest/`。
迷い猫（Cat）は試験用の部屋で猫が小さく、使える画像にならなかった。

## Claude が撮るとき

パッケージ版を `-KakuAgentPort=7799 -KakuAgentShots=<フォルダ> -KakuNoSave -KakuSaveSlot=<コピーしたスロット>` で起動し、テキストの窓口の `shot <名前>` で撮る（AI テストプレイと同時だと GPU が足りない）。今の撮影は 1280×720。

## 撮るときのコツ

- 画面の操作の案内（キーの形の表示）は、設定で消すと見栄えがよい（入れたままでも「遊び方がわかる」利点はある）
- 動画は最初の 1 秒で何が起きているかわかる所から始める（X は流し見される）
- 縦長にはしない（X はそのままの比率で出る。16:9 で OK）
