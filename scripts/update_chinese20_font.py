"""Add glyphs to the SD-card LVGL 20px Chinese font.

Examples:
    python scripts/update_chinese20_font.py "照片到了"
    python scripts/update_chinese20_font.py

The second form prompts for the characters to add. Existing glyphs are read
from the current chinese_20.bin, so updating the font does not discard the
characters already used by the UI.
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FONT = ROOT / "simhei.ttf"
DEFAULT_OUTPUT = ROOT / "sd_card_files" / "fonts" / "chinese_20.bin"
NODE = ROOT / ".tools" / "node-v24.21.0-win-x64" / "node.exe"
NPX_CLI = ROOT / ".tools" / "node-v24.21.0-win-x64" / "node_modules" / "npm" / "bin" / "npx-cli.js"
# LVGL 8's binary cmap stores sparse offsets in uint16. Keep unusually distant
# symbols out of the font, but retain requested CJK punctuation U+3002.
UNSUPPORTED_CODEPOINTS = {0x00B0}

DEFAULT_SYMBOLS = "，。：；‘’！@#￥%………&*（）“”？｛｝{}"


def load_simplified_character_source() -> str:
    candidates = [
        ROOT / "font_assets" / "common_3500.txt",
        ROOT / "font_assets" / "common_1000.txt",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.read_text(encoding="utf-8")
    raise FileNotFoundError("font_assets/common_3500.txt not found")


def read_existing_codepoints(font_path: Path) -> set[int]:
    """Extract Unicode codepoints from an LVGL v8 binary font."""
    data = font_path.read_bytes()
    if len(data) < 16 or data[4:8] != b"head":
        raise ValueError(f"not an LVGL binary font: {font_path}")

    head_size = struct.unpack_from("<I", data, 0)[0]
    cmap_size = struct.unpack_from("<I", data, head_size)[0]
    table_count = struct.unpack_from("<I", data, head_size + 8)[0]
    table_pos = head_size + 12
    table_size = struct.calcsize("<IIHHHBB")
    tables = [struct.unpack_from("<IIHHHBB", data, table_pos + i * table_size)
              for i in range(table_count)]

    codepoints: set[int] = set()
    for table_index, table in enumerate(tables):
        data_offset, range_start, range_length, _glyph_start, count, fmt, _ = table
        if fmt == 0:  # format 0: compact cmap stores every codepoint in range
            codepoints.update(range(range_start, range_start + range_length))
        elif fmt == 2:  # format 0 tiny: every codepoint in the range is present
            codepoints.update(range(range_start, range_start + range_length))
        elif fmt == 3:  # sparse tiny: the table stores uint16 offsets
            list_pos = head_size + data_offset
            offsets = struct.unpack_from("<" + "H" * count, data, list_pos)
            codepoints.update(range_start + offset for offset in offsets)
        else:
            raise ValueError(f"unsupported cmap format {fmt} in table {table_index}")
    return codepoints


def find_node_tools() -> tuple[Path, Path]:
    if not NODE.is_file() or not NPX_CLI.is_file():
        raise FileNotFoundError(
            "Bundled Node.js/lv_font_conv was not found. Run this script from the project directory."
        )
    return NODE, NPX_CLI


def main() -> int:
    parser = argparse.ArgumentParser(description="Add Chinese glyphs to chinese_20.bin")
    parser.add_argument("text", nargs="?", help="characters to add; omitted means interactive prompt")
    parser.add_argument("--text-file", type=Path,
                        help="UTF-8 text file whose contents should be added")
    parser.add_argument("--font", type=Path, default=DEFAULT_FONT,
                        help="TTF source font (default: simhei.ttf)")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT,
                        help="LVGL font to replace (default: sd_card_files/fonts/chinese_20.bin)")
    parser.add_argument("--target-count", type=int, default=3500,
                        help="minimum total number of glyphs")
    args = parser.parse_args()

    if args.text is not None and args.text_file is not None:
        parser.error("text and --text-file cannot be used together")
    text = args.text
    if args.text_file is not None:
        try:
            text = args.text_file.read_text(encoding="utf-8")
        except OSError as exc:
            print(f"读取字符文件失败：{exc}", file=sys.stderr)
            return 1
    if text is None:
        text = input("请输入要加入的汉字（可直接粘贴一段文字）：").strip()
    if not text:
        text = DEFAULT_SYMBOLS + load_simplified_character_source()

    if not args.font.is_file():
        print(f"字体源文件不存在：{args.font}", file=sys.stderr)
        return 1
    if not args.output.is_file():
        print(f"原字库不存在：{args.output}", file=sys.stderr)
        return 1

    try:
        codepoints = read_existing_codepoints(args.output)
        codepoints.difference_update(UNSUPPORTED_CODEPOINTS)
        before = len(codepoints)
        codepoints.update(ord(char) for char in text)
        codepoints.update(ord(char) for char in DEFAULT_SYMBOLS)
        # LRC/text files contain CR/LF separators; they are not printable
        # glyphs and make lv_font_conv calculate an invalid font range.
        codepoints = {codepoint for codepoint in codepoints if codepoint >= 0x20}
        codepoints.difference_update(UNSUPPORTED_CODEPOINTS)
        common_text = load_simplified_character_source()
        codepoints.update(ord(char) for char in common_text)
        codepoints = {codepoint for codepoint in codepoints if codepoint >= 0x20}
        codepoints.difference_update(UNSUPPORTED_CODEPOINTS)
        han_count = sum(0x3400 <= codepoint <= 0x9FFF for codepoint in codepoints)
        if han_count < args.target_count:
            raise RuntimeError(
                f"当前字符源只有 {han_count} 个汉字，无法达到 {args.target_count}；"
                "请补充更大的简体中文字符表。"
            )
        symbols = "".join(chr(codepoint) for codepoint in sorted(codepoints))
        node, npx_cli = find_node_tools()
    except (OSError, ValueError, FileNotFoundError) as exc:
        print(f"准备字库失败：{exc}", file=sys.stderr)
        return 1

    temp_output = args.output.with_suffix(args.output.suffix + ".new")
    command = [
        str(node), str(npx_cli), "--yes", "lv_font_conv",
        "--font", str(args.font), "--size", "20", "--bpp", "4",
        "--format", "bin", "--no-kerning", "--output", str(temp_output),
        f"--symbols={symbols}",
    ]

    try:
        subprocess.run(command, cwd=ROOT, check=True)
        if not temp_output.is_file() or temp_output.stat().st_size == 0:
            raise RuntimeError("lv_font_conv did not create a valid output file")
        temp_output.replace(args.output)
    except (OSError, subprocess.CalledProcessError, RuntimeError) as exc:
        if temp_output.exists():
            temp_output.unlink()
        print(f"生成字库失败：{exc}", file=sys.stderr)
        return 1

    added = len(codepoints) - before
    print(f"已更新：{args.output}")
    print(f"汉字数：{han_count}，字符总数：{len(codepoints)}，本次新增：{added}，文件大小：{args.output.stat().st_size} bytes")
    print("请将该文件复制到 SD 卡的 /fonts/chinese_20.bin，然后重启设备。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
