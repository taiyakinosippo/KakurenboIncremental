// 鬼。グリッド上の A* で移動し、連打の音に寄ってきて、視界に入ったプレイヤーを見つける。
// 進路上の壁は範囲攻撃で壊す。
//
// 状態遷移:
//   Wander（うろうろ）──音を聞く──▶ Investigate（音のした方へ）──着いて何もない──▶ Wander
//        └──────進路に壁──────▶ Attack（壁を壊す）──壊し終わる──▶ 元の状態へ
//   どの状態でも、プレイヤーが見えたら、またはプレイヤーにぶつかったら即「発見」（＝プレイヤーの負け）

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "KakurenboTypes.h"
#include "OniCharacter.generated.h"

class AHiderCharacter;
class UKakurenboGridSubsystem;
class USpotLightComponent;
class UStaticMeshComponent;

DECLARE_MULTICAST_DELEGATE(FOnOniFoundHider);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnOniDestroyedWalls, int32 /*Count*/);

UCLASS()
class KAKURENBOINCREMENTAL_API AOniCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AOniCharacter();

	// ===== 見た目 =====

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Oni")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** 顔の向きがわかる目印 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Oni")
	TObjectPtr<UStaticMeshComponent> FaceMesh;

	/** 視界を表す懐中電灯（角度・距離は視界と同じ） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Oni")
	TObjectPtr<USpotLightComponent> Flashlight;

	// ===== 能力（GameMode がステージに応じて設定する） =====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Move")
	float WanderSpeed = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Move")
	float InvestigateSpeed = 380.f;

	/** 視界の距離（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float SightRadius = 900.f;

	/** 視界の半角（度）。45 なら正面 90 度が見える */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float SightHalfAngle = 40.f;

	/** この距離以内なら向きに関係なく気づく（ただし壁越しは不可） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float CloseSenseRadius = 150.f;

	/** 体（カプセル）同士がこの距離まで近づいたら「ぶつかった」とみなして発見する（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float TouchMargin = 8.f;

	/** 連打の音が聞こえる距離（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float HearingRadius = 1200.f;

	/** 音の方向の誤差（マス）。遠くの音ほど大きくずれる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float NoiseInaccuracyCells = 3.f;

	/** 音のした場所に着いてから見回す時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float SearchDuration = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Attack")
	double AttackDamage = 1.0;

	/** 範囲攻撃の半径（cm）。鬼の中心からこの距離にあるブロックに当たる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Attack")
	float AttackRadius = 160.f;

	/** 攻撃の溜め時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Attack")
	float AttackWindup = 0.8f;

	/** 攻撃後の硬直（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Attack")
	float AttackRecovery = 0.4f;

	/** 経路探索で「攻撃 1 回」を何マスぶんの回り道と同じとみなすか */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Attack")
	float PathCostPerAttack = 4.f;

	/** 経路や視界をデバッグ表示する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Debug")
	bool bDrawDebug = false;

	// ===== 外から呼ぶ =====

	/** 行動開始（スポーン後に GameMode が呼ぶ） */
	void Activate(AHiderCharacter* InTarget);

	/** 音を聞かせる。聞こえる距離なら音の方へ向かう */
	void HearNoise(const FVector& NoiseLocation, float Loudness = 1.f);

	EOniState GetOniState() const { return State; }

	/** 何でプレイヤーを見つけたか（"sight" = 視線 / "touch" = ぶつかった / None = まだ） */
	FName GetFoundReason() const { return FoundReason; }

	FOnOniFoundHider OnFoundHider;
	FOnOniDestroyedWalls OnDestroyedWalls;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	// 感覚
	bool CanSeeTarget() const;
	bool IsTouchingTarget() const;
	void FoundTarget(FName Reason);

	// 移動
	void SetState(EOniState NewState);
	bool RequestPathTo(const FIntPoint& Goal, bool bAllowWalls);
	void FollowPath(float DeltaSeconds);
	void PickWanderTarget();
	void FaceTowards(const FVector& Location, float DeltaSeconds, float DegreesPerSecond);

	// 攻撃
	void BeginAttack(const FIntPoint& WallCell);
	void TickAttack(float DeltaSeconds);

	void DrawDebug() const;

	UKakurenboGridSubsystem* Grid() const;

	UPROPERTY()
	TObjectPtr<AHiderCharacter> Target;

	EOniState State = EOniState::Wander;
	bool bActive = false;
	FName FoundReason = NAME_None;

	TArray<FIntPoint> Path;
	int32 PathIndex = 0;
	FIntPoint GoalCell = FIntPoint::ZeroValue;
	bool bHasGoal = false;
	int32 PathGridVersion = -1;
	float RepathTimer = 0.f;

	/** 音を聞いて向かっている先（Investigate 中） */
	FIntPoint NoiseCell = FIntPoint::ZeroValue;
	float SearchTimer = 0.f;
	float IdleTimer = 0.f;

	/** Attack 中 */
	FIntPoint AttackCell = FIntPoint::ZeroValue;
	float AttackTimer = 0.f;
	bool bAttackFired = false;
	EOniState StateBeforeAttack = EOniState::Wander;

	/** 引っかかり検出 */
	FVector LastProgressLocation = FVector::ZeroVector;
	float StuckTimer = 0.f;

	float SenseTimer = 0.f;
};
