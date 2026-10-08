#include "KakurenboSoundSubsystem.h"

#include "Components/AudioComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
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
		return Sfx == EKakurenboSfx::OniStep || Sfx == EKakurenboSfx::TreasureSparkle || Sfx == EKakurenboSfx::Summon
			|| Sfx == EKakurenboSfx::OniNotice || Sfx == EKakurenboSfx::Alert;
	}

	/** 重なってもよい音（間隔を空けない） */
	bool AllowsOverlap(EKakurenboSfx Sfx)
	{
		return Sfx == EKakurenboSfx::OniStep || Sfx == EKakurenboSfx::TreasureSparkle || Sfx == EKakurenboSfx::PlayerStep
			|| Sfx == EKakurenboSfx::OniNotice;
	}

	/**
	 * その場所から聞こえる音の距離：FullCm までは最大の大きさ、MaxCm で聞こえなくなる。
	 * 手がかりの音（鬼の足音・お宝）は遠くからでもかすかに聞こえるように長めにする
	 */
	void GetDistanceRange(EKakurenboSfx Sfx, float& OutFullCm, float& OutMaxCm)
	{
		switch (Sfx)
		{
		case EKakurenboSfx::OniStep:         OutFullCm = 300.f;  OutMaxCm = 3200.f; break;
		case EKakurenboSfx::TreasureSparkle: OutFullCm = 200.f;  OutMaxCm = 2800.f; break; // 近づくほどはっきり大きくなる
		case EKakurenboSfx::OniNotice:       OutFullCm = 600.f;  OutMaxCm = 4000.f; break;
		case EKakurenboSfx::Alert:           OutFullCm = 800.f;  OutMaxCm = 6000.f; break;
		case EKakurenboSfx::Summon:          OutFullCm = 1000.f; OutMaxCm = 6000.f; break;
		default:                             OutFullCm = 500.f;  OutMaxCm = 4000.f; break;
		}
	}
}

bool UKakurenboSoundSubsystem::GetListener(FVector& OutLocation, FRotator& OutRotation) const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return false;
	}
	// 向きはカメラ（画面の左右 = 耳の左右）。距離は自分の体から測る（三人称のカメラは体の後ろにあるので）
	OutRotation = PC->PlayerCameraManager->GetCameraRotation();
	const APawn* Pawn = PC->GetPawn();
	OutLocation = (Pawn && !Pawn->IsHidden()) ? Pawn->GetActorLocation() + FVector(0.f, 0.f, 50.f) : PC->PlayerCameraManager->GetCameraLocation();
	return true;
}

