// ゲーム全体で共有する型（enum / struct）

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "KakurenboTypes.generated.h"

class UMaterialInterface;
class UTexture;
class UStaticMesh;

/** ゲームのパート。購入 → 設置 → かくれんぼ → リザルト → 購入 … と循環する */
UENUM(BlueprintType)
enum class EKakurenboPhase : uint8
{
	Shop   UMETA(DisplayName = "Shop"),
	Build  UMETA(DisplayName = "Build"),
	Hide   UMETA(DisplayName = "Hide"),
	Result UMETA(DisplayName = "Result"),
};

/** プレイヤーの視点 */
UENUM(BlueprintType)
enum class EHiderViewMode : uint8
{
	Overhead    UMETA(DisplayName = "Overhead"),    // 俯瞰（購入・リザルト）：斜め上から自分を見下ろす
	ThirdPerson UMETA(DisplayName = "ThirdPerson"), // 三人称（かくれんぼ）：自分の背後の少し上から見る。マウスで回せる
	TopDown     UMETA(DisplayName = "TopDown"),     // 真上から（設置）：プレイヤーは表示せず、カメラだけを WASD で動かす
};

/** 鬼の状態（HUD 表示にも使う） */
UENUM(BlueprintType)
enum class EOniState : uint8
{
	Wander      UMETA(DisplayName = "Wander"),      // うろうろ
	Investigate UMETA(DisplayName = "Investigate"), // 音のした方へ向かう
	Chase       UMETA(DisplayName = "Chase"),       // プレイヤーを見つけて追いかける
	Inspect     UMETA(DisplayName = "Inspect"),     // 怪しい場所（壁に囲まれた空洞・見つけた壁）を調べに行く
	Attack      UMETA(DisplayName = "Attack"),      // 壁を壊している
	Stunned     UMETA(DisplayName = "Stunned"),     // 罠にかかって動けない
};

/** 鬼の種類（パックマンのお化けのように、それぞれ動き方が違う） */
UENUM(BlueprintType)
enum class EOniType : uint8
{
	Balanced UMETA(DisplayName = "Balanced"), // 標準：速さも壁を壊す力も普通
	Scout    UMETA(DisplayName = "Scout"),    // スピード：足が速く視界が広い。マップ全体を大まかに回る。壁を壊す力は弱い
	Breaker  UMETA(DisplayName = "Breaker"),  // パワー：遅いが壁を壊す力が強い。見つけた壁を壊しに行く。自分から探さない
	Careful  UMETA(DisplayName = "Careful"),  // 慎重：広い場所を手分けして見渡しながら調べる。囲まれた場所は最後。壁は 1 個ずつ壊す
	Treasure UMETA(DisplayName = "Treasure"), // 宝物：お宝の周りをぐるぐる回る。壁を壊すのも探すのも苦手。連打には鈍く、足音に敏感
	Detector UMETA(DisplayName = "Detector"), // 探知：決まった場所へ行ったら動かず見張る。プレイヤーを見つけると周りの鬼を呼び寄せる
};

/** プレイヤーなどが出す音の種類（鬼の種類によって聞こえ方が違う） */
UENUM(BlueprintType)
enum class EKakurenboNoise : uint8
{
	Mash  UMETA(DisplayName = "Mash"),  // 連打
	Dash  UMETA(DisplayName = "Dash"),  // ダッシュ
	Step  UMETA(DisplayName = "Step"),  // 足音・ジャンプ（足音として聞く）
	Decoy UMETA(DisplayName = "Decoy"), // おとり
};

/** 罠の種類（Data/Traps.csv の Kind 列） */
UENUM(BlueprintType)
enum class ETrapKind : uint8
{
	Sticky UMETA(DisplayName = "Sticky"), // トリモチ：踏んだ鬼をしばらく動けなくする（1 回使うと消える）
	Decoy  UMETA(DisplayName = "Decoy"),  // おとり：一定の間隔で音を出して鬼を呼び寄せる（鬼が触れると壊れる）
};

