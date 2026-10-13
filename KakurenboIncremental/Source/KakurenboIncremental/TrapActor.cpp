#include "TrapActor.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "FakeHiderCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GridPathfinder.h"
#include "KakurenboGameMode.h"
#include "KakurenboGridSubsystem.h"
#include "KakurenboLibrary.h"
#include "KakurenboSoundSubsystem.h"
#include "OniCharacter.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 迷い猫の色（茶トラ・白） */
	const FLinearColor CatFurColor(1.f, 0.55f, 0.2f);
	const FLinearColor CatWhiteColor(0.95f, 0.92f, 0.85f);

	/** なでた鬼が、同じ猫にもう一度寄ってくるまでの秒数 */
	constexpr float CatPetCooldownSeconds = 12.f;

	/** 鬼の体の中心がこの距離（cm）まで来たら猫をなでる */
	constexpr float CatPetRadius = 130.f;
}

ATrapActor::ATrapActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(false); // プレイヤーも鬼も上を通れる（のれんは後で鬼を止める箱だけ有効にする）

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	CylinderMesh = CylinderFinder.Object;
	SphereMesh = SphereFinder.Object;
	CubeMesh = CubeFinder.Object;
	ConeMesh = ConeFinder.Object;

	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	BaseMesh->SetupAttachment(RootComponent);
	BaseMesh->SetStaticMesh(CylinderFinder.Object);
	BaseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	TopMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TopMesh"));
	TopMesh->SetupAttachment(RootComponent);
	TopMesh->SetStaticMesh(SphereFinder.Object);
	TopMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

UStaticMeshComponent* ATrapActor::AddPart(UStaticMesh* Mesh, const FVector& Location, const FVector& Scale, const FLinearColor& Color, USceneComponent* Parent, const FRotator& Rotation)
{
	// 実行中に部品を足す（NewObject → 付ける → RegisterComponent で世界に出る）
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetStaticMesh(Mesh);
	Part->SetupAttachment(Parent ? Parent : RootComponent.Get());
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetRelativeLocation(Location);
	Part->SetRelativeRotation(Rotation);
	Part->SetRelativeScale3D(Scale);
	Part->RegisterComponent();
	UKakurenboLibrary::ApplyColor(Part, Color);
	return Part;
}

void ATrapActor::SetDir(int32 InDir)
{
	Dir = ((InDir % 4) + 4) % 4;
	// 向き 0 = +X。のれんの矢印（部品の +X）が鬼の通れる向きを指す
	if (Def.Kind == ETrapKind::OneWay)
	{
		SetActorRotation(FRotator(0.f, Dir * 90.f, 0.f));
	}
}

void ATrapActor::InitTrap(int32 InTrapTypeIndex, const FKakurenboTrapRow& InDef)
{
	TrapTypeIndex = InTrapTypeIndex;
	Def = InDef;

	// 基本の形（円柱・球）は 100cm。マスの床の上に置く
	switch (Def.Kind)
	{
	case ETrapKind::Sticky:
		// 床に広がった平たいもち
		BaseScale = FVector(0.7f, 0.7f, 0.04f);
		TopScale = FVector(0.35f, 0.35f, 0.1f);
		BaseMesh->SetRelativeLocation(FVector(0.f, 0.f, 2.f));
		TopMesh->SetRelativeLocation(FVector(0.f, 0.f, 4.f));
		UKakurenboLibrary::ApplyColor(BaseMesh, Def.Color);
		UKakurenboLibrary::ApplyColor(TopMesh, Def.Color * 0.75f);
		break;
	case ETrapKind::Decoy:
		// 小さな人形（胴体と頭）
		BaseScale = FVector(0.35f, 0.35f, 0.3f);
		TopScale = FVector(0.3f);
		BaseMesh->SetRelativeLocation(FVector(0.f, 0.f, 15.f));
		TopMesh->SetRelativeLocation(FVector(0.f, 0.f, 42.f));
		UKakurenboLibrary::ApplyColor(BaseMesh, Def.Color);
		UKakurenboLibrary::ApplyColor(TopMesh, FMath::Lerp(Def.Color, FLinearColor::White, 0.4f));
		break;
	default:
		// 宝石のお店の道具は、それぞれの見た目を組み立てる（土台の円柱・球は使わない）
		BaseScale = FVector::ZeroVector;
		TopScale = FVector::ZeroVector;
		BaseMesh->SetVisibility(false);
		TopMesh->SetVisibility(false);
		break;
	}
	BaseMesh->SetRelativeScale3D(BaseScale.IsZero() ? FVector(0.01f) : BaseScale);
	TopMesh->SetRelativeScale3D(TopScale.IsZero() ? FVector(0.01f) : TopScale);
	AnimTime = FMath::FRandRange(0.f, 5.f);

	switch (Def.Kind)
	{
	case ETrapKind::OneWay:   BuildOneWayLook(); SetDir(Dir); break;
	case ETrapKind::Darkness: BuildDarknessLook(); break;
	case ETrapKind::Cat:      BuildCatLook(); break;
	case ETrapKind::FakeHider: SpawnFakeHider(); break;
	default: break;
	}
}

