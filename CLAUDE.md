# KakurenboIncremental

UE 5.8 の C++ プロジェクト。隠れる側のかくれんぼインクリメンタルゲーム。
ゲーム仕様は [docs/GameDesign.md](docs/GameDesign.md) を参照し、仕様が変わったら更新すること。

## 構成

- リポジトリルート: `C:\KakurenboIncremental`
- UE プロジェクト: `KakurenboIncremental/KakurenboIncremental.uproject`
- C++ モジュール: `KakurenboIncremental/Source/KakurenboIncremental/`
- エンジン: `C:\Program Files\Epic Games\UE_5.8`
- エディタ: VSCode（ユーザー）。ビルドは MSVC

## ビルド

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" KakurenboIncrementalEditor Win64 Development "-Project=C:\KakurenboIncremental\KakurenboIncremental\KakurenboIncremental.uproject" -WaitMutex -NoHotReloadFromIDE
```

- エディタが起動中で Live Coding が有効だと、外部ビルドが失敗することがある。その場合はユーザーにエディタを閉じてもらうか、エディタ内で Ctrl+Alt+F11 を押してもらう
- 新しい `UCLASS` を追加したときや、ヘッダの `UPROPERTY` 構成を変えたときは、Live Coding ではなくエディタを再起動してフルビルドする
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

## Git

- `*.uasset` と `*.umap` は Git LFS で管理する（`.gitattributes`）
- push はユーザーの確認を取ってから行う
