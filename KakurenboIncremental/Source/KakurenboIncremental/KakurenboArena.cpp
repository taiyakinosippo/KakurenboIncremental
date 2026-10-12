#include "KakurenboArena.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "KakurenboLibrary.h"
#include "Misc/PackageName.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogKakurenboArena, Log, All);

AKakurenboArena::AKakurenboArena()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// エンジン付属の 100cm の立方体・球・円柱を使う（コンストラクタ内でのみ使えるアセット読み込み方法）
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	CubeMesh = CubeFinder.Object;
	SphereMesh = SphereFinder.Object;
	CylinderMesh = CylinderFinder.Object;

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

FVector AKakurenboArena::CellCenter(const FIntPoint& Cell, float Z) const
{
	const FVector Origin = GetGridOrigin();
	return FVector(Origin.X + (Cell.X + 0.5f) * CellSize, Origin.Y + (Cell.Y + 0.5f) * CellSize, Origin.Z + Z);
}

void AKakurenboArena::BeginPlay()
{
	Super::BeginPlay();

	// 見た目の設定（bOverride_〇〇 を true にした項目だけが上書きされる）。
	// 夜の館：暗いところは暗いまま（露出の範囲を狭くする）、色は鮮やかに、四隅は暗く
	FPostProcessSettings& Look = PostProcess->Settings;
	Look.bOverride_AutoExposureMinBrightness = true;
	Look.AutoExposureMinBrightness = MinExposureEV100;
	Look.bOverride_AutoExposureMaxBrightness = true;
	Look.AutoExposureMaxBrightness = MaxExposureEV100;
	Look.bOverride_MotionBlurAmount = bDisableMotionBlur;
	Look.MotionBlurAmount = 0.f;
	Look.bOverride_VignetteIntensity = true;
	Look.VignetteIntensity = VignetteIntensity;
	Look.bOverride_ColorSaturation = true;
	Look.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.f);
	Look.bOverride_ColorContrast = true;
	Look.ColorContrast = FVector4(1.08f, 1.08f, 1.08f, 1.f);
	// 影の部分を少し紫に寄せる（夜っぽく、でも真っ黒にしない）
	Look.bOverride_ColorGainShadows = true;
	Look.ColorGainShadows = FVector4(0.95f, 0.88f, 1.2f, 1.f);
	Look.bOverride_BloomIntensity = true;
	Look.BloomIntensity = 0.6f;

	// 舞台の外側の地面（俯瞰で見たときに外が真っ暗にならないように。床より一段低い）
	constexpr float OuterSize = 30000.f;
	AddCube(GetActorLocation() + FVector(0, 0, -60.f), FVector(OuterSize, OuterSize, 20.f), OuterGroundColor);

	RebuildShell(GridSizeX, GridSizeY);

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

void AKakurenboArena::RebuildShell(int32 InSizeX, int32 InSizeY)
{
	// 前の大きさで作った部品を消す
	for (USceneComponent* Comp : ShellComponents)
	{
		if (Comp)
		{
			Comp->DestroyComponent();
		}
	}
	ShellComponents.Reset();
	BorderWalls.Reset();
	TallWalls.Reset();
	FloorTilesA = nullptr;
	FloorTilesB = nullptr;
	GridSizeX = FMath::Max(4, InSizeX);
	GridSizeY = FMath::Max(4, InSizeY);

	const float SizeX = GridSizeX * CellSize;
	const float SizeY = GridSizeY * CellSize;
	const FVector C = GetActorLocation();

	BuildFloor();

	// 外周の壁（4 辺）。色はマップごとに塗り替える。
	// 下の部分（部屋の壁と同じ高さ）と、外の世界が見えないようにする高い部分に分ける（高い部分は上から見下ろすパートでは隠す）
	const FLinearColor DefaultWall(0.2f, 0.1f, 0.3f);
	const float TallHeight = FMath::Max(OuterWallHeight - BorderHeight, 1.f);
	auto AddBorder = [&](const FVector& XY, const FVector& Size)
	{
		BorderWalls.Add(AddShellCube(C + XY + FVector(0.f, 0.f, BorderHeight * 0.5f), FVector(Size.X, Size.Y, BorderHeight), DefaultWall));
		UStaticMeshComponent* Tall = AddShellCube(C + XY + FVector(0.f, 0.f, BorderHeight + TallHeight * 0.5f), FVector(Size.X, Size.Y, TallHeight), DefaultWall);
		Tall->SetCastShadow(false); // 高い壁の長い影で舞台の端が真っ暗にならないように
		BorderWalls.Add(Tall);
		TallWalls.Add(Tall);
	};
	AddBorder(FVector((SizeX + BorderThickness) * 0.5f, 0, 0), FVector(BorderThickness, SizeY + BorderThickness * 2, 0));
	AddBorder(FVector(-(SizeX + BorderThickness) * 0.5f, 0, 0), FVector(BorderThickness, SizeY + BorderThickness * 2, 0));
	AddBorder(FVector(0, (SizeY + BorderThickness) * 0.5f, 0), FVector(SizeX, BorderThickness, 0));
	AddBorder(FVector(0, -(SizeY + BorderThickness) * 0.5f, 0), FVector(SizeX, BorderThickness, 0));

	// 外周の上の見えない柵（ブロックに乗ってジャンプしても外へ出られないように。三人称カメラも外へ出さない）。
	// 設置のカーソルや鬼の視線（Visibility）は通す
	constexpr float FenceHeight = 3000.f;
	const float Fz = OuterWallHeight + FenceHeight * 0.5f;
	auto AddFence = [this](const FVector& Center, const FVector& Size)
	{
		UStaticMeshComponent* Fence = AddShellCube(Center, Size, FLinearColor::Black);
		Fence->SetHiddenInGame(true);
		Fence->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	};
	AddFence(C + FVector((SizeX + BorderThickness) * 0.5f, 0, Fz), FVector(BorderThickness, SizeY + BorderThickness * 2, FenceHeight));
	AddFence(C + FVector(-(SizeX + BorderThickness) * 0.5f, 0, Fz), FVector(BorderThickness, SizeY + BorderThickness * 2, FenceHeight));
	AddFence(C + FVector(0, (SizeY + BorderThickness) * 0.5f, Fz), FVector(SizeX, BorderThickness, FenceHeight));
	AddFence(C + FVector(0, -(SizeY + BorderThickness) * 0.5f, Fz), FVector(SizeX, BorderThickness, FenceHeight));

	BuildOniGate(BorderThickness);
	SetTallWallsVisible(bTallWallsVisible);
	UE_LOG(LogKakurenboArena, Log, TEXT("Arena shell rebuilt: %d x %d cells"), GridSizeX, GridSizeY);
}

void AKakurenboArena::SetTallWallsVisible(bool bVisible)
{
	bTallWallsVisible = bVisible;
	for (UStaticMeshComponent* Wall : TallWalls)
	{
		if (Wall)
		{
			Wall->SetHiddenInGame(!bVisible);
		}
	}
	if (TallWallPanels)
	{
		TallWallPanels->SetHiddenInGame(!bVisible);
	}
	for (UStaticMeshComponent* Wall : TallRoomWalls)
	{
		if (Wall)
		{
			Wall->SetHiddenInGame(!bVisible);
		}
	}
}

