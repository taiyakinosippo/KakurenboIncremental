// 効果音を鳴らすワールドサブシステム。
// 波形は Data/Sounds/<音>.wav（採用した AI の効果音。Tools/SoundGen/ExportForGame.py）があればそれ、無ければ KakurenboSynth がその場で作る。
// どちらも同じ左右の振り分け・こもりを通す。GameMode の SoundOverrides に音アセットを設定すると、そちらを鳴らす。
//   2D: 画面全体で聞こえる（UI・連打・お宝・結果）
//   3D: その場所から聞こえる（鬼・壁・罠。左右の聞こえ方と大きさで方向と距離がわかる）
//       鬼の足音・お宝のキラキラは「手がかりの音」として、近いほどはっきり大きく、遠いと聞こえない距離の減り方にする
// BGM は Data/Sounds/Music_<曲>.wav（BGM を作る AI の曲。Tools/MusicGen/ExportForGame.py がくり返せる形にしたもの）があればそれを、
// 無ければプログラムで作った曲をくり返し流す（PlayMusic）。

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

/** M16：聞こえた音 1 つ（テキストの窓口が「聞こえた音」として AI に伝える） */
struct FKakurenboHeardSound
{
	EKakurenboSfx Sfx = EKakurenboSfx::Mash;
	/** その場所から聞こえる音か（false なら画面全体の音） */
	bool bSpatial = false;
	FVector Location = FVector::ZeroVector;
	/** 聞こえた大きさ（距離と壁で小さくなった後。音量の設定は掛けない。だいたい 0〜1） */
	float Loudness = 0.f;
	/** 壁・家具の向こうでこもって聞こえた */
	bool bOccluded = false;
};

UCLASS()
class KAKURENBOINCREMENTAL_API UKakurenboSoundSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** M16：聞こえた音を記録する（テキストの窓口が動いているときだけ）。読んだら空にする */
	bool bRecordHeard = false;
	TArray<FKakurenboHeardSound> Heard;

	/** 画面全体で聞こえる音を鳴らす */
	void Play(EKakurenboSfx Sfx, float VolumeScale = 1.f, float PitchScale = 1.f);

	/** その場所から聞こえる音を鳴らす */
	void PlayAt(EKakurenboSfx Sfx, const FVector& Location, float VolumeScale = 1.f, float PitchScale = 1.f);

	/** WorldContext（アクターなど）のワールドで鳴らす。サブシステムが無ければ何もしない */
	static void Play2D(const UObject* WorldContext, EKakurenboSfx Sfx, float VolumeScale = 1.f, float PitchScale = 1.f);
	static void Play3D(const UObject* WorldContext, EKakurenboSfx Sfx, const FVector& Location, float VolumeScale = 1.f, float PitchScale = 1.f);

	/** 全体の音量（0〜1）。設定画面・コンソールの KakuVolume で変えられる。BGM の音量も変わる */
	float MasterVolume = 0.7f;
	void SetMasterVolume(float Volume);

	/** 効果音だけの音量（0〜1。全体の音量も掛かる）。設定画面で変えられる */
	float SfxVolume = 1.f;

	/** 鳴らした回数（テスト用。-NoSound で実際には音が出ないときも数える） */
	int32 GetPlayCount(EKakurenboSfx Sfx) const;

	/** 実際に再生を始めた数（テスト用。音が出る環境でだけ増える） */
	int32 GetStartedCount() const { return StartedCount; }

	/** BGM を切り替える（同じ曲なら何もしない。前の曲は小さくしながら止める） */
	void PlayMusic(EKakurenboMusic Music);

	/** 今流している BGM（-NoSound で音が出ないときも、流すはずの曲を返す） */
	EKakurenboMusic GetCurrentMusic() const { return CurrentMusic; }

	/** その曲を Data/Sounds の WAV から読んだか（テスト用。一度 PlayMusic した曲だけわかる） */
	bool IsMusicFromFile(EKakurenboMusic Music) const;

	/** Data/Sounds/Music_<曲>.wav（16bit の WAV）を読む。無い・読めなければ false */
	static bool LoadMusicWav(EKakurenboMusic Music, TArray<int16>& OutSamples, int32& OutSampleRate, int32& OutChannels);

	/** Data/Sounds/<音>.wav（モノラル・44100Hz・16bit。採用した AI の効果音）を読む。無い・形が違えば false（プログラムの音を使う） */
	static bool LoadSfxWav(EKakurenboSfx Sfx, TArray<int16>& OutSamples);

	/** その効果音を Data/Sounds の WAV で鳴らしているか（テスト用。一度鳴らした音だけわかる） */
	bool IsSfxFromFile(EKakurenboSfx Sfx) const;

	/** 16bit の WAV ファイルを読む */
	static bool ReadWav16(const FString& Path, TArray<int16>& OutSamples, int32& OutSampleRate, int32& OutChannels);

	/** BGM の音量（0〜1。効果音の MasterVolume も掛かる）。コンソールの KakuMusicVolume で変えられる */
	float MusicVolume = 0.45f;
	void SetMusicVolume(float Volume);

	virtual void Deinitialize() override;

	/**
	 * BGM の波形が切れないよう、残りが 1 周を切ったら足す（1 秒ごとのタイマーで呼ぶ）。
	 * 一時停止中はタイマーが止まるので、PlayerController が毎フレーム呼ぶ
	 */
	void RefillMusic();

	/** その音を最後に鳴らしたときの、左右の聞こえ方（テスト用） */
	FKakurenboSpatialDebug GetLastSpatial(EKakurenboSfx Sfx) const;

	/** 聞く人の位置（自分の体。設置パートなどで体が隠れているときはカメラ）と向き（カメラ） */
	bool GetListener(FVector& OutLocation, FRotator& OutRotation) const;

