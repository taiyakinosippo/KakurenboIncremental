// 身代わり人形（宝石のお店の道具。M15）。見た目も体の大きさもプレイヤーと同じで、鬼からはプレイヤーに見える。
// 鬼に見つかって追いかけられると、その鬼と反対の方へまっすぐ走って逃げる（プレイヤーの歩きより少し速い）。
// 逃げている間は大きな音（ドタバタ）を立てるので、周りの鬼も寄ってくる。鬼に捕まると「ポン」と消える。
// コントローラーは持たない（AddMovementInput は bRunPhysicsWithNoController で動く）。置いた罠（ATrapActor）が作って持つ。

#pragma once

#include "CoreMinimal.h"
#include "HiderCharacter.h"
#include "FakeHiderCharacter.generated.h"

class ATrapActor;

UCLASS()
class KAKURENBOINCREMENTAL_API AFakeHiderCharacter : public AHiderCharacter
{
	GENERATED_BODY()

public:
	AFakeHiderCharacter();

	/** この人形を置いた道具（音を出すときに使う） */
	TWeakObjectPtr<ATrapActor> OwnerTrap;

	/** 置いた場所（かくれんぼ以外のパートで戻る） */
	FVector HomeLocation = FVector::ZeroVector;

	/** 置いた向き（ヨー。見つかったらこの向きへ逃げる。設置パートで人形をクリックすると 90 度ずつ回る） */
	float HomeYaw = 0.f;

	/** 逃げる速さ（cm/秒。プレイヤーの歩きは 420） */
	float FleeSpeed = 480.f;

	/** 追われていなくなってから、止まるまでの秒数 */
	float CalmDownSeconds = 1.5f;

	/** 逃げているか（テスト用） */
	bool IsFleeing() const { return bFleeing; }

	/** 逃げている向き（テスト用） */
	FVector GetFleeDirection() const { return FleeDir; }

	/** 逃げながら出した音の回数（テスト用） */
	int32 GetNoiseCount() const { return NoiseCount; }

	/** 逃げながら鳴らした足音の数（テスト用） */
	int32 GetStepCount() const { return StepCount; }

	/** 置いた場所へ戻って止まる（かくれんぼ以外のパート） */
	void ReturnHome();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** 置いた向き（Preferred）に近い順に、ふさがっていない向きを選ぶ（後ろ向きには逃げない） */
	FVector ChooseFleeDirection(const FVector& Preferred) const;

	bool bFleeing = false;
	FVector FleeDir = FVector::ForwardVector;
	float CalmTimer = 0.f;
	float NoiseTimer = 0.f;
	float StuckTimer = 0.f;
	int32 NoiseCount = 0;
	/** 歩いた距離（歩幅ごとに足音「ドタドタ」を鳴らす） */
	float StepDistance = 0.f;
	int32 StepCount = 0;
	/** 足音を鳴らす歩幅（cm）。鬼の足音（150cm）より細かく、あわてて走る感じ */
	static constexpr float DollStepStride = 110.f;
};
