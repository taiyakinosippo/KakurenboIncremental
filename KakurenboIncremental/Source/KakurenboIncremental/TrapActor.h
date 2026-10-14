// 罠と、宝石のお店の道具。設置パートで床のマスに置き、かくれんぼ中に鬼に働く。
//   トリモチ（Sticky）: 鬼が上を通ると、しばらく動けなくする（その間はプレイヤーに触れても捕まらない）。1 回で消える
//   おとり（Decoy）   : 一定の間隔で音を出して鬼を呼び寄せる。鬼が触れると壊れる
//   ---- 宝石のお店の道具（M15） ----
//   身代わり人形（FakeHider）: 自分そっくりの人形（AFakeHiderCharacter）を立たせる。鬼に見つかると、大きな音を立てて鬼と反対へまっすぐ逃げる。捕まると消える
//   一方通行ののれん（OneWay）: 鬼は矢印の向きにしか通れない（プレイヤー・人形はどちらからも通れる）。Uses 回通られると破れる
//   真っ暗ブロック（Darkness）: 置いておくと、かくれんぼの間は館じゅうが真っ暗（GameMode が見る）。1 ラウンドで壊れる
//   ゴールドラッシュ（GoldRush）: 置いておくと、かくれんぼの間は連打のコインが 2 倍（GameMode が見る）。1 ラウンドで壊れる
//   迷い猫（Cat）: 近くをうろうろして「にゃーん」と鳴く。寄ってきた鬼は猫をなでて、しばらく動かない
// 消えた罠は設計図に残り、次の設置パートで在庫から自動で置き直される（壁と同じ）。
// 当たり判定は持たない（プレイヤーも鬼も上を通れる）。のれんだけは鬼を通さない箱を持つ（プレイヤーは通れる）。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KakurenboTypes.h"
#include "TrapActor.generated.h"

