#include "KakurenboMaps.h"

namespace
{
	/** 区切りの行（"[floor]" など）か。区切りの名前を返す */
	bool IsSectionLine(const FString& Line, FString& OutName)
	{
		FString Trimmed = Line.TrimStartAndEnd();
		if (Trimmed.StartsWith(TEXT("[")) && Trimmed.EndsWith(TEXT("]")) && Trimmed.Len() >= 2)
		{
			OutName = Trimmed.Mid(1, Trimmed.Len() - 2).ToLower();
			return true;
		}
		return false;
	}
}

FString KakurenboMaps::GetSection(const FString& Text, const FString& Name)
{
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	FString Current; // 最初の区切りより前は間取り（名前は空）
	const FString Wanted = Name.ToLower();
	FString Out;
	for (const FString& Line : Lines)
	{
		FString Section;
		if (IsSectionLine(Line, Section))
		{
			Current = Section;
			continue;
		}
		if (Current == Wanted)
		{
			Out += Line;
			Out += TEXT("\n");
		}
	}
	return Out;
}

KakurenboMaps::FLayout KakurenboMaps::ParseFloorLayer(const FString& Text, int32 SizeX, int32 SizeY)
{
	TArray<FString> Ignore;
	FLayout Floor = ParseLayout(GetSection(Text, TEXT("floor")), SizeX, SizeY, Ignore);
	for (TCHAR& C : Floor.Cells)
	{
		if (C == EmptyChar)
		{
			C = DefaultFloorChar; // 書いていないマスはマップの床
		}
	}
	return Floor;
}

KakurenboMaps::FLayout KakurenboMaps::ParseLayout(const FString& InText, int32 SizeX, int32 SizeY, TArray<FString>& OutProblems)
{
	FLayout Layout;
	Layout.SizeX = FMath::Max(1, SizeX);
	Layout.SizeY = FMath::Max(1, SizeY);
	Layout.Cells.Init(EmptyChar, Layout.SizeX * Layout.SizeY);

	// 区切り（[floor] など）より前だけが間取り
	const FString Text = GetSection(InText, FString());
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	int32 Y = 0;
	for (FString Line : Lines)
	{
		Line.TrimEndInline();
		if (Line.IsEmpty() || Line.StartsWith(TEXT(";")))
		{
			continue; // メモ・空行
		}
		if (Y >= Layout.SizeY)
		{
			OutProblems.Add(FString::Printf(TEXT("%d 行より後は使いません"), Layout.SizeY));
			break;
		}
		if (Line.Len() != Layout.SizeX)
		{
			OutProblems.Add(FString::Printf(TEXT("%d 行目の長さが %d 文字です（%d 文字にしてください）"), Y + 1, Line.Len(), Layout.SizeX));
		}
		for (int32 X = 0; X < FMath::Min(Line.Len(), Layout.SizeX); ++X)
		{
			const TCHAR C = Line[X];
			Layout.Set(FIntPoint(X, Y), (C == TEXT(' ') || C == TEXT('\t')) ? EmptyChar : C);
		}
		++Y;
	}
	if (Y < Layout.SizeY)
	{
		OutProblems.Add(FString::Printf(TEXT("%d 行しかありません（%d 行にしてください。残りは床）"), Y, Layout.SizeY));
	}
	return Layout;
}

bool KakurenboMaps::MeasureLayout(const FString& InText, int32& OutSizeX, int32& OutSizeY)
{
	TArray<FString> Lines;
	GetSection(InText, FString()).ParseIntoArrayLines(Lines, false);
	int32 Rows = 0;
	int32 Longest = 0;
	for (FString Line : Lines)
	{
		Line.TrimEndInline();
		if (Line.IsEmpty() || Line.StartsWith(TEXT(";")))
		{
			continue;
		}
		++Rows;
		Longest = FMath::Max(Longest, Line.Len());
	}
	if (Rows == 0 || Longest == 0)
	{
		return false;
	}
	OutSizeX = Longest;
	OutSizeY = Rows;
	return true;
}

