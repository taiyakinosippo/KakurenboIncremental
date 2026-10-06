#include "KakurenboSynth.h"

namespace KakurenboSynth
{
	namespace
	{
		// 音名の高さ（Hz）
		constexpr float G5 = 784.0f;
		constexpr float C6 = 1046.5f;
		constexpr float E6 = 1318.5f;
		constexpr float G6 = 1568.0f;
		constexpr float C7 = 2093.0f;

		FNote Tone(float Start, float Duration, float Freq, float FreqEnd, EWave Wave, float Volume, float DecayPower = 1.5f)
		{
			FNote N;
			N.Start = Start;
			N.Duration = Duration;
			N.Freq = Freq;
			N.FreqEnd = FreqEnd;
			N.Wave = Wave;
			N.Volume = Volume;
			N.DecayPower = DecayPower;
			return N;
		}

		FNote Noise(float Start, float Duration, float Volume, float LowPass, float DecayPower = 1.5f)
		{
			FNote N = Tone(Start, Duration, 0.f, 0.f, EWave::Noise, Volume, DecayPower);
			N.LowPass = LowPass;
			return N;
		}

		FNote Vibrato(FNote N, float Depth, float Rate)
		{
			N.VibratoDepth = Depth;
			N.VibratoRate = Rate;
			return N;
		}
	}

