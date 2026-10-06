// どこからでも使える便利関数（BP からも呼べる）

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "KakurenboLibrary.generated.h"

class UStaticMeshComponent;

UCLASS()
class KAKURENBOINCREMENTAL_API UKakurenboLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** インフレする数値を見やすく整形する（例: 1234 → "1.23K", 1e20 → "1.00e20"）。1000 未満は小数点以下を切り捨てる（コイン用） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	static FString FormatBigNumber(double Value);

	/** 収入・耐久・攻撃力など、小さな値の小数に意味がある数値の整形（例: 1.5 → "1.5", 2 → "2", 2.56 → "2.6"。100 以上は FormatBigNumber と同じ） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	static FString FormatStatNumber(double Value);

	/** 指数カーブ: Base * Growth^Level （価格や収入の計算に使う） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	static double ExpCurve(double Base, double Growth, int32 Level);

	/** BasicShapeMaterial に色を付けた動的マテリアルをメッシュに設定する（仮の見た目用） */
	static void ApplyColor(UStaticMeshComponent* Mesh, const FLinearColor& Color);
};
