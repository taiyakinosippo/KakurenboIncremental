// 館のマップの間取り（文字で書いた図）を読む純粋なロジック（ワールドに依存しないので単体テストできる）。
//
// 間取りファイル（Data/Maps/*.txt）の書き方:
//   1 行 = Y が同じマスの並び（上の行から Y = 0, 1, 2 …）、1 文字 = 1 マス（左から X = 0, 1, 2 …）。
//   右端（X が一番大きい側）の真ん中に鬼の出入り口がある。
//   '.' は何もない床。'#' は部屋の壁。それ以外の文字は Data/Furniture.csv の行の名前（家具）。
//   ';' で始まる行と空の行は読み飛ばす（メモ用）。
//   同じ文字が長方形に並んでいる所は 1 つの家具になる（例: "BB" は横に 2 マスの本棚）。

#pragma once

#include "CoreMinimal.h"

namespace KakurenboMaps
{
	/** 何もない床の文字 */
	constexpr TCHAR EmptyChar = TEXT('.');

	/** 部屋の壁の文字（Furniture.csv に無くても使える） */
	constexpr TCHAR WallChar = TEXT('#');

	/** 読んだ間取り（マスごとの文字） */
	struct KAKURENBOINCREMENTAL_API FLayout
	{
		int32 SizeX = 0;
		int32 SizeY = 0;
		TArray<TCHAR> Cells;

		bool IsInside(const FIntPoint& P) const { return P.X >= 0 && P.Y >= 0 && P.X < SizeX && P.Y < SizeY; }
		TCHAR Get(const FIntPoint& P) const { return IsInside(P) ? Cells[P.Y * SizeX + P.X] : EmptyChar; }
		void Set(const FIntPoint& P, TCHAR C) { if (IsInside(P)) { Cells[P.Y * SizeX + P.X] = C; } }
	};

	/** 家具 1 つ（同じ文字の長方形） */
	struct KAKURENBOINCREMENTAL_API FPiece
	{
		TCHAR Key = EmptyChar;
		FIntPoint Min = FIntPoint::ZeroValue; // 左上（X・Y が一番小さい）のマス
		FIntPoint Size = FIntPoint(1, 1);     // マス数
	};

	/**
	 * 文字の間取りを SizeX × SizeY のマスに読む。足りない行・文字は床、はみ出した分は捨てる。
	 * @param OutProblems 大きさが合わないなどの注意（読めはする）
	 */
	KAKURENBOINCREMENTAL_API FLayout ParseLayout(const FString& Text, int32 SizeX, int32 SizeY, TArray<FString>& OutProblems);

	/**
	 * 同じ文字のマスを長方形にまとめる（左上から順に、横に伸ばせるだけ伸ばし、次に同じ幅で下へ伸ばす）。
	 * '.' は含めない
	 */
	KAKURENBOINCREMENTAL_API TArray<FPiece> FindPieces(const FLayout& Layout);
}
