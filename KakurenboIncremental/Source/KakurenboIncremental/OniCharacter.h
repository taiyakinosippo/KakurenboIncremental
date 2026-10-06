// 鬼。グリッド上の A* で移動する。
//
// 状態遷移:
//   Wander（うろうろ）
//     ├─ 連打の音を聞く ─────▶ Investigate（音のした方へ）
//     │                          └─ 着いて見回しても何もない／InvestigateMaxDuration たつ ─▶ Wander
//     └─ プレイヤーが見える ─▶ Chase（追いかける）
//                                └─ LoseSightDuration 見失う／ChaseMaxDuration たつ ─▶ Wander
//                                   （時間切れで諦めた直後は GiveUpSightCooldown のあいだ視線に反応しない）
//   進路に壁 ─▶ Attack（範囲攻撃で壊す）─▶ 元の状態へ
//
//   プレイヤーに「ぶつかったら」発見（＝プレイヤーの負け）。見えただけでは負けにならない。

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

	// ===== 移動（GameMode がステージに応じて設定する） =====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Move")
	float WanderSpeed = 420.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Move")
	float InvestigateSpeed = 700.f;

	/** 追いかけるときの速さ（プレイヤー 420 の 2 倍） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Move")
	float ChaseSpeed = 840.f;

	// ===== 感覚 =====

	/** 視界の距離（cm）。見えたら追いかける */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float SightRadius = 900.f;

	/** 視界の半角（度）。40 なら正面 80 度が見える */
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

	/** 音を調べに行ってから、この時間で諦めてうろうろに戻る（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float InvestigateMaxDuration = 8.f;

	/** 追いかけ始めてから、この時間で諦めてうろうろに戻る（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float ChaseMaxDuration = 10.f;

	/** 追いかけている相手がこの時間見えなかったら見失ったとみなす（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float LoseSightDuration = 2.f;

	/** 追跡を時間切れで諦めた直後、この時間は見えても追いかけない（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float GiveUpSightCooldown = 3.f;

	// ===== 攻撃 =====

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

	/** 経路などをデバッグ表示する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Debug")
	bool bDrawDebug = false;

	// ===== 外から呼ぶ =====

	/** 行動開始（スポーン後に GameMode が呼ぶ） */
	void Activate(AHiderCharacter* InTarget);

	/** 行動停止（ラウンド終了時） */
	void Deactivate();

	/** 音を聞かせる。聞こえる距離なら音の方へ向かう（追いかけている間は無視） */
	void HearNoise(const FVector& NoiseLocation, float Loudness = 1.f);

	EOniState GetOniState() const { return State; }

	/** 攻撃中も含めた「今何をしようとしているか」（Wander / Investigate / Chase） */
	EOniState GetIntent() const { return State == EOniState::Attack ? StateBeforeAttack : State; }

	/** 何でプレイヤーを見つけたか（"touch" = ぶつかった / None = まだ） */
	FName GetFoundReason() const { return FoundReason; }

	/** 直近の視界チェックでプレイヤーが見えていたか */
	bool IsTargetInSight() const { return bTargetInSight; }

	FOnOniFoundHider OnFoundHider;
	FOnOniDestroyedWalls OnDestroyedWalls;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	enum class EFollowResult : uint8 { Moving, Arrived, Attacking, Failed };

	// 感覚
	bool CanSeeTarget() const;
	bool IsTouchingTarget() const;
	void FoundTarget(FName Reason);

	// 状態
	void SetState(EOniState NewState);
	void StartInvestigate(const FIntPoint& Cell);
	void StartChase();
	void ReturnToWander(bool bWithSightCooldown);
	void TickWander(float DeltaSeconds);
	void TickInvestigate(float DeltaSeconds);
	void TickChase(float DeltaSeconds);

	// 移動
	bool RequestPathTo(const FIntPoint& Goal, bool bAllowWalls);
	EFollowResult FollowPath(float DeltaSeconds, bool bAllowWalls);
	void PickWanderTarget();
	void FaceTowards(const FVector& Location, float DeltaSeconds, float DegreesPerSecond);
	FIntPoint ClampToGrid(const FIntPoint& Cell) const;

	// 攻撃
	void BeginAttack(const FIntPoint& WallCell);
	void TickAttack(float DeltaSeconds);
	void ResetBodyScale();

	void DrawDebug() const;

	UKakurenboGridSubsystem* Grid() const;

	UPROPERTY()
	TObjectPtr<AHiderCharacter> Target;

	EOniState State = EOniState::Wander;
	EOniState StateBeforeAttack = EOniState::Wander;
	bool bActive = false;
	FName FoundReason = NAME_None;

	TArray<FIntPoint> Path;
	int32 PathIndex = 0;
	FIntPoint GoalCell = FIntPoint::ZeroValue;
	bool bHasGoal = false;
	int32 PathGridVersion = -1;
	float RepathTimer = 0.f;

	/** Investigate / Chase を始めてからの時間（攻撃中も数える） */
	float IntentElapsed = 0.f;
	float LostSightTimer = 0.f;
	float SightCooldown = 0.f;
	bool bTargetInSight = false;
	float ChaseRepathTimer = 0.f;

	/** 最後にプレイヤーが見えた場所（見えない間はここへ向かう） */
	FVector LastKnownTargetLocation = FVector::ZeroVector;

	/** 音を聞いて向かっている先（Investigate 中） */
	FIntPoint NoiseCell = FIntPoint::ZeroValue;
	float SearchTimer = 0.f;
	float IdleTimer = 0.f;

	/** Attack 中 */
	FIntPoint AttackCell = FIntPoint::ZeroValue;
	float AttackTimer = 0.f;
	bool bAttackFired = false;

	/** 引っかかり検出 */
	FVector LastProgressLocation = FVector::ZeroVector;
	float StuckTimer = 0.f;

	float SenseTimer = 0.f;
};
