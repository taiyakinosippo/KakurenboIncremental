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
}