void AKakurenboArena::BuildFloor()
{
	const FVector C = GetActorLocation();
	const float SizeX = GridSizeX * CellSize;
	const float SizeY = GridSizeY * CellSize;

	// 当たり判定のある床：上面が Z=0
	constexpr float FloorThickness = 50.f;
	AddShellCube(C + FVector(0, 0, -FloorThickness * 0.5f), FVector(SizeX + BorderThickness * 2, SizeY + BorderThickness * 2, FloorThickness), FLinearColor(0.05f, 0.03f, 0.03f));

	// 見た目だけの市松模様のタイル（InstancedStaticMesh: 同じメッシュをたくさん並べても 1 つの部品で描ける）
	auto MakeTiles = [this](const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* Tiles = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), Name));
		Tiles->SetStaticMesh(CubeMesh);
		Tiles->SetupAttachment(RootComponent);
		Tiles->SetMobility(EComponentMobility::Movable);
		Tiles->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Tiles->RegisterComponent();
		ShellComponents.Add(Tiles);
		return Tiles;
	};
	FloorTilesA = MakeTiles(TEXT("FloorTilesA"));
	FloorTilesB = MakeTiles(TEXT("FloorTilesB"));
	for (int32 Y = 0; Y < GridSizeY; ++Y)
	{
		for (int32 X = 0; X < GridSizeX; ++X)
		{
			// 上面が床の少し上（0.3cm）に来る薄い板
			const FTransform Tile(FRotator::ZeroRotator, CellCenter(FIntPoint(X, Y), -0.2f), FVector(CellSize * 0.995f / 100.f, CellSize * 0.995f / 100.f, 0.01f));
			((X + Y) % 2 == 0 ? FloorTilesA : FloorTilesB)->AddInstance(Tile, true);
		}
	}
	UKakurenboLibrary::ApplyColor(FloorTilesA, FLinearColor(0.16f, 0.09f, 0.06f));
	UKakurenboLibrary::ApplyColor(FloorTilesB, FLinearColor(0.12f, 0.07f, 0.05f));
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

