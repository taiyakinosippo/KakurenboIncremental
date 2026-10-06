#include "PlaceableBlock.h"

#include "Components/StaticMeshComponent.h"
#include "KakurenboLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

APlaceableBlock::APlaceableBlock()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetStaticMesh(CubeFinder.Object);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetMobility(EComponentMobility::Movable); // 下の段が壊れたら落ちるので Movable
	RootComponent = Mesh;
}

void APlaceableBlock::InitBlock(int32 InWallTypeIndex, double InMaxHP, const FLinearColor& InColor)
{
	WallTypeIndex = InWallTypeIndex;
	MaxHP = FMath::Max(InMaxHP, 0.0001);
	HP = MaxHP;
	BaseColor = InColor;
	UKakurenboLibrary::ApplyColor(Mesh, BaseColor);
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

void APlaceableBlock::UpdateColor()
{
	// 傷ついた壁は暗く・赤っぽくする
	const float Ratio = static_cast<float>(HP / MaxHP);
	const FLinearColor Damaged = FMath::Lerp(FLinearColor(0.35f, 0.05f, 0.05f), BaseColor, Ratio);
	if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
	{
		MID->SetVectorParameterValue(TEXT("Color"), Damaged);
	}
}
