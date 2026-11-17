"""Check the N16R8 board's modeled PSRAM headroom from the actual linked ELF.

This is an allocation budget, not a measurement of runtime free heap. It covers
the copied XiP sections, external BSS, LVGL pool, RGB framebuffers, one cached
wallpaper, and nine SD icons. WiFi/TLS/audio still use the remaining space.
"""
import argparse
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]


def symbols(path):
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected little-endian ELF32')
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    sections = [struct.unpack_from('<IIIIIIIIII', data, h[6] + i * h[11]) for i in range(h[12])]
    result = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings_section = sections[section[6]]
        strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
        for at in range(section[4], section[4] + section[5], section[9]):
            name, value, _, _, _, _ = struct.unpack_from('<IIIBBH', data, at)
            result[strings[name:].split(b'\0', 1)[0].decode('ascii', 'replace')] = value
    return result


def inspect(elf, minimum=512 * 1024):
    values = symbols(elf)
    # With this board's two XiP options enabled, the linker places external BSS
    # after the copied text/rodata pages in the ESP32-S3 0x3c000000 alias.
    static_end = values['_ext_ram_bss_end']
    static_bytes = (static_end - 0x3c000000 + 15) & ~15
    if not 0 < static_bytes < 8 * 1024 * 1024:
        raise ValueError('Unexpected ESP32-S3 external-memory layout')
    config = (ROOT/'include/lv_conf.h').read_text(encoding='utf8')
    match = re.search(r'#define LV_MEM_SIZE \((\d+)U \* 1024U\)', config)
    if not match:
        raise ValueError('Update the budget for the new LVGL pool expression')
    lvgl = int(match[1]) * 1024
    allocations = dict(copied_sections_and_external_bss=static_bytes,
                       lvgl_pool=lvgl, rgb_framebuffers=2 * 480 * 480 * 2,
                       cached_wallpaper=480 * 480 * 3, sd_icons=9 * 64 * 64 * 3)
    remaining = 8 * 1024 * 1024 - sum(allocations.values())
    return dict(physical_psram_bytes=8 * 1024 * 1024, budget_bytes=allocations,
                modeled_headroom_before_runtime_allocations=remaining,
                minimum_headroom=minimum, passed=remaining >= minimum,
                note='Budget only; runtime WiFi/TLS/audio allocations are not measured here')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--min-headroom', type=int, default=512 * 1024)
    args = parser.parse_args()
    result = inspect(args.elf, args.min_headroom)
    print(json.dumps(result))
    if not result['passed']:
        parser.exit(1, 'Modeled PSRAM headroom is below the runtime budget.\n')


if __name__ == '__main__':
    main()