void AKakurenboArena::BuildOniGate(float InBorderThickness)
{
	const FVector Gate = GetOniGateLocation();
	const float Width = FMath::Clamp(OniGateWidthCells, 1, GridSizeY) * CellSize;
	// 鳥居のような赤い門：柱 2 本と上の横木 2 本。外周の壁の内側の面（壁に貼る板の手前）に薄く付ける。
	// 見た目だけ（当たり判定なし）なので、門の横のマスに置く壁・鬼の出入りの邪魔はしない
	const float FrontX = Gate.X - 12.f;
	const float PostHeight = BorderHeight + 80.f;
	auto AddGatePart = [&](const FVector& Center, const FVector& Size)
	{
		UStaticMeshComponent* Part = AddShellCube(Center, Size, OniGateColor);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	};
	for (const float Side : { -1.f, 1.f })
	{
		AddGatePart(FVector(FrontX, Gate.Y + Side * (Width * 0.5f + 20.f), PostHeight * 0.5f), FVector(20.f, 40.f, PostHeight));
	}
	AddGatePart(FVector(FrontX - 4.f, Gate.Y, PostHeight + 15.f), FVector(28.f, Width + 180.f, 30.f));
	AddGatePart(FVector(FrontX, Gate.Y, BorderHeight + 20.f), FVector(20.f, Width + 40.f, 20.f));
	(void)InBorderThickness;

	// 門の中は真っ暗な戸（当たり判定なし）
	UStaticMeshComponent* Door = AddShellCube(FVector(Gate.X - 4.f, Gate.Y, (BorderHeight - 20.f) * 0.5f), FVector(2.f, Width, BorderHeight - 20.f), FLinearColor(0.02f, 0.005f, 0.005f));
	Door->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 門の上の赤いちょうちん（どこから鬼が来るか、暗くてもわかるように）
	UPointLightComponent* Lantern = NewObject<UPointLightComponent>(this);
	ShellComponents.Add(Lantern);
	Lantern->SetMobility(EComponentMobility::Movable);
	Lantern->SetupAttachment(RootComponent);
	Lantern->SetWorldLocation(FVector(Gate.X - 60.f, Gate.Y, BorderHeight + 40.f));
	Lantern->SetIntensityUnits(ELightUnits::Candelas);
	Lantern->SetIntensity(30.f);
	Lantern->SetAttenuationRadius(700.f);
	Lantern->SetLightColor(FLinearColor(1.f, 0.15f, 0.1f));
	Lantern->SetCastShadows(false);
	Lantern->RegisterComponent();

	// 鬼が出てくるマスの床を赤くする（当たり判定なし。設置のカーソルは下の床に当たる）
	for (const FIntPoint& Cell : GetOniGateCells())
	{
		UStaticMeshComponent* Tile = AddShellCube(CellCenter(Cell, 1.f), FVector(CellSize * 0.94f, CellSize * 0.94f, 2.f), OniGateFloorColor);
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

UStaticMeshComponent* AKakurenboArena::AddShellCube(const FVector& Center, const FVector& SizeCm, const FLinearColor& Color)
{
	UStaticMeshComponent* Mesh = AddCube(Center, SizeCm, Color);
	ShellComponents.Add(Mesh);
	return Mesh;
}

UInstancedStaticMeshComponent* AKakurenboArena::MakePanelSet(UMaterialInterface* Material)
{
	UInstancedStaticMeshComponent* Panels = NewObject<UInstancedStaticMeshComponent>(this);
	Panels->SetStaticMesh(CubeMesh);
	Panels->SetupAttachment(RootComponent);
	Panels->SetMobility(EComponentMobility::Movable);
	Panels->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Panels->SetMaterial(0, Material);
	Panels->RegisterComponent();
	MapComponents.Add(Panels);
	return Panels;
}

void AKakurenboArena::AddFacePanels(UInstancedStaticMeshComponent* Panels, const FVector& FaceStart, const FVector& Along, const FVector& Normal, float Length, float ZFrom, float ZTo)
{
	// 壁の面を 1 マス幅 × WallPanelHeight の薄い板で埋める（板 1 枚に模様が 1 回入るので、大きな壁でも模様が引き伸ばされない）
	if (!Panels || Length <= 1.f || ZTo - ZFrom <= 1.f)
	{
		return;
	}
	const int32 Columns = FMath::Max(1, FMath::RoundToInt(Length / CellSize));
	const int32 Rows = FMath::Max(1, FMath::RoundToInt((ZTo - ZFrom) / WallPanelHeight));
	const float W = Length / Columns;
	const float H = (ZTo - ZFrom) / Rows;
	const FRotator Rot = Normal.Rotation(); // 板の X（厚み）を面の外向きに
	for (int32 Col = 0; Col < Columns; ++Col)
	{
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const FVector Center = FaceStart + Along * (W * (Col + 0.5f)) + Normal * 1.f + FVector(0.f, 0.f, ZFrom + H * (Row + 0.5f));
			Panels->AddInstance(FTransform(Rot, Center, FVector(0.02f, W * 0.998f / 100.f, H * 0.998f / 100.f)), true);
		}
	}
}

UStaticMeshComponent* AKakurenboArena::AddMapCube(const FVector& Center, const FVector& SizeCm, const FLinearColor& Color, bool bCollision)
{
	UStaticMeshComponent* Mesh = AddCube(Center, SizeCm, Color);
	if (!bCollision)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	MapComponents.Add(Mesh);
	return Mesh;
}

void AKakurenboArena::AddMapLight(const FVector& Location, const FLinearColor& Color, float Intensity, float Radius)
{
	UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetupAttachment(RootComponent);
	Light->SetWorldLocation(Location);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(Intensity);
	Light->SetAttenuationRadius(Radius);
	Light->SetLightColor(Color);
	Light->SetCastShadows(false); // 影を落とす明かりは重いので、壁の影は月明かりだけにする
	Light->RegisterComponent();
	MapComponents.Add(Light);
}

void AKakurenboArena::ClearMap()
{
	for (USceneComponent* Comp : MapComponents)
	{
		if (Comp)
		{
			Comp->DestroyComponent();
		}
	}
	MapComponents.Reset();
	WallPanels = nullptr;
	TallWallPanels = nullptr;
	WainscotPanels = nullptr;
	FloorKindCount = 0;
	DecorCounts.Reset();
	TallRoomWalls.Reset();
	MaxFurnitureOverhang = 0.f;
	FurnitureStraddleCount = 0;
	FurnitureCollisionBoxes = 0;
	DoorHeaders = 0;
	CurrentWallMaterial = nullptr;
	bUseWallPanels = false;
	FurniturePieces = 0;
	FurnitureMeshes = 0;
}

void AKakurenboArena::BuildFloorTiles(const KakurenboMaps::FLayout& FloorLayer, const TMap<TCHAR, FKakurenboFloorLook>& FloorLooks, const FKakurenboMapRow& Map)
{
	// 床の種類ごと・市松模様の明るい方 / 暗い方ごとに、タイルの集まりを作る（部屋ごとに床が変わる）
	TMap<TCHAR, TPair<UInstancedStaticMeshComponent*, UInstancedStaticMeshComponent*>> Sets;
	bFloorUsesMaterial = false;
	auto LookOf = [&](TCHAR Kind) -> FKakurenboFloorLook
	{
		if (const FKakurenboFloorLook* Found = FloorLooks.Find(Kind))
		{
			return *Found;
		}
		if (const FKakurenboFloorLook* Default = FloorLooks.Find(KakurenboMaps::DefaultFloorChar))
		{
			return *Default;
		}
		FKakurenboFloorLook Fallback;
		Fallback.Color = Map.FloorColor;
		return Fallback;
	};
	auto MakeTiles = [this](UMaterialInterface* Material, const FLinearColor& Color)
	{
		UInstancedStaticMeshComponent* Tiles = NewObject<UInstancedStaticMeshComponent>(this);
		Tiles->SetStaticMesh(CubeMesh);
		Tiles->SetupAttachment(RootComponent);
		Tiles->SetMobility(EComponentMobility::Movable);
		Tiles->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Tiles->RegisterComponent();
		if (Material)
		{
			Tiles->SetMaterial(0, Material);
		}
		else
		{
			UKakurenboLibrary::ApplyColor(Tiles, Color);
		}
		MapComponents.Add(Tiles);
		return Tiles;
	};
	for (int32 Y = 0; Y < GridSizeY; ++Y)
	{
		for (int32 X = 0; X < GridSizeX; ++X)
		{
			const TCHAR Kind = FloorLayer.IsInside(FIntPoint(X, Y)) ? FloorLayer.Get(FIntPoint(X, Y)) : KakurenboMaps::DefaultFloorChar;
			TPair<UInstancedStaticMeshComponent*, UInstancedStaticMeshComponent*>* Set = Sets.Find(Kind);
			if (!Set)
			{
				const FKakurenboFloorLook Look = LookOf(Kind);
				bFloorUsesMaterial |= Look.Material != nullptr;
				Set = &Sets.Add(Kind, TPair<UInstancedStaticMeshComponent*, UInstancedStaticMeshComponent*>(
					MakeTiles(Look.Material, Look.Color), MakeTiles(Look.MaterialAlt ? Look.MaterialAlt : Look.Material, Look.Color * 0.72f)));
			}
			// 上面が床の少し上（0.3cm）に来る薄い板
			const FTransform Tile(FRotator::ZeroRotator, CellCenter(FIntPoint(X, Y), -0.2f), FVector(CellSize * 0.995f / 100.f, CellSize * 0.995f / 100.f, 0.01f));
			((X + Y) % 2 == 0 ? Set->Key : Set->Value)->AddInstance(Tile, true);
		}
	}
	FloorKindCount = Sets.Num();
	// 大きさを変えたときの床（色の市松模様）は隠す（種類ごとのタイルが上に乗る）
	for (UInstancedStaticMeshComponent* Shell : { FloorTilesA.Get(), FloorTilesB.Get() })
	{
		if (Shell)
		{
			Shell->SetHiddenInGame(true);
		}
	}
}

UStaticMeshComponent* AKakurenboArena::PlaceDecor(const FKakurenboDecorRow& Row, UStaticMesh* Mesh, const FVector& Center, const FVector& Normal, float WidthOverride)
{
	// メッシュの薄い向きを奥行き（壁から部屋の側）とみなし、その向きを部屋へ向ける（逆なら Decor.csv の MeshYaw = 180）
	const FBox Bounds = Mesh->GetBoundingBox();
	const FVector Ext = Bounds.GetSize();
	const bool bThinX = Ext.X <= Ext.Y;
	const float MeshWidth = FMath::Max(static_cast<float>(bThinX ? Ext.Y : Ext.X), 1.f);
	const float MeshDepth = FMath::Max(static_cast<float>(bThinX ? Ext.X : Ext.Y), 1.f);
	const float MeshHeight = FMath::Max(static_cast<float>(Ext.Z), 1.f);
	const float Width = WidthOverride > 0.f ? WidthOverride : Row.Width;
	const float Scale = FMath::Min(Width / MeshWidth, Row.Height / MeshHeight);
	const FRotator Rot(0.f, Normal.Rotation().Yaw + (bThinX ? 0.f : -90.f) + Row.MeshYaw, 0.f);
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	Comp->SetupAttachment(RootComponent);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetWorldScale3D(FVector(Scale));
	Comp->SetWorldRotation(Rot);
	// メッシュの中心（原点がずれているアセットもある）を、壁から奥行きの半分だけ出した所に。下の端を Bottom の高さに
	const FVector Pivot = Rot.RotateVector(Bounds.GetCenter() * Scale);
	const FVector Wanted = Center + Normal * (Row.Offset + MeshDepth * Scale * 0.5f);
	Comp->SetWorldLocation(FVector(Wanted.X - Pivot.X, Wanted.Y - Pivot.Y, Center.Z + Row.Bottom - Bounds.Min.Z * Scale));
	Comp->RegisterComponent();
	MapComponents.Add(Comp);
	DecorCounts.FindOrAdd(Row.Kind)++;
	return Comp;
}

void AKakurenboArena::BuildDecor(const KakurenboMaps::FLayout& Layout, const TArray<FKakurenboDecorRow>& Decor, const TMap<TCHAR, FKakurenboFurnitureRow>& Furniture,
	const FKakurenboMapRow& Map, const TArray<FIntPoint>& KeepClear)
{
	// 種類ごとの飾り（このパソコンにメッシュがあるものだけ）
	TMap<EKakurenboDecorKind, TArray<TPair<const FKakurenboDecorRow*, UStaticMesh*>>> ByKind;
	for (const FKakurenboDecorRow& Row : Decor)
	{
		if (Row.Mesh.IsNull() || !FPackageName::DoesPackageExist(Row.Mesh.ToSoftObjectPath().GetLongPackageName()))
		{
			continue;
		}
		if (UStaticMesh* Mesh = Row.Mesh.LoadSynchronous())
		{
			ByKind.FindOrAdd(Row.Kind).Add(TPair<const FKakurenboDecorRow*, UStaticMesh*>(&Row, Mesh));
		}
	}
	// メッシュが 1 つも無くても（Fab のアセットが無いパソコン）、出入り口の上の壁は作る（Pick が null を返すので飾りは置かれない）
	auto Pick = [&ByKind](EKakurenboDecorKind Kind, uint32 Hash) -> const TPair<const FKakurenboDecorRow*, UStaticMesh*>*
	{
		const TArray<TPair<const FKakurenboDecorRow*, UStaticMesh*>>* List = ByKind.Find(Kind);
		return (List && List->Num() > 0) ? &(*List)[Hash % List->Num()] : nullptr;
	};
	auto IsWall = [&Layout](const FIntPoint& P) { return Layout.IsInside(P) && Layout.Get(P) == KakurenboMaps::WallChar; };
	auto IsOpen = [&Layout](const FIntPoint& P) { return Layout.IsInside(P) && Layout.Get(P) != KakurenboMaps::WallChar; };
	auto HeightAt = [&](const FIntPoint& P) -> float
	{
		const TCHAR C = Layout.Get(P);
		const FKakurenboFurnitureRow* Row = C == KakurenboMaps::EmptyChar ? nullptr : Furniture.Find(C);
		return (Row && !Row->bWalkable) ? Row->Height : 0.f;
	};
	const FVector Origin = GetGridOrigin();

	// 1) 出入り口の枠：壁の並びの中の、1〜2 マスの通り道（両側が壁で、前後が部屋か廊下）
	TSet<FIntPoint> DoorCells;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const FIntPoint Along = Axis == 0 ? FIntPoint(1, 0) : FIntPoint(0, 1); // 壁の並びの向き
		const FIntPoint Across = Axis == 0 ? FIntPoint(0, 1) : FIntPoint(1, 0); // 通り抜ける向き
		for (int32 Y = 0; Y < Layout.SizeY; ++Y)
		{
			for (int32 X = 0; X < Layout.SizeX; ++X)
			{
				const FIntPoint Start(X, Y);
				if (!IsWall(Start - Along) || !IsOpen(Start) || IsWall(Start + Across) || IsWall(Start - Across))
				{
					continue;
				}
				// 壁の並びの切れ目の長さ（前後が開いているマスが続く数）。1〜2 マスで、その先がまた壁なら出入り口
				int32 Length = 1;
				while (Length < 3 && IsOpen(Start + Along * Length) && IsOpen(Start + Along * Length + Across) && IsOpen(Start + Along * Length - Across))
				{
					++Length;
				}
				if (Length > 2 || !IsWall(Start + Along * Length))
				{
					continue;
				}
				// 壁の並びになっているか（通り道の両隣の壁が、通り抜ける向きにも続いていない＝壁の線）
				if (IsWall(Start - Along + Across) || IsWall(Start - Along - Across))
				{
					continue;
				}
				for (int32 i = 0; i < Length; ++i)
				{
					DoorCells.Add(Start + Along * i);
				}
				const TPair<const FKakurenboDecorRow*, UStaticMesh*>* Frame = Pick(Length == 2 ? EKakurenboDecorKind::DoorFrameWide : EKakurenboDecorKind::DoorFrame, X * 31 + Y * 17);
				const FVector Mid = (CellCenter(Start) + CellCenter(Start + Along * (Length - 1))) * 0.5f;
				const FVector Normal(Across.X, Across.Y, 0.f);
				float HeaderBottom = DoorHeaderHeight;
				if (Frame)
				{
					// 枠は通り道の真ん中（壁の厚みの真ん中）に、通り抜ける向きを正面にして置く（PlaceDecor は奥行きの半分だけ前へ出すので、その分戻す）
					const FBox FrameBox = Frame->Value->GetBoundingBox();
					const FVector FrameSize = FrameBox.GetSize();
					const float Depth = static_cast<float>(FMath::Min(FrameSize.X, FrameSize.Y));
					const float Scale = static_cast<float>(FMath::Min(Length * CellSize / FMath::Max(FMath::Max(FrameSize.X, FrameSize.Y), 1.0), Frame->Key->Height / FMath::Max(FrameSize.Z, 1.0)));
					if (const UStaticMeshComponent* FrameComp = PlaceDecor(*Frame->Key, Frame->Value, FVector(Mid.X, Mid.Y, Origin.Z) - Normal * (Frame->Key->Offset + Depth * Scale * 0.5f), Normal, Length * CellSize))
					{
						// 上の壁は枠のてっぺんから（枠が壁を支えているように少し重ねる）
						HeaderBottom = FMath::Clamp(static_cast<float>(FrameComp->Bounds.GetBox().Max.Z - Origin.Z) - 6.f, 170.f, DoorHeaderHeight + 40.f);
					}
				}
				// M14：出入り口の上の壁（かくれんぼ中だけ見える高い部分と一緒に出す）。無いと枠だけが天井まで抜けた隙間に立って見えた。
				// 当たり判定はカメラだけ（出入り口に置いた壁・よじ登り・鬼の飛び乗りの邪魔をしない）
				const float HeaderHeight = OuterWallHeight - HeaderBottom;
				UStaticMeshComponent* Header = AddMapCube(FVector(Mid.X, Mid.Y, Origin.Z + HeaderBottom + HeaderHeight * 0.5f),
					FVector(Along.X != 0 ? Length * CellSize : CellSize, Along.Y != 0 ? Length * CellSize : CellSize, HeaderHeight), Map.WallColor, false);
				Header->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Header->SetCollisionResponseToAllChannels(ECR_Ignore);
				Header->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
				Header->SetCastShadow(false);
				TallRoomWalls.Add(Header);
				++DoorHeaders;
				if (TallWallPanels)
				{
					// 両側の面に壁と同じ板（壁紙）を貼る
					const FVector AlongV(Along.X, Along.Y, 0.f);
					const FVector FaceStart = FVector(Mid.X, Mid.Y, Origin.Z) - AlongV * (Length * CellSize * 0.5f);
					AddFacePanels(TallWallPanels, FaceStart + Normal * (CellSize * 0.5f), AlongV, Normal, Length * CellSize, HeaderBottom, OuterWallHeight);
					AddFacePanels(TallWallPanels, FaceStart - Normal * (CellSize * 0.5f), AlongV, -Normal, Length * CellSize, HeaderBottom, OuterWallHeight);
				}
			}
		}
	}

	// 2) 壁の面：外周の壁には窓、どの壁にも絵と明かり（決まった間隔で。背の高い家具の後ろ・出入り口の横・鬼の門のまわりは除く）
	static const FIntPoint Dirs[4] = { FIntPoint(0, -1), FIntPoint(0, 1), FIntPoint(-1, 0), FIntPoint(1, 0) };
	int32 Lights = 0;
	for (int32 Y = 0; Y < Layout.SizeY; ++Y)
	{
		for (int32 X = 0; X < Layout.SizeX; ++X)
		{
			const FIntPoint P(X, Y);
			if (!IsOpen(P) || DoorCells.Contains(P) || KeepClear.Contains(P) || HeightAt(P) >= 140.f)
			{
				continue;
			}
			for (int32 d = 0; d < 4; ++d)
			{
				const FIntPoint N = P + Dirs[d];
				const bool bOuter = !Layout.IsInside(N);
				if (!bOuter && !IsWall(N))
				{
					continue;
				}
				// 出入り口のすぐ横の壁には飾らない（枠と重なる）
				const FIntPoint Side = d < 2 ? FIntPoint(1, 0) : FIntPoint(0, 1);
				if (DoorCells.Contains(P + Side) || DoorCells.Contains(P - Side) || DoorCells.Contains(N + Side) || DoorCells.Contains(N - Side))
				{
					continue;
				}
				const int32 AlongIndex = d < 2 ? X : Y;
				const uint32 Hash = static_cast<uint32>(X * 73856093) ^ static_cast<uint32>(Y * 19349663) ^ static_cast<uint32>(d * 83492791);
				const FVector Normal(-Dirs[d].X, -Dirs[d].Y, 0.f);
				const FVector FaceCenter = CellCenter(P) + FVector(Dirs[d].X, Dirs[d].Y, 0.f) * (CellSize * 0.5f);
				EKakurenboDecorKind Kind = EKakurenboDecorKind::Painting;
				bool bPlace = false;
				if (AlongIndex % 6 == 4)
				{
					Kind = EKakurenboDecorKind::Sconce;
					bPlace = Lights < 64; // 影を落とさない明かりなので、大きな館でも 64 個まで
				}
				else if (bOuter && AlongIndex % 4 == 1)
				{
					Kind = EKakurenboDecorKind::Window;
					bPlace = true;
				}
				else if (AlongIndex % 3 == 2 && Hash % 3 != 0)
				{
					Kind = EKakurenboDecorKind::Painting;
					bPlace = true;
				}
				const TPair<const FKakurenboDecorRow*, UStaticMesh*>* Chosen = bPlace ? Pick(Kind, Hash) : nullptr;
				if (!Chosen)
				{
					continue;
				}
				UStaticMeshComponent* Comp = PlaceDecor(*Chosen->Key, Chosen->Value, FVector(FaceCenter.X, FaceCenter.Y, Origin.Z), Normal);
				if (Kind == EKakurenboDecorKind::Sconce && Chosen->Key->LightIntensity > 0.f && Comp)
				{
					++Lights;
					// 壁の明かりは館の明かりの色を少し白っぽく（真っ橙にならないように）
					AddMapLight(Comp->Bounds.Origin + Normal * 30.f + FVector(0.f, 0.f, Comp->Bounds.BoxExtent.Z * 0.6f), FMath::Lerp(Map.LightColor, FLinearColor::White, 0.5f),
						Chosen->Key->LightIntensity, 650.f);
				}
				else if (Kind == EKakurenboDecorKind::Window && Comp)
				{
					// 窓の向こう（夜空）：枠の中に見える暗い青の板。枠は壁に半分埋めてある（Decor.csv の Offset が負）
					const FKakurenboDecorRow& Row = *Chosen->Key;
					AddMapCube(FVector(FaceCenter.X, FaceCenter.Y, Origin.Z + Row.Bottom + Row.Height * 0.5f) + Normal * 3.f,
						FVector(Normal.X != 0.f ? 2.f : Row.Width * 0.8f, Normal.Y != 0.f ? 2.f : Row.Width * 0.8f, Row.Height * 0.82f), FLinearColor(0.015f, 0.03f, 0.08f), false);
				}
			}
		}
	}
}

