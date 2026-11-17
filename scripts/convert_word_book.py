"""Convert UTF-8 English/Chinese lines to Breeze OS indexed SD vocabulary.

Each nonempty line: English, two or more spaces (or a tab), Chinese meaning.
BWV1: 24-byte little-endian header, count+1 uint32 payload offsets, UTF-8
English\0Chinese\0 records. No meaning correction or silent truncation.
"""
from pathlib import Path
import argparse
import re
import struct

ROOT = Path(__file__).resolve().parents[1]


def parse_text(text):
    entries = []
    for number, line in enumerate(text.lstrip('\ufeff').splitlines(), 1):
        if not line.strip():
            continue
        parts = re.split(r'\s{2,}|\t+', line.strip(), maxsplit=1)
        if len(parts) != 2 or not all(parts):
            raise ValueError(f'Line {number}: expected English and Chinese separated by 2 spaces or a tab')
        english, chinese = parts
        if any(ord(c) < 32 or ord(c) == 127 for c in english + chinese):
            raise ValueError(f'Line {number}: control character')
        if len(english.encode('utf-8')) >= 48 or len(chinese.encode('utf-8')) >= 128:
            raise ValueError(f'Line {number}: exceeds device record capacity')
        entries.append((english, chinese))
    if not 1 <= len(entries) <= 100000:
        raise ValueError('Expected 1..100000 entries')
    return entries


def encode_book(entries):
    payload = bytearray()
    offsets = [0]
    for english, chinese in entries:
        payload.extend(english.encode('utf-8') + b'\0' + chinese.encode('utf-8') + b'\0')
        offsets.append(len(payload))
    data_offset = 24 + 4 * len(offsets)
    return (struct.pack('<4s5I', b'BWV1', len(entries), 24, data_offset, len(payload), 0)
            + struct.pack('<' + 'I' * len(offsets), *offsets) + payload)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=ROOT / 'assets/words/cet4.txt')
    parser.add_argument('--output', type=Path, default=ROOT / 'sd_card_files/words/cet4.bwv')
    parser.add_argument('--font-symbols', type=Path, help='UTF-8 character set for a matching LVGL font')
    args = parser.parse_args()
    entries = parse_text(args.input.read_text(encoding='utf-8-sig'))
    data = encode_book(entries)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    if args.font_symbols:
        ui = '背单词四级词库英文中文已配对错误换一组重新开始下一组继续重试请选择英文和中文卡片配对正确完成保存进度失败文件不可用请将复制到根目录学习词语释义侧边返回'
        ui_source = ROOT / 'src/WordMatchUI.c'
        if ui_source.exists():
            ui += ''.join(c for c in ui_source.read_text(encoding='utf-8') if ord(c) >= 128)
        chars = set(''.join(a + b for a, b in entries) + ui + ''.join(chr(c) for c in range(32, 127)))
        args.font_symbols.parent.mkdir(parents=True, exist_ok=True)
        args.font_symbols.write_text(''.join(sorted(chars)), encoding='utf-8')
    print(f'{len(entries)} entries; {len(data)} bytes -> {args.output}')


if __name__ == '__main__':
    main()
