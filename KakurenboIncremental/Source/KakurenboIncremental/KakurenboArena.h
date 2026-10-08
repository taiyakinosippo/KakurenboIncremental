// かくれんぼの舞台（館）を C++ だけで組み立てるアクター。
// レベルに何も置いていなくても遊べるよう、GameMode が自動でスポーンする。
//
//   BeginPlay   : 夜の明かり（月・空・霧）・画面の色味・外側の地面
//   RebuildShell: 床（市松模様）・外周の壁・鬼の出入り口（マップの大きさが変わるたびに作り直す）
//   ApplyMap    : マップごとの家具・部屋の壁・ランプ（マップが変わるたびに作り直す）

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KakurenboMaps.h"
#include "KakurenboTypes.h"
#include "KakurenboArena.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UPostProcessComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class KAKURENBOINCREMENTAL_API AKakurenboArena : public AActor
{
	GENERATED_BODY()

public:
	AKakurenboArena();

	/** グリッドのマス数（X 方向） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	int32 GridSizeX = 24;

	/** グリッドのマス数（Y 方向） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	int32 GridSizeY = 24;

	/** 1 マスの大きさ（cm）。ブロック 1 個の大きさでもある */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	float CellSize = 100.f;

	/** 部屋の壁の高さ（cm）。外周の壁のランプ・飾りの縁もこの高さ。壊せない */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	float BorderHeight = 250.f;

	/**
	 * 外周の壁の本当の高さ（cm）。外の世界が見えないよう高くする（かくれんぼ中だけ。BorderHeight より上の部分は、
	 * 上から見下ろす購入・設置・リザルトでは隠す。そうしないと壁際のプレイヤーが壁に隠れて見えなくなる）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	float OuterWallHeight = 1000.f;

	/** 壁にマテリアル（木の板など）を貼るときの板 1 枚の高さ（cm）。幅は 1 マス。模様が引き伸ばされないよう、小さな板を並べて貼る */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	float WallPanelHeight = 125.f;

	/** レベルにライトが無い場合、月・空・環境光・霧を自動で作る */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	bool bSpawnLightingIfMissing = true;

	/** 舞台の外側の地面の色 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	FLinearColor OuterGroundColor = FLinearColor(0.02f, 0.015f, 0.04f);

	// ===== 見た目（ポップで、夜の館に隠れる暗い雰囲気） =====

	/**
	 * 明るさの自動補正の範囲（EV100）。下限を上げると暗い所を無理に明るくしない（暗い所は暗く見える）。
	 * 上限を下げると明るい所がまぶしくならない
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	float MinExposureEV100 = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	float MaxExposureEV100 = 1.5f;

	/** モーションブラーを切る（視点をすばやく動かしたときに画面がぶれないように） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	bool bDisableMotionBlur = true;

	/** 画面の四隅を暗くする強さ（0〜1） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	float VignetteIntensity = 0.6f;

	/** 色の鮮やかさ（1 = そのまま。大きいほどポップ） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	float Saturation = 1.25f;

	/** 月明かりの強さ（lux）と色 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	float MoonIntensity = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	FLinearColor MoonColor = FLinearColor(0.55f, 0.6f, 1.f);

	/** 霧の色と濃さ */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	FLinearColor FogColor = FLinearColor(0.12f, 0.05f, 0.2f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	float FogDensity = 0.03f;

	/** 外周の壁に付けるランプの間隔（マス。0 ならランプなし） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	int32 WallLampSpacingCells = 6;

	/** ランプ 1 つの明るさ（カンデラ）と届く距離（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	float WallLampIntensity = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	float WallLampRadius = 700.f;

	/** 家具のメッシュの正面の向きの補正（度）。家具は部屋の空いている側を向くように置く */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	float FurnitureFrontYaw = 0.f;

	/** 画面全体に効く見た目の設定 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	TObjectPtr<UPostProcessComponent> PostProcess;

	// ===== 鬼の出入り口 =====
	// 東側（+X）の外周の真ん中に赤い門を立てる。鬼は必ずその前のマスから出てくる（そこには壁・罠を置けない）

	/** 門の幅（マス） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Gate")
	int32 OniGateWidthCells = 3;

	/** 門の前の、鬼が出てくるマスの奥行き（マス） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Gate")
	int32 OniGateDepthCells = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Gate")
	FLinearColor OniGateColor = FLinearColor(0.9f, 0.08f, 0.1f);

	/** 門の前の床の色 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Gate")
	FLinearColor OniGateFloorColor = FLinearColor(0.42f, 0.04f, 0.08f);

	/** グリッド (0,0) マスの角（最小 XY、床の上面の高さ） */
	FVector GetGridOrigin() const;

	/** 鬼が出てくるマス（門に近い順・真ん中から順。この順番で鬼を出す） */
	TArray<FIntPoint> GetOniGateCells() const;

	/** 門の位置（外周の壁の内側の面・床の高さ） */
	FVector GetOniGateLocation() const;

	/**
	 * マップの家具・部屋の壁を作り直す（前のマップの分は消す）。
	 * @param Furniture 文字ごとの家具（'#' が無ければ部屋の壁として作る）
	 * @param KeepClear 何も置かないマス（鬼の出入り口の前）
	 * @param OutObstacles 通れないマス（グリッドに渡す）
	 * @param FloorMaterial / FloorMaterialAlt / WallMaterial 床（市松模様の 2 色）と壁の見た目。null なら Map の色
	 */
	void ApplyMap(const FKakurenboMapRow& Map, const KakurenboMaps::FLayout& Layout, const TMap<TCHAR, FKakurenboFurnitureRow>& Furniture,
		const TArray<FIntPoint>& KeepClear, TArray<FIntPoint>& OutObstacles,
		UMaterialInterface* FloorMaterial = nullptr, UMaterialInterface* FloorMaterialAlt = nullptr, UMaterialInterface* WallMaterial = nullptr);

	/**
	 * 床・外周の壁・見えない柵・鬼の出入り口を、SizeX × SizeY マスの大きさで作り直す（舞台の真ん中はこのアクターの位置のまま）。
	 * グリッドの大きさも合わせて変えること（GameMode がする）
	 */
	void RebuildShell(int32 InSizeX, int32 InSizeY);

	/** 外周の壁の高い部分を見せる（かくれんぼ中）/ 隠す（上から見下ろすパート） */
	void SetTallWallsVisible(bool bVisible);
	bool AreTallWallsVisible() const { return bTallWallsVisible; }

	/** 外周の壁の高い部分（テスト用） */
	const TArray<TObjectPtr<UStaticMeshComponent>>& GetTallWalls() const { return TallWalls; }

	/** 今のマップで作った家具（メッシュか箱）の数（テスト用） */
	int32 GetFurniturePieceCount() const { return FurniturePieces; }

	/** 家具のうち、メッシュ（Fab のアセット）で表示できた数（テスト用） */
	int32 GetFurnitureMeshCount() const { return FurnitureMeshes; }

	/** 床・壁にマテリアル（Fab の木の床・壁）を使えているか（テスト用） */
	bool IsFloorUsingMaterial() const { return bFloorUsesMaterial; }
	bool IsWallUsingPanels() const { return bUseWallPanels; }

	/** 家具のメッシュが自分のマスから一番はみ出している長さ（cm。テスト用） */
	float GetMaxFurnitureOverhang() const { return MaxFurnitureOverhang; }

	/** 部屋の壁の高い部分の数（テスト用） */
	int32 GetTallRoomWallCount() const { return TallRoomWalls.Num(); }

protected:
	virtual void BeginPlay() override;

private:
	UStaticMeshComponent* AddCube(const FVector& Center, const FVector& SizeCm, const FLinearColor& Color);
	/** 大きさが変わるたびに作り直す部品として足す（RebuildShell で消える） */
	UStaticMeshComponent* AddShellCube(const FVector& Center, const FVector& SizeCm, const FLinearColor& Color);
	/** マップの部品として、マテリアルを貼った板の集まりを作る */
	UInstancedStaticMeshComponent* MakePanelSet(UMaterialInterface* Material);
	/** 壁の面（FaceStart から Along の方向に Length、高さ ZFrom〜ZTo。Normal は面の外向き）に板を並べる */
	void AddFacePanels(UInstancedStaticMeshComponent* Panels, const FVector& FaceStart, const FVector& Along, const FVector& Normal, float Length, float ZFrom, float ZTo);
	/** マップごとに作り直す部品として足す（ApplyMap で消える） */
	UStaticMeshComponent* AddMapCube(const FVector& Center, const FVector& SizeCm, const FLinearColor& Color, bool bCollision);
	void AddMapLight(const FVector& Location, const FLinearColor& Color, float Intensity, float Radius);
	void ClearMap();
	void SpawnLighting();
	void BuildOniGate(float BorderThickness);
	void BuildFloor();
	void BuildPiece(const KakurenboMaps::FPiece& Piece, const KakurenboMaps::FLayout& Layout, const FKakurenboFurnitureRow& Row, const FKakurenboMapRow& Map);
	void BuildWallLamps(const FKakurenboMapRow& Map, const KakurenboMaps::FLayout& Layout);
	FVector CellCenter(const FIntPoint& Cell, float Z = 0.f) const;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	/** 床の市松模様（明るいマス・暗いマス） */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> FloorTilesA;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> FloorTilesB;

	/** 外周の壁（マップの色に塗り替える。下の部分と高い部分） */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> BorderWalls;

	/** 外周の壁の高い部分（BorderHeight より上） */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> TallWalls;

	/** 大きさに合わせて作った部品（床・外周の壁・柵・門） */
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> ShellComponents;

	bool bTallWallsVisible = true;

	/** 壁に貼った板（下の部分・部屋の壁 / 外周の壁の高い部分） */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> WallPanels;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> TallWallPanels;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> CurrentWallMaterial;

	bool bUseWallPanels = false;

	/** 部屋の壁の高い部分（外周の壁の高い部分と一緒に見せる・隠す） */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> TallRoomWalls;

	float MaxFurnitureOverhang = 0.f;
	bool bFloorUsesMaterial = false;

	/** マップごとに作った部品（家具・部屋の壁・ランプ） */
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> MapComponents;

	int32 FurniturePieces = 0;
	int32 FurnitureMeshes = 0;
	float BorderThickness = 50.f;
};