void AKakurenboArena::ApplyMap(const FKakurenboMapRow& Map, const KakurenboMaps::FLayout& Layout, const TMap<TCHAR, FKakurenboFurnitureRow>& Furniture,
	const TArray<FIntPoint>& KeepClear, TArray<FIntPoint>& OutObstacles,
	const KakurenboMaps::FLayout& FloorLayer, const TMap<TCHAR, FKakurenboFloorLook>& FloorLooks,
	UMaterialInterface* WallMaterial, UMaterialInterface* WainscotMaterial, const TArray<FKakurenboDecorRow>* Decor)
{
	ClearMap();
	OutObstacles.Reset();

	// 床：部屋ごとの種類（木の床・寄木・タイル・石…）。テクスチャが無ければ色の市松模様
	CurrentFloorLayer = FloorLayer;
	CurrentLayout = Layout;
	BuildFloorTiles(FloorLayer, FloorLooks, Map);

	// 外周の壁：マテリアル（木の板の壁など）があれば内側の面に板を貼る。下の方は腰壁（あれば）。無ければ壁紙の色
	CurrentWallMaterial = WallMaterial;
	bUseWallPanels = CurrentWallMaterial != nullptr;
	for (UStaticMeshComponent* Wall : BorderWalls)
	{
		UKakurenboLibrary::ApplyColor(Wall, Map.WallColor);
	}
	const FVector C = GetActorLocation();
	const float SizeX = GridSizeX * CellSize;
	const float SizeY = GridSizeY * CellSize;
	if (bUseWallPanels)
	{
		WallPanels = MakePanelSet(CurrentWallMaterial);
		TallWallPanels = MakePanelSet(CurrentWallMaterial);
		TallWallPanels->SetCastShadow(false);
		WainscotPanels = WainscotMaterial ? MakePanelSet(WainscotMaterial) : nullptr;
		const FVector Origin = GetGridOrigin();
		struct FFace { FVector Start; FVector Along; FVector Normal; float Length; };
		const FFace Faces[4] = {
			{ FVector(Origin.X, Origin.Y, 0.f), FVector(0, 1, 0), FVector(1, 0, 0), SizeY },                 // 西の壁（内側は +X 向き）
			{ FVector(Origin.X + SizeX, Origin.Y, 0.f), FVector(0, 1, 0), FVector(-1, 0, 0), SizeY },        // 東の壁
			{ FVector(Origin.X, Origin.Y, 0.f), FVector(1, 0, 0), FVector(0, 1, 0), SizeX },                 // 北の壁
			{ FVector(Origin.X, Origin.Y + SizeY, 0.f), FVector(1, 0, 0), FVector(0, -1, 0), SizeX },        // 南の壁
		};
		for (const FFace& Face : Faces)
		{
			const float LowTop = WainscotPanels ? WainscotHeight : 0.f;
			if (WainscotPanels)
			{
				AddFacePanels(WainscotPanels, Face.Start, Face.Along, Face.Normal, Face.Length, 0.f, LowTop);
			}
			AddFacePanels(WallPanels, Face.Start, Face.Along, Face.Normal, Face.Length, LowTop, BorderHeight);
			AddFacePanels(TallWallPanels, Face.Start, Face.Along, Face.Normal, Face.Length, BorderHeight, OuterWallHeight);
		}
	}
	SetTallWallsVisible(bTallWallsVisible);

	// 外周の壁の飾りの縁（壁紙より明るい色）：部屋の壁の高さの所にぐるりと
	const FLinearColor Trim = FLinearColor::LerpUsingHSV(Map.WallColor, FLinearColor(1.f, 0.85f, 0.5f), 0.45f);
	const float TrimZ = BorderHeight + 6.f;
	AddMapCube(C + FVector((SizeX + BorderThickness) * 0.5f - 8.f, 0, TrimZ), FVector(BorderThickness + 16.f, SizeY + BorderThickness * 2 + 16.f, 12.f), Trim, false);
	AddMapCube(C + FVector(-(SizeX + BorderThickness) * 0.5f + 8.f, 0, TrimZ), FVector(BorderThickness + 16.f, SizeY + BorderThickness * 2 + 16.f, 12.f), Trim, false);
	AddMapCube(C + FVector(0, (SizeY + BorderThickness) * 0.5f - 8.f, TrimZ), FVector(SizeX, BorderThickness + 16.f, 12.f), Trim, false);
	AddMapCube(C + FVector(0, -(SizeY + BorderThickness) * 0.5f + 8.f, TrimZ), FVector(SizeX, BorderThickness + 16.f, 12.f), Trim, false);

	for (const KakurenboMaps::FPiece& Piece : KakurenboMaps::FindPieces(Layout))
	{
		// 鬼の出入り口の前にかかる家具は置かない（間取りの書き間違い）
		bool bBlocked = false;
		for (int32 DY = 0; DY < Piece.Size.Y && !bBlocked; ++DY)
		{
			for (int32 DX = 0; DX < Piece.Size.X && !bBlocked; ++DX)
			{
				bBlocked = KeepClear.Contains(Piece.Min + FIntPoint(DX, DY));
			}
		}
		if (bBlocked)
		{
			UE_LOG(LogKakurenboArena, Warning, TEXT("Map: '%c' at (%d,%d) is in front of the oni gate. Skipped."), Piece.Key, Piece.Min.X, Piece.Min.Y);
			continue;
		}

		FKakurenboFurnitureRow Row;
		if (const FKakurenboFurnitureRow* Found = Furniture.Find(Piece.Key))
		{
			Row = *Found;
		}
		else if (Piece.Key == KakurenboMaps::WallChar)
		{
			// 部屋の壁（Furniture.csv に無くても使える）
			Row.Height = BorderHeight;
			Row.Color = Map.WallColor;
		}
		else
		{
			UE_LOG(LogKakurenboArena, Warning, TEXT("Map: unknown furniture '%c' at (%d,%d). Add it to Furniture.csv."), Piece.Key, Piece.Min.X, Piece.Min.Y);
			continue;
		}

		BuildPiece(Piece, Layout, Row, Map);
		if (!Row.bWalkable)
		{
			for (int32 DY = 0; DY < Piece.Size.Y; ++DY)
			{
				for (int32 DX = 0; DX < Piece.Size.X; ++DX)
				{
					OutObstacles.Add(Piece.Min + FIntPoint(DX, DY));
				}
			}
		}
	}

	// 壁の飾り（窓・絵・明かり・出入り口の枠）。壁の明かりのメッシュが無ければ、今まで通りのランプ（光る玉）
	DecorCounts.Reset();
	if (Decor)
	{
		BuildDecor(Layout, *Decor, Furniture, Map, KeepClear);
	}
	if (GetDecorCount(EKakurenboDecorKind::Sconce) == 0)
	{
		BuildWallLamps(Map, Layout);
	}
	SetTallWallsVisible(bTallWallsVisible);
	UE_LOG(LogKakurenboArena, Log, TEXT("Map '%s': %d pieces (%d with meshes), %d blocked cells, decor: %d windows %d paintings %d lights %d door frames"),
		*Map.DisplayName.ToString(), FurniturePieces, FurnitureMeshes, OutObstacles.Num(), GetDecorCount(EKakurenboDecorKind::Window), GetDecorCount(EKakurenboDecorKind::Painting),
		GetDecorCount(EKakurenboDecorKind::Sconce), GetDecorCount(EKakurenboDecorKind::DoorFrame) + GetDecorCount(EKakurenboDecorKind::DoorFrameWide));
}

