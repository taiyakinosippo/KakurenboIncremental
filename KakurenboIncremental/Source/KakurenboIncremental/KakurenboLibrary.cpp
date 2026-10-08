#include "KakurenboLibrary.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Texture.h"
#include "KakurenboTypes.h"
#include "Misc/PackageName.h"
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

bool UKakurenboLibrary::DoesAssetPackageExist(const FSoftObjectPath& Path)
{
	return !Path.IsNull() && FPackageName::DoesPackageExist(Path.GetLongPackageName());
}

UMaterialInstanceDynamic* UKakurenboLibrary::CreateSurfaceMaterial(UObject* Outer, const FKakurenboSurfaceRow& Surface, const FLinearColor& Tint)
{
	// 模様のテクスチャが無ければ作らない（Fab のアセットが無いパソコン）
	UTexture* BaseColor = LoadIfExists(Surface.BaseColor);
	if (!BaseColor)
	{
		return nullptr;
	}
	static TWeakObjectPtr<UMaterialInterface> Master;
	if (!Master.IsValid())
	{
		Master = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Kakurenbo/Materials/M_KakuSurface.M_KakuSurface"));
	}
	UMaterialInstanceDynamic* MID = Master.IsValid() ? UMaterialInstanceDynamic::Create(Master.Get(), Outer) : nullptr;
	if (!MID)
	{
		return nullptr;
	}
	MID->SetTextureParameterValue(TEXT("BaseColorTex"), BaseColor);
	if (UTexture* Normal = LoadIfExists(Surface.Normal))
	{
		MID->SetTextureParameterValue(TEXT("NormalTex"), Normal);
	}
	if (UTexture* Roughness = LoadIfExists(Surface.Roughness))
	{
		MID->SetTextureParameterValue(TEXT("RoughnessTex"), Roughness);
	}
	MID->SetVectorParameterValue(TEXT("Color"), Surface.Tint * Tint);
	MID->SetScalarParameterValue(TEXT("Metallic"), Surface.Metallic);
	MID->SetScalarParameterValue(TEXT("RoughnessScale"), Surface.RoughnessScale);
	MID->SetScalarParameterValue(TEXT("UVScale"), Surface.UVScale);
	MID->SetScalarParameterValue(TEXT("Emissive"), Surface.Emissive);
	MID->SetScalarParameterValue(TEXT("Desaturate"), Surface.Desaturate);
	return MID;
}

bool UKakurenboLibrary::ApplySurface(UMeshComponent* Mesh, const FKakurenboSurfaceRow& Surface, const FLinearColor& Tint)
{
	if (!Mesh)
	{
		return false;
	}
	UMaterialInstanceDynamic* MID = CreateSurfaceMaterial(Mesh, Surface, Tint);
	if (!MID)
	{
		return false;
	}
	for (int32 i = 0; i < FMath::Max(1, Mesh->GetNumMaterials()); ++i)
	{
		Mesh->SetMaterial(i, MID);
	}
	return true;
}