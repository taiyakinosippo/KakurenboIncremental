// 罠。設置パートで床のマスに置き、かくれんぼ中に鬼に働く。
//   トリモチ（Sticky）: 鬼が上を通ると、しばらく動けなくする（その間はプレイヤーに触れても捕まらない）。1 回で消える
//   おとり（Decoy）   : 一定の間隔で音を出して鬼を呼び寄せる。鬼が触れると壊れる
// 消えた罠は設計図に残り、次の設置パートで在庫から自動で置き直される（壁と同じ）。
// 当たり判定は持たない（プレイヤーも鬼も上を通れる）。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KakurenboTypes.h"
#include "TrapActor.generated.h"

class AOniCharacter;
class UStaticMeshComponent;

UCLASS()
class KAKURENBOINCREMENTAL_API ATrapActor : public AActor
{
	GENERATED_BODY()

public:
	ATrapActor();

	/** 土台（トリモチ: 平たい円 / おとり: 胴体） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trap")
	TObjectPtr<UStaticMeshComponent> BaseMesh;

	/** 飾り（トリモチ: 真ん中のもち / おとり: 頭） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trap")
	TObjectPtr<UStaticMeshComponent> TopMesh;

	/** GameMode の TrapTypes のどれか（在庫に戻すときに使う） */
	UPROPERTY(BlueprintReadOnly, Category = "Trap")
	int32 TrapTypeIndex = 0;

	/** 置かれているマス */
	UPROPERTY(BlueprintReadOnly, Category = "Trap")
	FIntPoint Cell = FIntPoint::ZeroValue;

	/** 罠の数値（Traps.csv の行） */
	UPROPERTY(BlueprintReadOnly, Category = "Trap")
	FKakurenboTrapRow Def;

	/** 種類と数値を設定して見た目を整える（スポーン直後に呼ぶ） */
	void InitTrap(int32 InTrapTypeIndex, const FKakurenboTrapRow& InDef);

	ETrapKind GetKind() const { return Def.Kind; }

	/** おとりが音を出した回数 */
	int32 GetPingCount() const { return PingCount; }

	/** 鬼の体がこの罠の上にあるか */
	bool IsOniOnTrap(const AOniCharacter* Oni) const;

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	/** かくれんぼ中で、鬼が出てきているときだけ働く */
	bool IsArmed() const;
	void TickSticky();
	void TickDecoy(float DeltaSeconds);
	void TickAnimation(float DeltaSeconds);

	/** 次に音を出すまでの時間（鬼が出てきてから 0.5 秒後に最初の音） */
	float NoiseTimer = 0.5f;
	float AnimTime = 0.f;
	/** 音を出したときに体を膨らませる（1 → 0 に戻っていく） */
	float PingPulse = 0.f;
	int32 PingCount = 0;
	bool bTriggered = false;
	FVector BaseScale = FVector::OneVector;
	FVector TopScale = FVector::OneVector;
};
