# バランスデータ

ゲームの数値はこのフォルダの CSV で調整できます（Excel や VSCode で編集 → 保存 → 次の Play から反映）。
文字コードは UTF-8（BOM 付き）のまま保存してください。1 列目は行の名前です。

## Stages.csv（ステージごとの設定）

行の順番がステージ番号です（今は 25 ステージ）。表より後のステージ（26 以降）は、最後の行から
「制限時間 +5 秒 / 報酬 ×4 / 鬼の攻撃力 ×1.6 / 聞こえる距離 +100 / 速度 +15」ずつ伸びます
（伸ばし方は GameMode の Stage Growth で変えられます。鬼の顔ぶれは最後の行のまま。マップは 1 ステージごとに Maps.csv の**後ろの 3 つ**（一番大きい館）を順に回る）。

**転生しないときついステージ（壁）**：8・13・18・23 は鬼の数・種類が一気に強くなり、攻撃力が ×2 跳ね上がる（それ以降のステージもその分強いまま）。
転生のお店の煙幕ダッシュ・壁の硬さが無いと越えにくい。考え方は docs/GameDesign.md の「レベルデザイン」

| 列 | 意味 |
|---|---|
| HideDuration | 制限時間（秒） |
| OniTypes | 出てくる鬼の種類と数。`"(Balanced,Scout,Careful,Careful)"` のように並べる（同じ種類を 2 回書けば 2 体。最大 6 体＝出入り口のマスの数）。名前は OniTypes.csv の行の名前。並べ方の考え方は docs/GameDesign.md の「レベルデザイン」 |
| NumTreasures | お宝の数 |
| ClearReward | 逃げ切り報酬（コイン）。お宝 1 個はこの 30% |
| OniDamage | 鬼の攻撃力の基本値（壁の耐久値と比べる）。種類ごとの倍率（OniTypes.csv の DamageScale）がかかる |
| OniHearingRadius | 連打の音が聞こえる距離の基本値（cm。**一辺 40 マスの館での距離**。館の一辺に比例して小さくなる：16 マスなら ×0.4）。種類ごとの倍率（HearingScale）がかかる |
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
| HearingScale | プレイヤーの音（連打・煙幕ダッシュ）が聞こえる距離の倍率。0 なら気にしない（スピード鬼は 1.5＝音に敏感、宝物鬼・パワー鬼・探知鬼は 0）。聞こえる距離の基準は Stages.csv の OniHearingRadius × 館の大きさの倍率（下の Maps.csv） |
| DecoyHearingScale | おとりの音が聞こえる距離の倍率。0 ならおとりにだまされない（宝物鬼・探知鬼）。大きいほど遠くから寄っていく（スピード鬼 1.5・標準 1.0・パワー鬼 0.6。2026-10-18 にスピード鬼とパワー鬼を入れ替え） |
| StepHearingScale | プレイヤーの足音・よじ登る音が聞こえる距離の倍率（足音の大きさは連打の 0.5、よじ登る音は 0.6）。0 なら気にしない。宝物鬼は 1.2＝ふつうの鬼（0.6）の 2 倍 |
| SummonRadius | プレイヤーを見つけたとき、この距離（cm）以内の鬼を呼び寄せる。0 なら呼ばない（探知鬼 2400） |
| StunScale | トリモチで動けない時間の倍率（スピード鬼 1.5＝弱い、パワー鬼 0.4＝強い） |
| bDisarmTraps | True なら、トリモチから抜け出した直後（3 秒間）に踏んだトリモチを壊す（パワー鬼） |
| bSingleTargetAttack | True なら目の前の壁 1 個だけを壊す（範囲攻撃しない） |
| AttackRadius / AttackWindup | 範囲攻撃の半径（cm）/ 攻撃の溜め時間（秒） |
| AttackBoxWidth / AttackBoxDepth / AttackBoxLevels | 前方の箱の攻撃（M15。パワー鬼 2 / 2 / 2）：目の前の壁から横・奥（マス）・下から何段に当たるか。0 なら範囲攻撃（AttackRadius） |
| PocketInspectChance | うろうろ中に「壁で囲まれた空洞」を見つけたとき、調べに行く（壊して入る）確率（0〜1） |
| CornerTurnAngle / CornerSlowScale / CornerSlowDuration | 曲がり角が苦手（スピード鬼 60 / 0.35 / 0.8）：進む向きが一度に CornerTurnAngle 度以上変わると、速さが CornerSlowScale 倍まで落ち、CornerSlowDuration 秒かけて元に戻る（ゆっくり曲がっても、180 度/秒より速く向きを変え続けると角とみなす）。直線は速いが、角を曲がって逃げられると引き離される。0 / 1 / 0 なら無し |
| Intuition | 勘（0〜1。隠しステータスで画面には出ない）。うろうろの次の行き先を決めるとき、この確率でプレイヤーの本当の場所のそばにする（見えていなくても近づいて来やすい）。一度働いたら 15 秒は働かない（鬼の IntuitionCooldown）。宝物鬼は 0 |
| Color | 体の色（円柱のとき）・頭の上の玉の色・画面の一覧の色 |
| BodyScale | 体の大きさの倍率 |
| MeshTint | モデルの縁の光の色（Cute Creature のマテリアルの ReflectionColor）。A=0 なら変えない |
| MeshMaterial | キャラクターのモデル（Cute Creature）を使うときの色違いのマテリアル（`/Game/...` のパス）。空ならモデルのまま |

