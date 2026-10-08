# KakurenboIncremental

UE 5.8 の C++ プロジェクト。隠れる側のかくれんぼインクリメンタルゲーム。
ゲーム仕様は [docs/GameDesign.md](docs/GameDesign.md) を参照し、仕様が変わったら更新すること。
これまでのユーザーの依頼と決めたことは [docs/History.md](docs/History.md)。大きな依頼や決定があったら追記すること。

## 構成

- リポジトリルート: `C:\KakurenboIncremental`
- UE プロジェクト: `KakurenboIncremental/KakurenboIncremental.uproject`
- C++ モジュール: `KakurenboIncremental/Source/KakurenboIncremental/`
- エンジン: `C:\Program Files\Epic Games\UE_5.8`（`Tools/EnginePath.ps1` が場所を自動で探す。別の場所なら環境変数 `UE_ROOT`）
- エディタ: VSCode（ユーザー）。ビルドは MSVC（Visual Studio 2022・MSVC 14.44）
- 複数のパソコンで開発している。作業の前に `git pull`、終わったら push。別のパソコンの環境構築は [docs/Setup.md](docs/Setup.md)
- **pull したら必ず `Tools\Build.ps1` でビルドし直す**（Binaries は Git に入らないので、ビルドしないと古いゲームのまま動く）

### コードの構成

