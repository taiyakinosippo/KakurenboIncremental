# バランスデータ

ゲームの数値はこのフォルダの CSV で調整できます（Excel や VSCode で編集 → 保存 → 次の Play から反映）。
文字コードは UTF-8（BOM 付き）のまま保存してください。1 列目は行の名前です。

## Stages.csv（ステージごとの設定）

行の順番がステージ番号です（今は 20 ステージ）。表より後のステージ（21 以降）は、最後の行から
「制限時間 +5 秒 / 報酬 ×4 / 鬼の攻撃力 ×1.6 / 聞こえる距離 +100 / 速度 +15」ずつ伸びます
（伸ばし方は GameMode の Stage Growth で変えられます。鬼の顔ぶれは最後の行のまま。マップは 1 ステージごとに Maps.csv の**後ろの 6 つ**（大きい館）を順に回る）。

**転生しないときついステージ（壁）**：8・13・18 は鬼の数・種類が一気に強くなり、攻撃力が ×2 跳ね上がる（それ以降のステージもその分強いまま）。
転生のお店の煙幕ダッシュ・壁の硬さが無いと越えにくい。考え方は docs/GameDesign.md の「レベルデザイン」

| 列 | 意味 |
|---|---|
| HideDuration | 制限時間（秒） |
| OniTypes | 出てくる鬼の種類と数。`"(Balanced,Scout,Careful,Careful)"` のように並べる（同じ種類を 2 回書けば 2 体。最大 6 体＝出入り口のマスの数）。名前は OniTypes.csv の行の名前。並べ方の考え方は docs/GameDesign.md の「レベルデザイン」 |
| NumTreasures | お宝の数 |
| ClearReward | 逃げ切り報酬（コイン）。お宝 1 個はこの 30% |
| OniDamage | 鬼の攻撃力の基本値（壁の耐久値と比べる）。種類ごとの倍率（OniTypes.csv の DamageScale）がかかる |
| OniHearingRadius | 連打の音が聞こえる距離の基本値（cm）。種類ごとの倍率（HearingScale）がかかる |
| OniSpeedBonus | 鬼の速さに足す値（cm/秒）。基本の速さは うろうろ 420 / 調べる 700 / 追いかける 840（プレイヤーは 420）に種類ごとの倍率（SpeedScale）をかけたもの |
| Map | 館のマップ（Maps.csv の行の名前）。空なら前のステージと同じ。今はステージごとに全部違う。マップが変わると、置いていた壁と罠は在庫に戻る（設計図はマップごとに残る） |

## OniTypes.csv（鬼の種類）

行の名前（Balanced / Scout / Breaker / Careful / Treasure / Detector）はプログラムが探し方を切り替えるのに使うので変えないでください。

| 列 | 意味 |
|---|---|
| DisplayName | 画面に出す名前 |
| SpeedScale | 速さの倍率（うろうろ・調べる・追いかける すべてにかかる） |
| DamageScale | 壁を壊す力の倍率 |
| SightRadius / SightHalfAngle | 見える距離（cm）/ 見える角度の半分（度。40 なら正面 80 度） |
| HearingScale | プレイヤーの音（連打・煙幕ダッシュ）が聞こえる距離の倍率。0 なら気にしない（スピード鬼は 1.5＝音に敏感） |
| DecoyHearingScale | おとりの音が聞こえる距離の倍率。0 ならおとりにだまされない（スピード鬼）。大きいほど遠くから寄っていく（パワー鬼 1.6） |
| StepHearingScale | プレイヤーの足音・ジャンプの音が聞こえる距離の倍率（足音の大きさは連打の 0.5、ジャンプは 0.7）。0 なら気にしない。宝物鬼は 2.2＝足音に敏感 |
| SummonRadius | プレイヤーを見つけたとき、この距離（cm）以内の鬼を呼び寄せる。0 なら呼ばない（探知鬼 2400） |
| StunScale | トリモチで動けない時間の倍率（スピード鬼 1.5＝弱い、パワー鬼 0.4＝強い） |
| bDisarmTraps | True なら、トリモチから抜け出した直後（3 秒間）に踏んだトリモチを壊す（パワー鬼） |
| bSingleTargetAttack | True なら目の前の壁 1 個だけを壊す（範囲攻撃しない） |
| AttackRadius / AttackWindup | 範囲攻撃の半径（cm）/ 攻撃の溜め時間（秒） |
| PocketInspectChance | うろうろ中に「壁で囲まれた空洞」を見つけたとき、調べに行く（壊して入る）確率（0〜1） |
| Color | 体の色（円柱のとき）・頭の上の玉の色・画面の一覧の色 |
| BodyScale | 体の大きさの倍率 |
| MeshTint | モデルの縁の光の色（Cute Creature のマテリアルの ReflectionColor）。A=0 なら変えない |
| MeshMaterial | キャラクターのモデル（Cute Creature）を使うときの色違いのマテリアル（`/Game/...` のパス）。空ならモデルのまま |

## Upgrades.csv（強化）

行の名前（Mash = 連打、Time = 時間）はプログラムが使うので変えないでください。
（以前あった Wall = 壁の補強 はやめました。壁を硬くするのは転生です → Prestige.csv）