/** 転生のお店で買える永続強化（Data/PrestigeUpgrades.csv の行名と同じ名前） */
UENUM(BlueprintType)
enum class EPrestigeUpgrade : uint8
{
	WallHP       UMETA(DisplayName = "WallHP"),       // 壁の硬さ：すべての壁の耐久の倍率
	Treasure     UMETA(DisplayName = "Treasure"),     // お宝の強化：お宝の価値の倍率
	SmokeDuration UMETA(DisplayName = "SmokeDuration"), // 煙幕ダッシュ：Lv1 で使えるようになる。上げると煙が長く残る（1〜7 秒）
	SmokeCount   UMETA(DisplayName = "SmokeCount"),   // 煙幕の数：1 ラウンドに煙幕ダッシュを使える回数（1 + Lv）。セーブの並びを変えないよう、元の「ダッシュの回復」の場所
	Jump         UMETA(DisplayName = "Jump"),         // ジャンプ：Lv1 でジャンプできるようになる
	QuietHP      UMETA(DisplayName = "QuietHP"),      // 消音壁の丈夫さ：音を消せる回数の倍率（セーブの並びを変えないよう最後に足した）
	Count        UMETA(Hidden)
};

/** 効果音の種類（音のファイルが無くても、プログラムで波形を作って鳴らす） */
UENUM(BlueprintType)
enum class EKakurenboSfx : uint8
{
	Mash          UMETA(DisplayName = "Mash"),          // 連打
	Treasure      UMETA(DisplayName = "Treasure"),      // お宝を取った
	Alert         UMETA(DisplayName = "Alert"),         // 鬼に見つかって追いかけられ始めた
	BlockHit      UMETA(DisplayName = "BlockHit"),      // 壁が攻撃された（壊れていない）
	BlockBreak    UMETA(DisplayName = "BlockBreak"),    // 壁が壊れた
	Caught        UMETA(DisplayName = "Caught"),        // 見つかった（アウト）
	Clear         UMETA(DisplayName = "Clear"),         // 逃げ切った
	TrapSticky    UMETA(DisplayName = "TrapSticky"),    // トリモチにかかった
	DecoyPing     UMETA(DisplayName = "DecoyPing"),     // おとりが音を出した
	DecoyBreak    UMETA(DisplayName = "DecoyBreak"),    // おとりが壊された
	CountdownBeep UMETA(DisplayName = "CountdownBeep"), // 開始前のカウントダウン
	RoundStart    UMETA(DisplayName = "RoundStart"),    // 鬼が出てきた
	TimeTick      UMETA(DisplayName = "TimeTick"),      // 残り 5 秒からの秒読み
	Buy           UMETA(DisplayName = "Buy"),           // 購入できた
	BuyFail       UMETA(DisplayName = "BuyFail"),       // コインが足りない
	Place         UMETA(DisplayName = "Place"),         // 壁・罠を置いた
	PickUp        UMETA(DisplayName = "PickUp"),        // 壁・罠を回収した
	Dash          UMETA(DisplayName = "Dash"),          // ダッシュした（大きな音）
	Prestige      UMETA(DisplayName = "Prestige"),      // 転生した
	PlayerStep    UMETA(DisplayName = "PlayerStep"),    // プレイヤーの足音
	PlayerJump    UMETA(DisplayName = "PlayerJump"),    // プレイヤーのジャンプ
	OniStep       UMETA(DisplayName = "OniStep"),       // 鬼の足音（どたどた。その場所から聞こえる）
	TreasureSparkle UMETA(DisplayName = "TreasureSparkle"), // お宝のキラキラ（その場所から聞こえる）
	Summon        UMETA(DisplayName = "Summon"),        // 探知鬼が仲間を呼んだ
	Smoke         UMETA(DisplayName = "Smoke"),         // 煙幕を投げた（ボフッ）
	Heartbeat     UMETA(DisplayName = "Heartbeat"),     // 鬼が近い・向かってくる（ドクン。近いほど速く大きく）
	OniNotice     UMETA(DisplayName = "OniNotice"),     // 鬼が音に気づいた（「ン？」。その鬼の場所から聞こえる）
	Count         UMETA(Hidden)
};

