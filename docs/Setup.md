# 別のパソコンでの環境構築

このプロジェクトを別のパソコンでも開発できるようにする手順。Claude（Code タブ）も同じ手順で使える。

## 必要なもの

| もの | 入手先・設定 |
|---|---|
| Git for Windows | https://git-scm.com/ （Git LFS も一緒に入る） |
| Unreal Engine 5.8.3 | Epic Games Launcher からインストール。C ドライブ以外でもよい（下の「UE を別の場所に入れたとき」） |
| Visual Studio 2022 Community | Visual Studio Installer で「C++ によるゲーム開発」ワークロードを入れる。元のパソコンは MSVC 14.44・Windows SDK 10.0.26100 で動いている |
| Claude デスクトップアプリ | https://claude.ai/download 。同じアカウントでログインして Code タブを使う |
| VSCode（任意） | コードを読む・書く用 |

## 手順

1. 上のものをインストールする
2. PowerShell で Git LFS を有効にする（パソコンごとに 1 回）

   ```powershell
   git lfs install
   ```

3. リポジトリを取ってくる。元のパソコンと同じ `C:\KakurenboIncremental` に置くと CLAUDE.md の説明と場所が合う
   （非公開リポジトリなら、初回はブラウザで GitHub にログインする画面が出る）

   ```powershell
   git clone https://github.com/taiyakinosippo/KakurenboIncremental.git C:\KakurenboIncremental
   ```

4. ビルドする（初回は時間がかかる）。`Tools\AutoBuild.bat` をダブルクリックでもよい（pull → ビルド → 必要なら取り込み → 配布用の .exe（`Packaged/Windows`）まで、をまとめて行う。
   .exe は前回から変わっていなければ作り直さない。エディタ用のビルドだけでよいときは `-NoPackage`）

   ```powershell
   cd C:\KakurenboIncremental
   powershell -ExecutionPolicy Bypass -File Tools\AutoBuild.ps1
   ```

   **git pull したら自動でビルド（とパッケージ化）する**ようにしておくと、ビルドし忘れて古いゲームのまま動くことが無くなる（パソコンごとに 1 回）

   ```powershell
   powershell -ExecutionPolicy Bypass -File Tools\AutoBuild.ps1 -InstallHook
   ```

5. 動くか確かめる

   ```powershell
   powershell -ExecutionPolicy Bypass -File Tools\RunUnitTests.ps1
   powershell -ExecutionPolicy Bypass -File Tools\RunAutoTest.ps1 -Scenario Loop
   ```

6. Claude デスクトップアプリの Code タブで、フォルダに `C:\KakurenboIncremental` を選ぶ。
   CLAUDE.md（作業のルール）は自動で読まれるので、「docs/GameDesign.md を読んで続きをお願いします」のように頼めばよい

## UE を別の場所に入れたとき

`Tools` のスクリプトは UE 5.8 の場所を自動で探す（Epic Games Launcher の記録 → `C:\Program Files\Epic Games\UE_5.8` の順）。
見つからないときは、環境変数 `UE_ROOT` に UE_5.8 フォルダの場所を設定する。

```powershell
setx UE_ROOT "D:\Epic Games\UE_5.8"
```

（設定した後に開いた PowerShell・起動し直した Claude アプリから有効になる）

## 2 台で作業するときの約束

- **作業を始める前に `git pull`（または `Tools\AutoBuild.bat`）、終わったらコミットして `git push`**。Claude に「pull してから始めて」「終わったら push して」と頼めばよい
- **同時に 2 台で作業しない**（同じファイルを両方で変えると、あとで合わせる手間がかかる）
- Git に入らないもの
  - `Binaries/`・`Intermediate/`（ビルドの結果）: pull した後はビルドし直す（`Tools\AutoBuild` か、-InstallHook しておけば自動）
  - `Saved/`（セーブデータ・ログ・テストのスクリーンショット）: セーブを持っていきたいときは `KakurenboIncremental/Saved/SaveGames/Kakurenbo.sav` をコピーする
  - `Content/CuteCreature/`（鬼の見た目）・`Content/Stylized_Library/`（館の家具）・`Content/Substance_Materials_Vol1_Wood/`・`Content/Metallic_Floor/`（床と壁の模様）・
    `Content/Materials_Bundle_Vol1/`（石の壁）・`Content/Player/`・`Content/Gem/`（プレイヤーと宝石。FBX から取り込んだもの）・`Content/Characters/`（公式のマネキン）・`SourceArt/`（ダウンロードした FBX）:
    Fab のアセットは公開リポジトリに置けないため。下の手順でそのパソコンでも追加する

## 鬼の見た目（Cute Creature）と館の家具（Stylized Library）を入れる

無くても動く（鬼が円柱、家具が色の付いた箱になるだけ）。入れるときは：

1. `KakurenboIncremental.uproject` を開いてエディタを起動する
2. メニューの「ウィンドウ」→「Fab」（無ければ「編集」→「プラグイン」で Fab を有効にして再起動）
3. 「Cute Creature」を検索して「プロジェクトに追加」。`Content/CuteCreature` ができれば完了（パスの設定は Git に入っているので何もしなくてよい）
4. 同じように「Stylized Library」（48 Cozy Interior Props）を「プロジェクトに追加」。`Content/Stylized_Library` ができれば完了（家具のパスは `Data/Furniture.csv`）
5. 同じように「Substance Materials Vol 01 - Wood」（床・壁・木の壁）・「Stylized Metallic Floor」（鉄の壁）・「Stylized Hand-Painted Stone Wall」（石の壁）を「プロジェクトに追加」。
   `Content/Substance_Materials_Vol1_Wood`・`Content/Metallic_Floor`・`Content/Materials_Bundle_Vol1` ができれば完了（テクスチャのパスは `Data/Surfaces.csv`）
6. エディタを閉じる

## プレイヤーの見た目と宝石（FBX）を入れる（M12）

この 2 つは Fab からは FBX（zip）でダウンロードするので、取り込みはスクリプトで行う（エディタは閉じておく）。無くても動く（青い円柱・金色の立方体）。

1. Fab で「Cartoon Male Character Rigged 002」と「Diamond Gem Shape Set - 2 Collectibles」をダウンロードして展開する
2. 中身を次の場所に置く（`SourceArt/` は Git に入らない）
   - `KakurenboIncremental/SourceArt/Player/` に `Male_002.fbx`・`CharacterTexture_BaseColor.png`・`CharacterTexture_Roughness.png`
   - `KakurenboIncremental/SourceArt/Gem/` に `Diamond_Shape2.fbx`・`Diamond_Master2_*.jpeg`・`Diamond_Shape2_Diamond_Master2_Normal.png`
3. 取り込む（`Content/Player`・`Content/Gem` ができる。共通のマテリアル `Content/Kakurenbo/Materials/M_KakuSurface` も作り直す。
   プレイヤーを動かす **UE 公式のマネキンのアニメーション**も、UE のインストール先のテンプレートから `Content/Characters/Mannequins` へコピーする）。
   下の `Tools\AutoBuild.ps1` を使えば、FBX が置いてあって取り込んでいないときは自動でこれも行う

   ```powershell
   powershell -ExecutionPolicy Bypass -File Tools\ImportSourceArt.ps1
   ```
- Claude との会話の履歴と、コマンドの許可の設定はパソコンごと。ルールと仕様は CLAUDE.md と docs/GameDesign.md に書いてあるので、別のパソコンの Claude もそこから続けられる