	TArray<FNote> GetRecipe(EKakurenboSfx Sfx)
	{
		switch (Sfx)
		{
		case EKakurenboSfx::Mash:
			// 短い「ポッ」
			return { Tone(0.f, 0.07f, 900.f, 650.f, EWave::Sine, 0.45f, 2.f), Noise(0.f, 0.02f, 0.12f, 0.5f) };

		case EKakurenboSfx::Treasure:
			// きらきらした上がる和音
			return {
				Tone(0.00f, 0.12f, C6, 0.f, EWave::Triangle, 0.35f, 1.2f),
				Tone(0.06f, 0.12f, E6, 0.f, EWave::Triangle, 0.35f, 1.2f),
				Tone(0.12f, 0.12f, G6, 0.f, EWave::Triangle, 0.35f, 1.2f),
				Tone(0.18f, 0.30f, C7, 0.f, EWave::Triangle, 0.35f, 1.2f),
				Vibrato(Tone(0.18f, 0.35f, C7 * 2.f, 0.f, EWave::Sine, 0.12f, 1.f), 0.01f, 8.f),
			};

		case EKakurenboSfx::Alert:
			// 「！」の 2 音（高い方を伸ばす）
			return {
				Tone(0.00f, 0.07f, 740.f, 0.f, EWave::Square, 0.30f, 0.5f),
				Vibrato(Tone(0.08f, 0.24f, 1108.f, 0.f, EWave::Square, 0.30f, 0.8f), 0.02f, 12.f),
			};

		case EKakurenboSfx::BlockHit:
			// 「ゴッ」
			return { Noise(0.f, 0.09f, 0.50f, 0.25f, 2.f), Tone(0.f, 0.10f, 140.f, 90.f, EWave::Sine, 0.45f, 2.f) };

		case EKakurenboSfx::BlockBreak:
			// 「ガシャン」
			return {
				Noise(0.00f, 0.40f, 0.60f, 0.35f, 1.8f),
				Tone(0.00f, 0.30f, 110.f, 45.f, EWave::Sine, 0.55f, 1.5f),
				Noise(0.05f, 0.15f, 0.30f, 0.8f, 2.f),
			};

		case EKakurenboSfx::Caught:
			// 下がっていく「デーン」
			return {
				Vibrato(Tone(0.00f, 0.55f, 520.f, 260.f, EWave::Saw, 0.28f, 0.8f), 0.03f, 6.f),
				Tone(0.10f, 0.60f, 260.f, 130.f, EWave::Square, 0.18f, 1.f),
				Tone(0.00f, 0.40f, 80.f, 60.f, EWave::Sine, 0.40f, 1.5f),
			};

		case EKakurenboSfx::Clear:
			// ファンファーレ
			return {
				Tone(0.00f, 0.12f, G5, 0.f, EWave::Triangle, 0.32f, 1.f),
				Tone(0.10f, 0.12f, C6, 0.f, EWave::Triangle, 0.32f, 1.f),
				Tone(0.20f, 0.12f, E6, 0.f, EWave::Triangle, 0.32f, 1.f),
				Tone(0.30f, 0.45f, G6, 0.f, EWave::Triangle, 0.32f, 1.f),
				Tone(0.30f, 0.45f, C6, 0.f, EWave::Square, 0.10f, 1.f),
				Tone(0.30f, 0.50f, C7, 0.f, EWave::Sine, 0.12f, 1.f),
			};

		case EKakurenboSfx::TrapSticky:
			// 「ボヨン」
			return { Vibrato(Tone(0.f, 0.40f, 320.f, 110.f, EWave::Sine, 0.50f, 1.f), 0.12f, 16.f), Noise(0.f, 0.05f, 0.15f, 0.3f) };

		case EKakurenboSfx::DecoyPing:
			// 「ピピッ」
			return { Tone(0.00f, 0.05f, 1250.f, 0.f, EWave::Square, 0.25f, 0.3f), Tone(0.09f, 0.06f, 1550.f, 0.f, EWave::Square, 0.25f, 0.5f) };

		case EKakurenboSfx::DecoyBreak:
			return { Noise(0.f, 0.18f, 0.45f, 0.5f, 1.5f), Tone(0.f, 0.20f, 600.f, 150.f, EWave::Square, 0.25f, 1.f) };

		case EKakurenboSfx::CountdownBeep:
			return { Tone(0.f, 0.12f, 880.f, 0.f, EWave::Sine, 0.40f, 0.5f) };

		case EKakurenboSfx::RoundStart:
			return { Tone(0.f, 0.40f, 1320.f, 0.f, EWave::Sine, 0.35f, 1.f), Tone(0.f, 0.50f, 660.f, 0.f, EWave::Sine, 0.25f, 1.2f) };

		case EKakurenboSfx::TimeTick:
			return { Tone(0.f, 0.035f, 1600.f, 0.f, EWave::Sine, 0.30f, 2.f) };

		case EKakurenboSfx::Buy:
			// 「チャリン」
			return { Tone(0.00f, 0.05f, G6, 0.f, EWave::Square, 0.20f, 0.5f), Tone(0.05f, 0.14f, C7, 0.f, EWave::Square, 0.20f, 1.2f) };

		case EKakurenboSfx::BuyFail:
			return { Tone(0.f, 0.18f, 160.f, 0.f, EWave::Saw, 0.25f, 0.5f) };

		case EKakurenboSfx::Place:
			return { Tone(0.f, 0.08f, 220.f, 140.f, EWave::Sine, 0.50f, 2.f), Noise(0.f, 0.03f, 0.20f, 0.3f) };

		case EKakurenboSfx::PickUp:
			return { Tone(0.f, 0.09f, 500.f, 950.f, EWave::Sine, 0.30f, 1.f) };

		case EKakurenboSfx::Dash:
			// 「ダッ」と地面を蹴る音 ＋ 風を切る「シュッ」
			return {
				Tone(0.00f, 0.12f, 160.f, 60.f, EWave::Sine, 0.60f, 2.f),
				Noise(0.00f, 0.06f, 0.45f, 0.3f, 2.f),
				Noise(0.03f, 0.30f, 0.30f, 0.9f, 1.2f),
			};

		case EKakurenboSfx::Prestige:
			// 不思議な上がっていく響き
			return {
				Vibrato(Tone(0.00f, 0.80f, 330.f, 1320.f, EWave::Triangle, 0.30f, 0.6f), 0.02f, 7.f),
				Tone(0.20f, 0.60f, E6, 0.f, EWave::Sine, 0.18f, 1.f),
				Tone(0.35f, 0.60f, G6, 0.f, EWave::Sine, 0.18f, 1.f),
				Tone(0.50f, 0.70f, C7, 0.f, EWave::Sine, 0.18f, 1.f),
			};

		default:
			return {};
		}
	}

