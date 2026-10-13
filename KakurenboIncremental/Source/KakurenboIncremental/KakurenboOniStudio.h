// 鬼の写真スタジオ（M15：鬼のチュートリアル・図鑑の写真）。
// 舞台のずっと下に、鬼の種類ごとに見本の鬼（動かない・ポーズを決めて止めた鬼）と背景・明かり・カメラ（SceneCapture）を並べ、
// 写真をテクスチャ（RenderTarget）に撮る。HUD がそのテクスチャをチュートリアルの画面に貼る。
// 鬼のモデル（Cute Creature）が無いパソコンでは、円柱の見た目の鬼が写る。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KakurenboTypes.h"
#include "KakurenboOniStudio.generated.h"

class AKakurenboGameMode;
class AOniCharacter;
class UPointLightComponent;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

UCLASS()
class KAKURENBOINCREMENTAL_API AKakurenboOniStudio : public AActor
{
	GENERATED_BODY()

public:
	AKakurenboOniStudio();

	/** 写真の大きさ（ピクセル） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio")
	int32 PortraitSize = 512;

	/** 写真の明るさ（露出の補正。EV） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio")
	float ExposureBias = 0.f;

	/** 全種類の見本の鬼を並べて、少し待ってから撮る（GameMode が BeginPlay で呼ぶ） */
	void Setup(AKakurenboGameMode* GM);

	/** 撮り直す（チュートリアルを開くとき。テクスチャの読み込みが終わってからの、きれいな写真になる） */
	void CaptureAll();

	/** その種類の写真（まだ無ければ null） */
	UTextureRenderTarget2D* GetPortrait(EOniType Type) const;

	/** 撮った回数（テスト用） */
	int32 GetCaptureCount() const { return CaptureCount; }

private:
	/** 種類ごとの見本の鬼とカメラ（同じ並び） */
	UPROPERTY()
	TArray<TObjectPtr<AOniCharacter>> Models;

	UPROPERTY()
	TArray<TObjectPtr<USceneCaptureComponent2D>> Captures;

	UPROPERTY()
	TArray<TObjectPtr<UTextureRenderTarget2D>> Targets;

	TArray<EOniType> Types;
	int32 CaptureCount = 0;
};
