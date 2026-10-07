#include "KakurenboSoundSubsystem.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "KakurenboGameMode.h"
#include "KakurenboSynth.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"
#include "TimerManager.h"

namespace
{
	/** 同じ音をこの間隔（秒）より短く続けて鳴らさない（壁がまとめて壊れたときなど） */
	constexpr double MinRepeatInterval = 0.04;

	/** 鬼の足音・お宝のキラキラ：場所の手がかりになる音（何体ぶん重なってもよい。近いほど大きい減り方） */
	bool IsCueSfx(EKakurenboSfx Sfx)
	{
		return Sfx == EKakurenboSfx::OniStep || Sfx == EKakurenboSfx::TreasureSparkle || Sfx == EKakurenboSfx::Summon;
	}

	/** 重なってもよい音（間隔を空けない） */
	bool AllowsOverlap(EKakurenboSfx Sfx)
	{
		return Sfx == EKakurenboSfx::OniStep || Sfx == EKakurenboSfx::TreasureSparkle || Sfx == EKakurenboSfx::PlayerStep;
	}
}

void UKakurenboSoundSubsystem::Play2D(const UObject* WorldContext, EKakurenboSfx Sfx, float VolumeScale, float PitchScale)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (UKakurenboSoundSubsystem* Sound = World ? World->GetSubsystem<UKakurenboSoundSubsystem>() : nullptr)
	{
		Sound->Play(Sfx, VolumeScale, PitchScale);
	}
}

void UKakurenboSoundSubsystem::Play3D(const UObject* WorldContext, EKakurenboSfx Sfx, const FVector& Location, float VolumeScale, float PitchScale)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (UKakurenboSoundSubsystem* Sound = World ? World->GetSubsystem<UKakurenboSoundSubsystem>() : nullptr)
	{
		Sound->PlayAt(Sfx, Location, VolumeScale, PitchScale);
	}
}

void UKakurenboSoundSubsystem::Play(EKakurenboSfx Sfx, float VolumeScale, float PitchScale)
{
	PlayInternal(Sfx, nullptr, VolumeScale, PitchScale);
}

void UKakurenboSoundSubsystem::PlayAt(EKakurenboSfx Sfx, const FVector& Location, float VolumeScale, float PitchScale)
{
	PlayInternal(Sfx, &Location, VolumeScale, PitchScale);
}

int32 UKakurenboSoundSubsystem::GetPlayCount(EKakurenboSfx Sfx) const
{
	const int32 Index = static_cast<int32>(Sfx);
	return PlayCounts.IsValidIndex(Index) ? PlayCounts[Index] : 0;
}

USoundAttenuation* UKakurenboSoundSubsystem::GetAttenuation()
{
	if (!Attenuation)
	{
		// 5m 以内は最大の大きさ、そこから 35m かけて小さくなる。すぐ近くの音は左右に振らない
		Attenuation = NewObject<USoundAttenuation>(this);
		FSoundAttenuationSettings& S = Attenuation->Attenuation;
		S.bAttenuate = true;
		S.bSpatialize = true;
		S.AttenuationShape = EAttenuationShape::Sphere;
		S.AttenuationShapeExtents = FVector(500.f, 0.f, 0.f);
		S.FalloffDistance = 3500.f;
		S.NonSpatializedRadiusStart = 150.f;
		S.NonSpatializedRadiusEnd = 50.f;
	}
	return Attenuation;
}

USoundAttenuation* UKakurenboSoundSubsystem::GetCueAttenuation()
{
	if (!CueAttenuation)
	{
		// すぐ近く（1.5m）だけ最大で、そこから 20m かけて聞こえなくなる。左右の聞こえ方で方向もわかる
		CueAttenuation = NewObject<USoundAttenuation>(this);
		FSoundAttenuationSettings& S = CueAttenuation->Attenuation;
		S.bAttenuate = true;
		S.bSpatialize = true;
		S.AttenuationShape = EAttenuationShape::Sphere;
		S.AttenuationShapeExtents = FVector(150.f, 0.f, 0.f);
		S.FalloffDistance = 2000.f;
		S.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		S.NonSpatializedRadiusStart = 120.f;
		S.NonSpatializedRadiusEnd = 40.f;
	}
	return CueAttenuation;
}