private:
	void PlayInternal(EKakurenboSfx Sfx, const FVector* Location, float VolumeScale, float PitchScale);
	USoundAttenuation* GetAttenuation();
	/** 手がかりの音（鬼の足音・お宝）の減り方 */
	USoundAttenuation* GetCueAttenuation();

	UPROPERTY()
	TObjectPtr<USoundAttenuation> Attenuation;

	UPROPERTY()
	TObjectPtr<USoundAttenuation> CueAttenuation;

	UPROPERTY()
	TObjectPtr<UAudioComponent> MusicComponent;

	UPROPERTY()
	TObjectPtr<USoundWaveProcedural> MusicWave;

	EKakurenboMusic CurrentMusic = EKakurenboMusic::None;

	/** 曲の 1 周ぶんの波形（16bit。2ch なら左右が交互に並ぶ） */
	struct FMusicLoop
	{
		TArray<int16> Samples;
		int32 SampleRate = 0;
		int32 Channels = 1;
		bool bFromFile = false;
	};
	/** 曲ごとの 1 周ぶんの波形（一度読んだ・作ったら使い回す） */
	TMap<EKakurenboMusic, FMusicLoop> MusicCache;

	/** 効果音の波形（Data/Sounds の WAV。無ければプログラムで作る。PitchScale で高さ（と長さ）を変える） */
	TArray<int16> GetSfxSamples(EKakurenboSfx Sfx, float PitchScale);
	/** 効果音ごとの WAV の波形（空なら WAV が無い） */
	TMap<EKakurenboSfx, TArray<int16>> SfxFileCache;
	FTimerHandle MusicRefillTimer;

	TArray<int32> PlayCounts;
	TArray<FKakurenboSpatialDebug> LastSpatial;
	/** 同じ音が同時にたくさん鳴って大きくなりすぎないよう、最後に鳴らした時刻を覚えておく */
	TArray<double> LastPlayTimes;
	int32 StartedCount = 0;
};
