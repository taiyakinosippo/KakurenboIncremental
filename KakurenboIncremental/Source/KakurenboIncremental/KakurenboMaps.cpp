#include "KakurenboMaps.h"

KakurenboMaps::FLayout KakurenboMaps::ParseLayout(const FString& Text, int32 SizeX, int32 SizeY, TArray<FString>& OutProblems)
{
	FLayout Layout;
	Layout.SizeX = FMath::Max(1, SizeX);
	Layout.SizeY = FMath::Max(1, SizeY);
	Layout.Cells.Init(EmptyChar, Layout.SizeX * Layout.SizeY);

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