class AFakeHiderCharacter;
class AHiderCharacter;
class AOniCharacter;
class UBoxComponent;
class UStaticMesh;
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

	/** 種類と数値を設定して見た目を整える（スポーン直後に呼ぶ。Cell と向きは先に入れておく） */
	void InitTrap(int32 InTrapTypeIndex, const FKakurenboTrapRow& InDef);

	ETrapKind GetKind() const { return Def.Kind; }

	/** 向き（一方通行ののれん：鬼が通れる向き。0 +X / 1 +Y / 2 -X / 3 -Y） */
	int32 GetDir() const { return Dir; }
	void SetDir(int32 InDir);

	/** おとりが音を出した回数 */
	int32 GetPingCount() const { return PingCount; }

	/** 鬼の体がこの罠の上にあるか */
	bool IsOniOnTrap(const AOniCharacter* Oni) const;

	/** 道具が今いる場所（身代わり人形・迷い猫は動く。ほかは置いた場所） */
	FVector GetGadgetLocation() const;

	/** 迷い猫の向き（Yaw。テストで顔の向きを確かめる） */
	float GetCatYaw() const;

	/** 身代わり人形（無ければ null） */
	AHiderCharacter* GetFakeHider() const;

	/** 一方通行ののれん：鬼が通った回数 / 迷い猫：なでられた回数・鳴いた回数（テスト用） */
	int32 GetPassCount() const { return PassCount; }
	int32 GetPetCount() const { return PetCount; }
	int32 GetMeowCount() const { return MeowCount; }

	/** のれんの耐久（通常壁の 2 倍。GameMode がかくれんぼの始めに決める）。鬼の攻撃で減り、0 で破れる */
	double GetGateHP() const { return GateHP; }
	void SetGateHP(double HP) { GateHP = GateMaxHP = FMath::Max(HP, 0.01); UpdateGateLook(); }

	/** のれんに攻撃が当たった（@return 破れたか） */
	bool DamageGate(double Damage);

	/** 迷い猫：今なでられているか */
	bool IsBeingPetted() const { return PetTimer > 0.f; }

	/** 迷い猫：なでた鬼の頭の上に乗っているか（乗ったら、そのラウンドはもうなでられない） */
	bool IsOnOniHead() const { return RideOni.IsValid(); }

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** かくれんぼ中で、鬼が出てきているときだけ働く */
	bool IsArmed() const;
	void TickSticky();
	void TickDecoy(float DeltaSeconds);
	void TickAnimation(float DeltaSeconds);
	void TickFakeHider();
	void TickOneWay();
	void TickCat(float DeltaSeconds);

	/** 見た目の部品を足す（基本の形：Cube / Cone / Sphere / Cylinder。Parent が無ければ根元） */
	UStaticMeshComponent* AddPart(UStaticMesh* Mesh, const FVector& Location, const FVector& Scale, const FLinearColor& Color, USceneComponent* Parent = nullptr, const FRotator& Rotation = FRotator::ZeroRotator);
	void BuildOneWayLook();
	void BuildDarknessLook();
	void BuildCatLook();
	void BuildGoldRushLook();
	void SpawnFakeHider();

	/** 迷い猫：次に歩いて行く場所を決める（家から Radius 以内の歩けるマス） */
	void ChooseCatDestination();

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	/** のれん：鬼だけを止める箱（プレイヤーと人形はこの道具を無視して通る） */
	UPROPERTY()
	TObjectPtr<UBoxComponent> BlockBox;

	/** 迷い猫：猫の体の根元（歩くとこれが動く） */
	UPROPERTY()
	TObjectPtr<USceneComponent> CatRoot;

	/** 迷い猫のしっぽ（ゆらゆら） */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> CatTail;

	/** 迷い猫のモデル（取り込んであれば。骨が無いので、歩く動きは SetCatMotion で体を揺らす） */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> CatModel;

	/** モデルの置き方（揺らす前の位置・向き） */
	FVector CatModelBaseLocation = FVector::ZeroVector;
	float CatModelBaseYaw = 0.f;

	/** 迷い猫の動き：0 止まっている / 1 歩く / 2 走る（なでられている間はゴロゴロ揺れる） */
	void SetCatMotion(int32 Motion);

	/** 身代わり人形 */
	UPROPERTY()
	TObjectPtr<AFakeHiderCharacter> FakeHider;

	int32 Dir = 0;

	/** のれんの耐久と、傷んだ見た目（布を暗く） */
	double GateHP = 6.0;
	double GateMaxHP = 6.0;
	void UpdateGateLook();

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> GateCloths;

	/** 次に音を出すまでの時間（鬼が出てきてから 0.5 秒後に最初の音） */
	float NoiseTimer = 0.5f;
	float AnimTime = 0.f;
	/** 音を出したときに体を膨らませる（1 → 0 に戻っていく） */
	float PingPulse = 0.f;
	int32 PingCount = 0;
	bool bTriggered = false;
	FVector BaseScale = FVector::OneVector;
	FVector TopScale = FVector::OneVector;

	/** のれん：今通してあげている鬼（通り抜けたら数えて、また止める） */
	TArray<TWeakObjectPtr<AOniCharacter>> PassingOnis;
	int32 PassCount = 0;

	/** 迷い猫 */
	TArray<FVector> CatPath;
	int32 CatPathIndex = 0;
	float CatWait = 0.f;
	float MeowTimer = 2.f;
	float PetTimer = 0.f;
	int32 PetCount = 0;
	int32 MeowCount = 0;
	/** なでた鬼は、しばらく同じ猫には寄ってこない（ワールド時刻） */
	TMap<TWeakObjectPtr<AOniCharacter>, float> PetCooldownUntil;

	/** なでられた後は、その鬼の頭の上に乗る（ラウンドが終わるまで） */
	TWeakObjectPtr<AOniCharacter> PettingOni;
	TWeakObjectPtr<AOniCharacter> RideOni;
	/** 鬼に当たって壊れた扱い（グリッドから外れている）。かくれんぼが終わったら自分で消える */
	bool bTakenByOni = false;
};