void AKakurenboArena::BuildPiece(const KakurenboMaps::FPiece& Piece, const KakurenboMaps::FLayout& Layout, const FKakurenboFurnitureRow& Row, const FKakurenboMapRow& Map)
{
	++FurniturePieces;
	const float RectW = Piece.Size.X * CellSize;
	const float RectD = Piece.Size.Y * CellSize;
	const FVector Center = CellCenter(Piece.Min) + FVector((Piece.Size.X - 1) * CellSize * 0.5f, (Piece.Size.Y - 1) * CellSize * 0.5f, 0.f);
	const float Height = FMath::Max(Row.Height, 2.f);
	const bool bIsWall = Piece.Key == KakurenboMaps::WallChar;

	// 見た目のメッシュ（同じ場所なら毎回同じ方を選ぶ）
	auto LoadMesh = [](const TSoftObjectPtr<UStaticMesh>& Soft) -> UStaticMesh*
	{
		if (Soft.IsNull() || !FPackageName::DoesPackageExist(Soft.ToSoftObjectPath().GetLongPackageName()))
		{
			return nullptr; // そのパソコンに Fab のアセットが無い → 箱で代用
		}
		return Soft.LoadSynchronous();
	};
	UStaticMesh* Mesh = LoadMesh(Row.Mesh);
	if (UStaticMesh* Alt = LoadMesh(Row.AltMesh); Alt && (!Mesh || ((Piece.Min.X * 7 + Piece.Min.Y * 13) % 2 == 1)))
	{
		Mesh = Alt;
	}

	// 当たり判定（家具の大きさの箱）。メッシュがあるときは見えない箱にする。
	// M14：メッシュのある家具は、メッシュ 1 個ずつの大きさの箱にする（下の並べる所）。見えている床（本棚のすき間・浅い家具の前）には入れる
	if (!Row.bWalkable && (bIsWall || !Mesh))
	{
		UStaticMeshComponent* Box = AddMapCube(Center + FVector(0.f, 0.f, Height * 0.5f), FVector(RectW, RectD, Height), Row.Color, true);
		Box->SetHiddenInGame(Mesh != nullptr);
		if (!bIsWall)
		{
			// 三人称カメラは家具をすり抜ける（家具の陰にしゃがんでも自分が見え、カメラが家具に当たってがくがくしない）
			Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		}
		if (bIsWall)
		{
			// 部屋の壁の上の縁
			const FLinearColor Trim = FLinearColor::LerpUsingHSV(Map.WallColor, FLinearColor(1.f, 0.85f, 0.5f), 0.45f);
			AddMapCube(Center + FVector(0.f, 0.f, Height + 6.f), FVector(RectW + 12.f, RectD + 12.f, 12.f), Trim, false);
			// 部屋の壁は外周の壁と同じ高さまで（上に乗って逃げられない・隣の部屋が見えない）。高い部分はかくれんぼ中だけ見せる
			const float TallHeight = FMath::Max(OuterWallHeight - Height, 1.f);
			UStaticMeshComponent* Tall = AddMapCube(Center + FVector(0.f, 0.f, Height + TallHeight * 0.5f), FVector(RectW, RectD, TallHeight), Row.Color, true);
			Tall->SetCastShadow(false);
			TallRoomWalls.Add(Tall);
			// 外周の壁と同じ板を 4 つの面に貼る（下の方は腰壁）
			if (bUseWallPanels && WallPanels)
			{
				const FVector Min(Center.X - RectW * 0.5f, Center.Y - RectD * 0.5f, 0.f);
				const float LowTop = WainscotPanels ? WainscotHeight : 0.f;
				struct FBand { UInstancedStaticMeshComponent* Panels; float ZFrom; float ZTo; };
				const FBand Bands[] = { { WainscotPanels.Get(), 0.f, LowTop }, { WallPanels.Get(), LowTop, Height }, { TallWallPanels.Get(), Height, OuterWallHeight } };
				for (const FBand& Band : Bands)
				{
					if (!Band.Panels || Band.ZTo - Band.ZFrom <= 1.f)
					{
						continue;
					}
					AddFacePanels(Band.Panels, Min, FVector(0, 1, 0), FVector(-1, 0, 0), RectD, Band.ZFrom, Band.ZTo);
					AddFacePanels(Band.Panels, Min + FVector(RectW, 0.f, 0.f), FVector(0, 1, 0), FVector(1, 0, 0), RectD, Band.ZFrom, Band.ZTo);
					AddFacePanels(Band.Panels, Min, FVector(1, 0, 0), FVector(0, -1, 0), RectW, Band.ZFrom, Band.ZTo);
					AddFacePanels(Band.Panels, Min + FVector(0.f, RectD, 0.f), FVector(1, 0, 0), FVector(0, 1, 0), RectW, Band.ZFrom, Band.ZTo);
				}
			}
		}
	}
	else if (!Mesh)
	{
		// 歩ける飾り（じゅうたん）を箱で：床の上の薄い板
		AddMapCube(Center + FVector(0.f, 0.f, 0.6f), FVector(RectW * 0.96f, RectD * 0.96f, 1.f), Row.Color, false);
	}

	if (Mesh)
	{
		++FurnitureMeshes;
		// 正面を部屋の空いている側へ向ける：長い辺の両側（正方形なら 4 方向）のうち、空いているマスが多い方
		auto FreeCount = [&Layout](const FIntPoint& From, const FIntPoint& Step, int32 Count)
		{
			int32 Free = 0;
			for (int32 i = 0; i < Count; ++i)
			{
				const FIntPoint P = From + Step * i;
				Free += (Layout.IsInside(P) && Layout.Get(P) == KakurenboMaps::EmptyChar) ? 1 : 0;
			}
			return Free;
		};
		struct FSide { FVector Dir; int32 Free; };
		TArray<FSide> Sides;
		const bool bWideX = Piece.Size.X >= Piece.Size.Y;
		const bool bWideY = Piece.Size.Y >= Piece.Size.X;
		if (bWideX)
		{
			Sides.Add({ FVector(0, -1, 0), FreeCount(Piece.Min + FIntPoint(0, -1), FIntPoint(1, 0), Piece.Size.X) });
			Sides.Add({ FVector(0, 1, 0), FreeCount(Piece.Min + FIntPoint(0, Piece.Size.Y), FIntPoint(1, 0), Piece.Size.X) });
		}
		if (bWideY)
		{
			Sides.Add({ FVector(-1, 0, 0), FreeCount(Piece.Min + FIntPoint(-1, 0), FIntPoint(0, 1), Piece.Size.Y) });
			Sides.Add({ FVector(1, 0, 0), FreeCount(Piece.Min + FIntPoint(Piece.Size.X, 0), FIntPoint(0, 1), Piece.Size.Y) });
		}
		FSide Best = Sides[0];
		for (const FSide& Side : Sides)
		{
			if (Side.Free > Best.Free)
			{
				Best = Side;
			}
		}
		// メッシュの長い辺を長方形の長い辺にそろえる。正面は長い辺と直角の向き（+Y か +X）とみなし、
		// 空いている側へ向ける（逆を向くアセットなら FurnitureFrontYaw = 180）
		const FBox Bounds = Mesh->GetBoundingBox();
		const FVector Ext = Bounds.GetSize();
		const bool bMeshLongX = Ext.X >= Ext.Y;
		const float FrontLocalYaw = bMeshLongX ? 90.f : 0.f;
		const float Yaw = Best.Dir.Rotation().Yaw - FrontLocalYaw + FurnitureFrontYaw + Row.MeshYaw;
		const FRotator Rot(0.f, Yaw, 0.f);
		const float MeshLong = FMath::Max(bMeshLongX ? Ext.X : Ext.Y, 1.f);
		const float MeshShort = FMath::Max(bMeshLongX ? Ext.Y : Ext.X, 1.f);
		const bool bRectWideX = RectW >= RectD;
		const float RectLong = bRectWideX ? RectW : RectD;
		const float RectShort = bRectWideX ? RectD : RectW;
		// メッシュの長い辺が向く方向（ワールド）
		const FVector LongAxis = Rot.RotateVector(bMeshLongX ? FVector::ForwardVector : FVector::RightVector).GetSafeNormal2D();

		// 長い辺・短い辺の方向に並べる数と、1 個ずつの場所の長さ（マスの数。どれも整数なので、メッシュがマスの境目の途中で切れない）
		TArray<int32> LongSlots = { FMath::Max(1, FMath::RoundToInt(RectLong / CellSize)) };
		TArray<int32> ShortSlots = { FMath::Max(1, FMath::RoundToInt(RectShort / CellSize)) };
		float Scale = 1.f;
		// 長い辺だけは伸ばす・縮める（0.6〜1.5 倍）。幅 2 m の本棚を 1 マスに置いても、背の高さ（当たり判定の高さ）のまま見えるように
		const float MinLongStretch = 0.6f;
		const float MaxLongStretch = 1.5f;
		// 短い辺（奥行き）も少しだけ（0.6〜1.3 倍）。1 マスの奥行きに正方形のテーブルを置いても小さくなりすぎない・2 マスのベッドが 2 つに分かれない
		const float MinShortStretch = 0.6f;
		const float MaxShortStretch = 1.3f;
		float ShortSlot = RectShort;
		// Cells マスを Count 個に分ける（割り切れなければ真ん中の方を 1 マス長く）
		auto Split = [](int32 Cells, int32 Count)
		{
			TArray<int32> Sizes;
			for (int32 i = 0; i < Count; ++i)
			{
				Sizes.Add(Cells / Count);
			}
			for (int32 i = 0; i < Cells % Count; ++i)
			{
				++Sizes[(Count - Cells % Count) / 2 + i];
			}
			return Sizes;
		};
		if (!Row.bWalkable)
		{
			// 縦横比を保ったまま、奥行きと高さに収まる大きさにする。長い家具（本棚の列・食卓）は同じメッシュを何個か並べる。
			// 1 個のメッシュは必ず整数のマス（1 マス・2 マス…）を埋め、真ん中にぽつんと置いてマスの境目にまたがらないようにする
			// （またがると、空いて見えるマスに壁が置けない）。
			// 長い辺：伸ばして（最大 1.5 倍）埋められ、縮めて（0.6 倍まで）収まる一番少ない数。割り切れる数を先に探す
			Scale = FMath::Min(RectShort * 0.96f / (MeshShort * MinShortStretch), Height / FMath::Max(Ext.Z, 1.f));
			const int32 LongCells = LongSlots[0];
			auto Fits = [&](int32 Count, bool bEvenOnly)
			{
				const float Slot = RectLong / Count;
				return (!bEvenOnly || LongCells % Count == 0) && MeshLong * Scale * MaxLongStretch >= Slot * 0.92f && MeshLong * Scale * MinLongStretch <= Slot * 0.97f;
			};
			int32 Copies = 0;
			for (int32 Pass = 0; Pass < 2 && Copies == 0; ++Pass)
			{
				for (int32 Count = 1; Count <= LongCells && Copies == 0; ++Count)
				{
					Copies = Fits(Count, Pass == 0) ? Count : 0;
				}
			}
			if (Copies == 0)
			{
				Copies = FMath::Clamp(FMath::CeilToInt(RectLong * 0.92f / (MeshLong * Scale * MaxLongStretch)), 1, LongCells);
			}
			LongSlots = Split(LongCells, Copies);
			// 短い辺：伸ばしても場所の 6 割に届かないほど細いときは、横にも並べる（真ん中にぽつんと置かない）
			const int32 ShortCells = ShortSlots[0];
			int32 CopiesShort = ShortCells;
			for (int32 Count = 1; Count < ShortCells; ++Count)
			{
				if (ShortCells % Count == 0 && MeshShort * Scale * MaxShortStretch >= ShortCells * CellSize / Count * 0.62f)
				{
					CopiesShort = Count;
					break;
				}
			}
			ShortSlots = Split(ShortCells, CopiesShort);
			ShortSlot = ShortCells / CopiesShort * CellSize;
			Scale = FMath::Min(Scale, ShortSlot * 0.96f / (MeshShort * MinShortStretch));
		}

		const FVector ShortAxis = FVector::CrossProduct(FVector::UpVector, LongAxis).GetSafeNormal2D();
		// M14：後ろが壁・家具（空いていない）なら、奥行きの余りを前に寄せず、後ろの壁にぴったり付ける（浅い家具の前にすき間ができ、入れる）
		bool bBackBlocked = false;
		for (const FSide& Side : Sides)
		{
			if (Side.Dir.Equals(-Best.Dir))
			{
				bBackBlocked = Side.Free == 0;
			}
		}
		const bool bPushBack = !Row.bWalkable && bBackBlocked && ShortSlots.Num() == 1 && FMath::Abs(FVector::DotProduct(Best.Dir, ShortAxis)) > 0.9f;
		int32 ShortStart = 0;
		for (const int32 ShortSize : ShortSlots)
		{
			int32 LongStart = 0;
			for (const int32 LongSize : LongSlots)
			{
				FVector Scale3D;
				if (Row.bWalkable)
				{
					// じゅうたん：長方形いっぱいに広げる（縦横比は変えてよい）
					Scale3D = FVector(RectLong * 0.96f / MeshLong, RectShort * 0.96f / MeshShort, 1.f);
					if (!bMeshLongX)
					{
						Swap(Scale3D.X, Scale3D.Y);
					}
					Scale3D.Z = FMath::Min(Scale3D.X, Scale3D.Y);
				}
				else
				{
					// 自分の場所に合わせて長い辺を伸ばす・縮める（縮めきれないときだけ全体を小さく）
					const float SlotLength = LongSize * CellSize;
					const float CopyScale = FMath::Min(Scale, SlotLength * 0.97f / (MeshLong * MinLongStretch));
					Scale3D = FVector(CopyScale);
					(bMeshLongX ? Scale3D.X : Scale3D.Y) *= FMath::Clamp(SlotLength * 0.92f / (MeshLong * CopyScale), MinLongStretch, MaxLongStretch);
					(bMeshLongX ? Scale3D.Y : Scale3D.X) *= FMath::Clamp(ShortSlot * 0.9f / (MeshShort * CopyScale), MinShortStretch, MaxShortStretch);
				}
				FVector Slot = Center + LongAxis * ((LongStart + LongSize * 0.5f) * CellSize - RectLong * 0.5f)
					+ ShortAxis * ((ShortStart + ShortSize * 0.5f) * CellSize - RectShort * 0.5f);
				if (bPushBack)
				{
					const float Depth = MeshShort * (bMeshLongX ? Scale3D.Y : Scale3D.X);
					Slot -= Best.Dir * FMath::Max(0.f, (ShortSize * CellSize - Depth) * 0.5f - 2.f);
				}
				LongStart += LongSize;
				UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
				Comp->SetMobility(EComponentMobility::Movable);
				Comp->SetStaticMesh(Mesh);
				Comp->SetupAttachment(RootComponent);
				Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision); // 当たり判定は上の箱
				Comp->SetWorldScale3D(Scale3D);
				Comp->SetWorldRotation(Rot);
				// メッシュの中心（原点がずれているアセットもある）をその位置に、底を床に合わせる
				const FVector Pivot = Rot.RotateVector(Bounds.GetCenter() * Scale3D);
				Comp->SetWorldLocation(FVector(Slot.X - Pivot.X, Slot.Y - Pivot.Y, Center.Z - Bounds.Min.Z * Scale3D.Z + (Row.bWalkable ? 0.5f : 0.f)));
				Comp->RegisterComponent();
				MapComponents.Add(Comp);
				// マスからはみ出していないか（テスト用に一番大きなはみ出しを覚えておく）
				const FBox World = Comp->Bounds.GetBox();
				const float Overhang = FMath::Max(FMath::Max(Center.X - RectW * 0.5f - World.Min.X, World.Max.X - (Center.X + RectW * 0.5f)),
					FMath::Max(Center.Y - RectD * 0.5f - World.Min.Y, World.Max.Y - (Center.Y + RectD * 0.5f)));
				MaxFurnitureOverhang = FMath::Max(MaxFurnitureOverhang, Overhang);
				if (!Row.bWalkable)
				{
					// 当たり判定はこのメッシュの大きさ（上から見た外形 × 家具の高さ）。カメラは通す
					const FVector BoxCenter = World.GetCenter();
					const FVector BoxSize = World.GetSize();
					UStaticMeshComponent* Box = AddMapCube(FVector(BoxCenter.X, BoxCenter.Y, Center.Z + Height * 0.5f), FVector(BoxSize.X, BoxSize.Y, Height), Row.Color, true);
					Box->SetHiddenInGame(true);
					Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
					++FurnitureCollisionBoxes;
				}
				// マスの境目にまたがって、どちらのマスも埋めていないメッシュ（真ん中にぽつんと置かれて 2 マスぶんの場所を取って見える）を数える
				if (!Row.bWalkable)
				{
					const FVector Extent = World.GetExtent();
					for (int32 Axis = 0; Axis < 2; ++Axis)
					{
						const float SlotMin = (Axis == 0 ? Center.X - RectW * 0.5f : Center.Y - RectD * 0.5f);
						const float MeshMin = Axis == 0 ? World.Min.X : World.Min.Y;
						const float MeshMax = Axis == 0 ? World.Max.X : World.Max.Y;
						const float MeshSize = (Axis == 0 ? Extent.X : Extent.Y) * 2.f;
						// メッシュがかかっているマス（端から端まで）と、メッシュの中を通るマスの境目
						const int32 FirstCell = FMath::FloorToInt((MeshMin - SlotMin) / CellSize);
						const int32 LastCell = FMath::FloorToInt((MeshMax - 0.01f - SlotMin) / CellSize);
						bool bCrossesLine = false;
						for (int32 Line = FirstCell + 1; Line <= LastCell; ++Line)
						{
							const float LineAt = SlotMin + Line * CellSize;
							bCrossesLine |= LineAt > MeshMin + 10.f && LineAt < MeshMax - 10.f;
						}
						if (bCrossesLine && MeshSize < (LastCell - FirstCell + 1) * CellSize * 0.6f)
						{
							++FurnitureStraddleCount;
							UE_LOG(LogKakurenboArena, Warning, TEXT("Map: %s straddles a cell line (rect %.0fx%.0f, mesh %.0fx%.0f)"), *Row.DisplayName.ToString(), RectW, RectD, Extent.X * 2.f, Extent.Y * 2.f);
							break;
						}
					}
				}
			}
			ShortStart += ShortSize;
		}
	}

	// 暖炉・ランプの明かり
	if (Row.LightIntensity > 0.f)
	{
		AddMapLight(Center + FVector(0.f, 0.f, FMath::Min(Height * 0.6f, 150.f)), Map.LightColor, Row.LightIntensity, 800.f);
	}
}

