// ゲーム全体で共有する型（enum / struct）

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "InputCoreTypes.h"
#include "KakurenboTypes.generated.h"

class UMaterialInterface;
class UTexture;
class UStaticMesh;

/** ゲームのパート。購入 → 設置 → かくれんぼ → リザルト → 購入 … と循環する。起動直後はタイトル */
UENUM(BlueprintType)
enum class EKakurenboPhase : uint8
{
	Shop   UMETA(DisplayName = "Shop"),
	Build  UMETA(DisplayName = "Build"),
	Hide   UMETA(DisplayName = "Hide"),
	Result UMETA(DisplayName = "Result"),
	Title  UMETA(DisplayName = "Title"), // タイトル画面（はじめから / つづきから）。数字の並びを変えないよう最後に足した
};

/**
 * プレイヤーの操作（キー設定で好きなキーに変えられる）。
 * セーブには名前で入れるので、並びを変えてもよい（名前は変えない）
 */
UENUM(BlueprintType)
enum class EKakurenboAction : uint8
{
	MoveForward UMETA(DisplayName = "MoveForward"), // 前へ（設置パートではカメラを動かす）
	MoveBack    UMETA(DisplayName = "MoveBack"),
	MoveLeft    UMETA(DisplayName = "MoveLeft"),
	MoveRight   UMETA(DisplayName = "MoveRight"),
	Mash        UMETA(DisplayName = "Mash"),        // 連打（かくれんぼ）
	Sneak       UMETA(DisplayName = "Sneak"),       // しゃがむ（設定で 1 回押して切り替え / 押している間）
	Dash        UMETA(DisplayName = "Dash"),        // 煙幕ダッシュ
	Climb       UMETA(DisplayName = "Climb"),       // よじ登る（前の壁・家具の上へ。M14 でジャンプから変えた）
	CameraLeft  UMETA(DisplayName = "CameraLeft"),  // 俯瞰・真上のカメラを回す
	CameraRight UMETA(DisplayName = "CameraRight"),
	TiltDown    UMETA(DisplayName = "TiltDown"),    // 設置パートのカメラを斜めに傾ける
	TiltUp      UMETA(DisplayName = "TiltUp"),      // 真上に戻す
	Next        UMETA(DisplayName = "Next"),        // 次のパートへ
	SetStart    UMETA(DisplayName = "SetStart"),    // 設置パート：スタート位置をカーソルのマスへ
	BackToShop  UMETA(DisplayName = "BackToShop"),  // 設置パート → 購入パート
	ClearAll    UMETA(DisplayName = "ClearAll"),    // 設置パート：全部回収（2 回押す）
	SwitchShop  UMETA(DisplayName = "SwitchShop"),  // 購入パート：コインのお店 ↔ 転生のお店
	Refill      UMETA(DisplayName = "Refill"),      // 購入パート：まとめて補充
	Prestige    UMETA(DisplayName = "Prestige"),    // 購入パート：転生（2 回押す）
	Help        UMETA(DisplayName = "Help"),        // 操作説明を開く
	UseItem     UMETA(DisplayName = "UseItem"),     // かくれんぼ：だるまさんがころんだ（宝石のお店の道具）を使う
	Count       UMETA(Hidden)
};

/** 1 つの操作に割り当てたキー（2 つまで。空でもよい） */
USTRUCT(BlueprintType)
struct FKakurenboKeyBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	FKey Primary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	FKey Secondary;
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
	Cat   UMETA(DisplayName = "Cat"),   // 迷い猫の鳴き声（猫はかわいいので、音を気にしない鬼も寄ってくる。探知鬼は動かない）
};

