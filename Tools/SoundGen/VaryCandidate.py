# 気に入った候補に「近い」別の候補を作る（画像 AI の img2img と同じ考え。元の音にノイズを途中まで足して、説明文で途中から作り直す）。
#   Tools\SoundGen\.venv\Scripts\python.exe Tools\SoundGen\VaryCandidate.py --from OniStepTreasure_s34 --seed 41 --strength 0.3
#   --prompt を付けなければ Sounds.csv の説明文。--strength は 0〜1（小さいほど元の音のまま）。
#   2026-10-19 に CLAP で元の音との近さを測った：0.5 では新しく作った候補より遠い（0.43〜0.62）、0.3 で 0.51〜0.87。既定は 0.3
# できた WAV は GenerateSounds.py と同じ場所・名前（<名前>/<名前>_s<シード>.wav）。prompt.txt に「どれを元にしたか」も書く。
# Stable Audio の diffusers の initial_audio_waveforms は元の音にいちばん強いノイズを足すだけで、元の音がほとんど残らないので使わない
import argparse
import math
import sys
import time

import numpy as np
import soundfile as sf

import GenerateSounds as gen


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--from", dest="source", required=True, help="元にする候補（例：OniStepTreasure_s34）")
    parser.add_argument("--name", help="作る音の名前（省略で元の候補の名前）")
    parser.add_argument("--prompt", help="説明文（省略で Sounds.csv）")
    parser.add_argument("--seed", type=int, required=True, help="最初のシード値（候補ごとに 1 ずつ増える）")
    parser.add_argument("--variations", type=int, default=4)
    parser.add_argument("--strength", type=float, default=0.3, help="どれだけ作り直すか（0〜1）")
    parser.add_argument("--steps", type=int, default=100)
    args = parser.parse_args()

    name = args.name or args.source.rsplit("_s", 1)[0]
    row = {r["Name"]: r for r in gen.read_table()}.get(name, {})
    prompt = args.prompt or row.get("Prompt", "")
    seconds = float(row.get("Seconds") or 2.0)
    if not prompt:
        gen.log(f"{name} の説明文がありません（--prompt で渡す）")
        return 1

    import torch

    pipe, device = gen.load_pipeline()
    rate = pipe.vae.sampling_rate
    folder = gen.OUT_DIR / name
    record = folder / "prompt.txt"
    old = record.read_text(encoding="utf-8") if record.exists() else ""
    seeds = list(range(args.seed, args.seed + args.variations))
    record.write_text(old + f"s{seeds[0]}-s{seeds[-1]}: {args.source} を元に（strength {args.strength}）: {prompt}\nseconds={seconds} steps={args.steps}\n", encoding="utf-8")

    # 元の音を VAE の形（2ch・47 秒ぶん）にして、潜在（latent）にする
    audio, src_rate = sf.read(str(folder.parent / args.source.rsplit("_s", 1)[0] / f"{args.source}.wav"), dtype="float32", always_2d=True)
    audio = audio.T
    if src_rate != rate:
        n = int(round(audio.shape[1] * rate / src_rate))
        audio = np.stack([np.interp(np.linspace(0, audio.shape[1] - 1, n), np.arange(audio.shape[1]), ch) for ch in audio])
    if audio.shape[0] == 1:
        audio = np.repeat(audio, 2, axis=0)
    full = int(pipe.transformer.config.sample_size * pipe.vae.hop_length)
    padded = np.zeros((2, full), dtype=np.float32)
    padded[:, : min(full, audio.shape[1])] = audio[:, :full]
    with torch.no_grad():
        clean = pipe.vae.encode(torch.from_numpy(padded)[None].to(device, pipe.vae.dtype)).latent_dist.mean

    # スケジューラーの前半（ノイズの強い所）を飛ばす：sigma の並びの (1 - strength) の所から始める
    scheduler = pipe.scheduler
    original_set_timesteps = scheduler.set_timesteps
    start = {"sigma": 0.0}

    def set_timesteps(num_inference_steps=None, device=None):
        original_set_timesteps(num_inference_steps, device)
        skip = min(int(round(len(scheduler.timesteps) * (1.0 - args.strength))), len(scheduler.timesteps) - 1)
        scheduler.timesteps = scheduler.timesteps[skip:]
        scheduler.sigmas = scheduler.sigmas[skip:]
        start["sigma"] = float(scheduler.sigmas[0])

    scheduler.set_timesteps = set_timesteps
    for seed in seeds:
        path = folder / f"{name}_s{seed}.wav"
        started = time.time()
        generator = torch.Generator(device).manual_seed(seed)
        scheduler.set_timesteps(args.steps, device)  # start["sigma"] を先に知るため（パイプラインの中でもう一度呼ばれる）
        noise = torch.randn(clean.shape, generator=generator, device=device, dtype=clean.dtype)
        # パイプラインは渡した latents に init_noise_sigma を掛けるので、先に割っておく
        latents = (clean + noise * start["sigma"]) / scheduler.init_noise_sigma
        out = pipe(prompt, negative_prompt=gen.NEGATIVE, num_inference_steps=args.steps, audio_end_in_s=seconds,
                   num_waveforms_per_prompt=1, generator=generator, latents=latents, output_type="latent").audios
        frames = min(out.shape[-1], math.ceil(seconds * rate / pipe.vae.hop_length) + 4)
        with torch.no_grad():
            wave = pipe.vae.decode(out[:, :, :frames]).sample[:, :, : int(seconds * rate)]
        result = gen.clean_up(wave[0].float().cpu().numpy(), rate, False)
        sf.write(str(path), result.T, rate, subtype="PCM_16")
        gen.log(f"{path.relative_to(gen.ROOT)}（{args.source} を元に・{result.shape[1] / rate:.2f} 秒・{time.time() - started:.0f} 秒）")
        if device == "cuda":
            torch.cuda.empty_cache()
    return 0


if __name__ == "__main__":
    sys.exit(main())