	TArray<int16> Render(const TArray<FNote>& Notes, float PitchScale, int32 SampleRate, uint32 NoiseSeed)
	{
		TArray<int16> Out;
		if (Notes.Num() == 0 || SampleRate <= 0)
		{
			return Out;
		}

		float TotalSeconds = 0.f;
		for (const FNote& N : Notes)
		{
			TotalSeconds = FMath::Max(TotalSeconds, N.Start + N.Duration);
		}
		const int32 NumSamples = FMath::CeilToInt(TotalSeconds * SampleRate) + SampleRate / 100; // 最後に 10ms の無音
		TArray<float> Mix;
		Mix.SetNumZeroed(NumSamples);

		uint32 Seed = NoiseSeed;
		for (const FNote& N : Notes)
		{
			const int32 First = FMath::RoundToInt(N.Start * SampleRate);
			const int32 Length = FMath::RoundToInt(N.Duration * SampleRate);
			const float F0 = N.Freq * PitchScale;
			const float F1 = (N.FreqEnd > 0.f ? N.FreqEnd : N.Freq) * PitchScale;
			const float Attack = FMath::Clamp(N.Attack, 0.0005f, N.Duration * 0.5f);
			constexpr float Release = 0.005f; // 最後の 5ms で 0 にしてプツッという音を防ぐ

			double Phase = 0.0;
			float Filtered = 0.f;
			for (int32 i = 0; i < Length && First + i < NumSamples; ++i)
			{
				const float T = static_cast<float>(i) / SampleRate;
				const float U = T / N.Duration;

				// 音量の変化（立ち上がり → 減衰 → 最後に 0 へ）
				float Env = 1.f;
				if (T < Attack)
				{
					Env = T / Attack;
				}
				else if (N.DecayPower > 0.f)
				{
					Env = FMath::Pow(FMath::Max(0.f, 1.f - (T - Attack) / (N.Duration - Attack)), N.DecayPower);
				}
				Env *= FMath::Clamp((N.Duration - T) / Release, 0.f, 1.f);

				float Sample = 0.f;
				if (N.Wave == EWave::Noise)
				{
					// 決まった式で作る乱数（毎回同じ音になる）を、ローパスでこもらせる
					Seed = Seed * 1664525u + 1013904223u;
					const float White = static_cast<float>((Seed >> 8) & 0xFFFF) / 32768.f - 1.f;
					Filtered += FMath::Clamp(N.LowPass, 0.01f, 1.f) * (White - Filtered);
					Sample = Filtered;
				}
				else
				{
					// 高さは指数的に変える（耳には直線的に上がる・下がるように聞こえる）
					float Freq = (F0 > 0.f && F1 > 0.f) ? F0 * FMath::Pow(F1 / F0, U) : F0;
					if (N.VibratoDepth > 0.f)
					{
						Freq *= 1.f + N.VibratoDepth * FMath::Sin(2.f * UE_PI * N.VibratoRate * T);
					}
					Phase += 2.0 * UE_DOUBLE_PI * Freq / SampleRate;
					const float S = FMath::Sin(static_cast<float>(Phase));
					switch (N.Wave)
					{
					case EWave::Square:   Sample = FMath::Tanh(4.f * S); break; // 角を少し丸めた矩形波
					case EWave::Triangle: Sample = (2.f / UE_PI) * FMath::Asin(S); break;
					case EWave::Saw:      Sample = 2.f * static_cast<float>(FMath::Frac(Phase / (2.0 * UE_DOUBLE_PI))) - 1.f; break;
					default:              Sample = S; break;
					}
				}
				Mix[First + i] += Sample * Env * N.Volume;
			}
		}

		// 重なって大きくなりすぎた所は柔らかく抑えてから 16bit にする
		Out.SetNumUninitialized(NumSamples);
		for (int32 i = 0; i < NumSamples; ++i)
		{
			const float Y = FMath::Tanh(Mix[i]);
			Out[i] = static_cast<int16>(FMath::Clamp(FMath::RoundToInt(Y * 32767.f * 0.9f), -32767, 32767));
		}
		return Out;
	}

	TArray<int16> RenderSfx(EKakurenboSfx Sfx, float PitchScale, int32 SampleRate)
	{
		return Render(GetRecipe(Sfx), PitchScale, SampleRate, 12345u + static_cast<uint32>(Sfx) * 7919u);
	}

	float GetDuration(EKakurenboSfx Sfx)
	{
		float Seconds = 0.f;
		for (const FNote& N : GetRecipe(Sfx))
		{
			Seconds = FMath::Max(Seconds, N.Start + N.Duration);
		}
		return Seconds;
	}
}