// ---------------------------------------------------------------- 見た目

void ATrapActor::BuildOneWayLook()
{
	// 鳥居のような木の枠＋のれん（布）＋床の矢印（鬼が通れる向き）。部品の +X が矢印の向き
	const FLinearColor Wood(0.35f, 0.2f, 0.1f);
	for (const float Side : { -46.f, 46.f })
	{
		AddPart(CubeMesh, FVector(0.f, Side, 105.f), FVector(0.08f, 0.08f, 2.1f), Wood);
	}
	AddPart(CubeMesh, FVector(0.f, 0.f, 205.f), FVector(0.14f, 1.06f, 0.1f), Wood);
	// のれん：上から 3 枚垂れた布（鬼の通れる向きに少しなびく）
	for (int32 i = 0; i < 3; ++i)
	{
		AddPart(CubeMesh, FVector(4.f, -30.f + i * 30.f, 165.f), FVector(0.02f, 0.27f, 0.7f), Def.Color, nullptr, FRotator(-8.f, 0.f, 0.f));
	}
	// 床の矢印（白っぽい板の矢じり）
	const FLinearColor ArrowColor = FMath::Lerp(Def.Color, FLinearColor::White, 0.55f);
	AddPart(CubeMesh, FVector(-12.f, 0.f, 1.5f), FVector(0.5f, 0.12f, 0.02f), ArrowColor);
	AddPart(CubeMesh, FVector(22.f, 9.f, 1.6f), FVector(0.28f, 0.1f, 0.02f), ArrowColor, nullptr, FRotator(0.f, -45.f, 0.f));
	AddPart(CubeMesh, FVector(22.f, -9.f, 1.6f), FVector(0.28f, 0.1f, 0.02f), ArrowColor, nullptr, FRotator(0.f, 45.f, 0.f));

	// 鬼を止める箱（プレイヤーと人形は、この道具を「ぶつからない相手」にしてあるので通れる）。視線とカメラは通す
	BlockBox = NewObject<UBoxComponent>(this);
	BlockBox->SetupAttachment(RootComponent);
	BlockBox->SetBoxExtent(FVector(38.f, 46.f, 110.f));
	BlockBox->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
	BlockBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BlockBox->SetCollisionObjectType(ECC_WorldDynamic);
	BlockBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	BlockBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	BlockBox->RegisterComponent();
	SetActorEnableCollision(true);
	BaseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TopMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (const ACharacter* Player = Cast<ACharacter>(PC->GetPawn()))
		{
			Player->GetCapsuleComponent()->IgnoreActorWhenMoving(this, true);
		}
	}
	// 身代わり人形も通れる
	if (const AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>())
	{
		for (AHiderCharacter* Fake : GM->GetFakeHiders())
		{
			Fake->GetCapsuleComponent()->IgnoreActorWhenMoving(this, true);
		}
	}
}

