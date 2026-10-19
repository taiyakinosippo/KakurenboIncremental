# 音の候補と採用

効果音 43 種類と BGM 5 曲を、音を作る AI（Stable Audio Open 1.0・BGM は ACE-Step 1.5）で作った候補の一覧と、どれを採用したかの記録。
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

### ゲームへの組み込み（2026-10-15 から）

GameMode の `SoundOverrides` に音アセットを入れる方法だと、プログラムの音で付けている
「左右の聞こえ方・壁の向こうのこもり方・距離での大きさ」（鬼を音で探す遊びの中心）が無くなる。
そこで、採用した効果音は `KakurenboIncremental/Data/Sounds/<名前>.wav`（モノラル・44100Hz・16bit）にして、ゲームはその音を初めて鳴らすときに読み、
今と同じ左右・こもり・距離の処理を通して鳴らす（`UKakurenboSoundSubsystem::GetSfxSamples`）。高さを変えて鳴らす所（購入の音など）は読む速さを変える（長さも変わる）。
WAV が無い音は今のプログラムの音のまま。WAV にするのは次（表の採用をそのまま並べる。大きさは今のプログラムの音にそろえる。割れるものは少し小さめになる）：
```
Tools\SoundGen\.venv\Scripts\python.exe -I Tools\SoundGen\ExportForGame.py Buy=Buy_s13 BuyFail=BuyFail_s12 PickUp=PickUp_s12 Gem=Gem_s13 RoundStart=RoundStart_s13 Alert=Alert_s11 Caught=Caught_s12 PlayerStep=PlayerStep_s3 PlayerClimb=PlayerClimb_s12 Dash=Dash_s1 Smoke=Smoke_s11 Heartbeat=Heartbeat_s1 OniStep=OniStep_s13 Treasure=Treasure_s1 Meow=Meow_s12
```
「（今のまま）」と — の音は WAV を作らない（プログラムの音）。採用をやめるときは `Data/Sounds/<名前>.wav` を消す。

## 一覧

**4 回目（2026-10-19。s41〜s48）**：3 回目から BlockHit_s33・BlockBreak_s34・TrapSticky_s34・DollStep_s31 を採用。残りの 2 個を作り直した：
宝物鬼の足音「歩くたびにお金が落ちて、どしどし歩く感じ。強いて言うなら s34 が近い」→ **s41〜s44 は s34 を元に**作り直した候補（`Tools/SoundGen/VaryCandidate.py`。画像 AI の img2img と同じで、元の音にノイズを足して説明文で作り直す。strength 0.3。CLAP で s34 との近さ 0.51〜0.87、新しい説明文の候補は 0.63〜0.72）、s45〜s48 は新しい説明文から。
探知鬼の呼び声「見つかった時の絶望感と、鬼を呼び寄せる緊急性」→ s41〜s48。

**3 回目（2026-10-19。s31〜s34）**：2 回目を聞いたユーザーの感想から 6 個を作り直した：宝物鬼の足音「もっとジャラジャラしつつ重厚感」・探知鬼「もっとレーダーみたいな音、ただし鬼から出ているような音」（Summon。ゲームでも探知鬼の場所から鳴らすようにした）・
壁（BlockHit・BlockBreak）「もっと短く重厚感」・トリモチ「今みたいな音を世界観に合わせて」・身代わり人形「もっとドタドタと音を立てて進む」（ゲームでも歩幅ごとに鳴らすようにした）。
2 回目の「★AI の予想」は 19 個中 9 個が当たった（でたらめなら 4.8 個）。

**2 回目（2026-10-19。表の太字の候補）**：前に選ばなかった 14 個を、ユーザーの「選んでいないのは、音がリアルすぎたり、行動に合ってない音すぎたりするもの」から説明文を書き直して **s21〜s24**、
新しい音 12 個（メニュー 4・鬼の種類ごとの足音 4・飛び乗り・のれん・人形の足音・鬼の紹介。今は今までの音の高さ違いで鳴っている）を s21〜s24、
タイトル曲を明るく（「ちょっと暗くて気になる」から。ACE s4〜s6）、大きな館のかくれんぼ曲（ACE s1〜s3）。聞くページでは「★AI の予想」（`Tools/SoundGen/RankCandidates.py`。今までの選び方から学んだ予想で、当たるのは 4 回に 1 回くらい）が付いている。
候補の名前は `<音の名前>_s<シード値>`（例：`OniStep_s2`）。効果音の **s11〜s13** は BGM と同じ世界観（ポップなホラー。オルゴール・チェレスタ・グロッケン・ピチカート・ファゴット・チューバ、拍子木・木魚。耳で探す足音・心臓の音は本物らしいまま）の説明文で作り直したもの（2026-10-15。今の Sounds.csv の説明文。前の説明文は各フォルダの prompt.txt）。シード値が同じなら同じ説明文で同じ音をもう一度作れる（GPU が違うと少し変わることがある）。
説明文（英語）は `Tools/SoundGen/Sounds.csv`。「採用」の — は今の音（プログラムの音）のまま。

