#include "PlaceableBlock.h"

#include "Components/StaticMeshComponent.h"
#include "KakurenboLibrary.h"
#include "KakurenboTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

APlaceableBlock::APlaceableBlock()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetStaticMesh(CubeFinder.Object);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	// 三人称カメラはブロックをすり抜ける（壁で囲んでも外から自分が見えるように）
	Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Mesh->SetMobility(EComponentMobility::Movable); // 下の段が壊れたら落ちるので Movable
	RootComponent = Mesh;
}

void APlaceableBlock::InitBlock(int32 InWallTypeIndex, double InMaxHP, const FLinearColor& InColor, double InSoundHP, const FKakurenboSurfaceRow* Surface, bool bInLadder)
{
	bLadder = bInLadder;
	if (bLadder)
	{
		AddLadderRungs();
	}
	WallTypeIndex = InWallTypeIndex;
	MaxHP = FMath::Max(InMaxHP, 0.0001);
	HP = MaxHP;
	MaxSoundHP = FMath::Max(InSoundHP, 0.0);
	SoundHP = MaxSoundHP;
	BaseColor = InColor;
	// 木・石・鉄の模様（Data/Surfaces.csv）。無いパソコンでは色の箱
	bTextured = Surface && UKakurenboLibrary::ApplySurface(Mesh, *Surface);
	if (bTextured)
	{
		MaterialTint = Surface->Tint;
	}
	else
	{
		MaterialTint = BaseColor;
		UKakurenboLibrary::ApplyColor(Mesh, BaseColor);
	}
}

void APlaceableBlock::AddLadderRungs()
{
	// 4 つの面に横木を 3 本ずつ（はしごのように見える。クライミング壁はすばやく登れる）。
	// アクターは 1 マス × 1 段の大きさに伸ばしてあるので、部品の座標（-50〜50 の立方体）で置く。高さ方向の伸びは打ち消す
	const FVector ActorScale = GetActorScale3D();
	const float ZPerCm = 1.f / FMath::Max(ActorScale.Z, 0.01f);
	const float XPerCm = 1.f / FMath::Max(ActorScale.X, 0.01f);
	const float YPerCm = 1.f / FMath::Max(ActorScale.Y, 0.01f);
	const float BlockHeightCm = 100.f * ActorScale.Z;
	const FLinearColor RungColor(0.28f, 0.16f, 0.07f);
	for (int32 Face = 0; Face < 4; ++Face)
	{
		const bool bAlongX = Face < 2;              // +X・-X の面（横木は Y 方向に長い）
		const float Sign = (Face % 2 == 0) ? 1.f : -1.f;
		for (int32 Step = 0; Step < 3; ++Step)
		{
			const float HeightCm = BlockHeightCm * (0.22f + 0.28f * Step); // 床からの高さ
			UStaticMeshComponent* Rung = NewObject<UStaticMeshComponent>(this);
			Rung->SetStaticMesh(Mesh->GetStaticMesh());
			Rung->SetupAttachment(Mesh);
			Rung->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Rung->SetMobility(EComponentMobility::Movable);
			const float LocalZ = HeightCm * ZPerCm - 50.f;
			const FVector Location = bAlongX ? FVector(Sign * (50.f + 4.f * XPerCm), 0.f, LocalZ) : FVector(0.f, Sign * (50.f + 4.f * YPerCm), LocalZ);
			// 立方体（100cm）を 厚み 7cm・長さ 76cm・高さ 7cm にする
			const FVector Scale = bAlongX ? FVector(0.07f * XPerCm, 0.76f, 0.07f * ZPerCm) : FVector(0.76f, 0.07f * YPerCm, 0.07f * ZPerCm);
			Rung->SetRelativeLocation(Location);
			Rung->SetRelativeScale3D(Scale);
			Rung->RegisterComponent();
			UKakurenboLibrary::ApplyColor(Rung, RungColor);
			Rungs.Add(Rung);
		}
	}
}

bool APlaceableBlock::ApplySoundDamage(double Amount)
{
	if (!IsSoundBreakable())
	{
		return false;
	}
	SoundHP -= Amount;
	if (SoundHP <= 0.0)
	{
		SoundHP = 0.0;
		return true;
	}
	UpdateColor();
	return false;
}

void APlaceableBlock::ScaleSoundHP(double Factor)
{
	if (IsSoundBreakable())
	{
		MaxSoundHP = FMath::Max(MaxSoundHP * Factor, 0.0001);
		SoundHP *= Factor;
		UpdateColor();
	}
}

bool APlaceableBlock::ApplyBlockDamage(double Damage)
{
	HP -= Damage;
	if (HP <= 0.0)
	{
		HP = 0.0;
		return true;
	}
	UpdateColor();
	return false;
}

void APlaceableBlock::ScaleHP(double Factor)
{
	MaxHP = FMath::Max(MaxHP * Factor, 0.0001);
	HP *= Factor;
	UpdateColor();
}

void APlaceableBlock::UpdateColor()
{
	// 傷ついた壁（消音壁は音を消して弱った壁も）は暗く・赤っぽくする
	float Ratio = static_cast<float>(HP / MaxHP);
	if (IsSoundBreakable())
	{
		Ratio = FMath::Min(Ratio, static_cast<float>(SoundHP / MaxSoundHP));
	}
	const FLinearColor Damaged = FMath::Lerp(FLinearColor(0.35f, 0.05f, 0.05f), MaterialTint, Ratio);
	if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
	{
		MID->SetVectorParameterValue(TEXT("Color"), Damaged);
	}
}
