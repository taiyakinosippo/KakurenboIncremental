# アセット一覧（見た目・音の素材）

リポジトリは非公開（private）にしたので、**Fab などから入れたアセットも Git（Git LFS）に入れている**。
別のパソコンでは `git pull`（または `Tools\AutoBuild.bat`）だけでそろう。

## Git に入っているもの

| 使い道 | アセット（入手先） | 場所 |
|---|---|---|
| 鬼の見た目 | Fab「Cute Creature」 | `Content/CuteCreature` |
| 館の家具 | Fab「Stylized Library」（48 Cozy Interior Props） | `Content/Stylized_Library` |
| 鉄の壁 | Fab「Stylized Metallic Floor」 | `Content/Metallic_Floor` |
| 石の壁・地下室などの壁 | Fab「Stylized Hand-Painted Stone Wall」 | `Content/Materials_Bundle_Vol1` |
| 消音壁 | Fab「Stucco Wall」（Megascans のしっくい壁。2K に縮めて取り込み） | `Content/QuietWall`（元の画像 `SourceArt/QuietWall`） |
| 床・壁紙・木の壁 | Fab「Substance Materials Vol 01 - Wood」の 16 種類を **1K に縮めて取り込んだもの** | `Content/Kakurenbo/Wood`（元の画像 `SourceArt/Wood`） |
| プレイヤー | Fab「Cartoon Male Character Rigged 002」（FBX。CC BY 4.0：作者 Character Download） | `Content/Player`（元の FBX `SourceArt/Player`） |
| プレイヤーの動き | UE 公式のマネキン（Third Person テンプレートと同じもの） | `Content/Characters/Mannequins` |
| お宝（宝石） | Fab「Diamond Gem Shape Set - 2 Collectibles」（FBX） | `Content/Gem`（元の FBX `SourceArt/Gem`） |
| 共通のマテリアル | 自作（Tools/ImportSourceArt.ps1 が作る） | `Content/Kakurenbo/Materials/M_KakuSurface` |

- 見た目のテクスチャの指定は `Data/Surfaces.csv`、プレイヤー・宝石・鬼のパスは `Config/DefaultGame.ini`、家具は `Data/Furniture.csv`
- FBX・画像（`SourceArt/`）から作り直すときは `Tools\ImportSourceArt.ps1`（エディタは閉じておく）

## Git に入れていないもの（入れなくても動く）

| アセット | 理由 | 入れたいとき |
|---|---|---|
| Fab「Substance Materials Vol 01 - Wood」の元のパック（`Content/Substance_Materials_Vol1_Wood`） | 2.8GB あり、Git LFS の容量を超えてしまうため。ゲームは上の 1K に縮めたもの（`Content/Kakurenbo/Wood`）を使うので、**無くても見た目は同じ** | エディタの Fab から「プロジェクトに追加」（ほかの木の模様を使いたいとき） |
| 効果音・BGM | まだ無い（プログラムで作った音で鳴っている） | 音のアセットを入れたら GameMode の SoundOverrides / MusicOverrides に設定する |

## 注意

- `.uasset` は Git LFS の「lockable」指定なので、取ってきたファイルは**読み取り専用**になる。エディタで編集するときは、エディタが聞いてきたら書き込みを許可する
  （`Tools\ImportSourceArt.ps1` は自分が作り直すフォルダだけ自動で書き込みできるようにする）
- 大きな素材を足すときは、Git LFS の容量（GitHub の無料枠）に気をつける。4K・8K のテクスチャは縮めてから入れる