void ATrapActor::BuildDarknessLook()
{
	// 黒い布をかぶせた箱と、紫にぼんやり光る消えかけのろうそく（館を暗くする道具）
	AddPart(CubeMesh, FVector(0.f, 0.f, 30.f), FVector(0.55f, 0.55f, 0.6f), Def.Color);
	AddPart(CubeMesh, FVector(0.f, 0.f, 62.f), FVector(0.62f, 0.62f, 0.06f), Def.Color * 0.6f);
	AddPart(CylinderMesh, FVector(0.f, 0.f, 75.f), FVector(0.08f, 0.08f, 0.2f), FLinearColor(0.85f, 0.8f, 0.75f));
	AddPart(SphereMesh, FVector(0.f, 0.f, 90.f), FVector(0.07f, 0.07f, 0.1f), FLinearColor(0.55f, 0.3f, 1.f));
}

void ATrapActor::BuildCatLook()
{
	// 基本の形で作った小さな猫（体・頭・耳・しっぽ）。CatRoot ごと歩く
	CatRoot = NewObject<USceneComponent>(this);
	CatRoot->SetupAttachment(RootComponent);
	CatRoot->RegisterComponent();
	AddPart(CubeMesh, FVector(0.f, 0.f, 20.f), FVector(0.48f, 0.24f, 0.24f), CatFurColor, CatRoot);
	AddPart(SphereMesh, FVector(28.f, 0.f, 38.f), FVector(0.26f, 0.26f, 0.24f), CatFurColor, CatRoot);
	AddPart(SphereMesh, FVector(36.f, 0.f, 33.f), FVector(0.1f, 0.12f, 0.08f), CatWhiteColor, CatRoot);
	for (const float Side : { -7.f, 7.f })
	{
		AddPart(ConeMesh, FVector(28.f, Side, 52.f), FVector(0.08f, 0.08f, 0.1f), CatFurColor, CatRoot);
		AddPart(SphereMesh, FVector(38.f, Side * 0.8f, 41.f), FVector(0.035f), FLinearColor(0.05f, 0.1f, 0.05f), CatRoot);
		for (const float Front : { -16.f, 16.f })
		{
			AddPart(CylinderMesh, FVector(Front, Side * 1.2f, 6.f), FVector(0.06f, 0.06f, 0.14f), CatWhiteColor, CatRoot);
		}
	}
	CatTail = AddPart(CylinderMesh, FVector(-30.f, 0.f, 34.f), FVector(0.05f, 0.05f, 0.34f), CatFurColor, CatRoot, FRotator(-30.f, 0.f, 0.f));
	MeowTimer = FMath::FRandRange(0.5f, 2.f);
}

void ATrapActor::SpawnFakeHider()
{
	// 自分そっくりの人形（プレイヤーと同じキャラクター。少し色を薄く）
	const FVector Location = GetActorLocation() + FVector(0.f, 0.f, 82.f);
	const FTransform At(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Location);
	FakeHider = GetWorld()->SpawnActorDeferred<AFakeHiderCharacter>(AFakeHiderCharacter::StaticClass(), At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!FakeHider)
	{
		return;
	}
	FakeHider->OwnerTrap = this;
	FakeHider->HomeLocation = Location;
	FakeHider->FleeSpeed = Def.Speed > 0.f ? Def.Speed : 480.f;
	FakeHider->FinishSpawning(At);
	if (AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>())
	{
		GM->ApplyPlayerLookTo(FakeHider, FLinearColor(0.75f, 0.85f, 1.f));
		GM->RegisterFakeHiderCollision(FakeHider);
	}
}

void ATrapActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (FakeHider)
	{
		FakeHider->Destroy();
		FakeHider = nullptr;
	}
	// 通していた鬼は、また止まるように戻す
	for (const TWeakObjectPtr<AOniCharacter>& Oni : PassingOnis)
	{
		if (Oni.IsValid())
		{
			Oni->GetCapsuleComponent()->IgnoreActorWhenMoving(this, false);
		}
	}
	PassingOnis.Reset();
	Super::EndPlay(EndPlayReason);
}

FVector ATrapActor::GetGadgetLocation() const
{
	if (FakeHider)
	{
		return FakeHider->GetActorLocation() - FVector(0.f, 0.f, FakeHider->GetSimpleCollisionHalfHeight());
	}
	if (CatRoot)
	{
		return CatRoot->GetComponentLocation();
	}
	return GetActorLocation();
}

