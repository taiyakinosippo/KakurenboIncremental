# 音を作る AI（Stable Audio Open 1.0）で効果音・BGM の候補の WAV を作る。
#   ふつうは Tools/SoundGen.ps1 から動かす（.venv を作ってから、このスクリプトに引数を渡す）
#   powershell -ExecutionPolicy Bypass -File Tools\SoundGen.ps1                     # Sounds.csv の全部（1 つにつき 3 候補）
#   powershell -ExecutionPolicy Bypass -File Tools\SoundGen.ps1 Mash OniStep        # 名前を選んで作る
#   powershell -ExecutionPolicy Bypass -File Tools\SoundGen.ps1 --variations 5 Meow # 候補の数
#   powershell -ExecutionPolicy Bypass -File Tools\SoundGen.ps1 --prompt "glass breaking" --seconds 2 --name Test
#   powershell -ExecutionPolicy Bypass -File Tools\SoundGen.ps1 --check             # ログインとモデルの使用許可だけ確かめる
# できた WAV は KakurenboIncremental/SourceArt/SoundGen/<名前>/<名前>_s<シード>.wav（Git には入れない）。
# 同じフォルダの index.html をブラウザで開くと全部聞き比べられる。気に入った音のシード値を覚えておけば同じ音を作り直せる。
# モデルは Hugging Face で規約に同意し、`hf auth login`（または huggingface-cli login）でログインしてから使える。
import argparse
import csv
import html
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CSV_PATH = Path(__file__).with_name("Sounds.csv")
OUT_DIR = ROOT / "KakurenboIncremental" / "SourceArt" / "SoundGen"
MODEL_ID = "stabilityai/stable-audio-open-1.0"
NEGATIVE = "Low quality, distorted, noise"  # 入ってほしくない音

# Windows はシンボリックリンクを作れないことが多い（その警告は出さない。キャッシュは少し大きくなるだけ）
os.environ.setdefault("HF_HUB_DISABLE_SYMLINKS_WARNING", "1")


def choose_model_cache():
    """モデル（約 10GB）を置く場所。ふだんは ~/.cache/huggingface。そこのドライブの空きが 12GB より少なければ、
    いちばん空いているドライブの KakurenboHFCache（ログインのトークンは ~/.cache/huggingface のまま）"""
    import shutil
    import string

    if "HF_HUB_CACHE" in os.environ or "HF_HOME" in os.environ:
        return
    need = 12 * 1024 ** 3
    default = Path.home() / ".cache" / "huggingface" / "hub"
    if (default / "models--stabilityai--stable-audio-open-1.0" / "blobs").exists() and \
            any(p.stat().st_size > 1024 ** 3 for p in (default / "models--stabilityai--stable-audio-open-1.0" / "blobs").iterdir()):
        return  # もうダウンロードしてある
    if shutil.disk_usage(Path.home().anchor).free >= need:
        return
    best, best_free = None, 0
    for letter in string.ascii_uppercase:
        root = f"{letter}:\\"
        try:
            free = shutil.disk_usage(root).free
        except OSError:
            continue
        if free > best_free:
            best, best_free = root, free
    if best and best_free >= need:
        os.environ["HF_HUB_CACHE"] = os.path.join(best, "KakurenboHFCache")


choose_model_cache()

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def log(msg):
    print(f"[SoundGen] {msg}", flush=True)


def read_table():
    with open(CSV_PATH, encoding="utf-8") as f:
        return [row for row in csv.DictReader(f) if row.get("Name")]


def check_access():
    """ログインしていて、モデルの規約に同意済みかを確かめる。だめなら何をすればいいかを出して False"""
    from huggingface_hub import get_token, hf_hub_download
    from huggingface_hub.errors import GatedRepoError, RepositoryNotFoundError

    if not get_token():
        log("Hugging Face にログインしていません。ターミナルで次を実行してトークンを入れてください:")
        log("  Tools\\SoundGen\\.venv\\Scripts\\hf.exe auth login")
        return False
    try:
        hf_hub_download(MODEL_ID, "model_index.json")
    except GatedRepoError:
        log(f"モデルの規約にまだ同意していません。https://huggingface.co/{MODEL_ID} を開いて同意してください")
        return False
    except RepositoryNotFoundError:
        log("モデルが見つかりません（トークンが正しいか確かめてください）")
        return False
    log("ログインとモデルの使用許可 OK")
    return True


