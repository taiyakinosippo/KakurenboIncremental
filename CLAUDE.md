# KakurenboIncremental

UE 5.8 の C++ プロジェクト。隠れる側のかくれんぼインクリメンタルゲーム。題名は「**かせぐんぼ**」（プロジェクト名・フォルダ名は KakurenboIncremental のまま）。
ゲーム仕様は [docs/GameDesign.md](docs/GameDesign.md) を参照し、仕様が変わったら更新すること。
これまでのユーザーの依頼と決めたことは [docs/History.md](docs/History.md)。大きな依頼や決定があったら追記すること。

## 構成

- リポジトリルート: `C:\KakurenboIncremental`
- UE プロジェクト: `KakurenboIncremental/KakurenboIncremental.uproject`
- C++ モジュール: `KakurenboIncremental/Source/KakurenboIncremental/`
- エンジン: `C:\Program Files\Epic Games\UE_5.8`（`Tools/EnginePath.ps1` が場所を自動で探す。別の場所なら環境変数 `UE_ROOT`）
- エディタ: VSCode（ユーザー）。ビルドは MSVC（Visual Studio 2022・MSVC 14.44）
- 複数のパソコンで開発している。作業の前に `git pull`、終わったら push。別のパソコンの環境構築は [docs/Setup.md](docs/Setup.md)
- **pull したら必ず `Tools\Build.ps1` でビルドし直す**（Binaries は Git に入らないので、ビルドしないと古いゲームのまま動く）。
  `Tools\AutoBuild.ps1`（ダブルクリックは `Tools\AutoBuild.bat`）が pull → ビルド → FBX・マネキンの取り込み（必要なら）→ **パッケージ化（Packaged/Windows の .exe）**をまとめて行う。
  パッケージ化は前回と同じコミット・手元の変更なしなら飛ばす（`-Package` で必ず作り直す・`-NoPackage` でしない）。
  `-InstallHook` で git pull の後に自動でビルド＋パッケージ化する（`.git/hooks/post-merge` に 1 行。このパソコンは設定済み。パッケージ化の分、pull が数分かかることがある）。`-Test` 単体テスト・`-Watch` 定期的に pull。
  **Claude が自分の作業でビルドするときは `-NoPackage`（または Build.ps1）でよい**
- **GitHub の履歴を書き換えたことがある**（2026-10-14：間違えて入れた猫のパックを M15b・M15c から消して force push）。
  `git pull` で「コミットが分かれている」・マージがぶつかって止まった（CONFLICT）と出たら、`Tools\RepairHistory.ps1` を動かす（AutoBuild とフックは自動で呼ぶ。止まったマージはやめてから合わせる）。
  手元の古いコミットが全部 GitHub に同じもの（作者・日時・題名）があれば main を GitHub に合わせる。手元だけのコミットがあれば何もしないので、書き換え後の main に載せ直す

### コードの構成

