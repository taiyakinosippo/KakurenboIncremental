// 鬼。グリッド上の A* で移動する。種類（EOniType）によって速さ・壁を壊す力・探し方が違う。
//
// 状態遷移:
//   Wander（うろうろ。探し方は種類ごと）
//     ├─ 連打の音を聞く ──────▶ Investigate（音のした方へ）─ 着いて見回しても何もない／時間切れ ─▶ Wander
//     ├─ プレイヤーが見える ──▶ Chase（追いかける）─ 見失う／時間切れ ─▶ Wander
//     └─ 空洞・見つけた壁 ──▶ Inspect（調べに行く・壊して入る）─ 着いて見回す／時間切れ ─▶ Wander
//   進路に壁 ─▶ Attack（壊す）─▶ 元の状態へ
//   罠にかかる ─▶ Stunned（動けない。この間はプレイヤーに触れても捕まえられない）─▶ Wander
//
// 移動のルール:
//   まず壁を「通れない」ものとして経路を探す（入り口があればそこから入る）。
//   経路が無いとき（壁で完全に囲まれている・相手が壁の上にいる）だけ、壁を壊す経路を使い、目の前の壁から壊す。
//
//   プレイヤーに「ぶつかったら」発見（＝プレイヤーの負け）。見えただけでは負けにならない。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "KakurenboTypes.h"
#include "OniCharacter.generated.h"