## Upgrades.csv（強化）

行の名前（Mash = 連打、Time = 時間、Wall = 壁の補強）はプログラムが使うので変えないでください。購入パートの並びはこの 3 つ → 壁 → 罠。

| 列 | 意味 |
|---|---|
| DisplayName | 購入パートでの表示名 |
| BaseCost / CostGrowth | 価格 = BaseCost × CostGrowth ^ レベル |
| BaseValue / ValueGrowth | 効果 = BaseValue × ValueGrowth ^ レベル（Mash: 連打 1 回のコイン / Time: 毎秒のコイン）。<br>**Wall だけ足し算**：壁の耐久の倍率 = BaseValue + ValueGrowth × レベル（1 → 1.5 → 2 …。転生のお店の「壁の硬さ」はさらに掛け算） |

## Prestige.csv（転生）

行の名前は Prestige のままにしてください（1 行だけ）。

| 列 | 意味 |
|---|---|
| MinStage | このステージまで来たら転生できる |
| PointsBase / PointsGrowth | 転生ポイント = 切り捨て(PointsBase × PointsGrowth ^ (今のステージ − MinStage))（指数。今は 4 と 1.28：ステージ 8 で 8、12 で 22） |
| StartCoinsPerPrestige | 転生した後のはじめのコイン = 転生した回数 × これ（強化ではなく、転生するたびに自動で増える。今は 500） |
| WallHPPerPrestige | 転生するたびに、すべての壁の耐久が自動でこの倍率ずつ硬くなる（× これ ^ 転生した回数。今は 1.4） |

## PrestigeUpgrades.csv（転生のお店）

転生ポイントで買う永続強化（転生しても残る）。Treasure（お宝の宝石）は M15 から足し算：お宝 1 個の宝石 = BaseValue + ValueGrowth × レベル。行の名前（WallHP / Treasure / SmokeDuration / SmokeCount / Climb / QuietHP / Income）は
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
| Climb | **Lv0 はよじ登れない**（Lv1 で解放。クライミング壁も買えるようになる）。Lv2 から登る速さ = 値(Lv) ÷ 値(1)（ValueGrowth 1.3 ずつ）。GameMode の ClimbTwoTierLevel（3）から 2 段の高さにも登れる |
| QuietHP | 消音壁が音を消せる回数（Walls.csv の SoundHP）の倍率 |
| Income | お金アップ：稼ぐコイン（連打・時間・逃げ切りの報酬）の倍率（Upgrades.csv の強化の上に掛かる）。2026-10-19 に前の MashIncome / TimeIncome をまとめた |

## Walls.csv（壁）

行の並び順が購入パート・設置パートでの並び順（数字キー）になります。行を足すと壁の種類が増えます。

| 列 | 意味 |
|---|---|
| DisplayName | 表示名 |
| MaxHP | 耐久値（転生のお店の「壁の硬さ」をかける前の値） |
| Cost / CostGrowth | 価格 = Cost × CostGrowth ^ 持っている数（在庫＋置いてある数。壊れた分は数えない）。通常壁 1.12、クライミング壁 1.15、消音壁 1.5 |
| Color | 色（"(R=0〜1,G=0〜1,B=0〜1,A=1.0)" の形。カンマを含むので " で囲む） |
| NoiseDamping | 音を小さくする割合（0〜1。消音壁は 0.8）。プレイヤーが壁に囲まれた空洞にいるときだけ、囲んでいる壁の平均だけ連打・ダッシュの音が小さくなる |
| SoundHP | 音を消せる回数（消音壁は 60。M14 で 40 から増やした）。音を小さくするたびに連打 1・ダッシュ 3 減り、0 で壊れる。0 なら減らない（普通の壁）。転生のお店の「消音壁の丈夫さ」で増える |
| Surface | 見た目（Surfaces.csv の行の名前。木の板・鉄の板など）。空・テクスチャがそのパソコンに無ければ Color の箱。傷つくと暗く赤くなる |
| ClimbSeconds | よじ登るのにかかる時間（秒。通常壁・消音壁 1、クライミング壁 0.3。家具は 1.2） |
| bLadder | True ならはしご付きの壁（クライミング壁）。横木が見える。**鬼はこの壁の上に飛び乗れない**（下から壊す）。よじ登るを解放するまで買えない |