/** 罠の種類（Data/Traps.csv の Kind 列） */
UENUM(BlueprintType)
enum class ETrapKind : uint8
{
	Sticky UMETA(DisplayName = "Sticky"), // トリモチ：踏んだ鬼をしばらく動けなくする（1 回使うと消える）
	Decoy  UMETA(DisplayName = "Decoy"),  // おとり：一定の間隔で音を出して鬼を呼び寄せる（鬼が触れると壊れる）
	// ---- 宝石のお店（宝石で買う。Traps.csv の bGemItem = True の行） ----
	FakeHider UMETA(DisplayName = "FakeHider"), // 身代わり人形（偽プレイヤー）：鬼に見つかると、鬼と反対の方へまっすぐ逃げる。捕まると消える
	OneWay    UMETA(DisplayName = "OneWay"),    // 一方通行ののれん：鬼は矢印の向きにしか通れない（プレイヤーはどちらからも通れる）。Uses 回通られると壊れる
	Darkness  UMETA(DisplayName = "Darkness"),  // 真っ暗ブロック：まわり（Radius）が真っ暗になり、鬼はすぐ近くしか見えない。自分も見えにくい
	Cat       UMETA(DisplayName = "Cat"),       // 迷い猫：近くをうろうろして鳴く。寄ってきた鬼は猫をなでてしばらく動かない（どの鬼も猫には弱い）
	Daruma    UMETA(DisplayName = "Daruma"),    // だるまさんがころんだ：かくれんぼ中に使う（置かない）。使った瞬間に自分の視界にいた鬼は、しばらく動けない。止まった鬼にタッチすると門へ帰る
	GoldRush  UMETA(DisplayName = "GoldRush"),  // ゴールドラッシュ：真っ暗ブロックと同じく、置いておくとそのかくれんぼの間は連打のコインが 2 倍。1 ラウンドで壊れる
};

/** 転生のお店で買える永続強化（Data/PrestigeUpgrades.csv の行名と同じ名前） */
UENUM(BlueprintType)
enum class EPrestigeUpgrade : uint8
{
	WallHP       UMETA(DisplayName = "WallHP"),       // 壁の硬さ：すべての壁の耐久の倍率
	Treasure     UMETA(DisplayName = "Treasure"),     // お宝の強化：お宝の価値の倍率
	SmokeDuration UMETA(DisplayName = "SmokeDuration"), // 煙幕ダッシュ：Lv1 で使えるようになる。上げると煙が長く残る（1〜7 秒）
	SmokeCount   UMETA(DisplayName = "SmokeCount"),   // 煙幕の数：1 ラウンドに煙幕ダッシュを使える回数（1 + Lv）。セーブの並びを変えないよう、元の「ダッシュの回復」の場所
	Climb        UMETA(DisplayName = "Climb"),        // よじ登る：Lv1 で壁・家具によじ登れるようになる（元の「ジャンプ」の場所。セーブのレベルはそのまま）
	QuietHP      UMETA(DisplayName = "QuietHP"),      // 消音壁の丈夫さ：音を消せる回数の倍率（セーブの並びを変えないよう最後に足した）
	MashIncome   UMETA(DisplayName = "MashIncome"),   // 連打のお金アップ：連打 1 回のコインの倍率
	TimeIncome   UMETA(DisplayName = "TimeIncome"),   // 時間のお金アップ：時間で増えるコインの倍率
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
	PlayerClimb   UMETA(DisplayName = "PlayerClimb"),   // プレイヤーがよじ登った（ガサッ）
	OniStep       UMETA(DisplayName = "OniStep"),       // 鬼の足音（どたどた。その場所から聞こえる）
	TreasureSparkle UMETA(DisplayName = "TreasureSparkle"), // お宝のキラキラ（その場所から聞こえる）
	Summon        UMETA(DisplayName = "Summon"),        // 探知鬼が仲間を呼んだ
	Smoke         UMETA(DisplayName = "Smoke"),         // 煙幕を投げた（ボフッ）
	Heartbeat     UMETA(DisplayName = "Heartbeat"),     // 鬼が近い・向かってくる（ドクン。近いほど速く大きく）
	OniNotice     UMETA(DisplayName = "OniNotice"),     // 鬼が音に気づいた（「ン？」。その鬼の場所から聞こえる）
	Meow          UMETA(DisplayName = "Meow"),          // 迷い猫の鳴き声（にゃーん）
	Daruma        UMETA(DisplayName = "Daruma"),        // だるまさんがころんだ（かけ声のメロディ）
	Poof          UMETA(DisplayName = "Poof"),          // 身代わり人形が捕まって消えた・のれんが壊れた・タッチで鬼が門へ帰った（ポン）
	Gem           UMETA(DisplayName = "Gem"),           // 宝石を全部集めた（ボーナス）
	Count         UMETA(Hidden)
};

