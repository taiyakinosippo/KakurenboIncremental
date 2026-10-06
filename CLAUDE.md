# KakurenboIncremental

UE 5.8 の C++ プロジェクト。隠れる側のかくれんぼインクリメンタルゲーム。
ゲーム仕様は [docs/GameDesign.md](docs/GameDesign.md) を参照し、仕様が変わったら更新すること。

## 構成

- リポジトリルート: `C:\KakurenboIncremental`
- UE プロジェクト: `KakurenboIncremental/KakurenboIncremental.uproject`
- C++ モジュール: `KakurenboIncremental/Source/KakurenboIncremental/`
- エンジン: `C:\Program Files\Epic Games\UE_5.8`
- エディタ: VSCode（ユーザー）。ビルドは MSVC

### コードの構成

| ファイル | 役割 |
|---|---|
| `KakurenboGameMode` | ルールと進行（パート遷移・収入・購入・壁の設置・鬼のスポーン）。数値は UPROPERTY |
| `KakurenboGameState` | 現在の状態（コイン・ステージ・在庫・強化レベル）。HUD はここを読む |
| `KakurenboPlayerController` | 入力（`PostProcessInput` でキー状態をポーリング）、パートごとの視点・入力モードの切り替え、カーソルでの設置、デバッグ用 Exec コマンド |
| `KakurenboHUD` | Canvas に直接描く仮 UI（日本語は `/Engine/EngineFonts/Roboto` のフォールバックで表示）、鬼の方向表示 |
| `HiderCharacter` / `OniCharacter` | プレイヤー（俯瞰・三人称カメラ） / 鬼（Wander/Investigate/Chase/Inspect/Attack/Stunned。種類 EOniType ごとに探し方が違う。ぶつかったらアウト） |
| `KakurenboOniBlackboard` | 慎重鬼どうしで共有する「調べたマス」と「向かっているマス」（WorldSubsystem） |
| `TreasureActor` | お宝（距離で取得） |
| `KakurenboLayout` | 設計図からの修復計画（純粋ロジック・単体テストあり） |
| `KakurenboBalance` | ステージ設定の解決（表より後は伸ばす）と CSV → DataTable の読み込み |
| `KakurenboSaveGame` | セーブデータ（GameMode の SaveProgress / LoadProgress / ResetProgress） |
| `GridPathfinder` | ワールドに依存しない A*。壁マスに「壊すコスト」を持たせる（負なら通れない）。空洞（いちばん広い空間から歩いて行けない場所）の検出 |
| `KakurenboGridSubsystem` | グリッドとブロック配置（積み上げ・範囲ダメージ・1 個だけダメージ・落下）、歩くだけの経路用グリッドと壁を壊す経路用グリッド |
| `KakurenboArena` | 床・外周の壁・外側の地面・ライト・ポストプロセス（露出の下限、モーションブラーなし）を C++ で生成（レベルアセット不要） |
| `KakurenboAutoTest.cpp` | `KakuAutoTest <Scenario>` の実装 |
| `Tests/KakurenboTests.cpp` | Automation の単体テスト |

- バランスの数値は `KakurenboIncremental/Data/*.csv`（Stages / Upgrades / Walls / OniTypes）。起動時に読み込む（ビルド不要）。書式は `Data/README.md`
- レベルアセットは無い。既定マップは `/Engine/Maps/Entry`、既定 GameMode は `KakurenboGameMode`（DefaultEngine.ini）
- 見た目はエンジン付属の BasicShapes と `BasicShapeMaterial`（"Color" パラメータ）で仮組み

## ビルドとテスト（UE エディタは閉じておく）

```powershell
powershell -ExecutionPolicy Bypass -File Tools\Build.ps1                       # ビルド
powershell -ExecutionPolicy Bypass -File Tools\RunUnitTests.ps1                # 単体テスト（描画なし）
powershell -ExecutionPolicy Bypass -File Tools\RunAutoTest.ps1 -Scenario Camera  # 下の一覧のシナリオ
powershell -ExecutionPolicy Bypass -File Tools\RunSaveRestartTest.ps1           # 再起動をまたぐセーブ（2 回起動する）
```

- `RunAutoTest` はゲームを実際に起動し、`[AutoTest]` ログと `KakurenboIncremental/Saved/AutoTest/*.png` を出力する。
  スクリーンショットを Read で確認して見た目も検証すること。最後に `CHECK: n passed, m failed` が出る
- シナリオ: Camera / Loop / Senses / Touch / Treasure / Build / Save（基本）、
  Entrance / Closed / Pocket / Spin / Breaker / Careful（鬼の移動と種類）。鬼の仕様を変えたら全部流す
- 鬼のテストは `KeepOnlyOni` で 1 体だけ残す（他は地下へ移して止める）と結果が安定する
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
- **.h / .cpp は UTF-8（BOM 付き）で保存する**（日本語環境の MSVC が日本語コメントや文字列を誤読しないため）。
  新規作成・編集後に BOM が付いているか確認する

## Git

- `*.uasset` と `*.umap` は Git LFS で管理する（`.gitattributes`）
- 区切りのよい単位でコミットし、origin/main へ push してよい（ユーザー許可済み）