/** BGM の曲（プログラムで作る。KakurenboSynth） */
UENUM(BlueprintType)
enum class EKakurenboMusic : uint8
{
	None UMETA(DisplayName = "None"),
	Calm UMETA(DisplayName = "Calm"), // 購入・設置・リザルト：夜の館のおもちゃ箱のような、のんびりした曲
	Hide UMETA(DisplayName = "Hide"), // かくれんぼ：速くて少し怖い曲
};

/** 購入パートの商品 1 つ分の表示用データ */
USTRUCT(BlueprintType)
struct FShopItemView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	FText DisplayName;

	/** 効果の説明（現在値 → 購入後の値 など） */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	FText Description;

	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	double Cost = 0.0;

	/** 強化ならレベル、壁なら在庫数 */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	FText OwnedText;

	/** 壁・罠：壊れた（使った）数と、全部直すのにあと何個要るか（無ければ空） */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	FText RepairText;

	/** 壁・罠：在庫が足りず、直せないものがある */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	bool bNeedsMoreForRepair = false;

	/** 転生のお店の商品か（価格の単位が転生ポイント） */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	bool bPrestigeItem = false;

	/** これ以上強化できない */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	bool bMaxed = false;
};

/**
 * 購入できる壁の種類。Data/Walls.csv（または GameMode の WallTable）の 1 行。
 * FTableRowBase を継承すると、DataTable（表形式のデータ）の行として使える
 */
USTRUCT(BlueprintType)
struct FWallTypeDef : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	FText DisplayName;

	/** 耐久値。鬼の攻撃力がこれ以上だと一撃で壊れる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	double MaxHP = 1.0;

	/** 1 個目の価格 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	double Cost = 10.0;

	/** 持っている数（在庫＋置いてある数）が 1 増えるごとの価格の倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	double CostGrowth = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	FLinearColor Color = FLinearColor(0.5f, 0.35f, 0.2f);

	/**
	 * 音を消せる回数（消音壁）。囲まれた中で連打・ダッシュの音を小さくするたびに減り、0 になると壊れる。
	 * 0 なら減らない（普通の壁）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	double SoundHP = 0.0;

	/**
	 * 音を小さくする割合（0〜1。消音壁）。
	 * プレイヤーが壁に囲まれた場所（空洞）にいるときだけ、囲んでいる壁の平均の割合だけ連打・ダッシュの音が小さくなる
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	float NoiseDamping = 0.f;

	/** 見た目（Data/Surfaces.csv の行。Fab の木・鉄など）。空・テクスチャが無ければ Color の箱 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	FName Surface;
};

/**
 * 見た目（テクスチャ）1 種類。Data/Surfaces.csv の 1 行。共通のマテリアル（/Game/Kakurenbo/Materials/M_KakuSurface）に
 * 実行時にテクスチャを差し込んで使う（Fab のテクスチャは Git に入っていないので、無いパソコンでは色だけで表示する）
 */
USTRUCT(BlueprintType)
struct FKakurenboSurfaceRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 模様（色） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	TSoftObjectPtr<UTexture> BaseColor;

	/** 凹凸（ノーマルマップ。空なら平ら） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	TSoftObjectPtr<UTexture> Normal;

	/** ざらざら具合（R チャンネル。空なら RoughnessScale の値） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	TSoftObjectPtr<UTexture> Roughness;

	/** 模様に掛ける色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	float Metallic = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	float RoughnessScale = 1.f;

	/** 1 面に模様を何回くり返すか */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	float UVScale = 1.f;

	/** 模様の色を抜く割合（0〜1）。1 なら白黒の模様に Tint の色を付ける（塗った板など） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	float Desaturate = 0.f;

	/** 自分で光る強さ（暗い館でも見えるように。宝石など） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	float Emissive = 0.f;
};

/** 転生の数値。Data/Prestige.csv の 1 行（行名 Prestige） */
USTRUCT(BlueprintType)
struct FKakurenboPrestigeRow : public FTableRowBase
{
	GENERATED_BODY()

	/** このステージまで来たら転生できる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prestige")
	int32 MinStage = 5;

	/** 転生ポイント = (今のステージ - MinStage + 1) × PointsPerStage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prestige")
	int32 PointsPerStage = 1;
};

/**
 * 転生のお店の商品 1 つ。Data/PrestigeUpgrades.csv の 1 行（行名は EPrestigeUpgrade の名前）。
 * 価格（転生ポイント） = 切り上げ(BaseCost × CostGrowth ^ 今のレベル)、効果 = BaseValue × ValueGrowth ^ レベル
 */
USTRUCT(BlueprintType)
struct FKakurenboPrestigeUpgradeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prestige")
	FText DisplayName;

