// ゲームのルールと進行を管理するクラス。
// パートの切り替え・コインの計算・購入処理・鬼とお宝の出現・罠・セーブとロードはすべてここに集める。
// 演出（破片・輪・画面の点滅）と効果音を鳴らすきっかけもここから出す。
//
// 数値（ステージ・強化・壁・鬼・罠・転生）は <プロジェクト>/Data/*.csv から読み込む（Data/README.md 参照）。
// CSV が読めないときは、このクラスに書いてある既定値を使う。
//
// Config=Game: UPROPERTY(Config) の値を Config/DefaultGame.ini の
// [/Script/KakurenboIncremental.KakurenboGameMode] から読む（鬼の見た目のアセットのパスなど）

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KakurenboMaps.h"
#include "KakurenboSaveGame.h"
#include "KakurenboTypes.h"
#include "OniCharacter.h"
#include "KakurenboGameMode.generated.h"

class AKakurenboArena;
class AKakurenboGameState;
class AOniCharacter;
class APlaceableBlock;
class ATrapActor;
class ATreasureActor;
class UAnimInstance;
class UAnimationAsset;
class UAnimSequence;
class UDataTable;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMesh;
class UStaticMesh;
class USoundBase;

UCLASS(Config = Game)
class KAKURENBOINCREMENTAL_API AKakurenboGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKakurenboGameMode();

	// ===== バランスデータ =====

	/**
	 * ステージごとの設定（行の順番＝ステージ番号）。起動時に Data/Stages.csv から読み込む。
	 * ここに DataTable アセットを設定すると CSV より優先される（行の型: KakurenboStageRow）
	 */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> StageTable;

	/** 強化の数値。Data/Upgrades.csv の代わりに使う DataTable（行の型: KakurenboUpgradeRow。行名 Mash / Time） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> UpgradeTable;

	/** 壁の種類。Data/Walls.csv の代わりに使う DataTable（行の型: WallTypeDef） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> WallTable;

	/** 鬼の種類ごとの数値。Data/OniTypes.csv の代わりに使う DataTable（行の型: KakurenboOniTypeRow。行名 Balanced / Scout / Breaker / Careful） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> OniTypeTable;

	/** 罠の種類。Data/Traps.csv の代わりに使う DataTable（行の型: KakurenboTrapRow） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> TrapTable;

	/** 転生の数値。Data/Prestige.csv の代わりに使う DataTable（行の型: KakurenboPrestigeRow。行名 Prestige） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> PrestigeTable;

	/** 転生のお店の商品。Data/PrestigeUpgrades.csv の代わりに使う DataTable（行の型: KakurenboPrestigeUpgradeRow） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> PrestigeUpgradeTable;

	/** 館のマップ。Data/Maps.csv の代わりに使う DataTable（行の型: KakurenboMapRow） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> MapTable;

	/** 家具。Data/Furniture.csv の代わりに使う DataTable（行の型: KakurenboFurnitureRow。行の名前は間取りの 1 文字） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> FurnitureTable;

	/** 見た目（テクスチャ）。Data/Surfaces.csv の代わりに使う DataTable（行の型: KakurenboSurfaceRow） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> SurfaceTable;

	/** 床の種類。Data/FloorKinds.csv の代わりに使う DataTable（行の型: KakurenboFloorKindRow。行の名前は間取りの [floor] の 1 文字） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> FloorKindTable;

	/** 読み込んだ床の種類（[floor] の 1 文字 → 種類） */
	TMap<TCHAR, FKakurenboFloorKindRow> FloorKindRows;

	/** 壁の飾り（窓・絵・明かり・出入り口の枠）。Data/Decor.csv の代わりに使う DataTable（行の型: KakurenboDecorRow） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> DecorTable;

	/** 読み込んだ壁の飾り */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Map")
	TArray<FKakurenboDecorRow> DecorRows;

	/** 読み込んだ鬼の種類ごとの数値 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance")
	TMap<EOniType, FKakurenboOniTypeRow> OniTypeRows;

	/** 読み込んだステージの表（CSV・DataTable が無ければ空 → ステージ 1 の既定値から伸ばす） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance")
	TArray<FKakurenboStageRow> StageRows;

	/** 読み込んだ館のマップ（行の名前 → 設定）と、表に書いてある順番 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Map")
	TMap<FName, FKakurenboMapRow> MapRows;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Map")
	TArray<FName> MapOrder;

	/** 表より後のステージで回るマップの数（Maps.csv の後ろから。一番大きい館だけを回す） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Map")
	int32 MapsCycledAfterTable = 3;

	/** 床にテクスチャを使うとき、市松模様の暗い方のマスに掛ける明るさ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Look")
	float FloorCheckerDarken = 0.8f;

	/** 見た目（テクスチャ）。Data/Surfaces.csv（行の名前 → 設定）。壁・床・宝石・プレイヤーで使う */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Look")
	TMap<FName, FKakurenboSurfaceRow> SurfaceRows;

	/** 見た目の行を探す（無ければ null） */
	const FKakurenboSurfaceRow* FindSurface(FName Name) const { return Name.IsNone() ? nullptr : SurfaceRows.Find(Name); }

	/** 見た目の動的マテリアルを作る（テクスチャがそのパソコンに無ければ null） */
	UMaterialInstanceDynamic* MakeSurfaceMaterial(FName Name, const FLinearColor& Tint = FLinearColor::White);

	/** 表より後のステージで、何ステージごとに次のマップへ移るか */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Map")
	int32 StagesPerMapAfterTable = 1;

	/** 表より後のステージで、1 ステージごとに伸ばす量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Stage Growth")
	float StageHideDurationGrowth = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Stage Growth")
	double StageClearRewardGrowth = 4.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Stage Growth")
	double StageOniDamageGrowth = 1.6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Stage Growth")
	float StageOniHearingGrowth = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Stage Growth")
	float StageOniSpeedGrowth = 15.f;

	/** Stages.csv より後のステージは、このステージ数ごとに鬼が 1 体増える（0 なら増えない。鬼の数に上限は無い） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Stage Growth")
	int32 StageExtraOniEvery = 3;

	// ===== かくれんぼのルール =====

	/** かくれんぼ開始から鬼が出てくるまでの猶予（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	float HideStartDelay = 3.f;

	// ===== 強化（Upgrades.csv で上書きされる） =====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double MashIncomeBase = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double MashIncomeGrowth = 1.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double TimeIncomeBase = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double TimeIncomeGrowth = 1.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double MashUpgradeBaseCost = 15.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double MashUpgradeCostGrowth = 1.7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double TimeUpgradeBaseCost = 25.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double TimeUpgradeCostGrowth = 1.7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	FText MashUpgradeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	FText TimeUpgradeName;

	/**
	 * 壁の補強（コインのお店）：すべての壁の耐久 = 元の耐久 × (WallReinforceBase + WallReinforcePerLevel × レベル)。
	 * 足し算で伸びる（1 周の中の強化）。転生のお店の「壁の硬さ」はかけ算（×1.5 ずつ）。Upgrades.csv の Wall の行で上書きされる
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double WallReinforceBase = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double WallReinforcePerLevel = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double WallReinforceBaseCost = 20.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double WallReinforceCostGrowth = 1.6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	FText WallReinforceName;

	// ===== 転生（Prestige.csv で上書きされる） =====

	/** 転生の条件と、もらえる転生ポイント */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prestige")
	FKakurenboPrestigeRow PrestigeSettings;

	/** 転生のお店の商品（インデックスは EPrestigeUpgrade。PrestigeUpgrades.csv で上書きされる） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prestige")
	TArray<FKakurenboPrestigeUpgradeRow> PrestigeUpgrades;

	// ===== お宝 =====

	/** M15：お宝 1 個でもらえる宝石の数（Lv0。お宝ではコインはもらえない） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	int32 GemsPerTreasure = 1;

	/** M15：そのラウンドのお宝を全部取ったときのボーナス（宝石の数・逃げ切り報酬に対するコインの割合） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	int32 AllTreasureBonusGems = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	double AllTreasureBonusCoinRatio = 0.0;

	/** お宝はプレイヤー・他のお宝からこのマス数以上離して置く */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	float TreasureMinDistanceCells = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	TSubclassOf<ATreasureActor> TreasureClass;

	/** 煙幕ダッシュで投げる煙幕（BP の派生クラスで見た目を変えてよい。空なら ASmokeCloud） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dash")
	TSubclassOf<class ASmokeCloud> SmokeClass;

	// ===== 壁（Walls.csv で上書きされる） =====

	/** 購入できる壁の種類（ショップの並び順）。耐久は転生の倍率をかける前の値（かけた後は GetEffectiveWallTypes） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	TArray<FWallTypeDef> WallTypes;

	// ===== 罠（Traps.csv で上書きされる） =====

	/** 購入できる罠の種類（ショップ・設置パートの並び順） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	TArray<FKakurenboTrapRow> TrapTypes;

	// ===== 音 =====

	/** 効果音を音アセットに差し替える（設定していない音はプログラムで作った音を鳴らす） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TMap<EKakurenboSfx, TObjectPtr<USoundBase>> SoundOverrides;

	/** BGM を音アセットに差し替える（くり返し再生の設定（Looping）をした音アセットにする。設定していない曲はプログラムで作った曲） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TMap<EKakurenboMusic, TObjectPtr<USoundBase>> MusicOverrides;

	// ===== プレイヤー・お宝の見た目（Fab の FBX を Tools/ImportSourceArt.ps1 で取り込んだもの。無ければ円柱・箱） =====

	/** プレイヤーの見た目のスケルタルメッシュ。体の高さ（160cm）に自動で合わせる。動きはプログラムで付ける（歩くと手足を振る） */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Player|Look")
	TSoftObjectPtr<USkeletalMesh> PlayerSkeletalMesh;

	/** プレイヤーの見た目のテクスチャ（Data/Surfaces.csv の行） */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Player|Look")
	FName PlayerSurface = TEXT("Player");

	/** メッシュの向き（度。正面が +Y を向いているモデルは -90） */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Player|Look")
	float PlayerMeshYaw = -90.f;

	/**
	 * 公式のアニメーション（UE のマネキン。Tools/ImportSourceArt.ps1 がテンプレートからコピーする）。
	 * 見えないマネキン（PlayerAnimSourceMesh）が再生し、同じ名前の骨の回転をモデルへ写す。無ければプログラムで作った簡単な動き
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Player|Look")
	TSoftObjectPtr<USkeletalMesh> PlayerAnimSourceMesh;

	/** 待機〜歩く〜走る（横軸が速さのブレンドスペース） */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Player|Look")
	TSoftObjectPtr<UAnimationAsset> PlayerAnimMove;

	/** 空中（ジャンプ中・落ちている） */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Player|Look")
	TSoftObjectPtr<UAnimationAsset> PlayerAnimFall;

	/**
	 * 迷い猫の見た目（Fab の「Low Poly Cat」を Tools/ImportSourceArt.ps1 で取り込んだもの。M15）。無ければ基本の形で作った猫。
	 * 骨の無いモデルなので、歩くときはプログラムで体を揺らす。CatLength は体の長さ（cm）、CatMeshYaw はモデルの向きの補正（顔が +X を向くように）
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Gem|Look")
	TSoftObjectPtr<UStaticMesh> CatMesh;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Gem|Look")
	float CatLength = 70.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Gem|Look")
	float CatMeshYaw = 0.f;

	/** お宝（宝石）のメッシュ。何種類か書くと、お宝ごとに違う形になる */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Treasure|Look")
	TArray<TSoftObjectPtr<UStaticMesh>> TreasureMeshes;

	/** 宝石のテクスチャ（Data/Surfaces.csv の行）。色はお宝の色（金）を掛ける */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Treasure|Look")
	FName TreasureSurface = TEXT("Gem");

	/** 宝石の大きさ（cm。一番長い辺） */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Treasure|Look")
	float TreasureMeshSize = 55.f;

	// ===== 鬼 =====

	/** スポーンする鬼のクラス（BP の派生クラスで見た目を変えてよい） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	TSubclassOf<AOniCharacter> OniClass;

	/**
	 * 鬼の見た目のスケルタルメッシュ（例: Fab の Cute Creature）。設定するとプログラムで作った円柱の代わりに表示する。
	 * DefaultGame.ini の [/Script/KakurenboIncremental.KakurenboGameMode] に OniSkeletalMesh=/Game/.../SK_xxx.SK_xxx と書く
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Oni|Look")
	TSoftObjectPtr<USkeletalMesh> OniSkeletalMesh;

	/** 鬼のアニメーション BP。ini では OniAnimClass=/Game/.../ABP_xxx.ABP_xxx_C。無ければ下の OniAnim〜 を直接再生する */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Oni|Look")
	TSoftClassPtr<UAnimInstance> OniAnimClass;

	/** アニメーション BP が無いときに直接再生するアニメーション（待機・走る・攻撃・見つけた・罠で動けない） */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Oni|Look")
	TSoftObjectPtr<UAnimSequence> OniAnimIdle;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Oni|Look")
	TSoftObjectPtr<UAnimSequence> OniAnimRun;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Oni|Look")
	TSoftObjectPtr<UAnimSequence> OniAnimAttack;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Oni|Look")
	TSoftObjectPtr<UAnimSequence> OniAnimWin;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Oni|Look")
	TSoftObjectPtr<UAnimSequence> OniAnimStunned;

	/** 走るアニメーションがちょうどよく見える速さ（cm/秒）。速く動くほど速く再生する */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Oni|Look")
	float OniRunAnimSpeed = 450.f;

	/** 体に重ねて表示する小物のメッシュ（Cute Creature の包帯など。同じ骨で動く） */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Oni|Look")
	TSoftObjectPtr<USkeletalMesh> OniAccessoryMesh;

	/**
	 * メッシュの大きさ（体の高さ 190cm に自動で合わせた上にかける倍率）・高さの調整（cm）・向き（度。多くのキャラクターは -90 で正面を向く）
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Oni|Look")
	float OniMeshScale = 1.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Oni|Look")
	float OniMeshZOffset = 0.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Oni|Look")
	float OniMeshYaw = -90.f;

	/** うろうろするときの速さ（プレイヤーは 420）。鬼の種類ごとの倍率（SpeedScale）を掛ける */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float OniWanderSpeedBase = 420.f;

	/** 音を調べに行くときの速さ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float OniInvestigateSpeedBase = 700.f;

	/** 追いかけるときの速さ（プレイヤーの 2 倍）。見つかったら走って逃げ切るのは難しく、壁の陰に隠れて見失わせる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float OniChaseSpeedBase = 840.f;

	/** 連打したとき、音の届く範囲を床に表示する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	bool bShowNoiseRing = true;

	// ===== 危ないときの心臓の音（画面を見なくても鬼が来ているとわかる） =====

	/** 鬼がこの距離（cm）より近いと一番危ない。DangerMaxDistance より遠い鬼は数えない（追いかけてくる鬼は別） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float DangerFullDistance = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float DangerMaxDistance = 1600.f;

	/** 危なさ（0〜1）がこれ以上で心臓の音が鳴り始める */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float HeartbeatMinDanger = 0.2f;

	/** 心臓の音の間隔（秒）：危なさが小さいとき / 一番危ないとき */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float HeartbeatSlowInterval = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float HeartbeatFastInterval = 0.38f;

	/** M15：心臓の音の大きさは、一番近い鬼との距離で決める。この距離（cm）より遠い鬼では鳴らない（向かってくる・追ってくる鬼は危なさで鳴る） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float HeartbeatMaxDistance = 2000.f;

	/** 鬼に追われなくなってから、追われている間の BGM（Chase）をかくれんぼの BGM に戻すまでの秒数（すぐ戻ると曲がばたばた変わるので） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float ChaseMusicHoldSeconds = 4.f;

	/** このステージから、かくれんぼの BGM を大きな館の曲（HideDeep。もっと深い夜）にする */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	int32 DeepHideMusicStage = 11;

	/** かくれんぼの BGM（DeepHideMusicStage からは大きな館の曲） */
	EKakurenboMusic GetHideMusic() const;

	/** 近さ（0〜1）がこれ以上で鳴り始める */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float HeartbeatMinCloseness = 0.12f;

	/** 心臓の音の大きさ：一番遠いとき / すぐそば（DangerFullDistance 以内）のとき */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float HeartbeatQuietVolume = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float HeartbeatLoudVolume = 1.2f; // M15：2.2 だと大きすぎた（ユーザーの指摘）

	/**
	 * 2026-10-19：心臓の音で鬼の足音・お宝のキラキラが聞こえにくい（ユーザーの指摘）→ 手がかりの音（足音・キラキラ・「ン？」）が
	 * 聞こえてから HeartbeatDuckSeconds の間は、心臓の音を HeartbeatDuckScale 倍にする（鳴っている途中の心臓の音もすぐ下げる）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float HeartbeatDuckScale = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float HeartbeatDuckSeconds = 0.6f;

	/** 今の危なさ（0〜1）・一番近い鬼の近さ（0〜1）・鳴らした心臓の音の数と最後の大きさ（テスト用） */
	float GetCurrentDanger() const { return CurrentDanger; }
	float GetCurrentCloseness() const { return CurrentCloseness; }
	int32 GetHeartbeatCount() const { return HeartbeatCount; }
	float GetLastHeartbeatVolume() const { return LastHeartbeatVolume; }
	float ComputeDanger() const;
	float ComputeOniCloseness() const;

	/** 鬼の経路などをデバッグ表示する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	bool bDebugOni = false;

	// ===== 舞台 =====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	int32 GridSizeX = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	int32 GridSizeY = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	float CellSize = 100.f;

	/** ブロック 1 段の高さ（cm）。プレイヤーの背の高さ（160cm）と同じにして、1 段で体が隠れるようにする */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	float BlockHeight = 160.f;

	/** ブロックを積める最大の段数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	int32 MaxStackHeight = 3;

	// ===== タイトル =====

	/** 起動したらタイトル画面から始める（起動オプション -KakuSkipTitle なら、今まで通りすぐ始める） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title")
	bool bStartAtTitle = true;

	/** 「つづきから」で遊べるか（セーブがある・この起動で遊んだ） */
	bool HasContinueData() const { return bHasContinueData; }

	/** タイトル：はじめから（セーブがあれば消して、ステージ 1 のかくれんぼから） */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void StartNewGame();

	/** タイトル：つづきから（設置パートから） */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void ContinueGame();

	/** タイトル画面へ戻る（一時停止メニューから）。かくれんぼの途中なら、そのラウンドは始める前の状態に戻す */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void ReturnToTitle();

	/** 自動テスト用：タイトルを抜けて始める（セーブがあればつづきから、無ければはじめから） */
	void LeaveTitleForTest();

	// ===== セーブ =====

	/** パートが変わるたびに自動で保存し、起動時に読み込む（起動オプション -KakuNoSave で無効） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	bool bSaveEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	FString SaveSlotName = TEXT("Kakurenbo");

	// ===== 計算 =====

	/** 今のステージの設定 */
	FKakurenboStageRow GetStageSettings() const;
	/** 指定したステージの設定（表より後は伸ばした値） */
	FKakurenboStageRow GetStageSettingsFor(int32 Stage) const;

	/** 連打 1 回のコイン（強化・転生のお店・ゴールドラッシュ込み） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetMashIncome() const;

	/** 連打の強化がこのレベルのときの 1 回のコイン（転生のお店込み。ゴールドラッシュは入れない＝お店の表示用） */
	double GetMashIncomeAtLevel(int32 Level) const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetTimeIncomePerSecond() const;

	/** 時間の強化がこのレベルのときの毎秒のコイン（転生のお店込み） */
	double GetTimeIncomeAtLevel(int32 Level) const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetMashUpgradeCost() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetTimeUpgradeCost() const;

	/** 今の壁の耐久の倍率（転生のお店の「壁の硬さ」× 転生した回数で自動で硬くなる分。かけ算） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetWallHPMultiplier() const;

	/** 壁の補強の倍率（元の耐久に対して。足し算で伸びる：Lv0 = 1、Lv1 = 1.5 …）。Level を省くと今のレベル */
	double GetWallReinforceFactor(int32 Level) const;
	double GetWallReinforceFactor() const;

	/** 壁の補強を 1 つ上げる価格 */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetWallReinforceCost() const;

	/** 壁の補強を買う（置いてある壁もすぐ硬くなる） */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool TryBuyWallReinforce();

	/** その壁を今買えるか（クライミング壁は「よじ登る」を解放してから） */
	bool IsWallTypeAvailable(int32 WallTypeIndex) const;

	/** 今のステージの標準鬼の攻撃で、耐久 HP の壁が何回で壊れるか */
	int32 GetHitsToBreak(double HP) const;

	/** 転生のお店の強化レベル */
	int32 GetPrestigeLevel(EPrestigeUpgrade Upgrade) const;

	/** 転生のお店の強化の効果（Level を省くと今のレベル） */
	double GetPrestigeValue(EPrestigeUpgrade Upgrade, int32 Level) const;
	double GetPrestigeValue(EPrestigeUpgrade Upgrade) const { return GetPrestigeValue(Upgrade, GetPrestigeLevel(Upgrade)); }

	/** 1 つ上げる価格（転生ポイント。最大なら INDEX_NONE） */
	int32 GetPrestigeUpgradeCost(EPrestigeUpgrade Upgrade) const;

	/** CostOf の値段の並びで Level まで上げるのに使ったポイント（なくなった商品のポイントを返すとき） */
	int32 GetPrestigeRefund(EPrestigeUpgrade CostOf, int32 Level) const;

	/** 転生した回数で自動で硬くなる壁の耐久の倍率（Prestige.csv の WallHPPerPrestige ^ 回数。PrestigeCount を省くと今の回数） */
	double GetPrestigeAutoWallHPMultiplier(int32 PrestigeCount) const;
	double GetPrestigeAutoWallHPMultiplier() const;

	/** 転生のお店の商品一覧（並び＝EPrestigeUpgrade の順＝番号キー順） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	TArray<FShopItemView> GetPrestigeShopItems() const;

	/** 転生のお店で買う（購入パートでだけ。Index は 0 始まり＝EPrestigeUpgrade の順） */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool TryBuyPrestigeUpgrade(int32 Index);

	/** 煙幕ダッシュ・よじ登るなど、転生のお店の強化をプレイヤーに反映する */
	void ApplyPrestigeToPlayer();

	/** 煙幕ダッシュが使えるか（転生のお店で解放） */
	bool IsDashUnlocked() const { return GetPrestigeLevel(EPrestigeUpgrade::SmokeDuration) > 0; }

	/** 1 ラウンドに煙幕ダッシュを使える回数（「煙幕の数」の行は足し算：BaseValue + ValueGrowth × レベル） */
	int32 GetSmokeUsesPerRound(int32 Level) const;
	int32 GetSmokeUsesPerRound() const { return GetSmokeUsesPerRound(GetPrestigeLevel(EPrestigeUpgrade::SmokeCount)); }

	/** 煙幕が残る時間（秒）。「煙幕ダッシュ」のレベルで決まる（足し算。MinSmokeDuration〜MaxSmokeDuration に収める） */
	float GetSmokeDuration(int32 Level) const;
	float GetSmokeDuration() const { return GetSmokeDuration(GetPrestigeLevel(EPrestigeUpgrade::SmokeDuration)); }

	/** 煙幕が残る時間の下限・上限（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dash")
	float MinSmokeDuration = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dash")
	float MaxSmokeDuration = 7.f;

	/** よじ登れるか（転生のお店で解放。M14 でジャンプの代わりに） */
	bool IsClimbUnlocked() const { return GetPrestigeLevel(EPrestigeUpgrade::Climb) > 0; }

	/** よじ登るの速さの倍率（転生のお店の「よじ登る」。Lv1 = ×1、そこから ValueGrowth ずつ速くなる） */
	double GetClimbSpeedMultiplier(int32 Level) const;
	double GetClimbSpeedMultiplier() const { return GetClimbSpeedMultiplier(GetPrestigeLevel(EPrestigeUpgrade::Climb)); }

	/** このレベルから、2 段の高さ（本棚・大時計・2 段に積んだ壁）にも一度で登れる（ユーザーの希望） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb")
	int32 ClimbTwoTierLevel = 3;

	/** 一度に登れる高さ（cm）：ふつう（壁 1 段 160cm まで）・2 段（壁 2 段 320cm まで。部屋の壁は 10m あるので登れない） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb")
	float ClimbMaxHeightOneTier = 175.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb")
	float ClimbMaxHeightTwoTier = 335.f;

	bool CanClimbTwoTiers(int32 Level) const { return Level >= ClimbTwoTierLevel; }
	bool CanClimbTwoTiers() const { return CanClimbTwoTiers(GetPrestigeLevel(EPrestigeUpgrade::Climb)); }

	/** 家具によじ登るのにかかる秒数（壁は Walls.csv の ClimbSeconds） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb")
	float FurnitureClimbSeconds = 1.2f;

	/** その壁の種類によじ登る秒数（壁でなければ家具の秒数） */
	float GetClimbSeconds(int32 WallTypeIndex) const;

	/** 補強と転生を反映した壁の種類（耐久 = 元の耐久 × 補強の倍率 × 壁の硬さの倍率） */
	TArray<FWallTypeDef> GetEffectiveWallTypes() const;

	// ===== 転生 =====

	/** 今のステージで転生できるか */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	bool CanPrestige() const;

	/** 今転生したらもらえる転生ポイント（できなければ 0） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	int32 GetPrestigePointsOnReset() const;

	/** 転生した後のはじめのコイン（転生した回数 × Prestige.csv の StartCoinsPerPrestige。強化ではなく自動で増える） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetPrestigeStartCoins(int32 PrestigeCount) const;

	/**
	 * 転生する（購入パートでだけ）。転生ポイント（転生のお店で永続強化に使う）をもらう代わりに
	 * コイン・ステージ・強化・壁と罠の在庫・置いた壁と罠がなくなる（設計図は残るので、在庫を買えば設置パートで自動で直る）。
	 * ステージ 1 のかくれんぼから始め直す
	 */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool Prestige();

	// ===== 消音壁 =====

	/**
	 * プレイヤーが出す音（連打・ダッシュ）の大きさの倍率（1 = そのまま）。
	 * 壁に囲まれた空洞にいるときだけ、囲んでいる壁の NoiseDamping の平均だけ小さくなる
	 */
	float GetPlayerNoiseMultiplier() const;

	/** プレイヤーを囲んでいる壁の数（囲まれていなければ 0） */
	int32 GetPlayerEnclosureWallCount() const;

	/** プレイヤーを囲んでいる消音壁のうち、一番早く壊れるものがあと何回音を消せるか（無ければ -1） */
	int32 GetPlayerQuietWallRemaining() const;

	/** 連打 1 回・ダッシュ 1 回で消音壁の「音を消せる回数」がいくつ減るか */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	double MashSoundDamage = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	double DashSoundDamage = 3.0;

	// ===== 足音・ジャンプの音 =====

	/** 連打（クリック）の音が届く距離の倍率（1 = Stages.csv の OniHearingRadius まで）。広すぎたので少し小さく */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	float MashNoiseLoudness = 0.75f;

	/** 足音 1 歩・よじ登る 1 回の音の大きさ（連打 = 1） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	float StepNoiseLoudness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	float ClimbNoiseLoudness = 0.6f;

	/** しのび足（Ctrl を押しながら歩く）のときの足音の倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	float SneakStepMultiplier = 0.3f;

	/** 壁に囲まれた場所（空洞）にいるときの足音・よじ登る音の倍率（消音壁でなくても小さくなる） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	float EnclosedStepMultiplier = 0.2f;

	/** 足音を床の輪で見せる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	bool bShowStepRing = true;

	/** プレイヤーが 1 歩歩いた（着地も 1 歩）。かくれんぼ中だけ鬼に聞こえる */
	void HandlePlayerStep(const FVector& FeetLocation, bool bSneaking);

	/** プレイヤーがよじ登り始めた（音が出る） */
	void HandlePlayerClimb(const FVector& FeetLocation);

	/** 今の場所での足音の倍率（壁に囲まれていれば小さい） */
	float GetStepNoiseMultiplier() const;

	// ===== 探知鬼 =====

	/** 探知鬼の見張りの場所は、プレイヤーのスタート位置・門・ほかの探知鬼からこのマス数以上離す */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float DetectorGuardMinDistanceCells = 6.f;

	/** 探知鬼が最後に呼び寄せた鬼の数（テスト用） */
	int32 GetLastSummonedCount() const { return LastSummonedCount; }

	/** 探知鬼がこのおとり・迷い猫で仲間を呼んでよいか（このラウンドでまだどの探知鬼も呼んでいなければ true にして、呼んだ印をつける） */
	bool ClaimLureForSummon(AActor* Lure);

	// ===== 館のマップ =====

	/** そのステージで使うマップ（Stages.csv の Map。空なら前のステージと同じ。表より後は順番に回る） */
	FName GetMapNameForStage(int32 Stage) const;

	/** テスト用：ステージに関係なくこのマップを使う（NAME_None なら家具の無い舞台） */
	bool bMapOverride = false;
	FName MapOverride;

	/** 今のマップ */
	FName GetCurrentMapName() const { return CurrentMapName; }
	FText GetCurrentMapDisplayName() const;

	/**
	 * マップを切り替える。今のマップの設計図はとっておき（次に来たとき在庫から直る）、置いてあった壁・罠は在庫に戻す。
	 * 次のマップの設計図があれば戻す（設置パートで在庫から自動で直る）
	 */
	void SwitchToMap(FName NewMap);

	// ===== 鬼の出入り口 =====

	/** 鬼が出てくるマス（出てくる順）。壁・罠は置けない */
	TArray<FIntPoint> GetOniGateCells() const;

	/** 壁の今の価格（持っている数が増えるほど高くなる） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetWallCost(int32 WallTypeIndex) const;

	/** 持っている壁の数（在庫＋置いてある数。壊れた分は数えない） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	int32 GetOwnedWallCount(int32 WallTypeIndex) const;

	/** 罠の今の価格（持っている数が増えるほど高くなる） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetTrapCost(int32 TrapTypeIndex) const;

	/** 持っている罠の数（在庫＋置いてある数。発動して消えた分は数えない） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	int32 GetOwnedTrapCount(int32 TrapTypeIndex) const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetClearReward() const;

	/** お宝 1 個でもらえる宝石の数（転生のお店の「お宝の宝石」で増える。Level を省くと今のレベル） */
	int32 GetGemsPerTreasure(int32 Level) const;
	int32 GetGemsPerTreasure() const { return GetGemsPerTreasure(GetPrestigeLevel(EPrestigeUpgrade::Treasure)); }

	/** お宝を全部取ったボーナスの宝石。転生のお店の「お宝の宝石」で、お宝 1 個の宝石と同じ割合で増える（Lv0 は AllTreasureBonusGems） */
	int32 GetAllTreasureBonusGems(int32 Level) const;
	int32 GetAllTreasureBonusGems() const { return GetAllTreasureBonusGems(GetPrestigeLevel(EPrestigeUpgrade::Treasure)); }

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	float GetHideDuration() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetOniAttackDamage() const;

	/** 鬼の聞こえる距離の基準（ステージの OniHearingRadius × 館の大きさの倍率 GetMapNoiseScale） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	float GetOniHearingRadius() const;

	/**
	 * M14：音の届く距離の、館の大きさによる倍率。館の一辺に比例する（一辺 ÷ MapNoiseReferenceSide。16 マスで ×0.4、40 マスで ×1）。
	 * 連打・足音・ダッシュ・おとりの音が全部これで小さくなる（小さい館で連打の音が館じゅうに届いて連打できなかった）。
	 * 間取りの無い舞台（テスト用の家具の無い舞台）は 1
	 */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	float GetMapNoiseScale() const;

	/** この一辺（マス）の館で音の倍率が 1（Stages.csv の OniHearingRadius は、この大きさの館での距離） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Noise")
	int32 MapNoiseReferenceSide = 40;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	int32 GetNumOnis() const;

	/** 鬼の種類ごとの数値（表に無ければ既定値） */
	FKakurenboOniTypeRow GetOniTypeRow(EOniType Type) const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	int32 GetNumTreasures() const;

	/** 鬼の種類の数値と見た目（モデル・色違い）を鬼に設定する（FinishSpawning の前に呼ぶ。写真スタジオも使う） */
	void ConfigureOniLook(AOniCharacter* Oni, EOniType Type) const;

	/** M15：鬼の写真スタジオ（チュートリアル・図鑑の写真） */
	class AKakurenboOniStudio* GetOniStudio() const { return OniStudio; }

	/** 今いる鬼（かくれんぼ中・リザルト中のみ） */
	const TArray<TObjectPtr<AOniCharacter>>& GetOnis() const { return Onis; }

	/** 今あるお宝（かくれんぼ中のみ） */
	const TArray<TObjectPtr<ATreasureActor>>& GetTreasures() const { return Treasures; }

	/** このラウンドで取られたお宝のあったマス（宝物鬼が見回りに来る） */
	const TArray<FIntPoint>& GetTakenTreasureCells() const { return TakenTreasureCells; }

	// ===== 購入 =====

	/** 購入パートの商品一覧（表示順＝番号キー順） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	TArray<FShopItemView> GetShopItems() const;

	/** 商品をインデックス（0 始まり）で購入する */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool TryBuyShopItem(int32 Index);

	/** M15：まとめ買い。商品を Count 個（強化は Count レベル）まで、買えるだけ買う。@return 買った数 */
	int32 TryBuyShopItem(int32 Index, int32 Count);

	/** まとめ買いで、今のコインで何個・いくらで買えるか（Count 個まで。1 個も買えなければ 0 個・1 個目の値段） */
	void GetShopItemBulkPrice(int32 Index, int32 Count, int32& OutCount, double& OutCost) const;

	/** M15：宝石のお店の商品一覧（宝石で買う道具：Traps.csv の bGemItem の行） */
	TArray<FShopItemView> GetGemShopItems() const;

	/** 宝石のお店で買う（Index は 0 始まり）。@return 買った数 */
	int32 TryBuyGemShopItem(int32 Index, int32 Count = 1);

	/** 宝石のお店の Index 番目の道具は、TrapTypes の何番か（無ければ INDEX_NONE） */
	int32 GetGemShopTrapType(int32 Index) const;

	/** 宝石のお店の番号（0 始まり）。その種類が宝石のお店に無ければ INDEX_NONE */
	int32 GetGemShopIndexOfTrap(int32 TrapTypeIndex) const;

	/** その種類の道具を探す（宝石のお店の道具・罠。無ければ INDEX_NONE） */
	int32 FindTrapTypeOfKind(ETrapKind Kind) const;

	/** お店に並ぶ罠の種類（bGem = false：コインのお店の罠 / true：宝石のお店の道具）。TrapTypes の番号の並び */
	TArray<int32> GetTrapTypesInShop(bool bGem) const;

	/** 宝石のお店：その道具の今の価格（宝石） */
	int32 GetGemTrapCost(int32 TrapTypeIndex) const { return FMath::RoundToInt(GetTrapCost(TrapTypeIndex)); }

	/** その罠が宝石で買う道具か */
	bool IsGemTrap(int32 TrapTypeIndex) const { return TrapTypes.IsValidIndex(TrapTypeIndex) && TrapTypes[TrapTypeIndex].bGemItem; }

	/** その罠を設置パートで置けるか（だるまさんがころんだは置かない。かくれんぼ中に使う） */
	bool IsPlaceableTrap(int32 TrapTypeIndex) const
	{
		return TrapTypes.IsValidIndex(TrapTypeIndex) && TrapTypes[TrapTypeIndex].Kind != ETrapKind::Daruma;
	}

	// ===== ゴールドラッシュ（宝石のお店） =====

	/** ゴールドラッシュ中の連打のコインの倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	double GoldRushMashMultiplier = 1.5; // ×2 は強すぎた

	/** このかくれんぼはゴールドラッシュ中か（ゴールドラッシュを置いていれば。真っ暗ブロックと同じく 1 ラウンドで壊れる） */
	bool IsGoldRushActive() const { return bGoldRushThisRound; }

	/** デバッグ用：宝石を増やす */
	void DebugAddGems(int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool TryBuyMashUpgrade();

	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool TryBuyTimeUpgrade();

	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool TryBuyWall(int32 WallTypeIndex);

	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool TryBuyTrap(int32 TrapTypeIndex);

	/**
	 * 壊れた壁・使った罠を、直すのに足りない分だけまとめて買う。値段の高いものから、買えるだけ買う
	 * @return 買った数
	 */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	int32 BuyAllMissing();

	/** まとめて買うと何個・いくらになるか（足りない分を全部買ったとき） */
	void GetRefillPlan(int32& OutCount, double& OutCost) const;

	/** 商品の番号（0 始まり）。並び: 強化（連打・時間・壁の補強）→ 壁 → 罠（コインで買う罠だけ。宝石の道具は宝石のお店） */
	int32 GetShopIndexOfWall(int32 WallTypeIndex) const;
	int32 GetShopIndexOfTrap(int32 TrapTypeIndex) const;

	// ===== 設置 =====

	/** 設置パート：在庫の壁をマスの一番上に置けるか（置けない理由も返す） */
	bool CanPlaceWall(const FIntPoint& Cell, int32 WallTypeIndex, FText* OutReason = nullptr) const;

	/** 設置パート：在庫の壁をマスの一番上に置く */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool PlaceWall(FIntPoint Cell, int32 WallTypeIndex);

	/** 設置パート：置いた壁を回収して在庫に戻す */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool PickUpWall(APlaceableBlock* Block);

	/** 設置パート：在庫の罠を床のマスに置けるか（置けない理由も返す） */
	bool CanPlaceTrap(const FIntPoint& Cell, int32 TrapTypeIndex, FText* OutReason = nullptr) const;

	/** 設置パート：在庫の罠を置く */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool PlaceTrap(FIntPoint Cell, int32 TrapTypeIndex);

	/** 設置パート：一方通行ののれんの向きを 90 度回す（左クリックで置いてあるのれんを押したとき） */
	bool RotateTrap(FIntPoint Cell);

	/** 次に置く一方通行ののれんの向き（0 +X / 1 +Y / 2 -X / 3 -Y。鬼が通れる向き。最初は門の反対＝西） */
	int32 OneWayPlaceDir = 2;

	/** 次に置く身代わり人形の向き（0 = 東 … 3 = 北。人形はこの向きへ逃げる。置いた人形をクリックすると回る） */
	int32 FakeHiderPlaceDir = 2;

	/** 設置パート：罠を回収する（残っていれば在庫に戻す。発動済みなら設計図から消すだけ） */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool PickUpTrap(FIntPoint Cell);

	/** 設置パート：かくれんぼを始める場所（プレイヤーの位置）を、壁の無いマスへ移す */
	bool MovePlayerStart(const FIntPoint& Cell, FText* OutReason = nullptr);

	// ===== かくれんぼ =====

	/** 連打 1 回分の処理（コイン獲得＋音を出す） */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void HandleMash(const FVector& NoiseLocation);

	/** 煙幕ダッシュした（大きな音を出し、足元に煙幕を投げる）。プレイヤーのダッシュが始まったときに呼ぶ */
	void HandleDash(const FVector& NoiseLocation, float Loudness);

	/** 音を鳴らして鬼に聞かせる（Loudness 1 = 連打と同じ距離まで届く。種類によって聞こえる距離が違う。Source は鳴らした物＝おとり・迷い猫） */
	void EmitNoise(const FVector& Location, float Loudness, EKakurenboNoise Kind = EKakurenboNoise::Mash, AActor* Source = nullptr);

	/** お宝を取得する（お宝がプレイヤーに触れたときに呼ぶ） */
	void CollectTreasure(ATreasureActor* Treasure);

	/** 罠が発動した（トリモチ: 鬼が動けなくなる / おとり: 鬼に壊された）。罠はここで消える */
	void HandleTrapTriggered(ATrapActor* Trap, AOniCharacter* Oni);

	/** おとりが音を出した */
	void HandleDecoyPing(ATrapActor* Trap);

	// ===== 宝石のお店の道具（M15） =====

	/** 真っ暗ブロック：画面の明るさを下げる量（EV） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	float DarknessExposureBias = -1.8f;

	/** 鬼の勘（OniTypes.csv の Intuition）を使うか。自動テストは Intuition シナリオ以外では切る（鬼の動きが変わらないように） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	bool bOniIntuition = true;

	/** だるまさんがころんだ：使った瞬間に、プレイヤーの体が向いている方からこの角度（度）以内・この距離（cm）以内に見えている鬼が止まる（カメラの向きではない） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	float DarumaViewHalfAngle = 38.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	float DarumaViewDistance = 3000.f;


	/** 身代わり人形が逃げながら出す大きな音（連打 = 1） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	float FakeHiderNoiseLoudness = 1.3f;

	/** 身代わり人形が逃げている間、何秒ごとに音を出すか */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	float FakeHiderNoiseInterval = 0.7f;

	/** 迷い猫・身代わり人形などの道具が音を出した（その場所から鬼に聞こえる） */
	void HandleGadgetNoise(ATrapActor* Trap, float Loudness, EKakurenboNoise Kind);

	/** 身代わり人形が鬼に捕まった（ポンと消える。鬼は「にせもの！」でしばらく見回す）。人形はここで消える */
	void HandleFakeHiderCaught(ATrapActor* Trap, AOniCharacter* Oni);

	/** のれんが壊れた（鬼に壊された・通られすぎた）。ここで消える */
	void HandleOneWayBroken(ATrapActor* Trap);

	/** 鬼の攻撃がのれんに当たった（耐久が 0 になったら破れる） */
	void HandleGateHit(ATrapActor* Trap, double Damage);

	/** のれんの耐久 ＝ 通常壁の耐久（補強・硬さを含む）× これ（M15：ユーザーの希望で 2 倍） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	double OneWayHPMultiplier = 2.0;

	/** 今ののれんの耐久 */
	double GetOneWayMaxHP() const;

	/** 今のラウンドで、館全体が真っ暗か（真っ暗ブロックを置いている） */
	bool IsMansionDark() const { return bMansionDark; }

	/** 真っ暗のとき、鬼が見える距離（cm。真っ暗ブロックの Radius） */
	float GetDarkSightRadius() const { return DarkSightRadius; }

	/** プレイヤーと同じ見た目にする（身代わり人形にも使う。Tint は模様に掛ける色） */
	void ApplyPlayerLookTo(AHiderCharacter* Hider, const FLinearColor& Tint = FLinearColor::White);

	/** 身代わり人形：プレイヤー・鬼・のれんとぶつからないようにする（鬼とは体が重なれる＝触れたら捕まる判定は距離で見る） */
	void RegisterFakeHiderCollision(AHiderCharacter* Fake);

	/** 身代わり人形（今いるもの） */
	TArray<class AHiderCharacter*> GetFakeHiders() const;

	/** だるまさんがころんだ：今使えるか・使う（在庫を 1 つ使う）・効いている残りの秒数 */
	bool CanUseDaruma() const;
	bool UseDaruma();
	float GetDarumaTimeLeft() const { return DarumaTimeLeft; }
	bool IsDarumaActive() const { return DarumaTimeLeft > 0.f; }

	/** 連打できるか（かくれんぼ中で、始めの「もういいかい」の数え終わった後だけ。準備の間は連打できない） */
	bool CanMash() const;

	/** その鬼がプレイヤーの視界（体が向いている方・DarumaViewHalfAngle 度以内・DarumaViewDistance 以内・間に壁が無い）にいるか */
	bool IsOniInPlayerView(const AOniCharacter* Oni) const;

	/** だるまさんがころんだで止まっている鬼か（使った瞬間に視界にいた鬼は、効いている間ずっと動けない。目をそらしても止まったまま） */
	bool IsOniDarumaFrozen(const AOniCharacter* Oni) const;

	/** 今のだるまさんがころんだで止めた鬼の数 */
	int32 GetDarumaFrozenCount() const { return IsDarumaActive() ? DarumaFrozenOnis.Num() : 0; }



	/** パワー鬼が、抜け出した直後にトリモチを踏んで壊した。罠はここで消える */
	void HandleTrapDisarmed(ATrapActor* Trap, AOniCharacter* Oni);

	/** 迷い猫に鬼が当たった：壊れた壁と同じ扱いで、その場で在庫から外れる（設計図は残る）。猫はラウンドの終わりまで鬼の頭の上にいる */
	void HandleCatTaken(ATrapActor* Cat, AOniCharacter* Oni);

	// ===== パート遷移 =====

	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void StartShopPhase();

	/** 設置パートへ。壊れた壁は在庫があれば自動で修復する */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void StartBuildPhase();

	/**
	 * 設置パート：置いた壁・罠をすべて回収する（今あるものは在庫に戻し、設計図も消す。壊れて無くなっていた分は戻らない）。
	 * @return 在庫に戻した数
	 */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	int32 ClearAllPlaced();

	/** 設置パートから購入パートへ戻る（置いた壁・罠はそのまま） */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void ReturnToShop();

	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void StartHidePhase();

	/** かくれんぼ終了。bCleared=true なら逃げ切り、false なら見つかった */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void EndHidePhase(bool bCleared);

	/** 「次へ」操作。今のパートから次のパートへ進める */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void AdvancePhase();

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	EKakurenboPhase GetPhase() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	AKakurenboArena* GetArena() const { return Arena; }

	// ===== セーブ・ロード =====

	/** 今の状態を保存する（bSaveEnabled のときだけ） */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool SaveProgress();

	/** 保存した状態を読み込んで反映する。セーブが無ければ false */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool LoadProgress();

	/** セーブを消して、最初（ステージ 1・何もない状態）からやり直す */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void ResetProgress(bool bShowNotice = true);

	/**
	 * 版 5 までのセーブの壁の種類（0 木 / 1 石 / 2 鉄 / 3 消音壁）を今の種類（0 通常壁 / 1 消音壁）に直す。
	 * 在庫は足し合わせ、置き方（設計図・残っていた壁）は種類の番号を付け替える（単体テストあり）
	 */
	static void RemapLegacyWallTypes(TArray<int32>& Stock, TArray<struct FKakurenboSavedColumn>& Columns);
	static int32 RemapLegacyWallType(int32 OldType);

	/** 画面上部に一時的なお知らせを出す */
	void ShowNotice(const FText& Text, float Seconds = 5.f);

	/** デバッグ用：コインを増やす */
	void DebugAddCoins(double Amount);

	/** デバッグ用：残り時間を減らす */
	void DebugSkipTime(float Seconds);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	/** PlayerStart が置かれていないレベルでは舞台の中央に出現させる */
	virtual void RestartPlayer(AController* NewPlayer) override;

	void SetPhase(EKakurenboPhase NewPhase);
	/** タイトル画面にする（鬼・お宝は片付ける。保存はしない） */
	void EnterTitle();
	bool bHasContinueData = false;
	AKakurenboGameState* GS() const;
	ACharacter* GetPlayerCharacter() const;

	/** CSV（または DataTable アセット）から数値を読み込んで反映する */
	void LoadBalanceData();

	void SpawnOnis();
	void DespawnOnis();
	void HandleOniFoundHider();
	void HandleOniDestroyedWalls(int32 Count);
	/** 探知鬼がプレイヤーを見つけた：周りの鬼を呼び寄せる */
	void HandleOniSummon(AOniCharacter* Caller, const FVector& TargetLocation);

	/** 今のマップの家具・部屋の壁を舞台とグリッドに反映する */
	void ApplyCurrentMap();
	/** 間取りのファイルを読む（無ければ全部床）。OutFloorLayer があれば床の種類（[floor]）も */
	KakurenboMaps::FLayout LoadMapLayout(const FKakurenboMapRow& Row, KakurenboMaps::FLayout* OutFloorLayer = nullptr) const;
	/** 家具の上に壁・罠が残っていたら在庫に戻して消す（古いセーブ・間取りを変えたとき） */
	void ClearLayoutOnObstacles();
	/** 今の館の外になったマスの壁・罠を在庫に戻して、列から取り除く */
	void RefundColumnsOutsideGrid(TArray<struct FKakurenboSavedColumn>& Columns);
	/** プレイヤーが家具の中や囲まれた場所にいたら、広い場所のマスへ移す */
	void EnsurePlayerOnFreeCell();

	/** 壁が攻撃された（破片を飛ばして音を鳴らす） */
	void HandleBlockHit(const FVector& Location, const FLinearColor& Color, bool bDestroyed);

	void SpawnTreasures();
	void ClearTreasures();

	/** 壊れた壁を設計図どおりに在庫から直す */
	void RepairWalls();

	/** 発動して消えた罠を設計図どおりに在庫から置き直す */
	void RefillTraps();

	/** 開始前のカウントダウン・残り時間の秒読みの音 */
	void TickCountdownSounds();
	void TickDangerHeartbeat(float DeltaSeconds);
	/** かくれんぼ中、鬼に追われている間は追われている間の BGM にする */
	void TickChaseMusic(float DeltaSeconds);
	/** パートに合う BGM（タイトル・かくれんぼ・それ以外） */
	EKakurenboMusic GetPhaseMusic(EKakurenboPhase Phase) const;

	/** 画面の点滅・大きな文字（HUD に頼む） */
	void FlashScreen(const FLinearColor& Color, float Duration) const;
	void ShowPopup(const FString& Text, const FLinearColor& Color) const;

	/** プレイヤーの体がそのマスの範囲（縦方向は問わない）に入っているか */
	bool IsPlayerInCellColumn(const FIntPoint& Cell) const;

	/** プレイヤーが出す音の輪（届く範囲）を床に出す */
	void ShowNoiseRing(const FVector& Location, float Loudness, const FLinearColor& Color, float Thickness) const;

	/**
	 * プレイヤーが音を出す（連打・ダッシュ）。消音壁に囲まれていれば小さくなり、その消音壁の「音を消せる回数」が SoundDamage 減る
	 * @return 鬼に届いた音の大きさ
	 */
	float MakePlayerNoise(const FVector& Location, float Loudness, double SoundDamage, const FLinearColor& RingColor, float RingThickness, EKakurenboNoise Kind);

	/** 鬼の見た目のメッシュを読み込む（設定が無ければ何もしない） */
	void LoadOniAppearance();

	/** 読み込んだ家具（間取りの 1 文字 → 家具） */
	TMap<TCHAR, FKakurenboFurnitureRow> FurnitureRows;

	/** 今のマップ（Maps.csv の行の名前） */
	FName CurrentMapName;

	/** 今ではないマップの設計図（壁・罠の置き方。そのマップに戻ったとき在庫から直す） */
	UPROPERTY()
	TMap<FName, FKakurenboSavedLayout> StoredMapLayouts;

	int32 LastSummonedCount = 0;
	/** このラウンドで探知鬼が仲間を呼んだおとり・迷い猫（1 つにつき 1 回だけ呼ぶ） */
	TArray<TWeakObjectPtr<AActor>> SummonedLures;

	/** 読み込んだ鬼の見た目（メッシュが無ければ空＝円柱のまま） */
	UPROPERTY()
	FKakurenboOniLook LoadedOniLook;

	/** 鬼の種類ごとの色違いのマテリアル（読み込んだもの） */
	UPROPERTY()
	TMap<EOniType, TObjectPtr<UMaterialInterface>> LoadedOniMaterials;

	UPROPERTY()
	TObjectPtr<AKakurenboArena> Arena;

	UPROPERTY()
	TObjectPtr<class AKakurenboOniStudio> OniStudio;

	UPROPERTY()
	TArray<TObjectPtr<AOniCharacter>> Onis;

	UPROPERTY()
	TArray<TObjectPtr<ATreasureActor>> Treasures;
	TArray<FIntPoint> TakenTreasureCells;

	/** CSV から作った一時的な DataTable（GC で消えないように持っておく） */
	UPROPERTY()
	TArray<TObjectPtr<UDataTable>> LoadedTables;

	bool bOnisSpawnedThisRound = false;
	FRandomStream TreasureRandom;

	/** 最後に音を鳴らしたカウントダウン・秒読みの秒数（同じ秒で何度も鳴らさないため） */
	int32 LastCountdownSecond = 0;
	int32 LastTimeTickSecond = 0;
	float CurrentDanger = 0.f;
	float CurrentCloseness = 0.f;
	float HeartbeatTimer = 0.f;
	/** 最後に鬼に追われていた時からの秒数（追われている間の BGM をいつ戻すか） */
	float ChaseMusicTimer = 0.f;
	int32 HeartbeatCount = 0;
	float LastHeartbeatVolume = 0.f;

	/** 真っ暗ブロック：このラウンドは館全体が暗い */
	bool bMansionDark = false;
	float DarkSightRadius = 300.f;
	/** 館を暗くする画面の効果（ラウンドの間だけ） */
	UPROPERTY()
	TObjectPtr<class APostProcessVolume> DarknessVolume;
	void SetMansionDark(bool bDark, float SightRadius);

	/** だるまさんがころんだ */
	float DarumaTimeLeft = 0.f;
	bool bGoldRushThisRound = false;
	void TickDaruma(float DeltaSeconds);

	/** だるまさんがころんだを使った瞬間に視界にいた鬼（効いている間は動けない） */
	TArray<TWeakObjectPtr<AOniCharacter>> DarumaFrozenOnis;

	/** そのラウンドの道具（真っ暗ブロック）を、ラウンドの終わりに使い切る */
	void ConsumeRoundGadgets();

};
