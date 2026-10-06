#include "KakurenboArena.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
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

	// bUnbound: 範囲を限定せず、ワールド全体に効かせる
	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(RootComponent);
	PostProcess->bUnbound = true;
}

FVector AKakurenboArena::GetGridOrigin() const
{
	const FVector Center = GetActorLocation();
	return FVector(Center.X - GridSizeX * CellSize * 0.5f, Center.Y - GridSizeY * CellSize * 0.5f, Center.Z);
}

void AKakurenboArena::BeginPlay()
{
	Super::BeginPlay();

	// 見た目の設定（bOverride_〇〇 を true にした項目だけが上書きされる）
	FPostProcessSettings& Look = PostProcess->Settings;
	Look.bOverride_AutoExposureMinBrightness = true;
	Look.AutoExposureMinBrightness = MinExposureEV100;
	Look.bOverride_MotionBlurAmount = bDisableMotionBlur;
	Look.MotionBlurAmount = 0.f;

	const float SizeX = GridSizeX * CellSize;
	const float SizeY = GridSizeY * CellSize;
	const FVector C = GetActorLocation();
	constexpr float FloorThickness = 50.f;
	constexpr float BorderThickness = 50.f;

	// 床：上面が Z=0 になるように半分下げる
	AddCube(C + FVector(0, 0, -FloorThickness * 0.5f), FVector(SizeX + BorderThickness * 2, SizeY + BorderThickness * 2, FloorThickness), FloorColor);

	// 舞台の外側の地面（俯瞰で見たときに外が真っ暗にならないように。床より一段低い）
	constexpr float OuterSize = 30000.f;
	AddCube(C + FVector(0, 0, -FloorThickness - 10.f), FVector(OuterSize, OuterSize, 20.f), OuterGroundColor);

	// 外周の壁（4 辺）
	const float Hz = BorderHeight * 0.5f;
	AddCube(C + FVector((SizeX + BorderThickness) * 0.5f, 0, Hz), FVector(BorderThickness, SizeY + BorderThickness * 2, BorderHeight), BorderColor);
	AddCube(C + FVector(-(SizeX + BorderThickness) * 0.5f, 0, Hz), FVector(BorderThickness, SizeY + BorderThickness * 2, BorderHeight), BorderColor);
	AddCube(C + FVector(0, (SizeY + BorderThickness) * 0.5f, Hz), FVector(SizeX, BorderThickness, BorderHeight), BorderColor);
	AddCube(C + FVector(0, -(SizeY + BorderThickness) * 0.5f, Hz), FVector(SizeX, BorderThickness, BorderHeight), BorderColor);

	// 外周の上の見えない柵（ブロックに乗ってジャンプしても外へ出られないように。三人称カメラも外へ出さない）。
	// 設置のカーソルや鬼の視線（Visibility）は通す
	constexpr float FenceHeight = 3000.f;
	const float Fz = BorderHeight + FenceHeight * 0.5f;
	auto AddFence = [this](const FVector& Center, const FVector& Size)
	{
		UStaticMeshComponent* Fence = AddCube(Center, Size, BorderColor);
		Fence->SetHiddenInGame(true);
		Fence->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	};
	AddFence(C + FVector((SizeX + BorderThickness) * 0.5f, 0, Fz), FVector(BorderThickness, SizeY + BorderThickness * 2, FenceHeight));
	AddFence(C + FVector(-(SizeX + BorderThickness) * 0.5f, 0, Fz), FVector(BorderThickness, SizeY + BorderThickness * 2, FenceHeight));
	AddFence(C + FVector(0, (SizeY + BorderThickness) * 0.5f, Fz), FVector(SizeX, BorderThickness, FenceHeight));
	AddFence(C + FVector(0, -(SizeY + BorderThickness) * 0.5f, Fz), FVector(SizeX, BorderThickness, FenceHeight));

	BuildOniGate(BorderThickness);

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

TArray<FIntPoint> AKakurenboArena::GetOniGateCells() const
{
	// 門（+X 側）に近い列から。各列では真ん中 → 左右の順
	TArray<FIntPoint> Cells;
	const int32 Width = FMath::Clamp(OniGateWidthCells, 1, GridSizeY);
	const int32 Depth = FMath::Clamp(OniGateDepthCells, 1, GridSizeX);
	const int32 MinY = GridSizeY / 2 - Width / 2;
	for (int32 Row = 0; Row < Depth; ++Row)
	{
		const int32 X = GridSizeX - 1 - Row;
		const int32 Mid = MinY + Width / 2;
		Cells.Add(FIntPoint(X, Mid));
		for (int32 Offset = 1; Offset <= Width; ++Offset)
		{
			if (Mid - Offset >= MinY)
			{
				Cells.Add(FIntPoint(X, Mid - Offset));
			}
			if (Mid + Offset < MinY + Width)
			{
				Cells.Add(FIntPoint(X, Mid + Offset));
			}
		}
	}
	return Cells;
}

FVector AKakurenboArena::GetOniGateLocation() const
{
	const FVector Origin = GetGridOrigin();
	const int32 Width = FMath::Clamp(OniGateWidthCells, 1, GridSizeY);
	const float CenterY = Origin.Y + (GridSizeY / 2 - Width / 2 + Width * 0.5f) * CellSize;
	return FVector(Origin.X + GridSizeX * CellSize, CenterY, Origin.Z);
}

void AKakurenboArena::BuildOniGate(float BorderThickness)
{
	const FVector Gate = GetOniGateLocation();
	const float Width = FMath::Clamp(OniGateWidthCells, 1, GridSizeY) * CellSize;
	const float WallX = Gate.X + BorderThickness * 0.5f; // 外周の壁の厚みの真ん中

	// 鳥居のような赤い門：柱 2 本と上の横木 2 本（外周の壁と同じ厚みに収めて、マスにははみ出さない）
	const float PostHeight = BorderHeight + 80.f;
	for (const float Side : { -1.f, 1.f })
	{
		AddCube(FVector(WallX, Gate.Y + Side * (Width * 0.5f + 20.f), PostHeight * 0.5f), FVector(BorderThickness, 40.f, PostHeight), OniGateColor);
	}
	AddCube(FVector(WallX, Gate.Y, PostHeight + 15.f), FVector(BorderThickness + 20.f, Width + 180.f, 30.f), OniGateColor);
	AddCube(FVector(WallX, Gate.Y, BorderHeight + 20.f), FVector(BorderThickness, Width + 40.f, 20.f), OniGateColor);

	// 門の中は真っ暗な戸（当たり判定なし）
	UStaticMeshComponent* Door = AddCube(FVector(Gate.X - 1.5f, Gate.Y, (BorderHeight - 20.f) * 0.5f), FVector(2.f, Width, BorderHeight - 20.f), FLinearColor(0.02f, 0.005f, 0.005f));
	Door->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 鬼が出てくるマスの床を赤くする（当たり判定なし。設置のカーソルは下の床に当たる）
	const FVector Origin = GetGridOrigin();
	for (const FIntPoint& Cell : GetOniGateCells())
	{
		const FVector Center(Origin.X + (Cell.X + 0.5f) * CellSize, Origin.Y + (Cell.Y + 0.5f) * CellSize, Origin.Z + 1.f);
		UStaticMeshComponent* Tile = AddCube(Center, FVector(CellSize * 0.94f, CellSize * 0.94f, 2.f), OniGateFloorColor);
		Tile->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