| ファイル | 役割 |
|---|---|
| `KakurenboGameMode` | ルールと進行（タイトル・つづきから・はじめから・パート遷移・収入・購入・壁の補強・壁と罠の設置と全部回収・鬼のスポーン・転生・消音・足音・よじ登る音・探知鬼の呼び寄せ・館のマップの切り替え・館の大きさによる音の倍率 `GetMapNoiseScale`・演出と効果音と BGM のきっかけ）。数値は UPROPERTY。`Config=Game` なので鬼の見た目のパスは DefaultGame.ini |
| `KakurenboGameState` | 現在の状態（コイン・ステージ・在庫・強化レベル・転生ポイント）。HUD はここを読む |
| `KakurenboPlayerController` | 入力（`PostProcessInput` でキー状態をポーリング。キーは `UKakurenboSettings` の操作 → キーの表で調べる。**コントローラー**：最後にさわった方へ切り替え `bUsingGamepad`、メニューのフォーカス移動 `HandleGamepadFocus`、設置パートのカーソル `HandleGamepadBuild`）、パートごとの視点・入力モードの切り替え、メニュー（一時停止・設定・キー設定・操作説明・確認）、タイトルの選択、はじめての操作のヒント、カーソルでの設置、ダッシュ、転生の確認、デバッグ用 Exec コマンド |
| `KakurenboSettings` | 設定（音量・マウス感度・スティック感度・上下反転・ヒント・キー設定・見たヒント。コントローラーのボタンは `GamepadKey` の固定の表）。GameInstanceSubsystem。`KakurenboSettings.sav` に保存（-KakuNoSave では読み書きしない）。操作 → キーの表は `FKakurenboKeyMap`（単体テストあり） |
| `KakurenboHUD` | Canvas に直接描く UI（日本語は `/Engine/EngineFonts/Roboto` のフォールバックで表示）。タイトル・メニュー・設定・キー設定・操作説明・ヒントも。`RichText` は `[ ]` をキーの形で描く。鬼・お宝の方向は出さない（音で探す） |
| `HiderCharacter` / `OniCharacter` | プレイヤー（俯瞰・真上・三人称カメラ（壁に当たったらすぐ寄り、離れたらゆっくり戻る）、ダッシュ、**よじ登る**（`TryStartClimb`・`FindClimbTarget`。ジャンプは無い）、足音・しのび足。Fab のモデルを `ApplyLook` で PoseableMesh に付け、`ApplyOfficialAnimation` で見えない公式マネキンのアニメーションの骨の回転を写す。無ければプログラムで骨を回す） / 鬼（Wander/Investigate/Chase/Inspect/Attack/Stunned。種類 EOniType ごとに探し方が違う：標準・スピード・パワー・慎重・宝物・探知。ぶつかったらアウト。鬼どうしはすり抜ける。歩くと足音。クライミング壁には飛び乗らない。家具のすき間の相手にはそばのマスから手を伸ばす。スケルタルメッシュに差し替え可） |
| `KakurenboOniBlackboard` | 慎重鬼どうしで共有する「調べたマス」と「向かっているマス」「調べている建物」（WorldSubsystem） |
| `TreasureActor` | お宝（距離で取得。ときどきキラキラと鳴る。GameMode が宝石のメッシュと色を渡す） |
| `SmokeCloud` | 煙幕（煙幕ダッシュで足元に投げる）。`IsSightBlocked` で煙の中・向こう側の視線をさえぎる（鬼の `CanSeeTarget` が使う）。見た目は半透明の `M_KakuSmoke`（無ければ不透明な球）。煙幕ダッシュは回数制（`DashUsesPerRound`・転生のお店の「煙幕の数」） |
| `TrapActor` | 罠（トリモチ: 踏んだ鬼を Stun / おとり: 一定間隔で EmitNoise）と**宝石のお店の道具**（M15：身代わり人形・一方通行ののれん（鬼だけ止める箱・向き）・真っ暗ブロック・迷い猫。だるまさんがころんだは置かない）。当たり判定なし、距離で発動。配置と設計図（のれんの向き TrapDir も）はグリッドが持つ。宝石の道具は Traps.csv の bGemItem |
| `KakurenboOniStudio` | 鬼の写真スタジオ（舞台のずっと下に見本の鬼・背景・明かり・SceneCapture）。チュートリアル・図鑑の写真（RenderTarget）を撮る |
| `FakeHiderCharacter` | 身代わり人形（AHiderCharacter の子。bIsDecoyDoll）。鬼に追われると反対へまっすぐ逃げ、大きな音を出す。鬼は `GetChaseTarget` で人形も追う |
| `KakurenboFx` | 仮の演出（破片 `AKakurenboBurstFx`・床の輪 `AKakurenboRingFx`）と窓口の `UKakurenboFxSubsystem`（輪は使い回す） |
| `KakurenboSoundSubsystem` | 効果音を鳴らす（2D / その場所から。その場所からの音は自前で左右に振り分けた 2ch の波形を 2D で鳴らす：聞く人＝自分の体・向き＝カメラ、壁の向こうはこもる。`GetLastSpatial` でテスト）と BGM（`PlayMusic`）。GameMode の `SoundOverrides` に音アセットがあればそちら（エンジンの 3D の音） |
| `KakurenboSynth` | 効果音と BGM の波形をプログラムで作る（純粋な計算・単体テストあり）。左右の振り分け（`ComputeSpatial`・`MakeStereo`・`DistanceGain`）も。`USoundWaveProcedural` で再生し、効果音は長さぶん経ったら止める。BGM は 1 秒ごとに 1 周ぶん足す |
| `KakurenboMaps` | 館の間取り（Data/Maps/*.txt の文字の図）を読み、同じ文字の長方形を家具にまとめる。館の大きさ＝文字数 × 行数（`MeasureLayout`）。`[floor]` の床の種類（`ParseFloorLayer`）（純粋ロジック・単体テストあり） |
| `KakurenboLayout` | 設計図からの修復計画（純粋ロジック・単体テストあり） |
| `KakurenboBalance` | ステージ設定の解決（表より後は伸ばす）と CSV → DataTable の読み込み |
| `KakurenboSaveGame` | セーブデータ（GameMode の SaveProgress / LoadProgress / ResetProgress） |
| `GridPathfinder` | ワールドに依存しない A*。壁マスに「壊すコスト」を持たせる（負なら通れない）。空洞（いちばん広い空間から歩いて行けない場所）の検出 |
| `KakurenboGridSubsystem` | グリッドとブロック配置（積み上げ・範囲ダメージ・1 個だけダメージ・落下）、家具のマス（`IsObstacle`。壊せない・通れない・置けない）、歩くだけの経路用グリッドと壁を壊す経路用グリッド |
| `KakurenboArena` | 床（市松模様）・外周の壁（高い部分はかくれんぼ中だけ `SetTallWallsVisible`）・鬼の出入り口（東側の赤い門と前の 3×2 マス）を館の大きさで作る（`RebuildShell`）、外側の地面・夜の明かり（月・空・霧・ランプ）・ポストプロセス、マップごとの家具（メッシュ 1 個ずつの当たり判定）と部屋ごとの床・壁紙と腰壁・壁の飾り（窓・絵・松明・出入り口の枠と上の壁。`BuildDecor`）（`ApplyMap`）を C++ で生成（レベルアセット不要） |
| `KakurenboAutoTest.cpp` | `KakuAutoTest <Scenario>` の実装 |
| `Tests/KakurenboTests.cpp` | Automation の単体テスト |

- バランスの数値は `KakurenboIncremental/Data/*.csv`（Stages / Upgrades / Walls / OniTypes / Traps / Prestige / PrestigeUpgrades / Maps / Furniture / Surfaces / FloorKinds / Decor）と間取り `Data/Maps/*.txt`。起動時に読み込む（ビルド不要）。書式は `Data/README.md`
- **間取りは `python Tools/GenerateMansion.py` で作る**（25 個全部を上書き。玄関ホール・廊下・部屋・家具・`[floor]`）。作り直したら `Maps` / `Mood` / 単体テストを流す
- 購入パートの商品の番号は `GetShopIndexOfWall` / `GetShopIndexOfTrap` で求める（テストで番号を決め打ちしない。並び: 強化 3 つ（連打・時間・壁の補強）→ 壁 → コインの罠）。
  `KakuBuy` は開いているお店（`ShopTab`：コイン / 宝石 / 転生）の商品（まとめ買い `BuyAmount` の数だけ）。転生のお店は `KakuBuyPrestige`（並びは `EPrestigeUpgrade`）、
  宝石のお店は `KakuBuyGem`（番号は `GetGemShopIndexOfTrap`。道具の種類は `FindTrapTypeOfKind`）。宝石は `KakuAddGems`
- 設置パートの下の欄は `GetBuildSlots()`（持っている宝石の道具だけ並ぶ）。`SelectedBuildSlot` は「壁の番号 / 壁の数＋罠の番号」のまま
- 壁の耐久 ＝ Walls.csv の MaxHP × 壁の補強（`GetWallReinforceFactor`・足し算）× 転生のお店の「壁の硬さ」（`GetWallHPMultiplier`・掛け算）。壁は `GetEffectiveWallTypes` の耐久で作る。
  壁は 3 種類（通常・消音・クライミング。クライミング壁は `IsClimbUnlocked` まで買えない）。
  ダッシュ・よじ登るは最初は使えない（`ApplyPrestigeToPlayer` が転生のお店のレベルから設定する）。テストで使うときは `PrestigeLevels` を上げて `ApplyPrestigeToPlayer`
- 購入パート・設置パート・メニューの UI は HUD が毎フレーム `Buttons` に登録し、PlayerController の `HandleUIClick` が次のフレームのクリックで使う。
  メニューを開いている間は後ろのボタンは登録しない（押せない）
- **起動するとタイトル画面**（`EKakurenboPhase::Title`。タイトルでは保存しない）。`KakuAutoTest` は最初に `LeaveTitleForTest`（セーブがあればつづきから、無ければはじめから）でタイトルを抜ける。
  `-KakuSkipTitle` でもタイトルを飛ばせる。タイトルそのものを確かめるのは Title シナリオ
- **コントローラーのボタンも EKeys で直接見ない**：A・B・X・Y は `UKakurenboSettings::PadFace(EKakurenboPadFace::A)` など（ボタン配置 Xbox / 任天堂で場所が変わる）。名前は `KeyLabel`
- **キーを EKeys で直接見ない**：`IsActionDown` / `WasActionPressed`（キー設定を反映）を使う。新しい操作は `EKakurenboAction` と `UKakurenboSettings::DefaultBinding`・`ActionDisplayName` に足す。
  画面に出すキーの名前は `KeyName`（`[ ]` で囲めば RichText がキーの形で描く）。文は直書きのキー名にしない
- メニューを開くとゲームを止める（`SetPause`）。**一時停止中はワールドのタイマー（テストの Steps・NextTick）も止まる**ので、メニューのテストは `RealDelay`（実時間）で進める
- 操作のヒント（`QueueTip`）は自動テスト中は出さない（スクリーンショットが変わらないように）。Hints シナリオだけ `bShowTipsInAutoTest`。
  鬼のチュートリアル（ゲームが止まる）も自動テスト中は出さない。OniIntro シナリオだけ `bShowOniIntroInAutoTest`
- 壁・罠の値段は持っている数で上がる（`GetWallCost` / `GetTrapCost`）。テストで値段を決め打ちしない
- ステージ 1 の鬼は 1 体（Stages.csv）。2 体以上を前提にするテストは `SetStageOniTypes` で決める
- **アセットは Git（LFS）に入れる**（リポジトリは非公開。一覧は [docs/Assets.md](docs/Assets.md)）。Fab の大きな元のパック（木 `Substance_Materials_Vol1_Wood` 2.8GB・
  `StylizedKitchen` 1.2GB・`Isometric-Interiors`・`Medieval Room Kit*`）は入れず（.gitignore）、ゲームは使う物だけ 1K に縮めたコピー（`Content/Kakurenbo/Wood`・`Content/Kakurenbo/Mansion`）を使う。
  元のパックから家具を足すときは `Tools/ExtractFabProps.py`（エディタの Python で FBX と画像を `SourceArt/FabProps` へ → `python Tools/ExtractFabProps.py --shrink` で 1K に）→ `Tools\ImportSourceArt.ps1`。**大きな素材（4K・8K のテクスチャ）は縮めてから入れる**（LFS の容量）。新しい素材を入れたら docs/Assets.md に書く。
  パスは DefaultGame.ini（鬼・プレイヤー・宝石）と Data/Furniture.csv（家具）・Data/Surfaces.csv（模様）。アセットが無くても円柱・箱・色で動くようにしておく（テストは両方で通るように書く）。
  `.uasset` は LFS の lockable で読み取り専用になる（ImportSourceArt.ps1 は自分が作るフォルダの読み取り専用を外す）
- FBX（プレイヤー・宝石・猫）の取り込みは `Tools\ImportSourceArt.ps1`（エディタの Python をコマンドラインで動かす。PythonScriptPlugin は uproject で有効）。
  煙幕のマテリアルだけ作り直すときは `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="Tools/ImportSourceArt.py --smoke-only"`。
  タイトルロゴは `SourceArt/Logo/TitleLogo.html` を `Tools\MakeLogo.ps1`（Edge のヘッドレスで透明な PNG に）→ `--logo-only` で `/Game/Kakurenbo/UI/T_TitleLogo`（HUD の `TitleLogoAsset`。無ければ文字）。
  猫は `--cat-only`（取り込み直し）・`--cat-materials-only`（マテリアルだけ）。猫（Low Poly Cat）はスタティックメッシュ。向きは DefaultGame.ini の `CatMeshYaw`。
  **スケルタルメッシュに使うマテリアルは `used_with_skeletal_mesh` を付ける**（無いとゲームでは灰色の既定のマテリアルになる）。
  Python で書き換えたアセットは `save_loaded_asset(..., only_if_is_dirty=False)` で保存する（変更の印が付かず保存されないことがある）。
  模様は共通のマテリアル `/Game/Kakurenbo/Materials/M_KakuSurface`（Git に入れる。同じスクリプトが作る）に実行時にテクスチャを差し込む（`UKakurenboLibrary::CreateSurfaceMaterial`）
- **館の大きさはマップごとに違う**（間取りの文字数 × 行数。ステージごとに大きくなる：16×16〜40×40）。変わるときは `ApplyCurrentMap` が舞台とグリッドを作り直す。テストで座標を決め打ちしない（家具の無いテスト用の舞台は 24×24）
- **自動テストは家具の無い舞台で行う**（`KakuAutoTest` の最初に `bMapOverride` で切り替える。壁を置くマスが家具で塞がらないように）。
  マップを確かめる `Maps` / `Mood` / `CrouchWalk` / `CameraWall` / `ShelfGap` / `BuildRay` と再起動セーブだけ本物のマップ。家具の無い舞台では音の倍率（`GetMapNoiseScale`）は 1。マップが変わると置いた壁・罠は在庫に戻る（`SwitchToMap`）
- 慎重鬼は広い場所を見終わるまで空洞を調べない。空洞を調べさせたいテストは Blackboard の全マスを「調べた」にしておく（CarefulShare 参照）
- 収入・耐久など小数に意味がある値の表示は `FormatStatNumber`（`FormatBigNumber` は 1000 未満を切り捨てるのでコイン専用）
- 設置パートのグリッド線・プレビュー枠は DrawDebug 系（Shipping では出ない）。演出は `UKakurenboFxSubsystem` を使う
- 設置パートでは外周・部屋の壁の高い部分を**隠しているだけで当たり判定は残っている**。カーソルのレイ（`UpdateBuildTargetAt`）は見えていない物を無視する（家具の当たり判定の箱は `AKakurenboArena::FurnitureCollisionTag` で見分けて当てる）
- HUD の文は枠からはみ出さないよう `TextFit` / `RichTextFit`（入らなければ小さく・「…」）を使う。幅は `MeasureText` / `MeasureRich`
- レベルアセットは無い。既定マップは `/Engine/Maps/Entry`、既定 GameMode は `KakurenboGameMode`（DefaultEngine.ini）
- 見た目はエンジン付属の BasicShapes と `BasicShapeMaterial`（"Color" パラメータ）で仮組み

## ビルドとテスト（UE エディタは閉じておく）

```powershell
powershell -ExecutionPolicy Bypass -File Tools\Build.ps1                       # ビルド（-Unity: コミット前の確認。全部まとめてビルド）
powershell -ExecutionPolicy Bypass -File Tools\AutoBuild.ps1 -NoPull           # ビルド＋必要な取り込み＋パッケージ化（エディタが開いていたら何もしない）。結果は Saved/Logs/AutoBuild.log
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
  Perch（M10：家具・壁の上のプレイヤーへ鬼が飛び乗る）、Hearing / Swatches / Crouch（M12：左右の聞こえ方・距離・気づいた音・心臓の音 / 模様・プレイヤー・宝石の見た目 / しゃがんで低い物に隠れる）、
  Title / Hints / CrouchWalk（M13：タイトル・設定・キー設定・一時停止 / はじめての操作のヒント / しゃがんで家具に向かって歩いてもがくがくしない）、
  Climb / Walls / Gamepad / CameraWall / Lineup / TreasureBreak（M14：よじ登る / 3 種類の壁と壁の補強 / コントローラー / 壁ぎわでカメラが揺れない / 全部の家具を壁ぎわに並べて向き・大きさ・すき間を確かめる / 宝物鬼が壁を壊してお宝へ）。
  CrouchFind / CarefulFind（M14：しゃがみは近くではのぞき込まれ、すぐそばでは気配で気づかれる / 慎重鬼が本物の館でしゃがんだプレイヤーを何秒で見つけるか。旧と新の探し方を比べる。約 8 分）、
  GemShop / OneWay / Darkness / Cat / FakeHider / Daruma / PowerBox / BuildRay / Heartbeat / ShelfGap（M15：宝石とまとめ買い / 宝石のお店の道具 5 つ / パワー鬼の前方 2×2×2 /
  設置パートのレイが見えない壁に当たらない（本物の館）/ 心臓の音が距離で大きく / 25 の館の背の高い家具の前のすき間に体が入る）、SideGap（本棚を並べた間のすき間は体が入る 78cm 以上か無いか）、
  OniIntro（初めて出てくる鬼のチュートリアル・図鑑。止まっている間のテストは RealDelay で進め、キーを押した結果はフレーム数で待って確かめる）、
  StaleDesign（在庫が無くて赤いマスに別の物を置くと、そのマスの設計図が消える）、
  Intuition（鬼の勘：見えず聞こえずでもプレイヤーのそばへ来やすい。**勘は自動テストでは Intuition シナリオ以外切る**（`bOniIntuition`））。
  Catalog は素材選び用（`Saved/catalog.txt` の `/Game/...` のメッシュを並べて撮る。テストではない）。仕様を変えたら全部流す
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