class AHiderCharacter;
class UKakurenboGridSubsystem;
class UKakurenboOniBlackboard;
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

	/** 罠で動けない間、頭の上を回る星 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Oni")
	TArray<TObjectPtr<UStaticMeshComponent>> StunStars;

	// ===== 種類 =====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Type")
	EOniType OniType = EOniType::Balanced;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Type")
	FText TypeDisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Type")
	FLinearColor BodyColor = FLinearColor(0.9f, 0.1f, 0.08f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Type")
	float BodyScaleMultiplier = 1.f;

	// ===== 移動（GameMode が種類とステージに応じて設定する） =====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Move")
	float WanderSpeed = 420.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Move")
	float InvestigateSpeed = 700.f;

	/** 追いかけるときの速さ（標準の鬼はプレイヤー 420 の 2 倍） */
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

	/** 連打の音が聞こえる距離（cm）。0 なら音を気にしない */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float HearingRadius = 1200.f;

	/** 音の方向の誤差（マス）。遠くの音ほど大きくずれる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float NoiseInaccuracyCells = 3.f;

	/** 調べに来た場所で見回す時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float SearchDuration = 2.5f;

	/** 音を調べに行ってから、この時間で諦めてうろうろに戻る（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float InvestigateMaxDuration = 8.f;

	/** 空洞・壁を調べに行ってから、この時間で諦めてうろうろに戻る（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float InspectMaxDuration = 20.f;

	/** 追いかけ始めてから、この時間で諦めてうろうろに戻る（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float ChaseMaxDuration = 10.f;

	/** 追いかけている相手がこの時間見えなかったら見失ったとみなす（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float LoseSightDuration = 2.f;

	/** 追跡を時間切れで諦めた直後、この時間は見えても追いかけない（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float GiveUpSightCooldown = 3.f;

	/** うろうろ中、壁に囲まれた空洞があればそこを調べに行く確率（0〜1） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float PocketInspectChance = 0.5f;

	/** 慎重鬼：調べたマスを覚えておく時間（秒）。これより前に調べたマスはまた調べる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float CarefulMemorySeconds = 45.f;

	/** 罠から抜け出した後、この時間は罠にかからない（秒。続けて踏んでも動けないままにならないように） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float StunImmunitySeconds = 3.f;

	// ===== 攻撃 =====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Attack")
	double AttackDamage = 1.0;

	/** true なら目の前の壁 1 個だけを壊す（範囲攻撃しない） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Attack")
	bool bSingleTargetAttack = false;

	/** 範囲攻撃の半径（cm）。鬼の中心からこの距離にあるブロックに当たる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Attack")
	float AttackRadius = 160.f;

	/** 攻撃の溜め時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Attack")
	float AttackWindup = 0.8f;

	/** 攻撃後の硬直（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Attack")
	float AttackRecovery = 0.4f;

	/** 経路探索で「攻撃 1 回」を何マスぶんの回り道と同じとみなすか（壁を壊す経路を選ぶときに使う） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Attack")
	float PathCostPerAttack = 4.f;

	/** 経路などをデバッグ表示する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Debug")
	bool bDrawDebug = false;

	// ===== 外から呼ぶ =====

	/** 種類ごとの数値（OniTypes.csv の行）を反映する。スポーン直後、Activate より前に呼ぶ */
	void ApplyTypeSettings(EOniType Type, const FKakurenboOniTypeRow& Row);

	/** 行動開始（スポーン後に GameMode が呼ぶ） */
	void Activate(AHiderCharacter* InTarget);

	/** 行動停止（ラウンド終了時） */
	void Deactivate();

	/** 音を聞かせる。聞こえる距離なら音の方へ向かう（追いかけている間・音を気にしない種類は無視） */
	void HearNoise(const FVector& NoiseLocation, float Loudness = 1.f);

	/** 罠などで動けなくする */
	void Stun(float Seconds);

	/** 今、罠にかかるか（動いていて、動けない最中でも抜け出した直後でもない） */
	bool CanBeStunned() const { return bActive && State != EOniState::Stunned && StunImmunityTimer <= 0.f; }

	/** 行動中か（出てきてから、ラウンドが終わるかプレイヤーを見つけるまで） */
	bool IsActive() const { return bActive; }

	EOniState GetOniState() const { return State; }

	/** 攻撃中も含めた「今何をしようとしているか」（Wander / Investigate / Chase / Inspect / Stunned） */
	EOniState GetIntent() const { return State == EOniState::Attack ? StateBeforeAttack : State; }

	/** 何でプレイヤーを見つけたか（"touch" = ぶつかった / None = まだ） */
	FName GetFoundReason() const { return FoundReason; }

	/** 直近の視界チェックでプレイヤーが見えていたか */
	bool IsTargetInSight() const { return bTargetInSight; }

	/** 最後に追いかけ始めたワールド時刻（HUD の「！」表示用。まだなら負の値） */
	float GetChaseStartTime() const { return ChaseStartTime; }

	/** 調べに行っている（または壊している）マス（Inspect 中のみ意味がある） */
	FIntPoint GetInspectCell() const { return InspectCell; }

	FOnOniFoundHider OnFoundHider;
	FOnOniDestroyedWalls OnDestroyedWalls;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	enum class EFollowResult : uint8 { Moving, Arrived, Attacking, Failed };
	enum class EPathMode : uint8
	{
		WalkOnly,      // 壁は通れない（入り口から入る）
		BreakIfNeeded, // 壁を通れない経路が無いときだけ、壁を壊す経路を使う
	};

	// 感覚
	bool CanSeeTarget() const;
	bool IsTouchingTarget() const;
	void FoundTarget(FName Reason);
	/** 見えている壁のうち一番近いマス（パワー鬼が壊しに行く） */
	bool FindVisibleWallCell(FIntPoint& OutCell) const;
	void UpdateMemory();

	// 状態
	void SetState(EOniState NewState);
	void StartInvestigate(const FIntPoint& Cell);
	void StartChase();
	void StartInspect(const FIntPoint& Cell);
	void ReturnToWander(bool bWithSightCooldown);
	void TickWander(float DeltaSeconds);
	void TickInvestigate(float DeltaSeconds);
	void TickInspect(float DeltaSeconds);
	void TickChase(float DeltaSeconds);
	void TickStunned(float DeltaSeconds);

	/** その場で左右を見渡す（くるくる回らないよう、元の向きを中心に往復する） */
	void BeginLookAround();
	void TickLookAround(float DeltaSeconds, float AmplitudeDegrees);

	// うろうろの行き先（種類ごと）
	bool ChooseWanderTarget();
	bool ChooseRandomTarget(int32 MaxDistanceCells);
	bool ChooseScoutTarget();
	bool ChooseCarefulTarget();
	bool TryInspectPocket();

	// 移動
	bool RequestPathTo(const FIntPoint& Goal, EPathMode Mode);
	EFollowResult FollowPath(float DeltaSeconds);
	void FaceTowards(const FVector& Location, float DeltaSeconds, float DegreesPerSecond);
	FIntPoint ClampToGrid(const FIntPoint& Cell) const;
	FIntPoint GetCurrentCell() const;

	// 攻撃
	void BeginAttack(const FIntPoint& WallCell);
	void TickAttack(float DeltaSeconds);
	void ResetBodyScale();

	void DrawDebug() const;

	UKakurenboGridSubsystem* Grid() const;
	UKakurenboOniBlackboard* Blackboard() const;

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
	EPathMode CurrentPathMode = EPathMode::WalkOnly;
	int32 PathGridVersion = -1;
	float RepathTimer = 0.f;

	/** Investigate / Chase / Inspect を始めてからの時間（攻撃中も数える） */
	float IntentElapsed = 0.f;
	float LostSightTimer = 0.f;
	float SightCooldown = 0.f;
	bool bTargetInSight = false;
	float ChaseRepathTimer = 0.f;
	float ChaseStartTime = -1.f;

	/** 最後にプレイヤーが見えた場所（見えない間はここへ向かう） */
	FVector LastKnownTargetLocation = FVector::ZeroVector;

	/** 音を聞いて向かっている先（Investigate 中） */
	FIntPoint NoiseCell = FIntPoint::ZeroValue;
	/** 調べに行っている先（Inspect 中） */
	FIntPoint InspectCell = FIntPoint::ZeroValue;

	/** 見回し（その場で左右を見る）・待ち時間 */
	float SearchTimer = 0.f;
	float IdleTimer = 0.f;
	float LookAroundBaseYaw = 0.f;
	float LookAroundTime = 0.f;

	/** パワー鬼：見えている壁を探す間隔 */
	float WallHuntTimer = 0.f;

	/** スピード鬼：区画（RegionSize×RegionSize マス）ごとの最後に訪れた時刻 */
	static constexpr int32 RegionSize = 4;
	TArray<float> RegionVisitTime;

	/** Attack 中 */
	FIntPoint AttackCell = FIntPoint::ZeroValue;
	float AttackTimer = 0.f;
	bool bAttackFired = false;

	/** Stunned 中 */
	float StunTimer = 0.f;
	/** 罠から抜け出した後、罠にかからない残り時間 */
	float StunImmunityTimer = 0.f;
	void SetStunStarsVisible(bool bVisible);

	/** 引っかかり検出 */
	FVector LastProgressLocation = FVector::ZeroVector;
	float StuckTimer = 0.f;

	float SenseTimer = 0.f;
	FVector BaseBodyScale = FVector(0.76f, 0.76f, 1.9f);
};
