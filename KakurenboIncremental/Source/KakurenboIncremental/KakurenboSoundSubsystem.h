// 効果音を鳴らすワールドサブシステム。
// 波形は KakurenboSynth がその場で作る（音のファイル不要）。GameMode の SoundOverrides に音アセットを設定すると、そちらを鳴らす。
//   2D: 画面全体で聞こえる（UI・連打・お宝・結果）
//   3D: その場所から聞こえる（鬼・壁・罠。左右の聞こえ方と大きさで方向と距離がわかる）
//       鬼の足音・お宝のキラキラは「手がかりの音」として、近いほどはっきり大きく、遠いと聞こえない距離の減り方にする
// BGM もプログラムで作った曲をくり返し流す（PlayMusic）。

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "KakurenboTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "KakurenboSoundSubsystem.generated.h"

class UAudioComponent;
class USoundAttenuation;
class USoundWaveProcedural;

/** 最後に鳴らした「その場所から聞こえる音」の聞こえ方（テスト用） */
struct FKakurenboSpatialDebug
{
	bool bValid = false;
	float Pan = 0.f;      // -1 = 真左 〜 +1 = 真右
	float Gain = 0.f;     // 距離による大きさ（0〜1）
	float GainL = 0.f;
	float GainR = 0.f;
	bool bOccluded = false; // 壁・家具の向こう
	float LowPass = 1.f;    // こもり具合（小さいほどこもる。遠い・後ろ・壁の向こう）
	bool bBehind = false;
};

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

	/** BGM を切り替える（同じ曲なら何もしない。前の曲は小さくしながら止める） */
	void PlayMusic(EKakurenboMusic Music);

	/** 今流している BGM（-NoSound で音が出ないときも、流すはずの曲を返す） */
	EKakurenboMusic GetCurrentMusic() const { return CurrentMusic; }

	/** BGM の音量（0〜1。効果音の MasterVolume も掛かる）。コンソールの KakuMusicVolume で変えられる */
	float MusicVolume = 0.45f;
	void SetMusicVolume(float Volume);

	virtual void Deinitialize() override;

	/** その音を最後に鳴らしたときの、左右の聞こえ方（テスト用） */
	FKakurenboSpatialDebug GetLastSpatial(EKakurenboSfx Sfx) const;

	/** 聞く人の位置（自分の体。設置パートなどで体が隠れているときはカメラ）と向き（カメラ） */
	bool GetListener(FVector& OutLocation, FRotator& OutRotation) const;

private:
	void PlayInternal(EKakurenboSfx Sfx, const FVector* Location, float VolumeScale, float PitchScale);
	USoundAttenuation* GetAttenuation();
	/** 手がかりの音（鬼の足音・お宝）の減り方 */
	USoundAttenuation* GetCueAttenuation();
	/** BGM の波形が切れないよう、残りが 1 周を切ったら足す */
	void RefillMusic();

	UPROPERTY()
	TObjectPtr<USoundAttenuation> Attenuation;

	UPROPERTY()
	TObjectPtr<USoundAttenuation> CueAttenuation;

	UPROPERTY()
	TObjectPtr<UAudioComponent> MusicComponent;

	UPROPERTY()
	TObjectPtr<USoundWaveProcedural> MusicWave;

	EKakurenboMusic CurrentMusic = EKakurenboMusic::None;
	/** 曲ごとの 1 周ぶんの波形（一度作ったら使い回す） */
	TMap<EKakurenboMusic, TArray<int16>> MusicCache;
	FTimerHandle MusicRefillTimer;

	TArray<int32> PlayCounts;
	TArray<FKakurenboSpatialDebug> LastSpatial;
	/** 同じ音が同時にたくさん鳴って大きくなりすぎないよう、最後に鳴らした時刻を覚えておく */
	TArray<double> LastPlayTimes;
	int32 StartedCount = 0;
};
