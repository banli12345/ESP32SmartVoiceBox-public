#!/usr/bin/env python3
"""Build src/weather_city_data.inc from the Seniverse domestic city sheet.

Source archive (not a CN admin-code table):
  https://cdn.sencdn.com/download/data/thinkpage_cities.zip
The domestic sheet used here is 城市列表_20260702.xlsx, already extracted to
JSON objects with columns A (12-char V3 id), B (admin path), C (short path),
D (pinyin, stored only as source context and not indexed).

Alias strings stay sorted by UTF-8 bytes. They are stored as a prefix-compressed
byte table: every 16 keys a uint32 checkpoint offset, then each key is
[prefix_len uint8, suffix_len uint8, suffix bytes, city row uint16 LE].
The first key of a block has prefix 0. Lookup rebuilds one key on the stack.

Regenerate from the repository root:
  py -3 tools/gen_weather_city_data.py
Optional args: input.json output.inc
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_IN = ROOT / "assets" / "weather_cities" / "domestic.json"
DEFAULT_OUT = ROOT / "src" / "weather_city_data.inc"
BLOCK = 16
ALIAS_MAX = 95


def parts(text):
    return [item for item in (text or "").split("/") if item]


def aliases(admin, short):
    found = set()
    admin_parts = parts(admin)
    short_parts = parts(short)
    for group in (admin_parts, short_parts):
        for index in range(len(group)):
            found.add("".join(group[index:]))
    if len(short_parts) >= 3:
        found.add(short_parts[0] + short_parts[-1])
    if admin:
        found.add(admin)
    if short:
        found.add(short)
    found.discard("")
    return found


def c_quote(text):
    out = ['"']
    for ch in text:
        code = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif 32 <= code < 127:
            out.append(ch)
        else:
            out.append("".join("\\x%02X" % b for b in ch.encode("utf-8")))
    out.append('"')
    return "".join(out)


def pack_keys(keys):
    blob = bytearray()
    checkpoints = []
    previous = b""
    max_alias = 0
    for index, (alias, city) in enumerate(keys):
        raw = alias.encode("utf-8")
        if len(raw) > ALIAS_MAX or len(raw) > 255:
            raise SystemExit("alias longer than %d bytes" % ALIAS_MAX)
        if city > 0xFFFF:
            raise SystemExit("city row does not fit in uint16")
        if index % BLOCK == 0:
            checkpoints.append(len(blob))
            if len(blob) > 0xFFFFFFFF:
                raise SystemExit("packed offset does not fit in uint32")
            prefix = 0
        else:
            limit = min(len(previous), len(raw))
            prefix = 0
            while prefix < limit and previous[prefix] == raw[prefix]:
                prefix += 1
        suffix = raw[prefix:]
        if prefix > 255 or len(suffix) > 255:
            raise SystemExit("prefix or suffix does not fit in uint8")
        blob.append(prefix)
        blob.append(len(suffix))
        blob.extend(suffix)
        blob.append(city & 0xFF)
        blob.append((city >> 8) & 0xFF)
        previous = raw
        if len(raw) > max_alias:
            max_alias = len(raw)
    if not checkpoints or checkpoints[0] != 0:
        raise SystemExit("missing checkpoint at offset 0")
    if len(checkpoints) != (len(keys) + BLOCK - 1) // BLOCK:
        raise SystemExit("checkpoint count does not cover every block")
    _verify_packed(keys, blob, checkpoints)
    return blob, checkpoints, max_alias


def _verify_packed(keys, blob, checkpoints):
    previous = b""
    cursor = 0
    for index, (alias, city) in enumerate(keys):
        if index % BLOCK == 0:
            if checkpoints[index // BLOCK] != cursor:
                raise SystemExit("checkpoint mismatch at key %d" % index)
            previous = b""
        if cursor + 4 > len(blob):
            raise SystemExit("truncated record at key %d" % index)
        prefix = blob[cursor]
        suffix_len = blob[cursor + 1]
        if prefix > len(previous) or cursor + 4 + suffix_len > len(blob):
            raise SystemExit("bad prefix or suffix at key %d" % index)
        if index % BLOCK == 0 and prefix != 0:
            raise SystemExit("block does not start with prefix 0")
        raw = previous[:prefix] + bytes(blob[cursor + 2:cursor + 2 + suffix_len])
        row = blob[cursor + 2 + suffix_len] | (blob[cursor + 3 + suffix_len] << 8)
        if raw != alias.encode("utf-8") or row != city:
            raise SystemExit("packed key %d does not round-trip" % index)
        cursor += 4 + suffix_len
        previous = raw
    if cursor != len(blob):
        raise SystemExit("trailing packed bytes")


def emit_bytes(lines, data):
    lines.append("static const uint8_t weather_city_key_packed[%d] = {" % len(data))
    for index in range(0, len(data), 24):
        chunk = data[index:index + 24]
        lines.append("    %s," % ",".join(str(value) for value in chunk))
    lines.append("};")


def main():
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_IN
    dest = Path(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_OUT
    rows = json.loads(source.read_text(encoding="utf-8"))
    if not rows or rows[0].get("A") != "城市ID":
        raise SystemExit("unexpected city table header")
    cities = rows[1:]
    keys = []
    for index, row in enumerate(cities):
        city_id = row["A"].strip()
        if len(city_id) != 12 or any(ch not in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" for ch in city_id):
            raise SystemExit("bad V3 id at row %s" % (index + 2))
        short_parts = parts(row["C"])
        if not short_parts:
            raise SystemExit("missing short path at row %s" % (index + 2))
        for alias in aliases(row["B"], row["C"]):
            keys.append((alias, index))
    keys.sort(key=lambda item: item[0].encode("utf-8"))
    blob, checkpoints, max_alias = pack_keys(keys)
    lines = [
        "/* Generated by tools/gen_weather_city_data.py. Do not edit.",
        " * Seniverse domestic city sheet inside",
        " * https://cdn.sencdn.com/download/data/thinkpage_cities.zip",
        " * (城市列表_20260702.xlsx). Columns: V3 id, admin path, short path.",
        " * Pinyin is not indexed; legacy ASCII locations stay unchanged.",
        " * Id, name, and rank stay plain const arrays.",
        " * Alias keys are UTF-8 byte sorted and prefix-packed in rodata:",
        " * every %d keys, weather_city_key_checkpoint holds a uint32 offset;" % BLOCK,
        " * each key is prefix_len, suffix_len, suffix bytes, city row uint16 LE.",
        " * Block-start prefix_len is 0. Do not expand the whole table into RAM.",
        " */",
        "static const char *const weather_city_id[%d] = {" % len(cities),
    ]
    for row in cities:
        lines.append("    %s," % c_quote(row["A"].strip()))
    lines.append("};")
    lines.append("static const char *const weather_city_name[%d] = {" % len(cities))
    ranks = []
    for row in cities:
        short_parts = parts(row["C"])
        lines.append("    %s," % c_quote(short_parts[-1]))
        ranks.append(len(short_parts))
    lines.append("};")
    lines.append("static const uint8_t weather_city_rank[%d] = {" % len(cities))
    for index in range(0, len(ranks), 24):
        chunk = ", ".join(str(value) for value in ranks[index:index + 24])
        lines.append("    %s," % chunk)
    lines.append("};")
    emit_bytes(lines, blob)
    lines.append("static const uint32_t weather_city_key_checkpoint[%d] = {" % len(checkpoints))
    for index in range(0, len(checkpoints), 12):
        chunk = ", ".join(str(value) for value in checkpoints[index:index + 12])
        lines.append("    %s," % chunk)
    lines.append("};")
    lines.append("static const size_t weather_city_count = %d;" % len(cities))
    lines.append("static const size_t weather_city_key_count = %d;" % len(keys))
    lines.append("static const size_t weather_city_key_block = %d;" % BLOCK)
    lines.append("static const size_t weather_city_key_packed_size = %d;" % len(blob))
    lines.append("static const size_t weather_city_key_checkpoint_count = %d;" % len(checkpoints))
    dest.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(
        "cities %d keys %d packed %d checkpoints %d (%d bytes) max_alias %d -> %s"
        % (len(cities), len(keys), len(blob), len(checkpoints), len(checkpoints) * 4, max_alias, dest)
    )


if __name__ == "__main__":
    main()