	/** 最大レベル（0 なら上限なし） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prestige")
	int32 MaxLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prestige")
	double BaseCost = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prestige")
	double CostGrowth = 1.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prestige")
	double BaseValue = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prestige")
	double ValueGrowth = 1.5;
};

/** 強化 1 種類の数値。Data/Upgrades.csv（または GameMode の UpgradeTable）の 1 行 */
USTRUCT(BlueprintType)
struct FKakurenboUpgradeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	FText DisplayName;

	/** Lv.0 から Lv.1 に上げる価格 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	double BaseCost = 10.0;

	/** レベルが 1 上がるごとの価格の倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	double CostGrowth = 1.5;

	/** Lv.0 のときの効果（連打 1 回のコイン、毎秒のコインなど） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	double BaseValue = 1.0;

	/** レベルが 1 上がるごとの効果の倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	double ValueGrowth = 1.5;
};

/** ステージ 1 つ分の設定。Data/Stages.csv（または GameMode の StageTable）の 1 行（行の順番＝ステージ番号） */
USTRUCT(BlueprintType)
struct FKakurenboStageRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 制限時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	float HideDuration = 30.f;

	/** 出てくる鬼の種類と数。CSV では "(Balanced,Scout)" のように書く */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	TArray<EOniType> OniTypes = { EOniType::Balanced, EOniType::Scout };

	/** お宝の数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	int32 NumTreasures = 3;

	/** 逃げ切り報酬（コイン） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	double ClearReward = 100.0;

	/** 鬼の攻撃力（壁の耐久値と比べる） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	double OniDamage = 1.0;

	/** 鬼に連打の音が聞こえる距離（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	float OniHearingRadius = 1200.f;

	/** 鬼の移動速度に足す値（cm/秒。うろうろ・調べる・追いかける の全部に足す） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	float OniSpeedBonus = 0.f;

	/** 館のマップ（Maps.csv の行の名前）。空なら前のステージと同じ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	FName Map;
};

/** 鬼の種類ごとの数値。Data/OniTypes.csv の 1 行（行名は Balanced / Scout / Breaker / Careful） */
USTRUCT(BlueprintType)
struct FKakurenboOniTypeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	FText DisplayName;

	/** 移動速度の倍率（基本の速さ: うろうろ 420 / 調べる 700 / 追いかける 840） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float SpeedScale = 1.f;

	/** 壁を壊す力の倍率（ステージの攻撃力に掛ける） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	double DamageScale = 1.0;

	/** 視界の距離（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float SightRadius = 900.f;

	/** 視界の半角（度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float SightHalfAngle = 40.f;

	/** プレイヤーの音（連打・ダッシュ）が聞こえる距離の倍率（0 なら気にしない） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float HearingScale = 1.f;

	/** おとりの音が聞こえる距離の倍率（0 ならおとりにだまされない。大きいほど遠くから寄っていく） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float DecoyHearingScale = 1.f;

	/** プレイヤーの足音・ジャンプの音が聞こえる距離の倍率（0 なら気にしない。宝物鬼は大きい） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float StepHearingScale = 1.f;

	/** プレイヤーを見つけたとき、この距離（cm）以内の鬼を呼び寄せる（0 なら呼ばない。探知鬼） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float SummonRadius = 0.f;

	/** トリモチで動けない時間の倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float StunScale = 1.f;

	/** true なら、トリモチから抜け出した直後（かからない間）に踏んだトリモチを壊す */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	bool bDisarmTraps = false;

	/** true なら目の前の壁 1 個だけを壊す（範囲攻撃しない） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	bool bSingleTargetAttack = false;

	/** 範囲攻撃の半径（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float AttackRadius = 160.f;

	/** 攻撃の溜め時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float AttackWindup = 0.8f;

	/** うろうろ中、壁に囲まれた空洞があればそこを調べに行く確率（0〜1） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float PocketInspectChance = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	FLinearColor Color = FLinearColor(0.9f, 0.1f, 0.08f);

	/** 見た目の太さの倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float BodyScale = 1.f;

	/** キャラクターのモデル（スケルタルメッシュ）を使うときの、この種類のマテリアル（色違い）。空ならメッシュのまま */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	TSoftObjectPtr<UMaterialInterface> MeshMaterial;

