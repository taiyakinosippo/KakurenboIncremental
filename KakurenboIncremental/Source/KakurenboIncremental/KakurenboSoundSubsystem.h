// 効果音を鳴らすワールドサブシステム。
// 波形は KakurenboSynth がその場で作る（音のファイル不要）。GameMode の SoundOverrides に音アセットを設定すると、そちらを鳴らす。
//   2D: 画面全体で聞こえる（UI・連打・お宝・結果）
//   3D: その場所から聞こえる（鬼・壁・罠。左右の聞こえ方と大きさで方向と距離がわかる）

#pragma once

#include "CoreMinimal.h"
#include "KakurenboTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "KakurenboSoundSubsystem.generated.h"

class USoundAttenuation;

UCLASS()
class KAKURENBOINCREMENTAL_API UKakurenboSoundSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 画面全体で聞こえる音を鳴らす */
	void Play(EKakurenboSfx Sfx, float VolumeScale = 1.f, float PitchScale = 1.f);

	/** その場所から聞こえる音を鳴らす */
	void PlayAt(EKakurenboSfx Sfx, const FVector& Location, float VolumeScale = 1.f, float PitchScale = 1.f);

	/** WorldContext（アクターなど）のワールドで鳴らす。サブシステムが無ければ何もしない */
	static void Play2D(const UObject* WorldContext, EKakurenboSfx Sfx, float VolumeScale = 1.f, float PitchScale = 1.f);
	static void Play3D(const UObject* WorldContext, EKakurenboSfx Sfx, const FVector& Location, float VolumeScale = 1.f, float PitchScale = 1.f);

	/** 全体の音量（0〜1）。コンソールの KakuVolume で変えられる */
	float MasterVolume = 0.7f;

	/** 鳴らした回数（テスト用。-NoSound で実際には音が出ないときも数える） */
	int32 GetPlayCount(EKakurenboSfx Sfx) const;

	/** 実際に再生を始めた数（テスト用。音が出る環境でだけ増える） */
	int32 GetStartedCount() const { return StartedCount; }

private:
	void PlayInternal(EKakurenboSfx Sfx, const FVector* Location, float VolumeScale, float PitchScale);
	USoundAttenuation* GetAttenuation();

	UPROPERTY()
	TObjectPtr<USoundAttenuation> Attenuation;

	TArray<int32> PlayCounts;
	/** 同じ音が同時にたくさん鳴って大きくなりすぎないよう、最後に鳴らした時刻を覚えておく */
	TArray<double> LastPlayTimes;
	int32 StartedCount = 0;
};