行の順番を入れ替えると、セーブデータの壁の種類がずれるので、新しい壁は最後に足してください。
M14 で 3 種類（Normal 通常壁・Quiet 消音壁・Climb クライミング壁）にしました。版 5 までのセーブは、消音壁はそのまま、木・石・鉄の壁は通常壁になります。

## Traps.csv（罠）

行の並び順が購入パート・設置パートでの並び順になります（壁の後ろに続く）。行を足すと罠の種類が増えます
（例: Kind を Sticky にして StunSeconds を長くした「強力トリモチ」）。
購入パートの数字キーは 1〜9 なので、強化 3 つ＋壁＋コインの罠の合計が 9 個までに収まるようにしてください（今は 3＋3＋2＝8）。

**M15：宝石のお店の道具もこの表**（bGemItem = True の行。宝石で買い、「宝石のお店」に並ぶ。コインの罠より後ろに書く）。
Kind は `FakeHider`（身代わり人形）・`OneWay`（一方通行ののれん）・`Darkness`（真っ暗ブロック）・`Cat`（迷い猫）・`Daruma`（だるまさんがころんだ。置かずにかくれんぼ中に使う）・`GoldRush`（ゴールドラッシュ。置いておくとそのかくれんぼの間は連打のコインが 2 倍。1 ラウンドで壊れる）。
働きは docs/GameDesign.md の「宝石と宝石のお店」

| 列 | 意味 |
|---|---|
| DisplayName | 表示名 |
| Kind | 働き。`Sticky`（トリモチ: 踏んだ鬼を動けなくする。1 回で消える）か `Decoy`（おとり: 音で鬼を呼ぶ。鬼が触れると壊れる） |
| Cost / CostGrowth | 価格 = Cost × CostGrowth ^ 持っている数（在庫＋置いてある数。発動して消えた分は数えない）。M14 で高くした：トリモチ 150 × 2.2・おとり 250 × 2.4 |
| StunSeconds | トリモチ: 鬼が動けない時間（秒）。抜け出した後 3 秒は罠にかからない |
| NoiseInterval | おとり: 音を出す間隔（秒） |
| NoiseLoudness | おとり: 音の大きさ（1 なら連打の音と同じ距離まで届く） |
| TriggerRadius | 鬼の体の中心がこの距離（cm）まで来たら発動する |
| Color | 色 |
| bGemItem | True なら宝石で買う道具（宝石のお店）。Cost は宝石の数 |
| Radius | 真っ暗ブロック：暗い間に鬼が見える距離（cm。300 = 3m）／迷い猫：うろうろする範囲（cm） |
| Speed | 身代わり人形：逃げる速さ（cm/秒。プレイヤーの歩きは 420）／迷い猫：歩く速さ |
| Uses | 一方通行ののれん：鬼が通れる回数（尽きると破れる） |
| Duration | 迷い猫：鬼が猫をなでて動かない秒数／だるまさんがころんだ：鬼が止まる秒数 |
| NoiseInterval / NoiseLoudness | 迷い猫：鳴く間隔（秒）と大きさ（どの鬼にも、種類の倍率なしで聞こえる） |

## Maps.csv（館のマップ）

行の名前を Stages.csv の Map 列に書きます。行の順番は、表より後のステージでマップが回る順番です（後ろの 3 つを回る）。
**館の大きさは間取りのファイルの文字数 × 行数で決まります**（マップごとに違ってよい。今はステージごとに 1 マスずつ大きくなる：16×16 〜 40×40）。

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
| WainscotSurface / WainscotTint | 腰壁（壁の下の 95cm に貼る木の板）の見た目と色。空なら腰壁なし（壁紙が床まで） |