def load_pipeline():
    import torch
    from diffusers import StableAudioPipeline

    if not torch.cuda.is_available():
        log("GPU（CUDA）が使えません。CPU で動かすのでとても遅くなります")
        return StableAudioPipeline.from_pretrained(MODEL_ID, dtype=torch.float32), "cpu"
    cache = os.environ.get("HF_HUB_CACHE", "~/.cache/huggingface/hub")
    log(f"モデルを読み込み中（初回は約 10GB を {cache} にダウンロード）… GPU: {torch.cuda.get_device_name(0)}")
    pipe = StableAudioPipeline.from_pretrained(MODEL_ID, dtype=torch.float16).to("cuda")
    return pipe, "cuda"


def clean_up(audio, rate, is_music):
    """前後の無音を切り、終わりを短くフェードアウトして、いちばん大きいところを -1dB にそろえる。audio は [ch, samples]"""
    import numpy as np

    audio = np.nan_to_num(audio.astype(np.float32))
    peak = float(np.abs(audio).max())
    if peak <= 1e-6:
        return audio
    if not is_music:
        # -45dB より小さいところを無音とみなす（効果音は鳴った瞬間から始まり、鳴り終わったら終わる）
        loud = np.where(np.abs(audio).max(axis=0) > peak * 10 ** (-45 / 20))[0]
        start = max(int(loud[0]) - int(0.005 * rate), 0)
        end = min(int(loud[-1]) + int(0.05 * rate), audio.shape[1])
        audio = audio[:, start:end]
    fade = min(int(0.03 * rate), audio.shape[1])
    if fade > 0:
        audio[:, -fade:] *= np.linspace(1.0, 0.0, fade, dtype=np.float32)
    return audio * (10 ** (-1 / 20) / peak)


def generate(pipe, device, name, prompt, seconds, seeds, steps, overwrite):
    import math
    import time

    import soundfile as sf
    import torch

    rate = pipe.vae.sampling_rate
    folder = OUT_DIR / name
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "prompt.txt").write_text(f"{prompt}\nseconds={seconds} steps={steps}\n", encoding="utf-8")
    for seed in seeds:
        path = folder / f"{name}_s{seed}.wav"
        if path.exists() and not overwrite:
            log(f"{path.relative_to(ROOT)} はもうあるので飛ばす")
            continue
        started = time.time()
        generator = torch.Generator(device).manual_seed(seed)
        # モデルはいつも 47 秒ぶんを作る。音にする（VAE）ときに全部を音にすると GPU のメモリをたくさん使うので、
        # 欲しい長さ（＋少し）だけ切り出して音にする
        latents = pipe(prompt, negative_prompt=NEGATIVE, num_inference_steps=steps, audio_end_in_s=float(seconds),
                       num_waveforms_per_prompt=1, generator=generator, output_type="latent").audios
        frames = min(latents.shape[-1], math.ceil(seconds * rate / pipe.vae.hop_length) + 4)
        with torch.no_grad():
            wave = pipe.vae.decode(latents[:, :, :frames]).sample[:, :, : int(seconds * rate)]
        audio = clean_up(wave[0].float().cpu().numpy(), rate, name.startswith("Music_"))
        del latents, wave
        if device == "cuda":
            torch.cuda.empty_cache()  # ほかのアプリ（UE など）と GPU のメモリを分け合うので、使い終わったら返す
        sf.write(str(path), audio.T, rate, subtype="PCM_16")
        log(f"{path.relative_to(ROOT)}（{audio.shape[1] / rate:.2f} 秒・{time.time() - started:.0f} 秒かかった）")


