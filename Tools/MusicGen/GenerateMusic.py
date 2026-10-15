# BGM を作る AI（ACE-Step 1.5。MIT・作った曲は商用で使える）で BGM の候補の WAV を作る。
#   ふつうは Tools/MusicGen.ps1 から動かす（ACE-Step の環境を作ってから、このスクリプトに引数を渡す）
#   powershell -ExecutionPolicy Bypass -File Tools\MusicGen.ps1                        # Music.csv の全部（1 曲につき 3 候補）
#   powershell -ExecutionPolicy Bypass -File Tools\MusicGen.ps1 Music_Hide             # 名前を選んで作る
#   powershell -ExecutionPolicy Bypass -File Tools\MusicGen.ps1 --variations 5 Music_Calm
#   powershell -ExecutionPolicy Bypass -File Tools\MusicGen.ps1 --prompt "Instrumental, ..." --seconds 60 --bpm 90 --name Test
#   powershell -ExecutionPolicy Bypass -File Tools\MusicGen.ps1 --download             # モデルのダウンロードだけ（-Setup が呼ぶ）
# できた WAV は KakurenboIncremental/SourceArt/MusicGen/<名前>/<名前>_s<シード>.wav。同じフォルダの index.html で聞き比べられる。
# モデル（約 10GB）は Tools/MusicGen/ACE-Step-1.5/checkpoints に自動でダウンロードされる（Hugging Face のログインは要らない）。
import argparse
import csv
import html
import os
import shutil
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ACE_DIR = Path(__file__).resolve().parent / "ACE-Step-1.5"
CHECKPOINTS = ACE_DIR / "checkpoints"
CSV_PATH = Path(__file__).with_name("Music.csv")
OUT_DIR = ROOT / "KakurenboIncremental" / "SourceArt" / "MusicGen"
DIT_MODEL = "acestep-v15-turbo"  # 2B の速い版（VRAM 8GB くらいから）

os.environ.setdefault("HF_HUB_DISABLE_SYMLINKS_WARNING", "1")
sys.path.insert(0, str(ACE_DIR))

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def log(msg):
    print(f"[MusicGen] {msg}", flush=True)


def read_table():
    with open(CSV_PATH, encoding="utf-8") as f:
        return [row for row in csv.DictReader(f) if row.get("Name")]


def download(lm_model):
    """曲を作るモデル（DiT・VAE・文字の理解）と、曲の設計をする言語モデル（LM）をダウンロードする（もうあれば何もしない）"""
    from acestep.model_downloader import ensure_dit_model, ensure_lm_model, ensure_main_model

    for ok, msg in (ensure_main_model(CHECKPOINTS), ensure_dit_model(DIT_MODEL, CHECKPOINTS),
                    ensure_lm_model(lm_model, CHECKPOINTS) if lm_model != "none" else (True, "LM なし")):
        log(msg)
        if not ok:
            return False
    return True


def load(lm_model, offload):
    import torch
    from acestep.handler import AceStepHandler
    from acestep.llm_inference import LLMHandler

    if torch.cuda.is_available():
        log(f"GPU: {torch.cuda.get_device_name(0)}")
    else:
        log("GPU（CUDA）が使えません。CPU で動かすのでとても遅くなります")
    dit = AceStepHandler()
    msg, ok = dit.initialize_service(project_root=str(ACE_DIR), config_path=DIT_MODEL, device="auto",
                                     offload_to_cpu=offload)
    if not ok:
        raise RuntimeError(f"曲のモデルを読み込めませんでした: {msg}")
    llm = None
    if lm_model != "none":
        llm = LLMHandler()
        # Windows では vllm（速い方）が動かないことが多いので PyTorch（pt）で動かす
        msg, ok = llm.initialize(checkpoint_dir=str(CHECKPOINTS), lm_model_path=lm_model, backend="pt",
                                 device="auto", offload_to_cpu=offload)
        if not ok:
            raise RuntimeError(f"言語モデルを読み込めませんでした: {msg}")
    return dit, llm


def to_pcm16(src, dst):
    """ACE-Step は 32bit の小数で書くので、16bit にして書き直す（Git LFS に入れるので大きさを半分に）"""
    import soundfile as sf

    audio, rate = sf.read(str(src), dtype="float32")
    sf.write(str(dst), audio, rate, subtype="PCM_16")
    if Path(src) != Path(dst):
        Path(src).unlink(missing_ok=True)


def generate(dit, llm, name, prompt, seconds, bpm, key, seeds, overwrite):
    from acestep.inference import GenerationConfig, GenerationParams, generate_music

    folder = OUT_DIR / name
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "prompt.txt").write_text(f"{prompt}\nseconds={seconds} bpm={bpm} key={key} model={DIT_MODEL}\n", encoding="utf-8")
    for seed in seeds:
        path = folder / f"{name}_s{seed}.wav"
        if path.exists() and not overwrite:
            log(f"{path.relative_to(ROOT)} はもうあるので飛ばす")
            continue
        started = time.time()
        params = GenerationParams(caption=prompt, lyrics="[Instrumental]", instrumental=True, duration=float(seconds),
                                  bpm=bpm, keyscale=key, seed=seed, shift=3.0,  # shift 3 はターボ版のおすすめ
                                  thinking=llm is not None)
        config = GenerationConfig(batch_size=1, use_random_seed=False, seeds=[seed], audio_format="wav")
        tmp = folder / "_tmp"
        result = generate_music(dit, llm, params, config, save_dir=str(tmp))
        if not result.success or not result.audios:
            log(f"{name} s{seed} を作れませんでした: {getattr(result, 'error', None) or result.status_message}")
            continue
        to_pcm16(result.audios[0]["path"], path)
        shutil.rmtree(tmp, ignore_errors=True)
        try:
            import torch
            torch.cuda.empty_cache()  # ほかのアプリ（UE など）と GPU のメモリを分け合うので、使い終わったら返す
        except Exception:
            pass
        log(f"{path.relative_to(ROOT)}（{time.time() - started:.0f} 秒かかった）")


