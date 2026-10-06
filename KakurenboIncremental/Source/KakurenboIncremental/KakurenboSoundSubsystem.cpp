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
	if (PlayCounts[Index] > 0 && Now - LastPlayTimes[Index] < MinRepeatInterval)
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
					UGameplayStatics::PlaySoundAtLocation(World, *Override, *Location, Volume, PitchScale, 0.f, GetAttenuation());
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
		? UGameplayStatics::SpawnSoundAtLocation(World, Wave, *Location, FRotator::ZeroRotator, Volume, 1.f, 0.f, GetAttenuation())
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
