# バランスデータ

ゲームの数値はこのフォルダの CSV で調整できます（Excel や VSCode で編集 → 保存 → 次の Play から反映）。
文字コードは UTF-8（BOM 付き）のまま保存してください。1 列目は行の名前です。

## Stages.csv（ステージごとの設定）

行の順番がステージ番号です。表より後のステージ（11 以降）は、最後の行から
「制限時間 +5 秒 / 報酬 ×4 / 鬼の攻撃力 ×1.6 / 聞こえる距離 +100 / 速度 +15」ずつ伸びます
（伸ばし方は GameMode の Stage Growth で変えられます。鬼の顔ぶれは最後の行のまま）。

| 列 | 意味 |
|---|---|
| HideDuration | 制限時間（秒） |
| OniTypes | 出てくる鬼の種類と数。`"(Balanced,Scout,Careful,Careful)"` のように並べる（同じ種類を 2 回書けば 2 体。最大 6 体＝出入り口のマスの数）。名前は OniTypes.csv の行の名前。並べ方の考え方は docs/GameDesign.md の「レベルデザイン」 |
| NumTreasures | お宝の数 |
| ClearReward | 逃げ切り報酬（コイン）。お宝 1 個はこの 30% |
| OniDamage | 鬼の攻撃力の基本値（壁の耐久値と比べる）。種類ごとの倍率（OniTypes.csv の DamageScale）がかかる |
| OniHearingRadius | 連打の音が聞こえる距離の基本値（cm）。種類ごとの倍率（HearingScale）がかかる |
| OniSpeedBonus | 鬼の速さに足す値（cm/秒）。基本の速さは うろうろ 420 / 調べる 700 / 追いかける 840（プレイヤーは 420）に種類ごとの倍率（SpeedScale）をかけたもの |

## OniTypes.csv（鬼の種類）

行の名前（Balanced / Scout / Breaker / Careful）はプログラムが探し方を切り替えるのに使うので変えないでください。

| 列 | 意味 |
|---|---|
| DisplayName | 画面に出す名前 |
| SpeedScale | 速さの倍率（うろうろ・調べる・追いかける すべてにかかる） |
| DamageScale | 壁を壊す力の倍率 |
| SightRadius / SightHalfAngle | 見える距離（cm）/ 見える角度の半分（度。40 なら正面 80 度） |
| HearingScale | プレイヤーの音（連打・ダッシュ）が聞こえる距離の倍率。0 なら気にしない（スピード鬼は 1.5＝音に敏感） |
| DecoyHearingScale | おとりの音が聞こえる距離の倍率。0 ならおとりにだまされない（スピード鬼）。大きいほど遠くから寄っていく（パワー鬼 1.6） |
| StunScale | トリモチで動けない時間の倍率（スピード鬼 1.5＝弱い、パワー鬼 0.4＝強い） |
| bDisarmTraps | True なら、トリモチから抜け出した直後（3 秒間）に踏んだトリモチを壊す（パワー鬼） |
| bSingleTargetAttack | True なら目の前の壁 1 個だけを壊す（範囲攻撃しない） |
| AttackRadius / AttackWindup | 範囲攻撃の半径（cm）/ 攻撃の溜め時間（秒） |
| PocketInspectChance | うろうろ中に「壁で囲まれた空洞」を見つけたとき、調べに行く（壊して入る）確率（0〜1） |
| Color | 体の色（円柱のとき）・頭の上の玉の色・画面の一覧の色 |
| BodyScale | 体の大きさの倍率 |
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

転生ポイントで買う永続強化（転生しても残る）。行の名前（WallHP / Treasure / DashSpeed / DashCooldown / Jump / QuietHP）は
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
| DashSpeed | ダッシュの速さの倍率。**Lv0 はダッシュできない**（Lv1 で解放） |
| DashCooldown | ダッシュのクールタイム（秒）。ValueGrowth を 1 より小さくすると短くなる |
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
