# バランスデータ

ゲームの数値はこのフォルダの CSV で調整できます（Excel や VSCode で編集 → 保存 → 次の Play から反映）。
文字コードは UTF-8（BOM 付き）のまま保存してください。1 列目は行の名前です。

## Stages.csv（ステージごとの設定）

行の順番がステージ番号です。表より後のステージ（11 以降）は、最後の行から
「制限時間 +5 秒 / 報酬 ×4 / 鬼の攻撃力 ×1.6 / 聞こえる距離 +100 / 速度 +15」ずつ伸びます
（伸ばし方は GameMode の Stage Growth で変えられます）。

| 列 | 意味 |
|---|---|
| HideDuration | 制限時間（秒） |
| NumOnis | 鬼の数 |
| NumTreasures | お宝の数 |
| ClearReward | 逃げ切り報酬（コイン）。お宝 1 個はこの 30% |
| OniDamage | 鬼の攻撃力（壁の耐久値と比べる） |
| OniHearingRadius | 連打の音が聞こえる距離（cm） |
| OniSpeedBonus | 鬼の速さに足す値（cm/秒）。基本の速さは うろうろ 420 / 調べる 700 / 追いかける 840（プレイヤーは 420） |

## Upgrades.csv（強化）

行の名前（Mash = 連打、Time = 時間）はプログラムが使うので変えないでください。

| 列 | 意味 |
|---|---|
| DisplayName | 購入パートでの表示名 |
| BaseCost / CostGrowth | 価格 = BaseCost × CostGrowth ^ レベル |
| BaseValue / ValueGrowth | 効果 = BaseValue × ValueGrowth ^ レベル（連打 1 回 / 毎秒のコイン） |

## Walls.csv（壁）

行の並び順が購入パート・設置パートでの並び順（数字キー）になります。行を足すと壁の種類が増えます。

| 列 | 意味 |
|---|---|
| DisplayName | 表示名 |
| MaxHP | 耐久値 |
| Cost | 価格 |
| Color | 色（"(R=0〜1,G=0〜1,B=0〜1,A=1.0)" の形。カンマを含むので " で囲む） |