void UKakurenboSoundSubsystem::PlayMusic(EKakurenboMusic Music)
{
	UWorld* World = GetWorld();
	if (!World || Music == CurrentMusic)
	{
		return;
	}
	CurrentMusic = Music;
	UE_LOG(LogTemp, Log, TEXT("BGM -> %s"), *UEnum::GetValueAsString(Music));

	// 前の曲は 1 秒かけて小さくして止める（止まると自動で消える）
	if (MusicComponent)
	{
		MusicComponent->FadeOut(1.f, 0.f);
		MusicComponent = nullptr;
	}
	MusicWave = nullptr;
	World->GetTimerManager().ClearTimer(MusicRefillTimer);
	if (Music == EKakurenboMusic::None)
	{
		return;
	}

	// 音アセットが設定されていればそれを流す（くり返しは音アセットの設定）
	if (const AKakurenboGameMode* GM = World->GetAuthGameMode<AKakurenboGameMode>())
	{
		if (const TObjectPtr<USoundBase>* Override = GM->MusicOverrides.Find(Music); Override && *Override)
		{
			if (UAudioComponent* Component = UGameplayStatics::SpawnSound2D(World, *Override, 1.f))
			{
				Component->FadeIn(1.f, FMath::Clamp(MasterVolume * MusicVolume, 0.f, 1.f));
				MusicComponent = Component;
			}
			return;
		}
	}

	// 1 周ぶんの波形は最初に一度だけ作る
	TArray<int16>* Loop = MusicCache.Find(Music);
	if (!Loop)
	{
		Loop = &MusicCache.Add(Music, KakurenboSynth::RenderMusic(Music));
	}
	if (Loop->Num() == 0)
	{
		return;
	}
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(KakurenboSynth::MusicSampleRate);
	Wave->NumChannels = 1;
	Wave->Duration = INDEFINITELY_LOOPING_DURATION;
	Wave->SoundGroup = SOUNDGROUP_Music;
	Wave->bLooping = false;
	Wave->QueueAudio(reinterpret_cast<const uint8*>(Loop->GetData()), Loop->Num() * sizeof(int16));
	Wave->QueueAudio(reinterpret_cast<const uint8*>(Loop->GetData()), Loop->Num() * sizeof(int16));

	// bPersistAcrossLevelTransition などは不要。2D（画面全体）で流し、1 秒かけて大きくする
	UAudioComponent* Component = UGameplayStatics::SpawnSound2D(World, Wave, 1.f);
	if (!Component)
	{
		return; // -NoSound などで音が出ない
	}
	Component->FadeIn(1.f, FMath::Clamp(MasterVolume * MusicVolume, 0.f, 1.f));
	MusicComponent = Component;
	MusicWave = Wave;
	// 流した分を 1 秒ごとに足していく（くり返し）
	World->GetTimerManager().SetTimer(MusicRefillTimer, FTimerDelegate::CreateUObject(this, &UKakurenboSoundSubsystem::RefillMusic), 1.f, true);
}

void UKakurenboSoundSubsystem::RefillMusic()
{
	const TArray<int16>* Loop = MusicCache.Find(CurrentMusic);
	if (!MusicWave || !Loop || Loop->Num() == 0)
	{
		return;
	}
	const int32 LoopBytes = Loop->Num() * sizeof(int16);
	if (MusicWave->GetAvailableAudioByteCount() < LoopBytes)
	{
		MusicWave->QueueAudio(reinterpret_cast<const uint8*>(Loop->GetData()), LoopBytes);
	}
}

void UKakurenboSoundSubsystem::SetMusicVolume(float Volume)
{
	MusicVolume = FMath::Clamp(Volume, 0.f, 1.f);
	if (MusicComponent)
	{
		MusicComponent->SetVolumeMultiplier(FMath::Clamp(MasterVolume * MusicVolume, 0.f, 1.f));
	}
}

void UKakurenboSoundSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MusicRefillTimer);
	}
	Super::Deinitialize();
}

void UKakurenboSoundSubsystem::PlayInternal(EKakurenboSfx Sfx, const FVector* Location, float VolumeScale, float PitchScale)
{
	UWorld* World = GetWorld();
	const int32 Index = static_cast<int32>(Sfx);
	if (!World || Index < 0 || Index >= static_cast<int32>(EKakurenboSfx::Count))
	{
		return;
	}

	// 同じ音が一度にたくさん鳴らないようにする
	const double Now = World->GetTimeSeconds();
	LastPlayTimes.SetNum(static_cast<int32>(EKakurenboSfx::Count));
	PlayCounts.SetNum(static_cast<int32>(EKakurenboSfx::Count));
	if (!AllowsOverlap(Sfx) && PlayCounts[Index] > 0 && Now - LastPlayTimes[Index] < MinRepeatInterval)
	{
		return;
	}
	LastPlayTimes[Index] = Now;
	PlayCounts[Index]++;

	const float Volume = FMath::Clamp(MasterVolume * VolumeScale, 0.f, 4.f);
	if (Volume <= 0.f)
	{
		return;
	}

	// 音アセットが設定されていればそれを鳴らす
	if (const AKakurenboGameMode* GM = World->GetAuthGameMode<AKakurenboGameMode>())
	{
		if (const TObjectPtr<USoundBase>* Override = GM->SoundOverrides.Find(Sfx))
		{
			if (*Override)
			{
				if (Location)
				{
					UGameplayStatics::PlaySoundAtLocation(World, *Override, *Location, Volume, PitchScale, 0.f, IsCueSfx(Sfx) ? GetCueAttenuation() : GetAttenuation());
				}
				else
				{
					UGameplayStatics::PlaySound2D(World, *Override, Volume, PitchScale);
				}
				return;
			}
		}
	}

	// 無ければ波形を作って鳴らす。
	// USoundWaveProcedural: 渡した波形データ（16bit・モノラル）をそのまま再生する音。1 回鳴らすごとに新しく作る
	const TArray<int16> Samples = KakurenboSynth::RenderSfx(Sfx, PitchScale);
	if (Samples.Num() == 0)
	{
		return;
	}
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(KakurenboSynth::DefaultSampleRate);
	Wave->NumChannels = 1;
	Wave->Duration = INDEFINITELY_LOOPING_DURATION;
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->bLooping = false;
	Wave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));

	UAudioComponent* Component = Location
		? UGameplayStatics::SpawnSoundAtLocation(World, Wave, *Location, FRotator::ZeroRotator, Volume, 1.f, 0.f, IsCueSfx(Sfx) ? GetCueAttenuation() : GetAttenuation())
		: UGameplayStatics::SpawnSound2D(World, Wave, Volume);
	if (!Component)
	{
		return; // -NoSound などで音が出ない
	}
	++StartedCount;

	// 波形を流し終わっても自分では止まらない（無音を出し続ける）ので、長さぶん経ったら止める（止まると自動で消える）
	const float Seconds = static_cast<float>(Samples.Num()) / KakurenboSynth::DefaultSampleRate + 0.1f;
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Component, [Component]() { Component->Stop(); }), Seconds, false);
}