| ファイル | 役割 |
|---|---|
| `KakurenboGameMode` | ルールと進行（パート遷移・収入・購入・壁と罠の設置・鬼のスポーン・転生・消音・足音・探知鬼の呼び寄せ・館のマップの切り替え・演出と効果音と BGM のきっかけ）。数値は UPROPERTY。`Config=Game` なので鬼の見た目のパスは DefaultGame.ini |
| `KakurenboGameState` | 現在の状態（コイン・ステージ・在庫・強化レベル・転生ポイント）。HUD はここを読む |
| `KakurenboPlayerController` | 入力（`PostProcessInput` でキー状態をポーリング）、パートごとの視点・入力モードの切り替え、カーソルでの設置、ダッシュ、転生の確認、デバッグ用 Exec コマンド |
| `KakurenboHUD` | Canvas に直接描く仮 UI（日本語は `/Engine/EngineFonts/Roboto` のフォールバックで表示）。鬼・お宝の方向は出さない（音で探す） |
| `HiderCharacter` / `OniCharacter` | プレイヤー（俯瞰・真上・三人称カメラ、ダッシュ、足音・しのび足。Fab のモデルを `ApplyLook` で PoseableMesh に付け、歩く動きはプログラムで骨を回す） / 鬼（Wander/Investigate/Chase/Inspect/Attack/Stunned。種類 EOniType ごとに探し方が違う：標準・スピード・パワー・慎重・宝物・探知。ぶつかったらアウト。鬼どうしはすり抜ける。歩くと足音。スケルタルメッシュに差し替え可） |
| `KakurenboOniBlackboard` | 慎重鬼どうしで共有する「調べたマス」と「向かっているマス」「調べている建物」（WorldSubsystem） |
| `TreasureActor` | お宝（距離で取得。ときどきキラキラと鳴る。GameMode が宝石のメッシュと色を渡す） |
| `SmokeCloud` | 煙幕（煙幕ダッシュで足元に投げる）。`IsSightBlocked` で煙の中・向こう側の視線をさえぎる（鬼の `CanSeeTarget` が使う）。煙幕ダッシュは回数制（`DashUsesPerRound`・転生のお店の「煙幕の数」） |
| `TrapActor` | 罠（トリモチ: 踏んだ鬼を Stun / おとり: 一定間隔で EmitNoise）。当たり判定なし、距離で発動。配置と設計図はグリッドが持つ |
| `KakurenboFx` | 仮の演出（破片 `AKakurenboBurstFx`・床の輪 `AKakurenboRingFx`）と窓口の `UKakurenboFxSubsystem`（輪は使い回す） |
| `KakurenboSoundSubsystem` | 効果音を鳴らす（2D / その場所から。その場所からの音は自前で左右に振り分けた 2ch の波形を 2D で鳴らす：聞く人＝自分の体・向き＝カメラ、壁の向こうはこもる。`GetLastSpatial` でテスト）と BGM（`PlayMusic`）。GameMode の `SoundOverrides` に音アセットがあればそちら（エンジンの 3D の音） |
| `KakurenboSynth` | 効果音と BGM の波形をプログラムで作る（純粋な計算・単体テストあり）。左右の振り分け（`ComputeSpatial`・`MakeStereo`・`DistanceGain`）も。`USoundWaveProcedural` で再生し、効果音は長さぶん経ったら止める。BGM は 1 秒ごとに 1 周ぶん足す |
| `KakurenboMaps` | 館の間取り（Data/Maps/*.txt の文字の図）を読み、同じ文字の長方形を家具にまとめる。館の大きさ＝文字数 × 行数（`MeasureLayout`）（純粋ロジック・単体テストあり） |
| `KakurenboLayout` | 設計図からの修復計画（純粋ロジック・単体テストあり） |
| `KakurenboBalance` | ステージ設定の解決（表より後は伸ばす）と CSV → DataTable の読み込み |
| `KakurenboSaveGame` | セーブデータ（GameMode の SaveProgress / LoadProgress / ResetProgress） |
| `GridPathfinder` | ワールドに依存しない A*。壁マスに「壊すコスト」を持たせる（負なら通れない）。空洞（いちばん広い空間から歩いて行けない場所）の検出 |
| `KakurenboGridSubsystem` | グリッドとブロック配置（積み上げ・範囲ダメージ・1 個だけダメージ・落下）、家具のマス（`IsObstacle`。壊せない・通れない・置けない）、歩くだけの経路用グリッドと壁を壊す経路用グリッド |
| `KakurenboArena` | 床（市松模様）・外周の壁（高い部分はかくれんぼ中だけ `SetTallWallsVisible`）・鬼の出入り口（東側の赤い門と前の 3×2 マス）を館の大きさで作る（`RebuildShell`）、外側の地面・夜の明かり（月・空・霧・ランプ）・ポストプロセス、マップごとの家具と床・壁の模様の板（`ApplyMap`）を C++ で生成（レベルアセット不要） |
| `KakurenboAutoTest.cpp` | `KakuAutoTest <Scenario>` の実装 |
| `Tests/KakurenboTests.cpp` | Automation の単体テスト |

- バランスの数値は `KakurenboIncremental/Data/*.csv`（Stages / Upgrades / Walls / OniTypes / Traps / Prestige / PrestigeUpgrades / Maps / Furniture / Surfaces）と間取り `Data/Maps/*.txt`。起動時に読み込む（ビルド不要）。書式は `Data/README.md`
- 購入パートの商品の番号は `GetShopIndexOfWall` / `GetShopIndexOfTrap` で求める（テストで番号を決め打ちしない。並び: 強化 2 つ → 壁 → 罠）。
  `KakuBuy` は開いているお店（`bPrestigeShopTab`）の商品。転生のお店は `KakuBuyPrestige`（並びは `EPrestigeUpgrade`）
- 壁の耐久の倍率は転生のお店の「壁の硬さ」で決まる（`GetWallHPMultiplier`）。壁は `GetEffectiveWallTypes` の耐久で作る。
  ダッシュ・ジャンプは最初は使えない（`ApplyPrestigeToPlayer` が転生のお店のレベルから設定する）。テストで使うときは `PrestigeLevels` を上げて `ApplyPrestigeToPlayer`
- 購入パート・設置パートの UI は HUD が毎フレーム `Buttons` に登録し、PlayerController の `HandleUIClick` が次のフレームのクリックで使う
- 壁・罠の値段は持っている数で上がる（`GetWallCost` / `GetTrapCost`）。テストで値段を決め打ちしない
- ステージ 1 の鬼は 1 体（Stages.csv）。2 体以上を前提にするテストは `SetStageOniTypes` で決める
- **Fab アセット（鬼の見た目 `Content/CuteCreature`・館の家具 `Content/Stylized_Library`・木と鉄の模様 `Content/Substance_Materials_Vol1_Wood`・`Content/Metallic_Floor`・
  FBX から取り込んだプレイヤーと宝石 `Content/Player`・`Content/Gem`・元の FBX `SourceArt/`）は Git に入れない**（公開リポジトリのため。`.gitignore` 済み）。
  パスは DefaultGame.ini（鬼・プレイヤー・宝石）と Data/Furniture.csv（家具）・Data/Surfaces.csv（模様）。アセットが無いパソコンでは円柱・箱・色で動く。テストは両方で通るように書く
- FBX（プレイヤー・宝石）の取り込みは `Tools\ImportSourceArt.ps1`（エディタの Python をコマンドラインで動かす。PythonScriptPlugin は uproject で有効）。
  模様は共通のマテリアル `/Game/Kakurenbo/Materials/M_KakuSurface`（Git に入れる。同じスクリプトが作る）に実行時にテクスチャを差し込む（`UKakurenboLibrary::CreateSurfaceMaterial`）
- **館の大きさはマップごとに違う**（間取りの文字数 × 行数。16×16〜31×29）。変わるときは `ApplyCurrentMap` が舞台とグリッドを作り直す。テストで座標を決め打ちしない（家具の無いテスト用の舞台は 24×24）
- **自動テストは家具の無い舞台で行う**（`KakuAutoTest` の最初に `bMapOverride` で切り替える。壁を置くマスが家具で塞がらないように）。
  マップを確かめる `Maps` / `Mood` と再起動セーブだけ本物のマップ。マップが変わると置いた壁・罠は在庫に戻る（`SwitchToMap`）
- 慎重鬼は広い場所を見終わるまで空洞を調べない。空洞を調べさせたいテストは Blackboard の全マスを「調べた」にしておく（CarefulShare 参照）
- 収入・耐久など小数に意味がある値の表示は `FormatStatNumber`（`FormatBigNumber` は 1000 未満を切り捨てるのでコイン専用）
- 設置パートのグリッド線・プレビュー枠は DrawDebug 系（Shipping では出ない）。演出は `UKakurenboFxSubsystem` を使う
- レベルアセットは無い。既定マップは `/Engine/Maps/Entry`、既定 GameMode は `KakurenboGameMode`（DefaultEngine.ini）
- 見た目はエンジン付属の BasicShapes と `BasicShapeMaterial`（"Color" パラメータ）で仮組み

## ビルドとテスト（UE エディタは閉じておく）

```powershell
powershell -ExecutionPolicy Bypass -File Tools\Build.ps1                       # ビルド（-Unity: コミット前の確認。全部まとめてビルド）
powershell -ExecutionPolicy Bypass -File Tools\RunUnitTests.ps1                # 単体テスト（描画なし）
powershell -ExecutionPolicy Bypass -File Tools\RunAutoTest.ps1 -Scenario Camera  # 下の一覧のシナリオ
powershell -ExecutionPolicy Bypass -File Tools\RunSaveRestartTest.ps1           # 再起動をまたぐセーブ（2 回起動する）
powershell -ExecutionPolicy Bypass -File Tools\Package.ps1                     # 配布用の .exe（Packaged/Windows）。Data/*.csv もコピーする
```

- `RunAutoTest` はゲームを実際に起動し、`[AutoTest]` ログと `KakurenboIncremental/Saved/AutoTest/*.png` を出力する。
  スクリーンショットを Read で確認して見た目も検証すること。最後に `CHECK: n passed, m failed` が出る
- シナリオ: Camera / Loop / Senses / Touch / Treasure / Build / Save（基本）、
  Entrance / Closed / Pocket / Spin / Breaker / Careful（鬼の移動と種類）、Trap / Shop / Fx（M5）、
  Gate / Crowd / Quiet / Dash / Prestige（M6）、Look（鬼の見た目を近くで撮る）、
  GateWalled / Matchup / CarefulShare（M8：門を囲まれたとき・鬼と罠の相性・慎重鬼が建物を分け合う）、
  BackToShop / Steps / TreasureOni / Detector / CarefulSweep / Maps / Mood / Sounds（M9：購入パートへ戻る・足音・宝物鬼・探知鬼・慎重鬼の探し方・館のマップ・見た目・音と BGM）、
  Perch（M10：家具・壁の上のプレイヤーへ鬼が飛び乗る）、Hearing / Swatches（M12：左右の聞こえ方・気づいた音・心臓の音 / 模様・プレイヤー・宝石の見た目）。仕様を変えたら全部流す
- 鬼のテストは `KeepOnlyOni` で 1 体だけ残す（他は地下へ移して止める）と結果が安定する。鬼は必ず東の門の前から出てくるので、
  プレイヤーの近くで試したいときは鬼を `SetActorLocation` で動かす。行き先を決めたいときは `DebugGoTo`
- カーソルを使うテスト（設置パートのマス・HUD のボタン）は `bUseTestCursor` / `TestCursorPosition` を使う（本物のマウスは動かさない）。
  ボタンは `ClickUI(EKakurenboUIAction, Index)`、画面上の点は `ClickAt`
- 効果音は既定では `-NoSound` で起動するが、鳴らした回数（`GetPlayCount`）は数えるのできっかけは確かめられる。
  `RunAutoTest.ps1 -Scenario Fx -Sound` で実際に再生まで確かめる（`-ExtraExec "KakuVolume 0.3,"` で小さめに）
- スクリーンショットを撮るステップでプレイヤーを動かすと、動かした後の画面が写る。撮ってから次のステップで動かす
- 入力が絡む変更は、`PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(...))` で疑似入力を流して検証する
  （`SetControlRotation` などで直接状態を書き換えるテストでは、入力の経路のバグを見逃す）
- 疑似入力は次のフレームの入力処理で反映される。結果の確認は `GetWorldTimerManager().SetTimerForNextTick` で行う
  （タイマーは TG_PrePhysics の入力処理より後に動く。固定の秒数で待つと、フレームの引っかかりで同じフレームに実行されて失敗することがある）
- `-ExtraExec "ShowFlag.VisualizeHDR 1,"` を付けると露出（EV100）の実測値が画面に出る
- ユーザーが UE エディタを開いているとリンクに失敗する（DLL がロックされる）。エディタのプロセスは勝手に終了せず、閉じてもらう

### UE の入力まわりの注意（ハマった点）

- 視点の回転入力は `PostProcessInput` で加える。`PlayerTick` の `Super` の後で `AddYawInput` しても、その回転は捨てられる
- `GetInputMouseDelta` には DefaultInput.ini の感度（0.07）がかかる。生の値は `PlayerInput->GetRawKeyValue(EKeys::MouseX)`
- このプロジェクトは「Enable Legacy Input Scales」が有効で、`AddYawInput`/`AddPitchInput` に ×2.5 / ×-2.5（上下反転）がかかる。
  マウス視点は `RotationInput` に直接足している
- マウスの移動量はビューポートがマウスをキャプチャしているときしか届かない（カーソル表示中は届かない）
- 自動テスト中は実際のキーボード・マウス入力を無視する（ViewportClient->SetIgnoreInput。疑似入力だけが届く）
- RunAutoTest は `-KakuNoSave` 付きで起動するので、ユーザーのセーブ（Saved/SaveGames/Kakurenbo.sav）は読み書きしない。Save 系テストは専用スロットを使う
- エディタが起動中で Live Coding が有効だと、外部ビルドが失敗することがある
- 実行時のログ: `KakurenboIncremental/Saved/Logs/KakurenboIncremental.log`

## 役割分担

- Claude: C++ のロジック、DataTable 用の CSV、.ini、設計書
- ユーザー: エディタ作業（BP の派生クラス作成、レベル配置、メッシュ、UI の見た目）とプレイテスト
- C++ で作ったクラスを BP で使う手順は、毎回ユーザーに具体的に伝える

## コーディング規約

- UE の命名規則に従う（`A`=Actor, `U`=UObject/Component, `F`=struct, `E`=enum, bool は `b` 接頭辞）
- UObject への参照は `UPROPERTY()` 付きの `TObjectPtr<>` で持つ（GC 対策）
- BP で調整したい数値は `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="...")` で公開する
- コインなどインフレする数値は `double` を使う
- ユーザーは C++ 初心者で C# 経験者。分かりにくい UE 特有の書き方には短いコメントを付ける
- ゲームロジックは C++、見た目と配置は BP というハイブリッド構成にする
- .cpp の無名名前空間の定数（色など）と、関数の中の変数に同じ名前を付けない。ビルドで複数の .cpp が 1 つにまとめられる（Unity ビルド）と
  C4459 エラーになる。まとめ方はパソコンによって変わるので、片方のパソコンでは通ってしまう。
  **変更中のファイルはまとめずにビルドされる**（git status で変更があるファイル）ので、自分の作業中は通っても、コミット後・別のパソコンでは失敗する。
  **コミット前に `Tools\Build.ps1 -Unity`**（全部まとめてビルド）で確かめる。クラスのメンバー名（`Mesh` など）と同じ名前のローカル変数も C4458 になる
- **.h / .cpp は UTF-8（BOM 付き）で保存する**（日本語環境の MSVC が日本語コメントや文字列を誤読しないため）。
  新規作成・編集後に BOM が付いているか確認する

## Git

- `*.uasset` と `*.umap` は Git LFS で管理する（`.gitattributes`）
- 区切りのよい単位でコミットし、origin/main へ push してよい（ユーザー許可済み）