/** BGM の曲（Data/Sounds/Music_<名前>.wav があればそれ。無ければプログラムで作る。KakurenboSynth） */
UENUM(BlueprintType)
enum class EKakurenboMusic : uint8
{
	None UMETA(DisplayName = "None"),
	Calm UMETA(DisplayName = "Calm"),   // 購入・設置・リザルト：夜の館のおもちゃ箱のような、のんびりした曲
	Hide UMETA(DisplayName = "Hide"),   // かくれんぼ：速くて少し怖い曲
	Title UMETA(DisplayName = "Title"), // タイトル：少し不気味な夜の館への招待（プログラムの曲は Calm と同じ）
	Chase UMETA(DisplayName = "Chase"), // かくれんぼ中に鬼に追われている間：どたばた（プログラムの曲は Hide と同じ）
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

	/** まだ買えない（転生のお店で解放するもの。クライミング壁は「よじ登る」を解放してから） */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	bool bLocked = false;

	/** 宝石のお店の商品か（価格の単位が宝石） */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	bool bGemItem = false;

	/** まとめ買い（×10・最大）のとき：買える数と合計の価格（1 個ずつのときは 1 と Cost） */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	int32 BulkCount = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	double BulkCost = 0.0;
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

	/** よじ登るのにかかる秒数（通常壁 1 秒・クライミング壁は速い） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	float ClimbSeconds = 1.f;

	/**
	 * 登りやすい壁（クライミング壁）。4 つの面に足場の横木を付けて見分けられるようにする。
	 * 転生のお店で「よじ登る」を解放するまでは買えない
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	bool bLadder = false;
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

	/** 転生した後のはじめのコイン = 転生した回数 × これ（強化ではなく、転生するたびに自動で増える） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prestige")
	double StartCoinsPerPrestige = 500.0;
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

	/**
	 * 前方の箱の攻撃（パワー鬼。M15）：目の前の壁から、横 AttackBoxWidth × 奥 AttackBoxDepth マス × 下から AttackBoxLevels 段の壁に当たる。
	 * 0 なら使わない（AttackRadius の範囲攻撃・bSingleTargetAttack の 1 個だけ）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	int32 AttackBoxWidth = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	int32 AttackBoxDepth = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	int32 AttackBoxLevels = 0;

	/** うろうろ中、壁に囲まれた空洞があればそこを調べに行く確率（0〜1） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float PocketInspectChance = 0.5f;

	/**
	 * 勘（0〜1）：プレイヤーが見えず音も聞こえていないのに、近づいて来やすい。うろうろの次の行き先を決めるとき、この確率で
	 * プレイヤーの本当の場所のそば（勘が高いほど近く）を行き先にする。スピード鬼は低め・慎重鬼は高め
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float Intuition = 0.3f;

	/**
	 * 曲がり角が苦手（スピード鬼）：進む向きが一度に CornerTurnAngle 度以上変わると、速さが CornerSlowScale 倍まで落ち、
	 * CornerSlowDuration 秒かけて元に戻る。直線は速いが、角を曲がって逃げられると引き離される。0 なら無し
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float CornerTurnAngle = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float CornerSlowScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float CornerSlowDuration = 0.f;

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

	/** 罠の働き（Sticky = トリモチ / Decoy = おとり / 宝石のお店の道具: FakeHider・OneWay・Darkness・Cat・Daruma） */
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

