#include "KakurenboOniStudio.h"

#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KakurenboGameMode.h"
#include "KakurenboLibrary.h"
#include "OniCharacter.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** スタジオの場所（舞台のずっと下。ここは見えない）と、種類ごとの間隔 */
	const FVector StudioOrigin(0.f, 0.f, -40000.f);
	constexpr float StudioSpacing = 2500.f;

	/** 種類ごとの、特徴が伝わるポーズ（0 待機 / 1 走る / 2 攻撃 / 3 見つけた）と、アニメーションの止める位置（0〜1） */
	void GetPose(EOniType Type, int32& OutPose, float& OutTime)
	{
		switch (Type)
		{
		case EOniType::Scout:    OutPose = 1; OutTime = 0.3f; break;  // 走っている（足が速い）
		case EOniType::Breaker:  OutPose = 2; OutTime = 0.12f; break; // 壁をなぐろうと振りかぶる（力持ち）
		case EOniType::Treasure: OutPose = 3; OutTime = 0.4f; break;  // お宝を見つけて喜ぶ
		case EOniType::Balanced: OutPose = 1; OutTime = 0.6f; break;  // 追いかけてくる
		default:                 OutPose = 0; OutTime = 0.3f; break;  // 慎重鬼・探知鬼：じっと見ている
		}
	}
}