| 列 | 意味 |
|---|---|
| DisplayName | 購入パートでの表示名 |
| BaseCost / CostGrowth | 価格 = BaseCost × CostGrowth ^ レベル |
| BaseValue / ValueGrowth | 効果 = BaseValue × ValueGrowth ^ レベル（Mash: 連打 1 回のコイン / Time: 毎秒のコイン） |

## Prestige.csv（転生）

行の名前は Prestige のままにしてください（1 行だけ）。

| 列 | 意味 |
|---|---|
| MinStage | このステージまで来たら転生できる |
| PointsPerStage | 転生ポイント = (今のステージ − MinStage + 1) × PointsPerStage |

## PrestigeUpgrades.csv（転生のお店）

転生ポイントで買う永続強化（転生しても残る）。行の名前（WallHP / Treasure / SmokeDuration / SmokeCount / Jump / QuietHP）は
プログラムが使うので変えないでください。並び順はプログラムで決まっています（この順）。

| 列 | 意味 |
|---|---|
| DisplayName | 表示名 |
| MaxLevel | 最大レベル（0 なら上限なし） |
| BaseCost / CostGrowth | 価格（転生ポイント）= 切り上げ(BaseCost × CostGrowth ^ 今のレベル) |
| BaseValue / ValueGrowth | 効果 = BaseValue × ValueGrowth ^ レベル |

| 行 | 効果の意味 |
|---|---|
| WallHP | すべての壁の耐久の倍率（鬼の攻撃力はステージごとに ×1.6） |
| Treasure | お宝の価値の倍率 |
| SmokeDuration | 煙幕が残る時間（秒）。**Lv0 は煙幕ダッシュを使えない**（Lv1 で解放）。**足し算**：時間 = BaseValue + ValueGrowth × (レベル − 1)（1, 3, 5, 7 秒。1〜7 秒に収める） |
| SmokeCount | 1 ラウンドに煙幕ダッシュを使える回数。**ここだけ足し算**：回数 = BaseValue + ValueGrowth × レベル（1, 2, 3, 4 回） |
| Jump | **Lv0 はジャンプできない**（Lv1 で解放）。値は使わない |
| QuietHP | 消音壁が音を消せる回数（Walls.csv の SoundHP）の倍率 |

## Walls.csv（壁）

行の並び順が購入パート・設置パートでの並び順（数字キー）になります。行を足すと壁の種類が増えます。

| 列 | 意味 |
|---|---|
| DisplayName | 表示名 |
| MaxHP | 耐久値（転生のお店の「壁の硬さ」をかける前の値） |
| Cost / CostGrowth | 価格 = Cost × CostGrowth ^ 持っている数（在庫＋置いてある数。壊れた分は数えない）。普通の壁は 1.04〜1.08、消音壁は罠と同じくらい（1.5） |
| Color | 色（"(R=0〜1,G=0〜1,B=0〜1,A=1.0)" の形。カンマを含むので " で囲む） |
| NoiseDamping | 音を小さくする割合（0〜1。消音壁は 0.8）。プレイヤーが壁に囲まれた空洞にいるときだけ、囲んでいる壁の平均だけ連打・ダッシュの音が小さくなる |
| SoundHP | 音を消せる回数（消音壁は 40）。音を小さくするたびに連打 1・ダッシュ 3 減り、0 で壊れる。0 なら減らない（普通の壁）。転生のお店の「消音壁の丈夫さ」で増える |
| Surface | 見た目（Surfaces.csv の行の名前。木の板・鉄の板など）。空・テクスチャがそのパソコンに無ければ Color の箱。傷つくと暗く赤くなる |

行の順番を入れ替えると、セーブデータの壁の種類がずれるので、新しい壁は最後に足してください。

## Traps.csv（罠）

行の並び順が購入パート・設置パートでの並び順になります（壁の後ろに続く）。行を足すと罠の種類が増えます
（例: Kind を Sticky にして StunSeconds を長くした「強力トリモチ」）。
購入パートの数字キーは 1〜9 なので、強化 2 つ＋壁＋罠の合計が 9 個までに収まるようにしてください（今は 2＋4＋2＝8）。

| 列 | 意味 |
|---|---|
| DisplayName | 表示名 |
| Kind | 働き。`Sticky`（トリモチ: 踏んだ鬼を動けなくする。1 回で消える）か `Decoy`（おとり: 音で鬼を呼ぶ。鬼が触れると壊れる） |
| Cost / CostGrowth | 価格 = Cost × CostGrowth ^ 持っている数（在庫＋置いてある数。発動して消えた分は数えない）。トリモチ 1.5・おとり 1.6 |
| StunSeconds | トリモチ: 鬼が動けない時間（秒）。抜け出した後 3 秒は罠にかからない |
| NoiseInterval | おとり: 音を出す間隔（秒） |
| NoiseLoudness | おとり: 音の大きさ（1 なら連打の音と同じ距離まで届く） |
| TriggerRadius | 鬼の体の中心がこの距離（cm）まで来たら発動する |
| Color | 色 |