	/** 宝石で買う道具（宝石のお店に並ぶ。価格の単位が宝石）。False ならコインで買う罠（コインのお店） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	bool bGemItem = false;

	/** 道具の働く範囲（cm）。真っ暗ブロック：暗くなる半径 / 迷い猫：うろうろする半径 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	float Radius = 0.f;

	/** 道具の速さ（cm/秒）。身代わり人形：逃げる速さ（プレイヤーの歩きは 420）/ 迷い猫：歩く速さ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	float Speed = 0.f;

	/** 回数。一方通行ののれん：鬼が通れる回数（尽きると壊れる） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	int32 Uses = 0;

	/** 秒数。迷い猫：鬼が猫をなでて動かない時間 / だるまさんがころんだ：鬼が止まる時間 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	float Duration = 0.f;

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

	/** 腰壁（壁の下の方の板）の見た目（Data/Surfaces.csv の行）。空なら腰壁なし（壁と同じ） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FName WainscotSurface;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FLinearColor WainscotTint = FLinearColor::White;
};

/**
 * 床の種類 1 つ。Data/FloorKinds.csv の 1 行（行の名前は間取りの [floor] で使う 1 文字）。
 * 部屋ごとに床を変える（台所・浴室はタイル、玄関ホールは石、廊下は寄木…）
 */
USTRUCT(BlueprintType)
struct FKakurenboFloorKindRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor")
	FText DisplayName;

	/** 見た目（Data/Surfaces.csv の行）。空ならマップの床（Maps.csv の FloorSurface） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor")
	FName Surface;

	/** 模様に掛ける色（マップの床の色にさらに掛ける。白ならそのまま） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor")
	FLinearColor Tint = FLinearColor::White;

	/** 市松模様の暗い方のマスの明るさ（1 なら模様なし） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor")
	float CheckerDarken = 0.85f;

	/** テクスチャが無いときの色 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor")
	FLinearColor Color = FLinearColor(0.2f, 0.12f, 0.08f);
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

	/** メッシュの正面の向きの補正（度）。家具は正面を部屋の空いている側へ向けて置く。後ろを向いてしまうメッシュは 180 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	float MeshYaw = 0.f;

	/**
	 * メッシュを上から見て空いている長方形（当たり判定の箱から抜く）。"最小X 最小Y 最大X 最大Y" をメッシュの外形に対する割合 0〜1 で。
	 * 例: L 字の本棚の手前の角 "0.4 0.44 1 1"。空なら外形そのままの箱。AltMeshCutout は AltMesh の分
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FString MeshCutout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FString AltMeshCutout;
};

/** 壁の飾りの種類（Data/Decor.csv の Kind 列） */
UENUM(BlueprintType)
enum class EKakurenboDecorKind : uint8
{
	Window        UMETA(DisplayName = "Window"),        // 窓（外周の壁だけ。月明かり）
	Painting      UMETA(DisplayName = "Painting"),      // 絵・額（壁ならどこでも）
	Sconce        UMETA(DisplayName = "Sconce"),        // 壁の明かり（たいまつ・燭台。明かりを付ける）
	DoorFrame     UMETA(DisplayName = "DoorFrame"),     // 1 マスの出入り口の枠
	DoorFrameWide UMETA(DisplayName = "DoorFrameWide"), // 2 マスの出入り口の枠
};

/**
 * 壁の飾り 1 種類。Data/Decor.csv の 1 行。館の部屋の壁・外周の壁・出入り口に、決まった間隔で自動で飾る（当たり判定なし）
 */
USTRUCT(BlueprintType)
struct FKakurenboDecorRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decor")
	EKakurenboDecorKind Kind = EKakurenboDecorKind::Painting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decor")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** 大きさ（cm。壁に沿った幅と高さ）。メッシュは縦横比を保ってこの中に収める */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decor")
	float Width = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decor")
	float Height = 100.f;

	/** 下の端の高さ（cm。床から） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decor")
	float Bottom = 120.f;

	/** 壁の面から部屋の側へ出す量（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decor")
	float Offset = 2.f;

	/** メッシュの正面の向きの補正（度。壁を向いてしまうなら 180） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decor")
	float MeshYaw = 0.f;

	/** 明かりの強さ（カンデラ。0 なら明かりなし） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decor")
	float LightIntensity = 0.f;
};