| 音の名前 | どこで鳴る | 候補 | 採用 |
|---|---|---|---|
| Mash | 連打 | s1, s2, s3, s900, s11, s12, s13, **s21, s22, s23, s24** | **Mash_s22** |
| Treasure | お宝を取った | s1, s2, s3, **s11, s12, s13** | **Treasure_s1** |
| Alert | 鬼に見つかって追いかけられ始めた | s1, s2, s3, **s11, s12, s13** | **Alert_s11** |
| BlockHit | 壁が攻撃された（壊れていない） | s1, s2, s3, s11, s12, s13, s21, s22, s23, s24, **s31, s32, s33, s34** | **BlockHit_s33** |
| BlockBreak | 壁が壊れた | s1, s2, s3, s11, s12, s13, s21, s22, s23, s24, **s31, s32, s33, s34** | **BlockBreak_s34** |
| Caught | 見つかった（アウト） | s1, s2, s3, **s11, s12, s13** | **Caught_s12** |
| Clear | 逃げ切った | s1, s2, s3, s11, s12, s13, **s21, s22, s23, s24** | **Clear_s22** |
| TrapSticky | トリモチにかかった | s1, s2, s3, s11, s12, s13, s21, s22, s23, s24, **s31, s32, s33, s34** | **TrapSticky_s34** |
| DecoyPing | おとりが音を出した | s1, s2, s3, s11, s12, s13, **s21, s22, s23, s24** | **DecoyPing_s24** |
| DecoyBreak | おとりが壊された | s1, s2, s3, s11, s12, s13, **s21, s22, s23, s24** | **DecoyBreak_s21** |
| CountdownBeep | 開始前のカウントダウン | s1, s2, s3, **s11, s12, s13** | （今のまま） |
| RoundStart | 鬼が出てきた | s1, s2, s3, **s11, s12, s13** | **RoundStart_s13** |
| TimeTick | 残り 5 秒からの秒読み | s1, s2, s3, s11, s12, s13, **s21, s22, s23, s24** | **TimeTick_s22** |
| Buy | 購入できた | s1, s2, s3, **s11, s12, s13** | **Buy_s13** |
| BuyFail | コインが足りない | s1, s2, s3, **s11, s12, s13** | **BuyFail_s12** |
| Place | 壁・罠を置いた | s1, s2, s3, s11, s12, s13, **s21, s22, s23, s24** | **Place_s23** |
| PickUp | 壁・罠を回収した | s1, s2, s3, **s11, s12, s13** | **PickUp_s12** |
| Dash | ダッシュした（大きな音） | s1, s2, s3, **s11, s12, s13** | **Dash_s1** |
| Prestige | 転生した | s1, s2, s3, s11, s12, s13, **s21, s22, s23, s24** | **Prestige_s22** |
| PlayerStep | プレイヤーの足音 | s1, s2, s3, **s11, s12, s13** | **PlayerStep_s3** |
| PlayerClimb | プレイヤーがよじ登った（ガサッ） | s1, s2, s3, **s11, s12, s13** | **PlayerClimb_s12** |
| OniStep | 鬼の足音（どたどた） | s1, s2, s3, **s11, s12, s13** | **OniStep_s13** |
| TreasureSparkle | お宝のキラキラ | s1, s2, s3, **s11, s12, s13** | （今のまま） |
| Summon | 探知鬼が仲間を呼んだ | s1, s2, s3, s11, s12, s13, s21, s22, s23, s24, s31, s32, s33, s34, **s41〜s48** | — |
| Smoke | 煙幕を投げた（ボフッ） | s1, s2, s3, **s11, s12, s13** | **Smoke_s11** |
| Heartbeat | 鬼が近い（ドクン） | s1, s2, s3, **s11, s12, s13** | **Heartbeat_s1** |
| OniNotice | 鬼が音に気づいた（「ン？」） | s1, s2, s3, s11, s12, s13, **s21, s22, s23, s24** | **OniNotice_s24** |
| Meow | 迷い猫の鳴き声 | s1, s2, s3, **s11, s12, s13** | **Meow_s12** |
| Daruma | だるまさんがころんだ（かけ声のメロディ） | s1, s2, s3, s11, s12, s13, **s21, s22, s23, s24** | **Daruma_s21** |
| Poof | 身代わり人形が消えた・のれんが壊れた（ポン） | s1, s2, s3, s11, s12, s13, **s21, s22, s23, s24** | — |
| Gem | 宝石を全部集めた（ボーナス） | s1, s2, s3, **s11, s12, s13** | **Gem_s13** |
| UIClick | ボタンを押した（設定・図鑑のページ） | **s21, s22, s23, s24** | **UIClick_s24** |
| UIMove | カーソル・選択の移動 | **s21, s22, s23, s24** | **UIMove_s24** |
| MenuOpen | メニューを開いた | **s21, s22, s23, s24** | **MenuOpen_s24** |
| MenuClose | メニューを閉じた | **s21, s22, s23, s24** | **MenuClose_s22** |
| OniIntro | 初めての鬼が出てきた（図鑑） | **s21, s22, s23, s24** | **OniIntro_s23** |
| OniLeap | 鬼が家具・壁に飛び乗った | **s21, s22, s23, s24** | **OniLeap_s22** |
| CurtainPass | 鬼がのれんをくぐった | **s21, s22, s23, s24** | **CurtainPass_s24** |
| DollStep | 身代わり人形の足音（とことこ） | s21, s22, s23, s24, **s31, s32, s33, s34** | **DollStep_s31** |
| OniStepScout | スピード鬼の足音（たったっ） | **s21, s22, s23, s24** | **OniStepScout_s23** |
| OniStepBreaker | パワー鬼の足音（ずしん） | **s21, s22, s23, s24** | **OniStepBreaker_s22** |
| OniStepCareful | 慎重鬼の足音（ぎしっ） | **s21, s22, s23, s24** | **OniStepCareful_s22** |
| OniStepTreasure | 宝物鬼の足音（じゃらっ） | s21, s22, s23, s24, s31, s32, s33, s34, **s41〜s44（s34 を元に）, s45, s46, s47, s48** | — |
| Music_Title | BGM：タイトル | ACE s1, s2, s3, **s4, s5, s6** | **MusicGen:Music_Title_s2** |
| Music_Calm | BGM：購入・設置・リザルト（のんびり） | s1, s2, s3, s900 / ACE s1, s2, s3 | **MusicGen:Music_Calm_s2** |
| Music_Hide | BGM：かくれんぼ中（速くて少し怖い） | s1, s2, s3 / ACE s1, s2, s3 | **MusicGen:Music_Hide_s2** |
| Music_HideDeep | BGM：かくれんぼ中・大きな館（ステージ 11 から） | **ACE s1, s2, s3** | **MusicGen:Music_HideDeep_s1** |
| Music_Chase | BGM：鬼に追われている間（どたばた） | ACE s1, s2, s3 | **MusicGen:Music_Chase_s3** |

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
1 曲 40〜60 秒（RTX 5070）。

**BGM はゲームに入っている**（2026-10-15。上の表の太字）。採用した曲は次でゲームの `KakurenboIncremental/Data/Sounds/Music_<曲>.wav` にする：
```
Tools\MusicGen\ACE-Step-1.5\.venv\Scripts\python.exe -I Tools\MusicGen\ExportForGame.py Music_Title=Music_Title_s2 Music_Calm=Music_Calm_s2 Music_Hide=Music_Hide_s2 Music_Chase=Music_Chase_s3
```
（曲の終わり方の手前で小節の切れ目に切り、続きの 1 小節を頭に重ねてくり返せるようにし、大きさを今までのプログラムの BGM にそろえる。numpy と soundfile があればどの Python でもよい）。
ゲームは WAV があればそれ、無ければプログラムの曲を流す（Data/README.md）。
AI の曲は終わりと始まりがつながっていないので、くり返すときは組み込みのときに継ぎ目を重ねる（クロスフェード）予定。