## Maps.csv（館のマップ）

行の名前を Stages.csv の Map 列に書きます。行の順番は、表より後のステージでマップが回る順番です（後ろの 6 つを回る）。
**館の大きさは間取りのファイルの文字数 × 行数で決まります**（マップごとに違ってよい。今は 16×16 〜 31×29）。

| 列 | 意味 |
|---|---|
| DisplayName | 画面に出す名前（左上の「館：〇〇」・マップが変わったときのお知らせ） |
| LayoutFile | 間取りのファイル（このフォルダの `Maps/` の中） |
| FloorColor | 床の色（市松模様の明るい方。暗い方は自動で 72%） |
| WallColor | 外周の壁と部屋の壁（`#`）の壁紙の色 |
| LightColor | 壁ぎわのランプ・暖炉の明かりの色 |
| FloorSurface | 床の見た目（Surfaces.csv の行の名前。1 マスに模様 1 回）。空・テクスチャが無ければ FloorColor の市松模様 |
| WallSurface | 外周の壁と部屋の壁に貼る板の見た目（1 マス × 125cm の板に模様 1 回）。空・テクスチャが無ければ WallColor |
| FloorTint / WallTint | 床・壁の模様に掛ける色（白ならそのまま。1 より大きくすると明るく。マップごとの雰囲気を出す）。市松模様の暗い方のマスは自動で 80% |

### Maps/*.txt（間取り）

- 1 行 = 横 1 列のマス。上の行から Y = 0, 1, 2 …、左の文字から X = 0, 1, 2 …。**全部の行を同じ長さにする**（一番長い行の文字数 × 行数が館の大きさ）
- **右端の真ん中（行数を 2 で割った行とその上下 2 行の、右 3 文字）は鬼の出入り口の前なので、`.` のままにする**（家具を書いても置かれない）
- 今の 20 個の間取りはプログラムで作った下書き（家具の多さ：ステージ 1 は 27% → ステージ 20 は 7%）。手で直してよい
- `.` は床、`#` は部屋の壁（壊せない。壁紙の色）、それ以外の文字は Furniture.csv の行の名前（家具）
- `;` で始まる行はメモ（読み飛ばす）
- 同じ文字が長方形に並んでいる所は 1 つの家具になる（`BBBB` は横 4 マスの本棚の列。メッシュを何個か並べる）
- 鬼が門から歩いて行けない場所（家具で閉じた部屋）を作らないように。自動テスト `Maps` で確かめられる

## Furniture.csv（家具）

行の名前は間取りで使う **1 文字**です（間取りと同じ文字で書く。`B` と `b` のように大文字・小文字だけ違う家具は作れない。`#` と `.` は使わない）。
家具は壊せず、鬼もプレイヤーも通れません（じゅうたんのように bWalkable が True のものは飾りだけ）。

| 列 | 意味 |
|---|---|
| DisplayName | 名前（メモ用） |
| Mesh / AltMesh | 見た目のメッシュ（`/Game/...` のパス。Fab の Stylized Library）。AltMesh があれば場所によってどちらか。無い・見つからなければ Color の箱 |
| Height | 高さ（cm）。メッシュはこの高さと家具のマスに収まる大きさにする。当たり判定の高さでもある（低い家具は鬼の視線を通す） |
| Color | 箱で表すときの色 |
| bWalkable | True なら上を歩ける飾り（じゅうたん） |
| LightIntensity | 明かりの強さ（カンデラ。暖炉など。0 なら無し） |

## Surfaces.csv（見た目のテクスチャ）

壁・床・宝石・プレイヤーの見た目。共通のマテリアル（/Game/Kakurenbo/Materials/M_KakuSurface。Git に入っている）に、
ゲームが実行時にここのテクスチャを差し込みます。テクスチャは Fab のアセット（Git に入っていない）なので、
そのパソコンに無ければ色だけの見た目になります（エラーにはならない）。行の名前を Maps.csv・Walls.csv の Surface 列、
DefaultGame.ini の PlayerSurface・TreasureSurface に書きます。

| 列 | 意味 |
|---|---|
| BaseColor | 模様（色）のテクスチャ（/Game/... のパス）。これが無いと使わない |
| Normal | 凹凸（ノーマルマップ）。空なら平ら |
| Roughness | ざらざら具合（R チャンネル）。空なら RoughnessScale の値 |
| Tint | 模様に掛ける色（1 より大きくすると明るく） |
| Metallic | 金属っぽさ（0〜1。鉄の壁 0.85） |
| RoughnessScale | Roughness に掛ける値 |
| UVScale | 1 面に模様を何回くり返すか |
| Emissive | 自分で光る強さ（宝石 0.6。暗い館でも見える） |

今の行：Wood01〜16（Fab「Substance Materials Vol 01 - Wood」）、Metal（Fab「Stylized Metallic Floor」）、Gem（Fab の宝石）、Player（プレイヤーの服の色の表）、
置く壁用の WoodWall・StoneWall・IronWall・QuietWall。**石の模様は手元に無いので、StoneWall はざらざらした木（Wood15）を灰色にして代わりにしている**