AHiderCharacter* ATrapActor::GetFakeHider() const
{
	return FakeHider;
}

bool ATrapActor::IsArmed() const
{
	const AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>();
	return GM && GM->GetPhase() == EKakurenboPhase::Hide && GM->GetOnis().Num() > 0;
}

bool ATrapActor::IsOniOnTrap(const AOniCharacter* Oni) const
{
	if (!Oni)
	{
		return false;
	}
	// 体の中心が罠の近くにあり、床の上にいる（壁の上などではない）
	const FVector OniLocation = Oni->GetActorLocation();
	const float FeetZ = OniLocation.Z - Oni->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	return FVector::Dist2D(OniLocation, GetActorLocation()) <= Def.TriggerRadius
		&& FMath::Abs(FeetZ - GetActorLocation().Z) < 60.f;
}

// ---------------------------------------------------------------- 毎フレーム

void ATrapActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickAnimation(DeltaSeconds);

	// 身代わり人形・迷い猫は、かくれんぼ以外のパートでは置いた場所に戻る
	if (Def.Kind == ETrapKind::FakeHider)
	{
		TickFakeHider();
	}
	if (Def.Kind == ETrapKind::Cat)
	{
		TickCat(DeltaSeconds);
	}

	if (bTriggered || !IsArmed())
	{
		return;
	}
	switch (Def.Kind)
	{
	case ETrapKind::Sticky: TickSticky(); break;
	case ETrapKind::Decoy:  TickDecoy(DeltaSeconds); break;
	case ETrapKind::OneWay: TickOneWay(); break;
	default: break;
	}
}

void ATrapActor::TickSticky()
{
	AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>();
	for (AOniCharacter* Oni : GM->GetOnis())
	{
		if (!Oni || !IsOniOnTrap(Oni))
		{
			continue;
		}
		// 動けなくなった直後の鬼には効かない（続けて踏んでも動けないままにならないように）
		if (Oni->CanBeStunned())
		{
			bTriggered = true;
			GM->HandleTrapTriggered(this, Oni); // ここで罠は消える
			return;
		}
		// パワー鬼は、抜け出した直後に踏んだトリモチを壊してしまう
		if (Oni->CanDisarmTraps())
		{
			bTriggered = true;
			GM->HandleTrapDisarmed(this, Oni); // ここで罠は消える
			return;
		}
	}
}

void ATrapActor::TickDecoy(float DeltaSeconds)
{
	AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>();

	// 鬼が触れたら壊れる
	for (AOniCharacter* Oni : GM->GetOnis())
	{
		if (Oni && Oni->IsActive() && Oni->GetOniState() != EOniState::Stunned && IsOniOnTrap(Oni))
		{
			bTriggered = true;
			GM->HandleTrapTriggered(this, Oni); // ここで罠は消える
			return;
		}
	}

	// 一定の間隔で音を出す
	NoiseTimer -= DeltaSeconds;
	if (NoiseTimer <= 0.f)
	{
		NoiseTimer = FMath::Max(0.2f, Def.NoiseInterval);
		++PingCount;
		PingPulse = 1.f;
		GM->HandleDecoyPing(this);
	}
}

void ATrapActor::TickFakeHider()
{
	// 捕まえるのは鬼の側（OniCharacter が人形に触れたら GameMode に知らせる）。ここでは、かくれんぼ以外のパートで元の場所へ戻すだけ
	const AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>();
	if (FakeHider && GM && GM->GetPhase() != EKakurenboPhase::Hide)
	{
		FakeHider->ReturnHome();
	}
}

