#include "KakurenboArena.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "KakurenboLibrary.h"
#include "UObject/ConstructorHelpers.h"

AKakurenboArena::AKakurenboArena()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// エンジン付属の 100cm 立方体を使う（コンストラクタ内でのみ使えるアセット読み込み方法）
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeMesh = CubeFinder.Object;
}

FVector AKakurenboArena::GetGridOrigin() const
{
	const FVector Center = GetActorLocation();
	return FVector(Center.X - GridSizeX * CellSize * 0.5f, Center.Y - GridSizeY * CellSize * 0.5f, Center.Z);
}

void AKakurenboArena::BeginPlay()
{
	Super::BeginPlay();

	const float SizeX = GridSizeX * CellSize;
	const float SizeY = GridSizeY * CellSize;
	const FVector C = GetActorLocation();
	constexpr float FloorThickness = 50.f;
	constexpr float BorderThickness = 50.f;

	// 床：上面が Z=0 になるように半分下げる
	AddCube(C + FVector(0, 0, -FloorThickness * 0.5f), FVector(SizeX + BorderThickness * 2, SizeY + BorderThickness * 2, FloorThickness), FloorColor);

	// 外周の壁（4 辺）
	const float Hz = BorderHeight * 0.5f;
	AddCube(C + FVector((SizeX + BorderThickness) * 0.5f, 0, Hz), FVector(BorderThickness, SizeY + BorderThickness * 2, BorderHeight), BorderColor);
	AddCube(C + FVector(-(SizeX + BorderThickness) * 0.5f, 0, Hz), FVector(BorderThickness, SizeY + BorderThickness * 2, BorderHeight), BorderColor);
	AddCube(C + FVector(0, (SizeY + BorderThickness) * 0.5f, Hz), FVector(SizeX, BorderThickness, BorderHeight), BorderColor);
	AddCube(C + FVector(0, -(SizeY + BorderThickness) * 0.5f, Hz), FVector(SizeX, BorderThickness, BorderHeight), BorderColor);

	if (bSpawnLightingIfMissing)
	{
		// TActorIterator: ワールド内の指定クラスのアクターを列挙する
		bool bHasSun = false;
		for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
		{
			bHasSun = true;
			break;
		}
		if (!bHasSun)
		{
			SpawnLighting();
		}
	}
}

UStaticMeshComponent* AKakurenboArena::AddCube(const FVector& Center, const FVector& SizeCm, const FLinearColor& Color)
{
	// 実行時にコンポーネントを追加するときは NewObject → 設定 → RegisterComponent の順
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(CubeMesh);
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetWorldLocation(Center);
	Mesh->SetWorldScale3D(SizeCm / 100.f);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->RegisterComponent();
	UKakurenboLibrary::ApplyColor(Mesh, Color);
	return Mesh;
}

void AKakurenboArena::SpawnLighting()
{
	// 太陽
	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(this);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetupAttachment(RootComponent);
	Sun->SetWorldRotation(FRotator(-50.f, 30.f, 0.f));
	Sun->SetIntensity(8.f);
	Sun->SetAtmosphereSunLight(true);
	Sun->RegisterComponent();

	// 空（大気）
	USkyAtmosphereComponent* Sky = NewObject<USkyAtmosphereComponent>(this);
	Sky->SetupAttachment(RootComponent);
	Sky->RegisterComponent();

	// 環境光（空の色をリアルタイムに取り込む）
	USkyLightComponent* SkyLight = NewObject<USkyLightComponent>(this);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->bRealTimeCapture = true;
	SkyLight->RegisterComponent();
	SkyLight->RecaptureSky();
}
