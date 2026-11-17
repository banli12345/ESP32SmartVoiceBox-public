"""Generate the SD-card 20px LVGL font with the GB2312 Hanzi set."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from update_chinese20_font import (
    DEFAULT_FONT,
    DEFAULT_OUTPUT,
    DEFAULT_SYMBOLS,
    UNSUPPORTED_CODEPOINTS,
    find_node_tools,
    read_existing_codepoints,
)


ROOT = Path(__file__).resolve().parents[1]
COMMON_SOURCE = ROOT / "font_assets" / "common_3500.txt"


def gb2312_characters() -> set[str]:
    characters: set[str] = set()
    for lead in range(0xA1, 0xF8):
        for trail in range(0xA1, 0xFF):
            try:
                char = bytes((lead, trail)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            if len(char) == 1 and "\u4e00" <= char <= "\u9fff":
                characters.add(char)
    return characters


def main() -> int:
    font_path = ROOT / "simhei.ttf"
    if not font_path.is_file() or not DEFAULT_OUTPUT.is_file() or not COMMON_SOURCE.is_file():
        raise FileNotFoundError("font, current output, or common character source is missing")

    existing = read_existing_codepoints(DEFAULT_OUTPUT)
    common = COMMON_SOURCE.read_text(encoding="utf-8")
    points = set(existing)
    points.update(ord(char) for char in common if char >= "\x20")
    points.update(ord(char) for char in gb2312_characters())
    points.update(range(0x20, 0x7F))
    points.update(ord(char) for char in DEFAULT_SYMBOLS)
    points.difference_update(UNSUPPORTED_CODEPOINTS)

    node, _npx_cli = find_node_tools()
    npx_cli = ROOT / ".tools" / "node-v24.21.0-win-x64" / "node_modules" / "npm" / "bin" / "npx-cli.js"
    symbol_file = Path(tempfile.gettempdir()) / "lvgl_gb2312_symbols.txt"
    symbol_file.write_bytes("".join(chr(cp) for cp in sorted(points)).encode("utf-8"))
    output = DEFAULT_OUTPUT.with_suffix(DEFAULT_OUTPUT.suffix + ".gb2312")
    # Pass the UTF-8 symbol string as a normal argument. The converter's
    # @file syntax is treated as a literal string by older releases.
    symbols = "".join(chr(cp) for cp in sorted(points))
    command = [
        str(node), str(npx_cli), "--yes", "lv_font_conv",
        "--font", str(font_path), "--size", "20", "--bpp", "4",
        "--format", "bin", "--no-kerning", "--output", str(output),
        "--symbols=" + symbols,
    ]
    try:
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                                encoding="utf-8", errors="replace")
    except OSError as exc:
        raise RuntimeError(f"could not start lv_font_conv: {exc}") from exc
    if result.returncode != 0:
        if output.exists():
            output.unlink()
        detail = (result.stderr or result.stdout).strip().splitlines()
        raise RuntimeError(detail[-1] if detail else "lv_font_conv failed")

    generated = read_existing_codepoints(output)
    required_hanzi = {ord(char) for char in gb2312_characters()}
    missing = required_hanzi - generated
    if missing:
        output.unlink()
        sample = "".join(chr(cp) for cp in sorted(missing)[:32])
        raise RuntimeError(f"generated font is missing {len(missing)} GB2312 Hanzi: {sample}")

    output.replace(DEFAULT_OUTPUT)
    han_count = sum(0x3400 <= cp <= 0x9FFF for cp in generated)
    print(f"generated {DEFAULT_OUTPUT}")
    print(f"glyphs={len(generated)} han={han_count} bytes={DEFAULT_OUTPUT.stat().st_size}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"font generation failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