- **床は部屋ごと**：間取りの `[floor]` の文字（下の FloorKinds.csv）。`w`（館の床）は FloorSurface / FloorTint を使う
- **音の届く距離は館の大きさで変わる**（M14。2026-10-19 から一辺に比例）：倍率 = 館の一辺 ÷ 40（16 マスで ×0.4、40 マスで ×1.0）。連打・足音・ダッシュ・おとりが全部これで小さくなる。
  数値は GameMode の MapNoiseReferenceSide（間取りの無いテスト用の舞台は ×1）

### Maps/*.txt（間取り）

- 1 行 = 横 1 列のマス。上の行から Y = 0, 1, 2 …、左の文字から X = 0, 1, 2 …。**全部の行を同じ長さにする**（一番長い行の文字数 × 行数が館の大きさ）
- **右端の真ん中（行数を 2 で割った行とその上下 2 行の、右 3 文字）は鬼の出入り口の前なので、`.` のままにする**（家具を書いても置かれない）
- **今の 25 個の間取りは `Tools/GenerateMansion.py` が作ったもの**（M14。玄関ホール・廊下・役割のある部屋。家具は床のマスの 20〜24%、転生の壁のステージは 15〜17%）。
  `python Tools/GenerateMansion.py` で全部作り直す（手で直した所は上書きされる）。館の数・大きさ・部屋の種類・家具の置き方はスクリプトの中
- 間取りの後ろに `[floor]` の行があれば、そこから下は**床の種類**の図（間取りと同じ大きさ。FloorKinds.csv の文字）。無ければ全部 `w`
- `.` は床、`#` は部屋の壁（壊せない。壁紙の色）、それ以外の文字は Furniture.csv の行の名前（家具）
- `;` で始まる行はメモ（読み飛ばす）
- 同じ文字が長方形に並んでいる所は 1 つの家具になる（`BBBB` は横 4 マスの本棚の列。メッシュを何個か並べる）
- 部屋の壁（`#`）の並びの中の 1〜2 マスのすき間は**出入り口**になる（木の枠・石のアーチ・上の壁が付く）
- 鬼が門から歩いて行けない場所（家具で閉じた部屋）を作らないように。自動テスト `Maps` で確かめられる

## Furniture.csv（家具）

行の名前は間取りで使う **1 文字**です（間取りと同じ文字で書く。`B` と `b` のように大文字・小文字だけ違う家具は作れない。`#` と `.` は使わない）。
家具は壊せず、鬼もプレイヤーも通れません（じゅうたんのように bWalkable が True のものは飾りだけ）。

| 列 | 意味 |
|---|---|
| DisplayName | 名前（メモ用） |
| Mesh / AltMesh | 見た目のメッシュ（`/Game/...` のパス）。AltMesh があれば場所によってどちらか。無い・見つからなければ Color の箱 |
| Height | 高さ（cm）。メッシュはこの高さと家具のマスに収まる大きさにする。当たり判定の高さでもある（低い家具は鬼の視線を通す） |
| MeshYaw | メッシュの向きを足す角度（度）。正面を部屋の空いている側へ向けたとき、逆を向くメッシュは 180 |
| Color | 箱で表すときの色 |
| bWalkable | True なら上を歩ける飾り（じゅうたん） |
| LightIntensity | 明かりの強さ（カンデラ。暖炉など。0 なら無し） |
| MeshCutout / AltMeshCutout | メッシュを上から見て空いている長方形（当たり判定から抜く）。`最小X 最小Y 最大X 最大Y` をメッシュの外形に対する割合 0〜1 で、空白で区切って書く。例: L 字の角の本棚 `0.4 0.44 1 1`（手前の角が空いている）。空なら外形の箱 |

- メッシュは家具のマスに収まるよう、縦横比を保って縮め、長い辺は 0.6〜1.5 倍・短い辺は 0.6〜1.3 倍まで伸び縮みさせる。長い家具は同じメッシュを並べる（1 個はいつも整数のマス）
- **当たり判定は見えているメッシュ 1 個ずつの大きさ**（M14）。見えている床（浅い家具の前・すき間）にはプレイヤーが入れる。壁・罠を置けない・鬼が通らないのは家具のマス全部
- 後ろが壁・家具なら、メッシュは後ろの壁にぴったり付ける
- MeshCutout のある家具（L 字の角の本棚）を長い辺に並べるときは 2 個ずつ向かい合わせ（片方は左右反転）にして、空いている角どうしを合わせる。
  全部置いた後、体が入らない空いている角（壁・家具の正面を向いて細い溝になった所）は箱で埋める（入れるすき間か、すき間無しのどちらか）