	/** モデルの縁の光の色（Cute Creature の ReflectionColor）。A が 0 なら変えない（宝物鬼は金色） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	FLinearColor MeshTint = FLinearColor(0.f, 0.f, 0.f, 0.f);
};

/** 罠 1 種類の数値。Data/Traps.csv の 1 行（行の並び順が購入パート・設置パートでの並び順） */
USTRUCT(BlueprintType)
struct FKakurenboTrapRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	FText DisplayName;

	/** 罠の働き（Sticky = トリモチ / Decoy = おとり） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	ETrapKind Kind = ETrapKind::Sticky;

	/** 1 個目の価格 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	double Cost = 60.0;

	/** 持っている数（在庫＋置いてある数）が 1 増えるごとの価格の倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	double CostGrowth = 1.3;

	/** トリモチ：鬼を動けなくする時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	float StunSeconds = 4.f;

	/** おとり：音を出す間隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	float NoiseInterval = 2.5f;

	/** おとり：音の大きさ（1 なら連打の音と同じ距離まで届く） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	float NoiseLoudness = 1.f;

	/** 鬼の体の中心がこの距離（cm）まで来たら発動する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	float TriggerRadius = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	FLinearColor Color = FLinearColor(1.f, 0.9f, 0.2f);
};

/**
 * 館のマップ 1 つ。Data/Maps.csv の 1 行（行の名前をステージの表の Map に書く）。
 * 間取り（家具・部屋の壁）は Data/Maps/<LayoutFile> に文字で書く（書き方は Data/README.md）
 */
USTRUCT(BlueprintType)
struct FKakurenboMapRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FText DisplayName;

	/** 間取りのファイル（Data/Maps/ の中） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FString LayoutFile;

	/** 床の色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FLinearColor FloorColor = FLinearColor(0.16f, 0.09f, 0.06f);

	/** 外周と部屋の壁（壁紙）の色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FLinearColor WallColor = FLinearColor(0.2f, 0.1f, 0.3f);

	/** 明かり（ランプ・暖炉）の色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FLinearColor LightColor = FLinearColor(1.f, 0.6f, 0.3f);

	/** 床の見た目（Data/Surfaces.csv の行。1 マスに模様 1 回）。空・テクスチャが無ければ FloorColor の市松模様 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FName FloorSurface;

	/** 外周と部屋の壁に貼る見た目（Data/Surfaces.csv の行。1 マス × 125cm の板に模様 1 回）。空・テクスチャが無ければ WallColor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FName WallSurface;

	/** 床・壁の見た目に掛ける色（白ならそのまま。マップごとの雰囲気を出す） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FLinearColor FloorTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FLinearColor WallTint = FLinearColor::White;
};

/**
 * 家具 1 種類。Data/Furniture.csv の 1 行（行の名前は間取りで使う 1 文字）。
 * 家具は壊せず、通れない（壁と同じように隠れるのに使える）。bWalkable の家具（じゅうたん）は飾りだけ
 */
USTRUCT(BlueprintType)
struct FKakurenboFurnitureRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FText DisplayName;

	/** 見た目のメッシュ（例: Fab の Stylized Library）。無い・見つからなければ色の付いた箱 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** もう 1 つの見た目（あればランダムにどちらか） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	TSoftObjectPtr<UStaticMesh> AltMesh;

	/** 高さ（cm）。メッシュの高さはこれに合わせる。箱のときの高さ・当たり判定の高さでもある */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	float Height = 200.f;

	/** 箱で表すときの色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FLinearColor Color = FLinearColor(0.4f, 0.25f, 0.15f);

	/** true なら上を歩ける飾り（じゅうたんなど）。通れない家具にならない */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	bool bWalkable = false;

	/** 明かりの強さ（カンデラ。0 なら明かりなし。暖炉・ランプ） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	float LightIntensity = 0.f;
};