def write_index():
    """SourceArt/MusicGen/index.html：曲ごとに今の BGM（SoundGen の _Current）と候補を並べて聞き比べるページ"""
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    current = ROOT / "KakurenboIncremental" / "SourceArt" / "SoundGen" / "_Current"
    table = read_table()
    names = [row["Name"] for row in table]
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
        clips = player("今の曲", f"../SoundGen/_Current/{name}.wav") if now.exists() else ""
        wavs = sorted(folder.glob("*.wav")) if folder.exists() else []
        clips += "".join(player(w.stem, f"{name}/{w.name}") for w in wavs) or '<div class="clip none">候補: まだ作っていない</div>'
        rows.append(f"<section><h2>{html.escape(name)} <small>{html.escape(row.get('Desc', ''))}</small></h2>"
                    f"<p>{html.escape(prompt)}</p><div class='clips'>{clips}</div></section>")
    page = ("<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'><title>MusicGen</title><style>"
            "body{font-family:'Yu Gothic UI',sans-serif;margin:16px;background:#1b1b22;color:#eee}"
            "section{border-top:1px solid #333;padding:8px 0}h2{margin:4px 0 2px;font-size:18px}small{color:#e8c66a;font-size:14px;margin-left:8px}"
            "p{margin:0 0 6px;color:#999;font-size:12px}.clips{display:flex;flex-wrap:wrap;gap:6px 12px}"
            ".clip{display:flex;flex-direction:column;font-size:12px;color:#ccc}.none{color:#666;justify-content:center}"
            "audio{height:32px;width:300px}</style>"
            "<h1>かせぐんぼの BGM の候補</h1><p>「今の曲」はプログラムで作っている、いま鳴っている BGM。ほかは ACE-Step 1.5 の候補（名前の s の後ろがシード値）。</p>"
            + "".join(rows))
    (OUT_DIR / "index.html").write_text(page, encoding="utf-8")
    log(f"一覧: {OUT_DIR / 'index.html'}")


def main():
    parser = argparse.ArgumentParser(description="ACE-Step 1.5 で BGM の候補を作る")
    parser.add_argument("names", nargs="*", help="Music.csv の Name（省略で全部）")
    parser.add_argument("--variations", type=int, default=3, help="1 曲につき作る候補の数")
    parser.add_argument("--seed", type=int, default=1, help="最初のシード値（候補ごとに 1 ずつ増える）")
    parser.add_argument("--prompt", help="表を使わずにこの説明文で作る（--name・--seconds・--bpm・--key と一緒に）")
    parser.add_argument("--name", default="Custom")
    parser.add_argument("--seconds", type=float, default=60.0)
    parser.add_argument("--bpm", type=int, default=None)
    parser.add_argument("--key", default="")
    parser.add_argument("--lm", default="acestep-5Hz-lm-1.7B",
                        help="曲の設計をする言語モデル（acestep-5Hz-lm-0.6B / 1.7B / 4B / none。大きいほど良く、VRAM を使う）")
    parser.add_argument("--offload", action="store_true", help="使っていないモデルを CPU に置く（VRAM が足りないとき。遅くなる）")
    parser.add_argument("--download", action="store_true", help="モデルのダウンロードだけ")
    parser.add_argument("--overwrite", action="store_true", help="同じシード値の WAV があっても作り直す")
    parser.add_argument("--index-only", action="store_true", help="一覧（index.html）だけ作り直す")
    args = parser.parse_args()

    if args.index_only:
        for wav in OUT_DIR.glob("*/*.wav"):
            import soundfile as sf
            if sf.info(str(wav)).subtype != "PCM_16":
                to_pcm16(wav, wav)
        write_index()
        return 0
    if not download(args.lm):
        return 1
    if args.download:
        return 0

    if args.prompt:
        jobs = [(args.name, args.prompt, args.seconds, args.bpm, args.key)]
    else:
        table = read_table()
        unknown = [n for n in args.names if n not in {row["Name"] for row in table}]
        if unknown:
            log(f"Music.csv に無い名前: {', '.join(unknown)}")
            return 1
        jobs = [(row["Name"], row["Prompt"], float(row["Seconds"]), int(row["BPM"]) if row.get("BPM") else None, row.get("Key", ""))
                for row in table if not args.names or row["Name"] in args.names]

    dit, llm = load(args.lm, args.offload)
    seeds = list(range(args.seed, args.seed + max(args.variations, 1)))
    for i, (name, prompt, seconds, bpm, key) in enumerate(jobs, 1):
        log(f"({i}/{len(jobs)}) {name}: {prompt}")
        generate(dit, llm, name, prompt, seconds, bpm, key, seeds, args.overwrite)
    write_index()
    return 0


if __name__ == "__main__":
    sys.exit(main())
