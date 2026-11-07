"""バランスを推論する AI：テストプレイ AI の記録を全部読み、ステージごとの特徴と傾向・難しさ・Level カーブ・転生による強化幅を推論して、
新しいファイル（Saved/AgentPlaytest/analysis/balance_<日時>.md・.html）を作る。Claude は使わない（標準ライブラリだけ）。

  python Tools/AgentPlaytest/analyze.py [記録の置き場（既定：KakurenboIncremental/Saved/AgentPlaytest）]
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from agentplay.balance import generate  # noqa: E402

if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "..", "KakurenboIncremental", "Saved", "AgentPlaytest")
    print(generate(os.path.abspath(root)))