- 向きと大きさは自動テスト `Lineup`（全部の家具を壁ぎわに並べて写す）で確かめる。新しい文字を足したら Lineup の一覧にも足す

## FloorKinds.csv（床の種類）

間取りの `[floor]` の文字（行の名前。小文字 1 文字）ごとの床の見た目。

| 列 | 意味 |
|---|---|
| DisplayName | 名前（メモ用） |
| Surface | 床の見た目（Surfaces.csv の行の名前）。`w` は空にして Maps.csv の FloorSurface を使う |
| Tint | 模様に掛ける色 |
| CheckerDarken | 市松模様の暗い方のマスの明るさ（1 なら市松模様なし） |
| Color | テクスチャが無いときの色 |

今の行：`w` 板張り（館の床）、`p` 寄木（廊下）、`m` 石（玄関ホール）、`t` タイル（台所・温室）、`b` タイル（浴室）、`s` 石（物置・地下）

## Decor.csv（壁の飾り）

壁に付ける飾りと出入り口の枠。種類（Kind）ごとに、場所によってどれかが選ばれる。メッシュが無ければ付けない（松明が無ければ今まで通りの光る玉のランプ）。

| 列 | 意味 |
|---|---|
| Kind | `Window`（窓。外周の壁だけ。窓の向こうは夜空の暗い青）・`Painting`（絵）・`Sconce`（壁の明かり）・`DoorFrame`（1 マスの出入り口の枠）・`DoorFrameWide`（2 マスの出入り口の枠） |
| Mesh | メッシュ（`/Game/...`）。一番薄い向きを奥行き（壁から部屋の側）とみなす |
| Width / Height | 幅・高さ（cm）。縦横比を保ってこの中に収める（枠は出入り口の幅） |
| Bottom | 床から下の端までの高さ（cm） |
| Offset | 壁から離す距離（cm）。負なら壁に埋める（窓は −24） |
| MeshYaw | 向きを足す角度（度） |
| LightIntensity | 明かりの強さ（松明。館に 64 個まで） |

## Sounds/<音>.wav（効果音）

名前は `EKakurenboSfx`（`OniStep.wav` など）。**モノラル・44100Hz・16bit** の WAV。その音を初めて鳴らすときに読み、プログラムの音と同じ左右の振り分け・こもり・距離の処理を通す。
無い音はプログラムで作った音。AI の候補から作るときは `Tools/SoundGen/ExportForGame.py`（形と大きさをそろえる）。採用の記録は docs/Sounds.md。

## Sounds/Music_*.wav（BGM）

`Music_Title` / `Music_Calm` / `Music_Hide` / `Music_Chase`（名前は `EKakurenboMusic`）。16bit のモノラルかステレオの WAV で、
最後から最初へそのままつながる（くり返せる）こと。起動後、その曲を初めて流すときに読む（無ければプログラムで作った曲）。
BGM を作る AI の候補から作るときは `Tools/MusicGen/ExportForGame.py`（くり返せる形に切って、大きさをそろえる）。採用の記録は docs/Sounds.md。

## Surfaces.csv（見た目のテクスチャ）

壁・床・宝石・プレイヤーの見た目。共通のマテリアル（/Game/Kakurenbo/Materials/M_KakuSurface。Git に入っている）に、
ゲームが実行時にここのテクスチャを差し込みます。テクスチャは Fab のアセット（Git に入っている。docs/Assets.md）で、
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
| Desaturate | 模様の色を抜く割合（0〜1）。1 なら白黒の模様に Tint の色を付ける（木の壁は 0.85 で青みがかった灰色に塗った板） |

今の行：Wood01〜16（Fab「Substance Materials Vol 01 - Wood」を 1K に縮めたもの。`/Game/Kakurenbo/Wood`）、Metal（Fab「Stylized Metallic Floor」）、Gem（Fab の宝石）、Player（プレイヤーの服の色の表）、
置く壁用の WoodWall（通常壁）・QuietWall（消音壁）・ClimbWall（クライミング壁）・StoneWall・IronWall（今は使っていない）、Stone（Fab「Stylized Hand-Painted Stone Wall」）、
M14 の館用：MedStone・MedStoneFloor・MedWoodFloor（Medieval Room Kit）、Tile・WallpaperA・WallpaperB・Plaster・Stucco（Stylized House Interior から取り出したもの。`/Game/Kakurenbo/Mansion/Props/Textures`）。
粗さのテクスチャが無い行は RoughnessScale の値のまま（マテリアルの既定のテクスチャは使わない）