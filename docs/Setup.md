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

4. ビルドする（初回は時間がかかる）。`.uproject` をダブルクリックして「リビルドしますか」に「はい」でもよい

   ```powershell
   cd C:\KakurenboIncremental
   powershell -ExecutionPolicy Bypass -File Tools\Build.ps1
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

- **作業を始める前に `git pull`、終わったらコミットして `git push`**。Claude に「pull してから始めて」「終わったら push して」と頼めばよい
- **同時に 2 台で作業しない**（同じファイルを両方で変えると、あとで合わせる手間がかかる）
- Git に入らないもの
  - `Binaries/`・`Intermediate/`（ビルドの結果）: pull した後はビルドし直す
  - `Saved/`（セーブデータ・ログ・テストのスクリーンショット）: セーブを持っていきたいときは `KakurenboIncremental/Saved/SaveGames/Kakurenbo.sav` をコピーする
  - `Content/CuteCreature/`（鬼の見た目）・`Content/Stylized_Library/`（館の家具）: Fab のアセットは公開リポジトリに置けないため。下の手順でそのパソコンでも追加する

## 鬼の見た目（Cute Creature）と館の家具（Stylized Library）を入れる

無くても動く（鬼が円柱、家具が色の付いた箱になるだけ）。入れるときは：

1. `KakurenboIncremental.uproject` を開いてエディタを起動する
2. メニューの「ウィンドウ」→「Fab」（無ければ「編集」→「プラグイン」で Fab を有効にして再起動）
3. 「Cute Creature」を検索して「プロジェクトに追加」。`Content/CuteCreature` ができれば完了（パスの設定は Git に入っているので何もしなくてよい）
4. 同じように「Stylized Library」（48 Cozy Interior Props）を「プロジェクトに追加」。`Content/Stylized_Library` ができれば完了（家具のパスは `Data/Furniture.csv`）
5. エディタを閉じる
- Claude との会話の履歴と、コマンドの許可の設定はパソコンごと。ルールと仕様は CLAUDE.md と docs/GameDesign.md に書いてあるので、別のパソコンの Claude もそこから続けられる
