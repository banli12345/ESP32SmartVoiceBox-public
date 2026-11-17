"""Regenerate the 32px city-name font without dropping existing glyphs."""

from __future__ import annotations

import argparse
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FONT = ROOT / "simhei.ttf"
DEFAULT_OUTPUT = ROOT / "sd_card_files" / "fonts" / "chinese_32.bin"
DEFAULT_CITY_SOURCE = ROOT / "font_assets" / "city_names_32.txt"
NODE = ROOT / ".tools" / "node-v24.21.0-win-x64" / "node.exe"
NPX_CLI = ROOT / ".tools" / "node-v24.21.0-win-x64" / "node_modules" / "npm" / "bin" / "npx-cli.js"


def read_codepoints(path: Path) -> set[int]:
    data = path.read_bytes()
    if len(data) < 16 or data[4:8] != b"head":
        raise ValueError(f"not an LVGL binary font: {path}")
    head_size = struct.unpack_from("<I", data, 0)[0]
    cmap_size = struct.unpack_from("<I", data, head_size)[0]
    table_count = struct.unpack_from("<I", data, head_size + 8)[0]
    table_pos = head_size + 12
    table_size = struct.calcsize("<IIHHHBB")
    points: set[int] = set()
    for index in range(table_count):
        offset, start, length, _glyph, count, fmt, _ = struct.unpack_from(
            "<IIHHHBB", data, table_pos + index * table_size)
        if fmt == 2:
            points.update(range(start, start + length))
        elif fmt == 3:
            positions = struct.unpack_from("<" + "H" * count,
                                           data, head_size + offset)
            points.update(start + value for value in positions)
        else:
            raise ValueError(f"unsupported cmap format {fmt}")
    return points


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", type=Path, default=DEFAULT_FONT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--cities", type=Path, default=DEFAULT_CITY_SOURCE)
    args = parser.parse_args()
    if not args.font.is_file() or not args.output.is_file() or not args.cities.is_file():
        raise FileNotFoundError("font, output, or city source is missing")
    original_points = read_codepoints(args.output)
    points = set(original_points)
    points.update(ord(c) for c in args.cities.read_text(encoding="utf-8")
                  if c >= "\u0020" and c != "\ufeff")
    symbols = "".join(chr(p) for p in sorted(points))
    command = [str(NODE), str(NPX_CLI), "--yes", "lv_font_conv",
               "--font", str(args.font), "--size", "32", "--bpp", "4",
               "--format", "bin", "--no-kerning", "--output",
               str(args.output) + ".new", f"--symbols={symbols}"]
    subprocess.run(command, cwd=ROOT, check=True)
    generated = Path(str(args.output) + ".new")
    if not generated.is_file() or generated.stat().st_size == 0:
        raise RuntimeError("lv_font_conv did not create a valid output file")
    generated_points = read_codepoints(generated)
    missing_city_glyphs = points - generated_points
    missing_old_glyphs = original_points - generated_points
    if missing_city_glyphs or missing_old_glyphs:
        generated.unlink()
        missing = "".join(chr(p) for p in sorted(missing_city_glyphs))
        raise RuntimeError(f"generated font is missing required glyphs: {missing}")
    generated.replace(args.output)
    print(f"已生成 {args.output}")
    print(f"字形数：{len(generated_points)}，城市字形校验通过，原字形全部保留")
    print(f"文件大小：{args.output.stat().st_size} bytes")
    print("请将该文件复制到 SD 卡的 /fonts/chinese_32.bin，然后重启设备。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