void ATrapActor::TickOneWay()
{
	// 鬼が矢印の向きから近づいてきたら、その鬼だけ通す（箱にぶつからなくする）。
	// 向こう側へ抜けたら 1 回と数えて、また止める。Uses 回通られたら破れる
	AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>();
	const FVector Center = GetActorLocation();
	const FIntPoint Step = FKakurenboPathGrid::DirToStep(Dir);
	const FVector Forward(Step.X, Step.Y, 0.f);
	for (AOniCharacter* Oni : GM->GetOnis())
	{
		if (!Oni)
		{
			continue;
		}
		const FVector Rel = Oni->GetActorLocation() - Center;
		const float Along = FVector::DotProduct(Rel, Forward);
		const float Across = FMath::Abs(FVector::CrossProduct(Forward, Rel).Z);
		const int32 Passing = PassingOnis.IndexOfByPredicate([Oni](const TWeakObjectPtr<AOniCharacter>& P) { return P.Get() == Oni; });
		if (Passing == INDEX_NONE)
		{
			// 矢印の後ろ側から、矢印の向きに進もうとしている鬼を通す
			const bool bBehind = Along < -20.f && Along > -150.f && Across < 50.f;
			const bool bGoingForward = FVector::DotProduct(Oni->GetVelocity().GetSafeNormal2D(), Forward) > 0.3f
				|| FVector::DotProduct(Oni->GetLastMovementInputVector().GetSafeNormal2D(), Forward) > 0.3f;
			if (bBehind && bGoingForward)
			{
				Oni->GetCapsuleComponent()->IgnoreActorWhenMoving(this, true);
				PassingOnis.Add(Oni);
			}
			continue;
		}
		// 通り抜けた（矢印の先へ出た）→ 1 回。引き返した・横へそれた → 数えずに止め直す
		const bool bThrough = Along > 70.f;
		const bool bBackedOff = Along < -160.f || Across > 120.f;
		if (bThrough || bBackedOff)
		{
			Oni->GetCapsuleComponent()->IgnoreActorWhenMoving(this, false);
			PassingOnis.RemoveAt(Passing);
			if (bThrough)
			{
				++PassCount;
				UKakurenboSoundSubsystem::Play3D(this, EKakurenboSfx::PickUp, Center + FVector(0.f, 0.f, 150.f), 0.7f, 0.7f);
				if (Def.Uses > 0 && PassCount >= Def.Uses)
				{
					bTriggered = true;
					GM->HandleOneWayBroken(this); // ここで消える
					return;
				}
			}
		}
	}
}

void ATrapActor::ChooseCatDestination()
{
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	CatPath.Reset();
	CatPathIndex = 0;
	if (!Grid || !Grid->IsConfigured() || !CatRoot)
	{
		return;
	}
	// 家（置いたマス）から Radius 以内の、歩いて行ける床のマスのどれか
	const int32 RadiusCells = FMath::Max(1, FMath::RoundToInt(Def.Radius / Grid->GetCellSize()));
	const FIntPoint Here = Grid->WorldToCell(CatRoot->GetComponentLocation());
	const FKakurenboPathGrid Walk = Grid->BuildWalkGrid();
	for (int32 Attempt = 0; Attempt < 12; ++Attempt)
	{
		const FIntPoint Goal = Cell + FIntPoint(FMath::RandRange(-RadiusCells, RadiusCells), FMath::RandRange(-RadiusCells, RadiusCells));
		TArray<FIntPoint> Path;
		if (Goal != Here && Walk.IsFree(Goal) && KakurenboPathfinding::FindPath(Walk, Here, Goal, Path) && Path.Num() <= RadiusCells * 3)
		{
			for (const FIntPoint& P : Path)
			{
				CatPath.Add(Grid->CellFloorCenter(P) + FVector(FMath::FRandRange(-20.f, 20.f), FMath::FRandRange(-20.f, 20.f), 0.f));
			}
			return;
		}
	}
}

