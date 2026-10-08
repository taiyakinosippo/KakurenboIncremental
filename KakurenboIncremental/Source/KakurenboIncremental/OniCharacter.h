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
//
// 種類ごとの探し方（うろうろの行き先）:
//   標準: ランダム / スピード: 長く行っていない区画 / パワー: 近場・見えた壁を壊しに行く
//   慎重: 担当の区画の、まだ誰も見ていない広い場所から（細い道と壁に囲まれた場所は後回し）。見渡したマスを仲間と共有
//   宝物: お宝の周りをぐるぐる回る / 探知: 決まった場所へ行ったら動かず見張り、見つけたら周りの鬼を呼ぶ
//
// 歩くと足音（どたどた）がその場所から聞こえる。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "KakurenboTypes.h"
#include "OniCharacter.generated.h"

class AHiderCharacter;
class UAnimInstance;
class UKakurenboGridSubsystem;
class UKakurenboOniBlackboard;
class USkeletalMesh;
class USpotLightComponent;
class UStaticMeshComponent;

class UAnimSequence;
class UMaterialInterface;
class USkeletalMeshComponent;

/** 鬼をキャラクターのモデル（スケルタルメッシュ）で表示するときの設定。Mesh が空なら円柱のまま */
USTRUCT()
struct FKakurenboOniLook
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<USkeletalMesh> Mesh;

	/** アニメーション BP（あればこちらを使う） */
	UPROPERTY()
	TSubclassOf<UAnimInstance> AnimClass;

	/** アニメーション BP が無いときに直接再生するアニメーション */
	UPROPERTY()
	TObjectPtr<UAnimSequence> Idle;

	UPROPERTY()
	TObjectPtr<UAnimSequence> Run;

	UPROPERTY()
	TObjectPtr<UAnimSequence> Attack;

	UPROPERTY()
	TObjectPtr<UAnimSequence> Win;

	UPROPERTY()
	TObjectPtr<UAnimSequence> Stunned;

	/** 体に重ねる小物（同じ骨で動く） */
	UPROPERTY()
	TObjectPtr<USkeletalMesh> Accessory;

	/** 種類ごとの色違い（無ければメッシュのまま） */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> Material;

	float Scale = 1.f;
	float ZOffset = 0.f;
	float Yaw = -90.f;
	/** 走るアニメーションが等速に見える速さ（cm/秒） */
	float RunAnimSpeed = 450.f;
};

DECLARE_MULTICAST_DELEGATE(FOnOniFoundHider);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnOniDestroyedWalls, int32 /*Count*/);
/** 探知鬼がプレイヤーを見つけて仲間を呼んだ（呼んだ鬼・プレイヤーの位置） */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnOniSummon, AOniCharacter* /*Caller*/, const FVector& /*TargetLocation*/);

