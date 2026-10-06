#include "KakurenboLibrary.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

FString UKakurenboLibrary::FormatBigNumber(double Value)
{
	const bool bNegative = Value < 0.0;
	const double Abs = FMath::Abs(Value);

	// 1000 未満は小数点以下を切り捨てて表示
	if (Abs < 1000.0)
	{
		return FString::Printf(TEXT("%s%d"), bNegative ? TEXT("-") : TEXT(""), FMath::FloorToInt(Abs));
	}

	static const TCHAR* Suffixes[] = { TEXT("K"), TEXT("M"), TEXT("B"), TEXT("T"), TEXT("Qa"), TEXT("Qi") };
	constexpr int32 NumSuffixes = UE_ARRAY_COUNT(Suffixes);

	// 3 桁ごとに単位を上げる。単位が尽きたら指数表記にする
	// 1e6 が 5.9999… と計算される誤差を吸収するため、わずかに足してから切り捨てる
	const int32 Exponent = FMath::FloorToInt(FMath::LogX(10.0, Abs) + 1e-9);
	const int32 Group = Exponent / 3;
	if (Group <= NumSuffixes)
	{
		const double Scaled = Abs / FMath::Pow(10.0, Group * 3);
		return FString::Printf(TEXT("%s%.2f%s"), bNegative ? TEXT("-") : TEXT(""), Scaled, Suffixes[Group - 1]);
	}

	const double Mantissa = Abs / FMath::Pow(10.0, Exponent);
	return FString::Printf(TEXT("%s%.2fe%d"), bNegative ? TEXT("-") : TEXT(""), Mantissa, Exponent);
}

FString UKakurenboLibrary::FormatStatNumber(double Value)
{
	if (FMath::Abs(Value) >= 100.0)
	{
		return FormatBigNumber(Value);
	}
	// 小数第 1 位まで。整数になるなら小数点を付けない
	const double Rounded = FMath::RoundToDouble(Value * 10.0) / 10.0;
	if (FMath::IsNearlyEqual(Rounded, FMath::RoundToDouble(Rounded), 1e-6))
	{
		return FString::Printf(TEXT("%d"), static_cast<int32>(FMath::RoundToDouble(Rounded)));
	}
	return FString::Printf(TEXT("%.1f"), Rounded);
}

double UKakurenboLibrary::ExpCurve(double Base, double Growth, int32 Level)
{
	return Base * FMath::Pow(Growth, static_cast<double>(Level));
}

void UKakurenboLibrary::ApplyColor(UStaticMeshComponent* Mesh, const FLinearColor& Color)
{
	if (!Mesh)
	{
		return;
	}
	// エンジン付属の BasicShapeMaterial（"Color" パラメータを持つ）から動的インスタンスを作って色を変える
	static TWeakObjectPtr<UMaterialInterface> BaseMaterial;
	if (!BaseMaterial.IsValid())
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial.Get(), Mesh))
	{
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		Mesh->SetMaterial(0, MID);
	}
}