TArray<KakurenboMaps::FPiece> KakurenboMaps::FindPieces(const FLayout& Layout)
{
	TArray<FPiece> Pieces;
	TArray<bool> Used;
	Used.Init(false, Layout.Cells.Num());
	auto Free = [&](int32 X, int32 Y, TCHAR Key)
	{
		const FIntPoint P(X, Y);
		return Layout.IsInside(P) && !Used[Y * Layout.SizeX + X] && Layout.Get(P) == Key;
	};

	for (int32 Y = 0; Y < Layout.SizeY; ++Y)
	{
		for (int32 X = 0; X < Layout.SizeX; ++X)
		{
			const TCHAR Key = Layout.Get(FIntPoint(X, Y));
			if (Key == EmptyChar || Used[Y * Layout.SizeX + X])
			{
				continue;
			}
			// 横に伸ばす
			int32 W = 1;
			while (Free(X + W, Y, Key))
			{
				++W;
			}
			// 同じ幅のまま下へ伸ばす（その行の W マスが全部同じ文字なら）
			int32 H = 1;
			for (;;)
			{
				bool bRowOk = true;
				for (int32 DX = 0; DX < W && bRowOk; ++DX)
				{
					bRowOk = Free(X + DX, Y + H, Key);
				}
				if (!bRowOk)
				{
					break;
				}
				++H;
			}
			for (int32 DY = 0; DY < H; ++DY)
			{
				for (int32 DX = 0; DX < W; ++DX)
				{
					Used[(Y + DY) * Layout.SizeX + X + DX] = true;
				}
			}
			FPiece& Piece = Pieces.AddDefaulted_GetRef();
			Piece.Key = Key;
			Piece.Min = FIntPoint(X, Y);
			Piece.Size = FIntPoint(W, H);
		}
	}
	return Pieces;
}

bool KakurenboMaps::ParseCutout(const FString& Text, FBox2D& OutCutout)
{
	TArray<FString> Parts;
	Text.ParseIntoArrayWS(Parts);
	if (Parts.Num() != 4)
	{
		return false;
	}
	float V[4];
	for (int32 i = 0; i < 4; ++i)
	{
		if (!Parts[i].IsNumeric())
		{
			return false;
		}
		V[i] = FMath::Clamp(FCString::Atof(*Parts[i]), 0.f, 1.f);
	}
	if (V[2] - V[0] <= 0.01f || V[3] - V[1] <= 0.01f)
	{
		return false;
	}
	OutCutout = FBox2D(FVector2D(V[0], V[1]), FVector2D(V[2], V[3]));
	return true;
}

TArray<FBox2D> KakurenboMaps::SplitFootprint(const FBox2D& Bounds, const FBox2D& Cutout)
{
	const FVector2D Size = Bounds.GetSize();
	if (!Bounds.bIsValid || !Cutout.bIsValid || Size.X <= 0.0 || Size.Y <= 0.0)
	{
		return { Bounds };
	}
	// 割合 → 長さ（外形の中に収める）
	const FVector2D CutMin = Bounds.Min + Size * FVector2D(FMath::Clamp(Cutout.Min.X, 0.0, 1.0), FMath::Clamp(Cutout.Min.Y, 0.0, 1.0));
	const FVector2D CutMax = Bounds.Min + Size * FVector2D(FMath::Clamp(Cutout.Max.X, 0.0, 1.0), FMath::Clamp(Cutout.Max.Y, 0.0, 1.0));
	if (CutMax.X - CutMin.X <= 0.5 || CutMax.Y - CutMin.Y <= 0.5)
	{
		return { Bounds };
	}
	// 抜いた長方形の左右は端から端まで、上下は抜いた長方形の幅だけ（重ならない）
	TArray<FBox2D> Parts;
	auto Add = [&Parts](const FVector2D& Min, const FVector2D& Max)
	{
		if (Max.X - Min.X > 0.5 && Max.Y - Min.Y > 0.5)
		{
			Parts.Add(FBox2D(Min, Max));
		}
	};
	Add(Bounds.Min, FVector2D(CutMin.X, Bounds.Max.Y));
	Add(FVector2D(CutMax.X, Bounds.Min.Y), Bounds.Max);
	Add(FVector2D(CutMin.X, Bounds.Min.Y), FVector2D(CutMax.X, CutMin.Y));
	Add(FVector2D(CutMin.X, CutMax.Y), FVector2D(CutMax.X, Bounds.Max.Y));
	return Parts;
}
