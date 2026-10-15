# 音の候補と採用

効果音 31 種類と BGM 2 曲を、音を作る AI（Stable Audio Open 1.0）で作った候補の一覧と、どれを採用したかの記録。
道具の使い方は [Setup.md](Setup.md) の「音を作る AI」、Claude 向けの手順は CLAUDE.md。

## 聞く

- **どのパソコン・スマホからでも**：https://claude.ai/artifact/SQq8ZzW6ZMYY9dEFoamWR6 （非公開。同じ Claude のアカウントでログインして開く）。
  音ごとに「今の音」（緑の丸）と候補（橙の丸）が並んでいて、押すと鳴る。
- **手元のファイルで**：`git pull` すると `KakurenboIncremental/SourceArt/SoundGen/` に WAV が入る（Git LFS）。
  `KakurenboIncremental/SourceArt/SoundGen/index.html` を **Edge や Chrome で開く**（エクスプローラーでダブルクリック）。
  Claude アプリの中のプレビューでは音が鳴らない

## 採用のしかた

1. 上のページで、音ごとに「これにする」（候補を使う）か「今のままにする」を押す
2. 下の帯の「コピー」を押して、Claude に送る（例：`使う音: Mash_s2, OniStep_s1, Music_Calm（今のまま）`）
3. Claude が下の表の「採用」を書き換え、ゲームに組み込む

選んだ印はそのブラウザにだけ残る（別のパソコン・スマホには引き継がれない）。決めたことはこの表が正。

### ゲームへの組み込み（まだ作っていない・予定）

今の音は GameMode の `SoundOverrides` にアセットを入れると差し替わるが、そうするとプログラムの音で付けている
「左右の聞こえ方・壁の向こうのこもり方・距離での大きさ」（鬼を音で探す遊びの中心）が無くなる。
そこで、採用した WAV を `KakurenboIncremental/Data/Sounds/<名前>.wav`（モノラル）にコピーし、起動時に読み込んで今と同じ処理を通して鳴らす形にする予定。
WAV が無い音は今のプログラムの音のまま。

## 一覧

候補の名前は `<音の名前>_s<シード値>`（例：`OniStep_s2`）。シード値が同じなら同じ説明文で同じ音をもう一度作れる（GPU が違うと少し変わることがある）。
説明文（英語）は `Tools/SoundGen/Sounds.csv`。「採用」の — は今の音（プログラムの音）のまま。