void AKakurenboArena::BuildWallLamps(const FKakurenboMapRow& Map, const KakurenboMaps::FLayout& Layout)
{
	if (WallLampSpacingCells <= 0)
	{
		return;
	}
	// 外周の壁の内側に、一定の間隔でランプ（光る玉と明かり）を付ける。鬼の出入り口の近くは門のちょうちんがあるので付けない
	const FVector Origin = GetGridOrigin();
	const float LampZ = BorderHeight - 50.f;
	auto AddLamp = [&](const FVector& Location)
	{
		UStaticMeshComponent* Bulb = AddMapCube(Location, FVector(22.f), Map.LightColor, false);
		Bulb->SetStaticMesh(SphereMesh);
		Bulb->SetCastShadow(false);
		AddMapLight(Location, Map.LightColor, WallLampIntensity, WallLampRadius);
	};
	const int32 Half = WallLampSpacingCells / 2;
	for (int32 X = Half; X < GridSizeX; X += WallLampSpacingCells)
	{
		AddLamp(FVector(Origin.X + (X + 0.5f) * CellSize, Origin.Y + 14.f, LampZ));
		AddLamp(FVector(Origin.X + (X + 0.5f) * CellSize, Origin.Y + GridSizeY * CellSize - 14.f, LampZ));
	}
	for (int32 Y = Half; Y < GridSizeY; Y += WallLampSpacingCells)
	{
		AddLamp(FVector(Origin.X + 14.f, Origin.Y + (Y + 0.5f) * CellSize, LampZ));
		const int32 GateMid = GridSizeY / 2;
		if (FMath::Abs(Y - GateMid) > OniGateWidthCells)
		{
			AddLamp(FVector(Origin.X + GridSizeX * CellSize - 14.f, Origin.Y + (Y + 0.5f) * CellSize, LampZ));
		}
	}
}