FKakurenboSpatialDebug UKakurenboSoundSubsystem::GetLastSpatial(EKakurenboSfx Sfx) const
{
	const int32 Index = static_cast<int32>(Sfx);
	return LastSpatial.IsValidIndex(Index) ? LastSpatial[Index] : FKakurenboSpatialDebug();
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

	float Volume = FMath::Clamp(MasterVolume * VolumeScale, 0.f, 4.f);
	const float BaseVolume = Volume; // 音アセットで鳴らすときは、距離の減り方をエンジンに任せる
	if (Volume <= 0.f)
	{
		return;
	}

	// その場所から聞こえる音：聞く人（自分）から見た向き・距離・間の壁で、左右の大きさとこもり具合を決める
	KakurenboSynth::FSpatial Spatial;
	bool bSpatial = false;
	FVector ListenerLocation;
	FRotator ListenerRotation;
	if (Location && GetListener(ListenerLocation, ListenerRotation))
	{
		bSpatial = true;
		float FullCm = 0.f;
		float MaxCm = 0.f;
		GetDistanceRange(Sfx, FullCm, MaxCm);
		const FVector ToSource = *Location - ListenerLocation;
		const float DistanceGain = KakurenboSynth::DistanceGain(ToSource.Size2D(), FullCm, MaxCm);

		// 間に壁・家具があるか（置いた壁・部屋の壁・家具の当たり判定は WorldStatic）
		FCollisionQueryParams Params(SCENE_QUERY_STAT(KakurenboSoundOcclusion), false);
		if (const APlayerController* PC = World->GetFirstPlayerController(); PC && PC->GetPawn())
		{
			Params.AddIgnoredActor(PC->GetPawn());
		}
		FHitResult Hit;
		const FVector Target = *Location + FVector(0.f, 0.f, 40.f);
		const bool bBlocked = World->LineTraceSingleByObjectType(Hit, ListenerLocation, Target, FCollisionObjectQueryParams(ECC_WorldStatic), Params)
			&& FVector::Dist(Hit.ImpactPoint, Target) > 60.f;

		// カメラの向きで見た位置（X = 前、Y = 右）
		const FVector Local = FRotator(0.f, ListenerRotation.Yaw, 0.f).UnrotateVector(ToSource);
		Spatial = KakurenboSynth::ComputeSpatial(Local, bBlocked ? 1.f : 0.f);
		Spatial.LowPass *= KakurenboSynth::DistanceLowPass(ToSource.Size2D(), FullCm, MaxCm);
		// 近くの音が割れて（頭打ちになって）遠くの音と同じ大きさに聞こえないよう、1 を超えないようにしてから距離で小さくする
		// 鬼の足音は種類・走っているかで大きさが変わる（最大 ×2.5）ので、その差が残るよう先に小さくしておく
		const float Headroom = Sfx == EKakurenboSfx::OniStep ? 0.55f : 1.f;
		Volume = FMath::Min(Volume * Headroom, 1.f) * DistanceGain;

		LastSpatial.SetNum(static_cast<int32>(EKakurenboSfx::Count));
		FKakurenboSpatialDebug& Debug = LastSpatial[Index];
		Debug.Pan = Spatial.Pan;
		Debug.Gain = DistanceGain;
		Debug.GainL = Spatial.GainL;
		Debug.GainR = Spatial.GainR;
		Debug.bOccluded = bBlocked;
		Debug.LowPass = Spatial.LowPass;
		Debug.bBehind = Spatial.bBehind;
		Debug.bValid = true;
		if (Volume <= 0.001f)
		{
			return; // 遠すぎて聞こえない
		}
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
					UGameplayStatics::PlaySoundAtLocation(World, *Override, *Location, BaseVolume, PitchScale, 0.f, IsCueSfx(Sfx) ? GetCueAttenuation() : GetAttenuation());
				}
				else
				{
					UGameplayStatics::PlaySound2D(World, *Override, BaseVolume, PitchScale);
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
	// その場所から聞こえる音は、左右に振り分けた 2ch の波形にして画面全体で鳴らす（左右の差・こもり具合は自前で付けた）
	const TArray<int16> Data = bSpatial ? KakurenboSynth::MakeStereo(Samples, Spatial) : Samples;
	const int32 Channels = bSpatial ? 2 : 1;
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(KakurenboSynth::DefaultSampleRate);
	Wave->NumChannels = Channels;
	Wave->Duration = INDEFINITELY_LOOPING_DURATION;
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->bLooping = false;
	Wave->QueueAudio(reinterpret_cast<const uint8*>(Data.GetData()), Data.Num() * sizeof(int16));

	UAudioComponent* Component = (Location && !bSpatial)
		? UGameplayStatics::SpawnSoundAtLocation(World, Wave, *Location, FRotator::ZeroRotator, Volume, 1.f, 0.f, IsCueSfx(Sfx) ? GetCueAttenuation() : GetAttenuation())
		: UGameplayStatics::SpawnSound2D(World, Wave, Volume);
	if (!Component)
	{
		return; // -NoSound などで音が出ない
	}
	++StartedCount;

	// 波形を流し終わっても自分では止まらない（無音を出し続ける）ので、長さぶん経ったら止める（止まると自動で消える）
	const float Seconds = static_cast<float>(Data.Num() / Channels) / KakurenboSynth::DefaultSampleRate + 0.1f;
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Component, [Component]() { Component->Stop(); }), Seconds, false);
}
