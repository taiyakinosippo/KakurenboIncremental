"""記録のフォルダから報告書（report.md・report.html）を作り直す。

  python make_report.py <記録のフォルダ>
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from agentplay.report import generate  # noqa: E402

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: make_report.py <run folder>")
        sys.exit(1)
    print(generate(sys.argv[1]))
