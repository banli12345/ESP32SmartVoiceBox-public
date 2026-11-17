"""Check the linked ESP32-S3 ELF against this project's zero-GCC-TLS stack budget.

ESP-IDF allocates the entire GCC TLS area on every task's stack, including
small system tasks. This checks the actual linked image rather than host mocks.
"""
from pathlib import Path
import argparse
import json
import struct


def tls_layout(path):
    data = path.read_bytes()
    if len(data) < 52 or data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected a little-endian ELF32 image')
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    offset, entry_size, count = header[6], header[11], header[12]
    if entry_size != 40 or not count or offset + entry_size * count > len(data):
        raise ValueError('Invalid ELF section table')
    sections = [struct.unpack_from('<IIIIIIIIII', data, offset + i * entry_size) for i in range(count)]
    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        start, size, strings_index, symbol_size = section[4], section[5], section[6], section[9]
        if symbol_size != 16 or strings_index >= len(sections) or start + size > len(data):
            raise ValueError('Invalid ELF symbol table')
        strings_section = sections[strings_index]
        strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
        for position in range(start, start + size, symbol_size):
            name_offset, value, _, _, _, _ = struct.unpack_from('<IIIBBH', data, position)
            if name_offset >= len(strings):
                continue
            name = strings[name_offset:].split(b'\0', 1)[0].decode('ascii', errors='replace')
            if name.startswith('_thread_local_'):
                symbols[name] = value
    names = ['_thread_local_data_start', '_thread_local_data_end',
             '_thread_local_bss_start', '_thread_local_bss_end']
    if any(name not in symbols for name in names):
        raise ValueError('Missing ESP-IDF thread-local linker symbols')
    initialized = symbols[names[1]] - symbols[names[0]]
    zeroed = symbols[names[3]] - symbols[names[2]]
    if initialized < 0 or zeroed < 0:
        raise ValueError('Invalid TLS linker ranges')
    return {'initialized_bytes': initialized, 'zeroed_bytes': zeroed,
            'per_task_stack_bytes': (initialized + zeroed + 15) & ~15}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path, nargs='?', default=Path(__file__).resolve().parents[1] / 'build/esp32_smart_voice_box.elf')
    parser.add_argument('--max-tls-bytes', type=int, default=0)
    args = parser.parse_args()
    try:
        result = tls_layout(args.elf)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(2, str(error) + '\n')
    result['budget_bytes'] = args.max_tls_bytes
    result['pass'] = result['per_task_stack_bytes'] <= args.max_tls_bytes
    print(json.dumps(result))
    if not result['pass']:
        parser.exit(1, 'GCC TLS exceeds the system-task stack budget; review every task before release.\n')


if __name__ == '__main__':
    main()
