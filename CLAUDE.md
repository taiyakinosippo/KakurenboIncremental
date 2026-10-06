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
| `KakurenboPlayerController` | 入力（毎フレームのキー状態をポーリング）、設置の照準、デバッグ用 Exec コマンド |
| `KakurenboHUD` | Canvas に直接描く仮 UI（日本語は `/Engine/EngineFonts/Roboto` のフォールバックで表示） |
| `HiderCharacter` / `OniCharacter` | プレイヤー / 鬼（状態遷移 AI） |
| `GridPathfinder` | ワールドに依存しない A*。壁マスに「壊すコスト」を持たせる |
| `KakurenboGridSubsystem` | グリッドとブロック配置（積み上げ・範囲ダメージ・落下） |
| `KakurenboArena` | 床・外周の壁・ライトを C++ で生成（レベルアセット不要） |
| `KakurenboAutoTest.cpp` | `KakuAutoTest <Scenario>` の実装 |
| `Tests/KakurenboTests.cpp` | Automation の単体テスト |

- レベルアセットは無い。既定マップは `/Engine/Maps/Entry`、既定 GameMode は `KakurenboGameMode`（DefaultEngine.ini）
- 見た目はエンジン付属の BasicShapes と `BasicShapeMaterial`（"Color" パラメータ）で仮組み

## ビルドとテスト（UE エディタは閉じておく）

```powershell
powershell -ExecutionPolicy Bypass -File Tools\Build.ps1                       # ビルド
powershell -ExecutionPolicy Bypass -File Tools\RunUnitTests.ps1                # 単体テスト（描画なし）
powershell -ExecutionPolicy Bypass -File Tools\RunAutoTest.ps1 -Scenario Loop  # Loop / Oni / Build
```

- `RunAutoTest` はゲームを実際に起動し、`[AutoTest]` ログと `KakurenboIncremental/Saved/AutoTest/*.png` を出力する。
  スクリーンショットを Read で確認して見た目も検証すること
- 実行中のゲームにユーザーのキー入力が入ることがあるので、ログに想定外の遷移があればそれを疑う
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
