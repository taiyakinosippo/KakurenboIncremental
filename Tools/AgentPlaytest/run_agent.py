"""テストプレイ AI を 1 つ動かす（ゲームは先に -KakuAgentPort=<port> で起動しておく。ふつうは Tools/AgentPlaytest.ps1 が両方起動する）。

  python run_agent.py --persona stay --port 7701 --dir <記録のフォルダ> --minutes 60 --speed 2

終わり方（戻り値）：0 = 時間になった（ゲームに quit を送る） / 2 = ゲームとつながらない・切れた・止まった（起動し直してもらう）
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from agentplay.agent import PERSONAS, Agent  # noqa: E402
from agentplay.brain import Brain  # noqa: E402
from agentplay.link import Link  # noqa: E402
from agentplay.recorder import Recorder  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--persona", choices=sorted(PERSONAS), required=True)
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--dir", required=True, help="記録のフォルダ（rounds.jsonl など）")
    ap.add_argument("--brain", default=None, help="学習の保存先（既定：<dir>/../brains/brain_<persona>.json。続けて学ぶ）")
    ap.add_argument("--deadline", type=float, default=None, help="終わる時刻（UNIX 秒）")
    ap.add_argument("--minutes", type=float, default=60.0)
    ap.add_argument("--speed", type=float, default=1.0)
    ap.add_argument("--connect-timeout", type=float, default=300.0)
    args = ap.parse_args()

    deadline = args.deadline or (time.time() + args.minutes * 60.0)
    brain_path = args.brain or os.path.join(os.path.dirname(os.path.abspath(args.dir)), "brains", f"brain_{args.persona}.json")
    os.makedirs(os.path.dirname(brain_path), exist_ok=True)
    rec = Recorder(args.dir, args.persona)
    log_path = os.path.join(rec.dir, "agent.log")

    def log(msg):
        line = time.strftime("%H:%M:%S ") + msg
        print(line, flush=True)
        with open(log_path, "a", encoding="utf-8") as f:
            f.write(line + "\n")

    brain = Brain(brain_path)
    link = Link(args.port)
    log(f"connect 127.0.0.1:{args.port} persona={args.persona} brain={brain_path} episodes={brain.episodes}")
    if not link.connect(args.connect_timeout):
        log("could not connect")
        rec.event("session", {"result": "no-connection"})
        return 2
    rec.event("session", {"result": "connected", "port": args.port, "episodes": brain.episodes})
    agent = Agent(args.persona, link, brain, rec, speed=args.speed, log=log)
    result = "error"
    try:
        result = agent.run(deadline)
    except KeyboardInterrupt:
        result = "interrupted"
    finally:
        brain.save()
        rec.event("session", {"result": result, "episodes": brain.episodes})
        log(f"end: {result} (episodes {brain.episodes})")
    if result in ("deadline", "interrupted"):
        link.send("quit")
        time.sleep(0.5)
        link.close()
        return 0
    link.close()
    return 2


if __name__ == "__main__":
    sys.exit(main())