def write_index():
    """SourceArt/SoundGen/index.html：音ごとに、ゲームのどこで鳴るか・今の音・候補を並べて聞き比べるページ"""
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    current = OUT_DIR / "_Current"  # 今の音（Tools/SoundGen.ps1 -Current が書き出す）
    table = read_table()
    names = [row["Name"] for row in table]
    # 表に無い名前（--prompt で作ったもの）も後ろに並べる
    names += sorted(p.name for p in OUT_DIR.iterdir() if p.is_dir() and p.name not in names and not p.name.startswith("_"))
    descs = {row["Name"]: row for row in table}

    def player(label, src):
        return f'<div class="clip"><span>{html.escape(label)}</span><audio controls preload="none" src="{src}"></audio></div>'

    rows = []
    for name in names:
        row = descs.get(name, {})
        folder = OUT_DIR / name
        prompt_file = folder / "prompt.txt"
        prompt = row.get("Prompt") or (prompt_file.read_text(encoding="utf-8").splitlines()[0] if prompt_file.exists() else "")
        now = current / f"{name}.wav"
        clips = player("今の音", f"_Current/{name}.wav") if now.exists() else '<div class="clip none">今の音: まだ書き出していない</div>'
        wavs = sorted(folder.glob("*.wav")) if folder.exists() else []
        clips += "".join(player(w.stem, f"{name}/{w.name}") for w in wavs) or '<div class="clip none">候補: まだ作っていない</div>'
        rows.append(f"<section><h2>{html.escape(name)} <small>{html.escape(row.get('Desc', ''))}</small></h2>"
                    f"<p>{html.escape(prompt)}</p><div class='clips'>{clips}</div></section>")
    page = ("<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'><title>SoundGen</title><style>"
            "body{font-family:'Yu Gothic UI',sans-serif;margin:16px;background:#1b1b22;color:#eee}"
            "section{border-top:1px solid #333;padding:8px 0}h2{margin:4px 0 2px;font-size:18px}small{color:#e8c66a;font-size:14px;margin-left:8px}"
            "p{margin:0 0 6px;color:#999;font-size:12px}.clips{display:flex;flex-wrap:wrap;gap:6px 12px}"
            ".clip{display:flex;flex-direction:column;font-size:12px;color:#ccc}.none{color:#666;justify-content:center}"
            "audio{height:32px;width:240px}</style>"
            "<h1>かせぐんぼの音の候補</h1><p>「今の音」はプログラムで作っている、いま鳴っている音。ほかは Stable Audio Open の候補（名前の s の後ろがシード値）。</p>"
            + "".join(rows))
    (OUT_DIR / "index.html").write_text(page, encoding="utf-8")
    log(f"一覧: {OUT_DIR / 'index.html'}")


def main():
    parser = argparse.ArgumentParser(description="Stable Audio Open で効果音・BGM の候補を作る")
    parser.add_argument("names", nargs="*", help="Sounds.csv の Name（省略で全部）")
    parser.add_argument("--variations", type=int, default=3, help="1 つの音につき作る候補の数")
    parser.add_argument("--seed", type=int, default=1, help="最初のシード値（候補ごとに 1 ずつ増える）")
    parser.add_argument("--steps", type=int, default=100, help="生成のステップ数（多いほどきれいで遅い）")
    parser.add_argument("--prompt", help="表を使わずにこの説明文で作る（--name・--seconds と一緒に）")
    parser.add_argument("--name", default="Custom")
    parser.add_argument("--seconds", type=float, default=2.0)
    parser.add_argument("--check", action="store_true", help="ログインとモデルの使用許可だけ確かめる")
    parser.add_argument("--overwrite", action="store_true", help="同じシード値の WAV があっても作り直す（ふだんは飛ばすので、止まっても続きから）")
    parser.add_argument("--index-only", action="store_true", help="一覧（index.html）だけ作り直す")
    args = parser.parse_args()

    if args.index_only:
        write_index()
        return 0

    if args.prompt:
        jobs = [(args.name, args.prompt, args.seconds)]
    else:
        table = read_table()
        known = {row["Name"] for row in table}
        unknown = [n for n in args.names if n not in known]
        if unknown:
            log(f"Sounds.csv に無い名前: {', '.join(unknown)}")
            return 1
        jobs = [(row["Name"], row["Prompt"], float(row["Seconds"])) for row in table if not args.names or row["Name"] in args.names]

    if not check_access():
        return 1
    if args.check:
        return 0

    pipe, device = load_pipeline()
    seeds = list(range(args.seed, args.seed + max(args.variations, 1)))
    for i, (name, prompt, seconds) in enumerate(jobs, 1):
        log(f"({i}/{len(jobs)}) {name}: {prompt}")
        generate(pipe, device, name, prompt, seconds, seeds, args.steps, args.overwrite)
    write_index()
    return 0


if __name__ == "__main__":
    sys.exit(main())