void AKakurenboArena::SpawnLighting()
{
	// 月（夜の明かり）。青白く弱い光で、壁の影はこれが落とす
	UDirectionalLightComponent* Moon = NewObject<UDirectionalLightComponent>(this);
	Moon->SetMobility(EComponentMobility::Movable);
	Moon->SetupAttachment(RootComponent);
	Moon->SetWorldRotation(FRotator(-55.f, 35.f, 0.f));
	Moon->SetIntensity(MoonIntensity);
	Moon->SetLightColor(MoonColor);
	Moon->SetAtmosphereSunLight(true);
	Moon->RegisterComponent();

	// 空（大気）。月が弱いので夜空になる
	USkyAtmosphereComponent* Sky = NewObject<USkyAtmosphereComponent>(this);
	Sky->SetupAttachment(RootComponent);
	Sky->RegisterComponent();

	// 環境光（空の色をリアルタイムに取り込む）。真っ暗にならないよう少しだけ紫を足す
	USkyLightComponent* SkyLight = NewObject<USkyLightComponent>(this);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SetLowerHemisphereColor(FLinearColor(0.05f, 0.02f, 0.08f));
	SkyLight->SetIntensity(0.8f);
	SkyLight->RegisterComponent();
	SkyLight->RecaptureSky();

	// 紫の霧（遠くが少しかすむ）
	UExponentialHeightFogComponent* Fog = NewObject<UExponentialHeightFogComponent>(this);
	Fog->SetupAttachment(RootComponent);
	Fog->SetFogDensity(FogDensity);
	Fog->SetFogInscatteringColor(FogColor);
	Fog->SetFogHeightFalloff(0.4f);
	Fog->SetStartDistance(600.f);
	Fog->RegisterComponent();
}
