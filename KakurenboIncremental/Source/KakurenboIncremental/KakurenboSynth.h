// 効果音の波形をプログラムで作る（音のファイルが無くても鳴らせるように）。
// ワールドに依存しない純粋な計算なので、単体テストで中身を確かめられる。
// 鳴らすのは UKakurenboSoundSubsystem。音を差し替えたいときは GameMode の SoundOverrides に音アセットを設定する。

#pragma once

#include "CoreMinimal.h"
#include "KakurenboTypes.h"

namespace KakurenboSynth
{
	constexpr int32 DefaultSampleRate = 44100;

	/** 音色 */
	enum class EWave : uint8
	{
		Sine,     // 正弦波（澄んだ音）
		Square,   // 矩形波（ピコピコした音）
		Triangle, // 三角波（柔らかいピコピコ）
		Saw,      // のこぎり波（ブザーのような音）
		Noise,    // ノイズ（ザッ・ガシャッという音）
	};

	/** 音 1 つ分 */
	struct FNote
	{
		float Start = 0.f;        // 鳴り始める時刻（秒）
		float Duration = 0.1f;    // 長さ（秒）
		float Freq = 440.f;       // 最初の高さ（Hz。ノイズでは使わない）
		float FreqEnd = 0.f;      // 最後の高さ（Hz。0 なら Freq のまま）
		EWave Wave = EWave::Sine;
		float Volume = 0.5f;
		float Attack = 0.004f;    // 立ち上がりの時間（秒）
		float DecayPower = 1.5f;  // 減り方（大きいほど早く小さくなる。0 なら最後まで同じ大きさ）
		float VibratoDepth = 0.f; // ビブラートの深さ（高さに対する割合）
		float VibratoRate = 0.f;  // ビブラートの速さ（Hz）
		float LowPass = 1.f;      // ノイズのこもり具合（0〜1。小さいほどこもった低い音）
	};

	/** 効果音の設計図（音の並び） */
	TArray<FNote> GetRecipe(EKakurenboSfx Sfx);

	/** 音の並びを 16bit・モノラルの波形にする。PitchScale で全体の高さを変える（長さは変わらない） */
	TArray<int16> Render(const TArray<FNote>& Notes, float PitchScale = 1.f, int32 SampleRate = DefaultSampleRate, uint32 NoiseSeed = 12345);

	/** 効果音の波形を作る */
	TArray<int16> RenderSfx(EKakurenboSfx Sfx, float PitchScale = 1.f, int32 SampleRate = DefaultSampleRate);

	/** 効果音の長さ（秒） */
	float GetDuration(EKakurenboSfx Sfx);

	/** BGM のサンプリング周波数（効果音より低くして、作る手間とメモリを減らす） */
	constexpr int32 MusicSampleRate = 32000;

	/** BGM の設計図（音の並び）と 1 周の長さ（秒） */
	TArray<FNote> GetMusicRecipe(EKakurenboMusic Music, float& OutLoopSeconds);

	/** BGM の 1 周ぶんの波形（くり返し再生するとつながる） */
	TArray<int16> RenderMusic(EKakurenboMusic Music, int32 SampleRate = MusicSampleRate);

	// ---------------------------------------------------------------- 左右の聞こえ方（自前の立体音響）
	// エンジンの 3D 音は左右の差が小さく、低い音は方向がわかりにくいので、
	// 「どっちから来ているか」が画面を見なくてもわかるよう、左右の大きさ・耳に届く時間差・後ろや壁の向こうのこもり方を自分で付ける。

	/** モノラルの音を左右に振り分ける設定 */
	struct FSpatial
	{
		float GainL = 1.f;     // 左の大きさ（0〜1）
		float GainR = 1.f;     // 右の大きさ（0〜1）
		int32 DelayL = 0;      // 左耳に届くのが遅れるサンプル数（反対側の耳は少し遅れて聞こえる）
		int32 DelayR = 0;
		float LowPass = 1.f;   // こもり具合（1 = そのまま。小さいほど高い音が消えてこもる）
		float Pan = 0.f;       // -1 = 真左 〜 +1 = 真右（テスト・表示用）
		bool bBehind = false;  // 後ろから聞こえる
	};

	/**
	 * 聞く人から見た音の向き（Local: X = 前、Y = 右）から、左右の振り分けを決める。
	 * @param Occlusion 0〜1（1 = 壁・家具の向こう。こもって小さくなる）
	 */
	FSpatial ComputeSpatial(const FVector& Local, float Occlusion, int32 SampleRate = DefaultSampleRate);

	/** モノラルの波形を、左右の振り分けをした 2ch（L, R, L, R…）の波形にする */
	TArray<int16> MakeStereo(const TArray<int16>& Mono, const FSpatial& Spatial);

	/**
	 * 距離による大きさ（0〜1）。FullDistance までは 1、MaxDistance で 0。
	 * 遠い音もかすかに聞こえるよう、なだらかに小さくなる
	 */
	float DistanceGain(float Distance, float FullDistance, float MaxDistance);
}
