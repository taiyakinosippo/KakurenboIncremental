// 館のマップの間取り（文字で書いた図）を読む純粋なロジック（ワールドに依存しないので単体テストできる）。
//
// 間取りファイル（Data/Maps/*.txt）の書き方:
//   1 行 = Y が同じマスの並び（上の行から Y = 0, 1, 2 …）、1 文字 = 1 マス（左から X = 0, 1, 2 …）。
//   右端（X が一番大きい側）の真ん中に鬼の出入り口がある。館の大きさは文字数・行数で決まる（マップごとに違ってよい）。
//   '.' は何もない床。'#' は部屋の壁。それ以外の文字は Data/Furniture.csv の行の名前（家具）。
//   ';' で始まる行と空の行は読み飛ばす（メモ用）。
//   同じ文字が長方形に並んでいる所は 1 つの家具になる（例: "BB" は横に 2 マスの本棚）。
//   "[floor]" の行から後は床の種類（同じ大きさの文字の図。1 文字 = Data/FloorKinds.csv の行の名前）。無ければ全部 'w'（マップの床）

#pragma once

#include "CoreMinimal.h"

namespace KakurenboMaps
{
	/** 何もない床の文字 */
	constexpr TCHAR EmptyChar = TEXT('.');

	/** 部屋の壁の文字（Furniture.csv に無くても使える） */
	constexpr TCHAR WallChar = TEXT('#');

	/** 床の種類の既定（マップの床。Maps.csv の FloorSurface） */
	constexpr TCHAR DefaultFloorChar = TEXT('w');

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
	 * 間取りの大きさ（一番長い行の文字数 × 行数。メモの行と空の行は数えない）。館の大きさはこれで決まる。
	 * @return 間取りが書いてあれば true（空なら OutSizeX / OutSizeY はそのまま）
	 */
	KAKURENBOINCREMENTAL_API bool MeasureLayout(const FString& Text, int32& OutSizeX, int32& OutSizeY);

	/**
	 * 同じ文字のマスを長方形にまとめる（左上から順に、横に伸ばせるだけ伸ばし、次に同じ幅で下へ伸ばす）。
	 * '.' は含めない
	 */
	KAKURENBOINCREMENTAL_API TArray<FPiece> FindPieces(const FLayout& Layout);

	/**
	 * 間取りのファイルの区切り "[Name]" の行から次の区切りまでの文（Name が空なら、最初の区切りより前＝間取り）
	 */
	KAKURENBOINCREMENTAL_API FString GetSection(const FString& Text, const FString& Name);

	/**
	 * 床の種類（"[floor]" の区切り）を SizeX × SizeY で読む。書いていないマスは DefaultFloorChar
	 */
	KAKURENBOINCREMENTAL_API FLayout ParseFloorLayer(const FString& Text, int32 SizeX, int32 SizeY);

	/**
	 * 家具のメッシュを上から見て空いている長方形（Furniture.csv の MeshCutout / AltMeshCutout）を読む。
	 * "最小X 最小Y 最大X 最大Y" を、メッシュの外形（バウンディングボックス）に対する割合 0〜1 で書く（例: L 字の本棚の手前の角 "0.4 0.44 1 1"）。
	 * @return 空・書き間違い・広さが無いときは false（抜かない）
	 */
	KAKURENBOINCREMENTAL_API bool ParseCutout(const FString& Text, FBox2D& OutCutout);

	/**
	 * 家具の当たり判定の形：外形 Bounds から Cutout（Bounds に対する割合）を抜いた残りを、重ならない長方形に分ける
	 * （L 字なら 2 つ・コの字なら 3 つ・真ん中の穴なら 4 つ）。Cutout が無効なら Bounds そのもの 1 つ
	 */
	KAKURENBOINCREMENTAL_API TArray<FBox2D> SplitFootprint(const FBox2D& Bounds, const FBox2D& Cutout);
}