| 音の名前 | どこで鳴る | 候補 | 採用 |
|---|---|---|---|
| Mash | 連打 | s1, s2, s3, s900 | — |
| Treasure | お宝を取った | s1, s2, s3 | — |
| Alert | 鬼に見つかって追いかけられ始めた | s1, s2, s3 | — |
| BlockHit | 壁が攻撃された（壊れていない） | s1, s2, s3 | — |
| BlockBreak | 壁が壊れた | s1, s2, s3 | — |
| Caught | 見つかった（アウト） | s1, s2, s3 | — |
| Clear | 逃げ切った | s1, s2, s3 | — |
| TrapSticky | トリモチにかかった | s1, s2, s3 | — |
| DecoyPing | おとりが音を出した | s1, s2, s3 | — |
| DecoyBreak | おとりが壊された | s1, s2, s3 | — |
| CountdownBeep | 開始前のカウントダウン | s1, s2, s3 | — |
| RoundStart | 鬼が出てきた | s1, s2, s3 | — |
| TimeTick | 残り 5 秒からの秒読み | s1, s2, s3 | — |
| Buy | 購入できた | s1, s2, s3 | — |
| BuyFail | コインが足りない | s1, s2, s3 | — |
| Place | 壁・罠を置いた | s1, s2, s3 | — |
| PickUp | 壁・罠を回収した | s1, s2, s3 | — |
| Dash | ダッシュした（大きな音） | s1, s2, s3 | — |
| Prestige | 転生した | s1, s2, s3 | — |
| PlayerStep | プレイヤーの足音 | s1, s2, s3 | — |
| PlayerClimb | プレイヤーがよじ登った（ガサッ） | s1, s2, s3 | — |
| OniStep | 鬼の足音（どたどた） | s1, s2, s3 | — |
| TreasureSparkle | お宝のキラキラ | s1, s2, s3 | — |
| Summon | 探知鬼が仲間を呼んだ | s1, s2, s3 | — |
| Smoke | 煙幕を投げた（ボフッ） | s1, s2, s3 | — |
| Heartbeat | 鬼が近い（ドクン） | s1, s2, s3 | — |
| OniNotice | 鬼が音に気づいた（「ン？」） | s1, s2, s3 | — |
| Meow | 迷い猫の鳴き声 | s1, s2, s3 | — |
| Daruma | だるまさんがころんだ（かけ声のメロディ） | s1, s2, s3 | — |
| Poof | 身代わり人形が消えた・のれんが壊れた（ポン） | s1, s2, s3 | — |
| Gem | 宝石を全部集めた（ボーナス） | s1, s2, s3 | — |
| Music_Title | BGM：タイトル（まだ BGM は無い） | ACE s1, s2, s3 | — |
| Music_Calm | BGM：購入・設置・リザルト（のんびり） | s1, s2, s3, s900 / ACE s1, s2, s3 | — |
| Music_Hide | BGM：かくれんぼ中（速くて少し怖い） | s1, s2, s3 / ACE s1, s2, s3 | — |
| Music_Chase | BGM：鬼に追われているとき（まだ BGM は無い。どたばた） | ACE s1, s2, s3 | — |

## 候補を増やす・作り直す

- 同じ音の別の候補：`powershell -ExecutionPolicy Bypass -File Tools\SoundGen.ps1 --seed 4 OniStep`（s4〜s6 ができる。今ある候補は消えない）
- 説明文を変える：`Tools/SoundGen/Sounds.csv` の Prompt を直してから、新しいシード値で作る
- 作ったら聞き比べのページを作り直す：`Tools\SoundGen\.venv\Scripts\python.exe Tools\SoundGen\MakeWebPage.py` → Claude に「音のページを出し直して」（同じ URL に出し直す）
- 候補の WAV は Git（LFS）に入れてコミットする（別のパソコンでも聞けて採用できるように）

## BGM 専用の AI（ACE-Step 1.5）

BGM は Stable Audio Open より曲作りに向いた ACE-Step 1.5 でも作れる（`Tools\MusicGen.ps1`。使い方は [Setup.md](Setup.md) の「BGM を作る AI」）。
説明文・長さ・テンポ・調は `Tools/MusicGen/Music.csv`。候補は `KakurenboIncremental/SourceArt/MusicGen/<名前>/<名前>_s<シード>.wav`（Git LFS）、
一覧は同じフォルダの `index.html`。上の聞くページにも「ACE s1」などとして並ぶ（コピーの文では `MusicGen:Music_Hide_s2`）。
採用するときは表の「採用」に `MusicGen:Music_Hide_s2` のように書く。
今ある曲（2026-10-15）：タイトル・購入/設置・かくれんぼ・追われているとき（Music_Title / Calm / Hide / Chase）の 4 曲 × 3 候補。1 曲 90 秒（追われているときは 60 秒）、
ユーザーが書いた「ポップなホラー、明るい雰囲気の緊張感」の設定（オルゴール・チェレスタ・ピチカート・ファゴット・チューバ、歌なし、中音域をあける）から作った。
タイトルと追われているときの BGM はゲームにまだ無いので、採用したら鳴らす場面も作る。
1 曲 40〜60 秒（RTX 5070）。
AI の曲は終わりと始まりがつながっていないので、くり返すときは組み込みのときに継ぎ目を重ねる（クロスフェード）予定。