class ATreasureActor;

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

	/** スケルタルメッシュの見た目にしたとき、種類がわかるよう頭の上に浮かべる色付きの玉 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Oni")
	TObjectPtr<UStaticMeshComponent> TypeMarker;

	/** スケルタルメッシュに重ねる小物（包帯など） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Oni")
	TObjectPtr<USkeletalMeshComponent> AccessoryMesh;

	/**
	 * 見た目をスケルタルメッシュ（キャラクターのモデル）にする。スポーン直後、FinishSpawning より前に呼ぶ。
	 * 円柱の体と顔は隠し、種類の色は頭の上の玉と色違いのマテリアルで示す
	 */
	void SetSkeletalAppearance(const FKakurenboOniLook& InLook);

	bool UsesSkeletalMesh() const { return Look.Mesh != nullptr; }

	/** 今再生しているアニメーション（テスト用。アニメーション BP を使っているときは null） */
	const UAnimSequence* GetPlayingAnimation() const { return PlayingAnim; }

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

	/** プレイヤーの音（連打・ダッシュ）が聞こえる距離（cm）。0 なら気にしない */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float HearingRadius = 1200.f;

	/** おとりの音が聞こえる距離（cm）。0 ならおとりにだまされない（スピード鬼：音に敏感で、にせものとわかる） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float DecoyHearingRadius = 1200.f;

	/** プレイヤーの足音・ジャンプの音が聞こえる距離（cm）。0 なら気にしない（宝物鬼は遠くから聞こえる） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float StepHearingRadius = 700.f;

	/** プレイヤーを見つけたとき、この距離（cm）以内の鬼を呼び寄せる（0 なら呼ばない。探知鬼） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float SummonRadius = 0.f;

	/** 仲間を呼んでから、次に呼べるまでの時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float SummonCooldown = 5.f;

	/** 探知鬼：見張りながら首を回す速さ（度/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float ScanDegreesPerSecond = 50.f;

	/** 慎重鬼：歩きながら見渡して「調べた」ことにする距離（マス。壁・家具の向こうは見えない） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	int32 CarefulLookCells = 4;

	/** 慎重鬼：細い道（両側が壁・家具）のマスを後回しにする強さ（何マスぶん遠いとみなすか。壁 1 面ごと） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float CarefulNarrowPenalty = 5.f;

	/** 慎重鬼：自分の担当の区画の外のマスを後回しにする強さ（マス） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float CarefulOtherSectorPenalty = 14.f;

	/** 慎重鬼：広い場所の調べ残しがこの割合以下になったら、壁に囲まれた場所（空洞）を調べに行く */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float CarefulPocketThreshold = 0.12f;

	/** 宝物鬼：お宝の周りを回る半径（マス）と、次のお宝へ移るまでに回る周数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	int32 TreasurePatrolRadiusCells = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	int32 TreasurePatrolLaps = 2;

	/** 足音（どたどた）を鳴らす歩幅（cm）と大きさ・高さ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float StepStride = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float StepVolume = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float StepPitch = 1.f;

	/** 追いかけてくる・音を調べに来るときの足音の大きさの倍率（向かってくるのが音でわかる） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float ChaseStepVolumeScale = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float InvestigateStepVolumeScale = 1.25f;

	/** 音に気づいたときの「ン？」を続けて鳴らさない間隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sound")
	float NoticeSoundCooldown = 4.f;

	/** トリモチで動けない時間の倍率（スピード鬼は長く、パワー鬼は短い） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float StunScale = 1.f;

	/** true なら、トリモチから抜け出した直後（かからない間）に踏んだトリモチを壊す（パワー鬼） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	bool bDisarmTraps = false;

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
	float CarefulMemorySeconds = 60.f;

	/** 罠から抜け出した後、この時間は罠にかからない（秒。続けて踏んでも動けないままにならないように） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Sense")
	float StunImmunitySeconds = 3.f;

	// ===== 飛び乗り（家具・壁の上に乗ったプレイヤーを追う） =====

	/** この高さ（cm。相手と自分の足元の差）までの台なら、そばまで行って飛び乗る。これより高い積んだ壁は下から壊す */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Leap")
	float LeapMaxHeight = 280.f;

	/** 飛び乗るとき、台の上をどれだけ越える高さまで跳ぶか（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Leap")
	float LeapClearance = 60.f;

	/** 台の上に出てから相手へ向かう横の速さ（cm/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Leap")
	float LeapForwardSpeed = 450.f;

	/** 飛び乗りに失敗したとき、次に跳ぶまでの待ち時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni|Leap")
	float LeapCooldownSeconds = 1.f;

	/** 飛び乗った回数（テスト用） */
	int32 GetLeapCount() const { return LeapCount; }

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

	/**
	 * 音を聞かせる。聞こえる距離なら音の方へ向かう（追いかけている間・音を気にしない種類は無視）。
	 * 聞こえる距離は音の種類で違う（連打・ダッシュ = HearingRadius / 足音 = StepHearingRadius / おとり = DecoyHearingRadius）
	 */
	void HearNoise(const FVector& NoiseLocation, float Loudness = 1.f, EKakurenboNoise Kind = EKakurenboNoise::Mash);

	/** 探知鬼に呼ばれた：その場所へ急いで向かう（追いかけ中・動けない間・探知鬼自身は無視） */
	void Summon(const FVector& Location);

	/** 探知鬼：見張る場所（Activate の前に設定する。そこへ行ったら動かない） */
	void SetGuardCell(const FIntPoint& Cell) { GuardCell = Cell; bHasGuardCell = true; }
	bool IsAtGuardPost() const { return bAtGuardPost; }
	FIntPoint GetGuardCell() const { return GuardCell; }

	/** 最後に仲間を呼んだワールド時刻（まだなら負の値） */
	float GetLastSummonTime() const { return LastSummonTime; }

	/** 慎重鬼：担当の区画（舞台を Y 方向に Count 等分した Index 番目）。GameMode が出現時に決める */
	void SetCarefulSector(int32 Index, int32 Count) { CarefulSector = Index; CarefulSectorCount = FMath::Max(1, Count); }
	bool IsInCarefulSector(const FIntPoint& Cell) const;

	/** 宝物鬼：お宝（か、取られたお宝のあった場所）の周りを回っているか・その場所 */
	bool IsPatrollingTreasure() const { return bHasPatrolCenter; }
	FIntPoint GetPatrolCenter() const { return PatrolCenter; }

	/** 鳴らした足音の数（テスト用） */
	int32 GetStepSoundCount() const { return StepSoundCount; }

	/** 今プレイヤーが見えるか（テスト用） */
	bool DebugCanSeeTarget() const { return CanSeeTarget(); }

	/** 音に気づいて「ン？」と鳴らした回数（テスト用） */
	int32 GetNoticeSoundCount() const { return NoticeSoundCount; }

	/** 罠などで動けなくする（StunScale 倍の時間） */
	void Stun(float Seconds);

	/** 今、踏んだトリモチを壊すか（パワー鬼が抜け出した直後のかからない間） */
	bool CanDisarmTraps() const { return bDisarmTraps && bActive && State != EOniState::Stunned && StunImmunityTimer > 0.f; }

	/** テスト用：指定したマスへ向かわせる（音を聞いたときと同じ動き） */
	void DebugGoTo(const FIntPoint& Cell);

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
	FOnOniSummon OnSummon;

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

	// 飛び乗り
	/** 相手が台（家具・壁）の上に立っているか。OutHeight は自分の足元からの高さ */
	bool IsTargetOnPlatform(float& OutHeight) const;
	/** 台のマスのそばの、歩けるマスのうち相手に一番近いもの（そこまで歩いてから飛び乗る） */
	bool FindApproachCell(const FIntPoint& PlatformCell, const FVector& TargetLocation, FIntPoint& OutCell) const;
	/** 目の前が台なら真上に跳ぶ（台より上に出たら相手の方へ進む） */
	bool TryStartLeap();
	void TickLeap(float DeltaSeconds);

	/** その場で左右を見渡す（くるくる回らないよう、元の向きを中心に往復する） */
	void BeginLookAround();
	void TickLookAround(float DeltaSeconds, float AmplitudeDegrees);

	// うろうろの行き先（種類ごと）
	bool ChooseWanderTarget();
	bool ChooseRandomTarget(int32 MaxDistanceCells);
	bool ChooseScoutTarget();
	bool ChooseCarefulTarget();
	bool ChooseTreasurePatrolTarget();
	bool TryInspectPocket();
	/** 探知鬼：見張りの場所へ行き、着いたらその場で首を回して見張る */
	void TickGuard(float DeltaSeconds);
	/** 探知鬼：プレイヤーが見えた（仲間を呼ぶ） */
	void HandleGuardSighting();
	/** 慎重鬼：見渡せるマスを「調べた」と記録する */
	void MarkVisibleCellsChecked(float Now);
	/** 歩いた距離に合わせて足音を鳴らす */
	void TickFootsteps();
	/** 種類ごとの飾り（宝物鬼の王冠・探知鬼のアンテナ） */
	void BuildTypeDecoration();
	/** 壁に囲まれた場所（出入り口の前を壁で囲まれたときなど）にいたら、壁を壊して外へ出る */
	bool TryEscapeEnclosure();
	/** 慎重鬼：そのマスを含む建物（つながった壁と中の空洞）を仲間に知らせて、仲間が来ないようにする */
	void ReserveStructure(const FIntPoint& Cell);

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
	/** 体の伸び縮み（攻撃の溜め・罠でぷるぷる）。円柱でもスケルタルメッシュでも同じ割合で変える */
	void SetBodySquash(const FVector& Ratio);

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

	/** 慎重鬼：担当の区画 */
	int32 CarefulSector = 0;
	int32 CarefulSectorCount = 1;

	/** 宝物鬼：回っているお宝の場所（取られた後の場所も回る）・次に向かう周りのマスの番号・回る向き・回ったマスの数 */
	FIntPoint PatrolCenter = FIntPoint::ZeroValue;
	bool bHasPatrolCenter = false;
	int32 PatrolIndex = 0;
	int32 PatrolDirection = 1;
	int32 PatrolSteps = 0;

	/** 探知鬼 */
	FIntPoint GuardCell = FIntPoint::ZeroValue;
	bool bHasGuardCell = false;
	bool bAtGuardPost = false;
	float GuardLookYaw = 0.f;
	float SummonTimer = 0.f;
	float LastSummonTime = -1.f;

	/** 足音 */
	FVector LastStepLocation = FVector::ZeroVector;
	float StepDistance = 0.f;
	int32 StepSoundCount = 0;

	/** 音に気づいた「ン？」 */
	float LastNoticeSoundTime = -100.f;
	int32 NoticeSoundCount = 0;

	/** 種類ごとの飾りの部品 */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> DecorationMeshes;

	/** Attack 中 */
	FIntPoint AttackCell = FIntPoint::ZeroValue;
	float AttackTimer = 0.f;
	bool bAttackFired = false;

	/** Stunned 中 */
	float StunTimer = 0.f;
	/** 罠から抜け出した後、罠にかからない残り時間 */
	float StunImmunityTimer = 0.f;
	void SetStunStarsVisible(bool bVisible);

	/** 飛び乗りの最中か・跳び越える台の上面の高さ・跳んでからの時間・次に跳べるまでの時間 */
	bool bLeaping = false;
	float LeapTopZ = 0.f;
	float LeapElapsed = 0.f;
	float LeapCooldown = 0.f;
	int32 LeapCount = 0;

	/** 引っかかり検出 */
	FVector LastProgressLocation = FVector::ZeroVector;
	float StuckTimer = 0.f;

	float SenseTimer = 0.f;
	FVector BaseBodyScale = FVector(0.76f, 0.76f, 1.9f);

	/** スケルタルメッシュの見た目（SetSkeletalAppearance で設定） */
	UPROPERTY()
	FKakurenboOniLook Look;

	/** 状態に合ったアニメーションを再生する（アニメーション BP が無いとき） */
	void UpdateMeshAnimation();

	UPROPERTY()
	TObjectPtr<UAnimSequence> PlayingAnim;

	FVector BaseMeshScale = FVector::OneVector;
};