AKakurenboOniStudio::AKakurenboOniStudio()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AKakurenboOniStudio::Setup(AKakurenboGameMode* GM)
{
	if (!GM)
	{
		return;
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	const UEnum* TypeEnum = StaticEnum<EOniType>();
	for (int32 i = 0; i < TypeEnum->NumEnums() - 1; ++i)
	{
		const EOniType Type = static_cast<EOniType>(TypeEnum->GetValueByIndex(i));
		const FKakurenboOniTypeRow Row = GM->GetOniTypeRow(Type);
		const FVector Spot = StudioOrigin + FVector(i * StudioSpacing, 0.f, 0.f);

		// 見本の鬼：カメラ（+X 側）の方を向いて立つ。動かない・落ちない
		const FTransform At(FRotator(0.f, 0.f, 0.f), Spot + FVector(0.f, 0.f, 100.f));
		AOniCharacter* Model = GetWorld()->SpawnActorDeferred<AOniCharacter>(GM->OniClass ? GM->OniClass.Get() : AOniCharacter::StaticClass(), At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Model)
		{
			continue;
		}
		GM->ConfigureOniLook(Model, Type);
		Model->FinishSpawning(At);
		Model->GetCharacterMovement()->GravityScale = 0.f;
		Model->GetCharacterMovement()->DisableMovement();
		Model->SetActorEnableCollision(false);
		int32 Pose = 0;
		float PoseTime = 0.f;
		GetPose(Type, Pose, PoseTime);
		Model->SetPortraitPose(Pose, PoseTime);

		// 背景（種類の色をうすく）と床
		auto AddPart = [this](UStaticMesh* Mesh, const FVector& Location, const FVector& Scale, const FLinearColor& Color)
		{
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
			Part->SetStaticMesh(Mesh);
			Part->SetupAttachment(RootComponent);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetWorldLocation(Location);
			Part->SetWorldScale3D(Scale);
			Part->RegisterComponent();
			UKakurenboLibrary::ApplyColor(Part, Color);
			return Part;
		};
		const FLinearColor Back = FLinearColor::LerpUsingHSV(Row.Color, FLinearColor(0.08f, 0.05f, 0.12f), 0.65f);
		UStaticMeshComponent* Wall = AddPart(Cube, Spot + FVector(-260.f, 0.f, 200.f), FVector(0.1f, 8.f, 6.f), Back);
		UStaticMeshComponent* Floor = AddPart(Cylinder, Spot + FVector(0.f, 0.f, -4.f), FVector(4.f, 4.f, 0.08f), FLinearColor(0.12f, 0.08f, 0.06f));

		// 明かり：正面の左上から（主な明かり）・後ろから（輪郭）
		auto AddLight = [this](const FVector& Location, float Intensity, const FLinearColor& Color)
		{
			UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
			Light->SetupAttachment(RootComponent);
			Light->SetWorldLocation(Location);
			Light->SetIntensityUnits(ELightUnits::Candelas);
			Light->SetIntensity(Intensity);
			Light->SetAttenuationRadius(900.f);
			Light->SetLightColor(Color);
			Light->SetCastShadows(false);
			Light->RegisterComponent();
		};
		AddLight(Spot + FVector(320.f, 160.f, 320.f), 160.f, FLinearColor(1.f, 0.95f, 0.88f));
		AddLight(Spot + FVector(260.f, -220.f, 120.f), 50.f, FLinearColor(0.75f, 0.8f, 1.f));
		AddLight(Spot + FVector(-150.f, 0.f, 300.f), 90.f, FLinearColor::LerpUsingHSV(Row.Color, FLinearColor::White, 0.4f));

		// カメラ：少し上・少し横から、体全体が入るように
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
		Target->RenderTargetFormat = RTF_RGBA8;
		Target->ClearColor = FLinearColor::Black;
		Target->InitAutoFormat(PortraitSize, PortraitSize);
		Target->UpdateResourceImmediate(true);

		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(this);
		Capture->SetupAttachment(RootComponent);
		const float Height = 190.f * FMath::Max(Row.BodyScale, 0.5f);
		const FVector Eye = Spot + FVector(430.f, 150.f, Height * 0.75f);
		const FVector LookAt = Spot + FVector(0.f, 0.f, Height * 0.5f);
		Capture->SetWorldLocationAndRotation(Eye, (LookAt - Eye).Rotation());
		Capture->FOVAngle = 38.f;
		Capture->TextureTarget = Target;
		Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		// 写すのは、この鬼と背景と床だけ（舞台・霧・空は写さない）
		Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		Capture->ShowOnlyActors.Add(Model);
		Capture->ShowOnlyComponents.Add(Wall);
		Capture->ShowOnlyComponents.Add(Floor);
		Capture->ShowFlags.SetFog(false);
		Capture->ShowFlags.SetAtmosphere(false);
		Capture->ShowFlags.SetVolumetricFog(false);
		// 明るさは固定（館の暗さ・明るさの自動補正に左右されない）
		FPostProcessSettings& PP = Capture->PostProcessSettings;
		PP.bOverride_AutoExposureMethod = true;
		PP.AutoExposureMethod = AEM_Manual;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PP.AutoExposureApplyPhysicalCameraExposure = false;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = ExposureBias;
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 0.25f;
		Capture->PostProcessBlendWeight = 1.f;
		Capture->RegisterComponent();

		Types.Add(Type);
		Models.Add(Model);
		Captures.Add(Capture);
		Targets.Add(Target);
	}
	// ポーズが決まって（アニメーションの 1 フレーム目が出て）から撮る。テクスチャが読み込まれた頃にもう一度
	FTimerHandle First, Second;
	GetWorldTimerManager().SetTimer(First, FTimerDelegate::CreateUObject(this, &AKakurenboOniStudio::CaptureAll), 0.4f, false);
	GetWorldTimerManager().SetTimer(Second, FTimerDelegate::CreateUObject(this, &AKakurenboOniStudio::CaptureAll), 2.0f, false);
}

void AKakurenboOniStudio::CaptureAll()
{
	for (USceneCaptureComponent2D* Capture : Captures)
	{
		if (Capture)
		{
			Capture->CaptureScene();
		}
	}
	++CaptureCount;
}

UTextureRenderTarget2D* AKakurenboOniStudio::GetPortrait(EOniType Type) const
{
	const int32 Index = Types.IndexOfByKey(Type);
	return Targets.IsValidIndex(Index) && CaptureCount > 0 ? Targets[Index].Get() : nullptr;
}
