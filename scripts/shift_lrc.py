"""Shift all LRC timestamps by a fixed number of milliseconds.

Positive values make lyrics appear earlier; negative values make them later.
Example: python scripts/shift_lrc.py sd_card_files/music/qingtian.lrc 800
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

TIME = re.compile(r"\[(\d+):(\d{2})\.(\d{2})\]")

if len(sys.argv) != 3:
    raise SystemExit("用法: python scripts/shift_lrc.py 文件.lrc 偏移毫秒")

path = Path(sys.argv[1])
offset = int(sys.argv[2])
lines = []
for line in path.read_text(encoding="utf-8-sig").splitlines():
    def replace(match: re.Match[str]) -> str:
        total = (int(match.group(1)) * 60_000 +
                 int(match.group(2)) * 1_000 + int(match.group(3)) * 10 - offset)
        total = max(0, total)
        minutes, remainder = divmod(total, 60_000)
        seconds, centis = divmod(remainder, 1_000)
        return f"[{minutes:02d}:{seconds:02d}.{centis // 10:02d}]"
    lines.append(TIME.sub(replace, line))

path.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"已调整 {path}：{offset:+d} ms")
