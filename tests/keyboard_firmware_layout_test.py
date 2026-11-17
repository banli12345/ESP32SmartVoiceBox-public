"""Check space labels and visibility in the actual linked ESP32 ELF, not host fakes."""
import argparse
import json
from pathlib import Path
import struct


def inspect(path):
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected little-endian ELF32')
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    offset, stride, count = header[6], header[11], header[12]
    if stride != 40 or offset + stride * count > len(data):
        raise ValueError('Invalid ELF sections')
    sections = [struct.unpack_from('<IIIIIIIIII', data, offset + i * stride) for i in range(count)]
    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings_section = sections[section[6]]
        strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
        for pos in range(section[4], section[4] + section[5], section[9]):
            name, address, size, _, _, index = struct.unpack_from('<IIIBBH', data, pos)
            if index < len(sections):
                text = strings[name:].split(b'\0', 1)[0].decode('ascii', 'replace')
                symbols[text] = (address, size)

    def at(address, size):
        for section in sections:
            if section[1] != 8 and section[3] <= address and address + size <= section[3] + section[5]:
                start = section[4] + address - section[3]
                return data[start:start + size]
        raise ValueError(f'Unmapped ELF address {address:x}')

    def array(name, fmt):
        address, size = symbols[name]
        width = struct.calcsize(fmt)
        return struct.unpack('<' + fmt * (size // width), at(address, size))

    reports = []
    for mode, map_name, controls_name in [
        ('lower', 'default_kb_map_lc', 'default_kb_ctrl_text_map'),
        ('upper', 'default_kb_map_uc', 'default_kb_ctrl_text_map'),
        ('symbols', 'default_kb_map_spec', 'default_kb_ctrl_sym_map'),
        ('digits', 'default_kb_map_digits', 'default_kb_ctrl_sym_map'),
    ]:
        labels = []
        for address in array(map_name, 'I'):
            label = at(address, 1)
            i = 1
            while label[-1:] != b'\0':
                label += at(address + i, 1)
                i += 1
                if i > 64:
                    raise ValueError('Unterminated keyboard label')
            label = label[:-1].decode('utf8')
            if not label:
                break
            if label != '\n':
                labels.append(label)
        index = next(i for i, label in enumerate(labels) if label in (' ', 'Space'))
        controls = array(controls_name, 'H')
        if len(controls) != len(labels):
            raise ValueError('Map and control lengths differ')
        flags = controls[index]
        report = dict(mode=mode, label=labels[index], width=flags & 7,
                      hidden=bool(flags & 8), disabled=bool(flags & 32))
        report['pass'] = report['label'] == 'Space' and report['width'] >= 4 and not report['hidden'] and not report['disabled']
        reports.append(report)
    return dict(maps=reports, passed=all(row['pass'] for row in reports))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    args = parser.parse_args()
    result = inspect(args.elf)
    print(json.dumps(result))
    if not result['passed']:
        parser.exit(1, 'Final firmware has a hidden, disabled, narrow, or unlabeled space key.\n')


if __name__ == '__main__':
    main()