void ATrapActor::TickCat(float DeltaSeconds)
{
	AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>();
	if (!CatRoot || !GM)
	{
		return;
	}
	// しっぽをゆらゆら
	if (CatTail)
	{
		CatTail->SetRelativeRotation(FRotator(-30.f, 25.f * FMath::Sin(AnimTime * (PetTimer > 0.f ? 9.f : 3.f)), 0.f));
	}
	if (!IsArmed())
	{
		// かくれんぼ以外：置いた場所で座っている
		CatRoot->SetWorldLocation(GetActorLocation());
		CatPath.Reset();
		PetTimer = 0.f;
		return;
	}

	// なでられている間は動かない（ゴロゴロ）
	if (PetTimer > 0.f)
	{
		PetTimer -= DeltaSeconds;
		return;
	}

	// 寄ってきた鬼は猫をなでる（どの鬼も猫には弱い。追いかけている鬼と探知鬼はなでない）
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector CatAt = CatRoot->GetComponentLocation();
	for (AOniCharacter* Oni : GM->GetOnis())
	{
		if (!Oni || !Oni->IsActive() || Oni->GetOniState() == EOniState::Stunned || Oni->GetIntent() == EOniState::Chase || Oni->OniType == EOniType::Detector)
		{
			continue;
		}
		const float* Until = PetCooldownUntil.Find(Oni);
		if ((Until && Now < *Until) || FVector::Dist2D(Oni->GetActorLocation(), CatAt) > CatPetRadius)
		{
			continue;
		}
		const float Seconds = Def.Duration > 0.f ? Def.Duration : 3.f;
		Oni->Pet(Seconds);
		PetCooldownUntil.Add(Oni, Now + Seconds + CatPetCooldownSeconds);
		PetTimer = Seconds;
		++PetCount;
		UKakurenboSoundSubsystem::Play3D(this, EKakurenboSfx::Meow, CatAt + FVector(0.f, 0.f, 40.f), 0.9f, 1.25f);
		return;
	}

	// ときどき「にゃーん」と鳴く（鬼に聞こえる）
	MeowTimer -= DeltaSeconds;
	if (MeowTimer <= 0.f)
	{
		MeowTimer = FMath::Max(1.f, Def.NoiseInterval) * FMath::FRandRange(0.8f, 1.3f);
		++MeowCount;
		UKakurenboSoundSubsystem::Play3D(this, EKakurenboSfx::Meow, CatAt + FVector(0.f, 0.f, 40.f), 1.f, FMath::FRandRange(0.92f, 1.1f));
		GM->HandleGadgetNoise(this, Def.NoiseLoudness, EKakurenboNoise::Cat);
	}

	// うろうろ歩く（家の近くをとことこ）
	if (CatPath.Num() == 0 || CatPathIndex >= CatPath.Num())
	{
		CatWait -= DeltaSeconds;
		if (CatWait <= 0.f)
		{
			CatWait = FMath::FRandRange(1.f, 3.f);
			ChooseCatDestination();
		}
		return;
	}
	const FVector Target = CatPath[CatPathIndex];
	const FVector To = Target - CatAt;
	const float Step = (Def.Speed > 0.f ? Def.Speed : 150.f) * DeltaSeconds;
	if (To.Size2D() <= Step)
	{
		CatRoot->SetWorldLocation(FVector(Target.X, Target.Y, GetActorLocation().Z));
		++CatPathIndex;
	}
	else
	{
		CatRoot->SetWorldLocation(CatAt + To.GetSafeNormal2D() * Step);
	}
	CatRoot->SetWorldRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
}

void ATrapActor::TickAnimation(float DeltaSeconds)
{
	AnimTime += DeltaSeconds;
	PingPulse = FMath::Max(0.f, PingPulse - DeltaSeconds * 3.f);
	if (Def.Kind == ETrapKind::Sticky)
	{
		// もちがゆっくり膨らんだり縮んだりする
		const float Breath = 1.f + 0.08f * FMath::Sin(AnimTime * 2.f);
		TopMesh->SetRelativeScale3D(FVector(TopScale.X * Breath, TopScale.Y * Breath, TopScale.Z / Breath));
	}
	else if (Def.Kind == ETrapKind::Decoy)
	{
		// 音を出すたびにぴょんと跳ねる
		const float Jump = 18.f * FMath::Sin(PingPulse * UE_PI);
		const float Swell = 1.f + 0.3f * PingPulse;
		BaseMesh->SetRelativeLocation(FVector(0.f, 0.f, 15.f + Jump));
		TopMesh->SetRelativeLocation(FVector(0.f, 0.f, 42.f + Jump));
		TopMesh->SetRelativeScale3D(TopScale * Swell);
	}
}
