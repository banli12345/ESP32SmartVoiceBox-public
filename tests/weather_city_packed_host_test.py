"""Exhaustive host check of the packed city resolver. No network.

Oracle aliases follow tools/gen_weather_city_data.py. Same-id rows pick the
smaller rank, then the smaller row. Different ids return 1. The compiled
WeatherCity.c resolver is called for every unique alias plus a province path,
a rare character, a missing name, and a duplicate name.
"""
import json
import shutil
import struct
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import gen_weather_city_data as gen  # noqa: E402

OUT = ROOT / ".tools" / "weather-city-packed-host"
JSON_PATH = ROOT / "assets" / "weather_cities" / "domestic.json"
RUNNER = r"""
#include "WeatherCity.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    FILE *in;
    FILE *out;
    uint32_t count;
    uint32_t index;
    if (argc != 3) return 2;
    in = fopen(argv[1], "rb");
    out = fopen(argv[2], "wb");
    if (!in || !out) return 2;
    if (fread(&count, 4, 1, in) != 1) return 2;
    for (index = 0; index < count; ++index) {
        uint16_t length = 0;
        char query[96];
        char id[16];
        char name[96];
        int code;
        uint8_t coded;
        uint16_t name_len;
        if (fread(&length, 2, 1, in) != 1 || length >= sizeof query) return 3;
        if (length && fread(query, length, 1, in) != 1) return 3;
        query[length] = 0;
        id[0] = 0;
        name[0] = 0;
        code = weather_city_lookup(query, id, sizeof id, name, sizeof name);
        if (code < 0 || code > 3) return 4;
        coded = (uint8_t)code;
        if (fwrite(&coded, 1, 1, out) != 1) return 4;
        if (code == 0) {
            name_len = (uint16_t)strlen(name);
            if (strlen(id) != 12 || fwrite(id, 12, 1, out) != 1 ||
                fwrite(&name_len, 2, 1, out) != 1 ||
                (name_len && fwrite(name, name_len, 1, out) != 1)) return 4;
        }
    }
    if (fclose(in) || fclose(out)) return 4;
    return 0;
}
"""


def parts(text):
    return [item for item in (text or "").split("/") if item]


def load_rows():
    rows = json.loads(JSON_PATH.read_text(encoding="utf-8"))
    if not rows or rows[0].get("A") != "城市ID":
        raise SystemExit("unexpected city table header")
    return rows[1:]


def legacy_aliases(admin, short):
    """Independent oracle for the uncompressed 1.26.1 alias rules."""
    result = set()
    admin_parts, short_parts = parts(admin), parts(short)
    for group in (admin_parts, short_parts):
        for i in range(len(group)):
            result.add(''.join(group[i:]))
    if len(short_parts) >= 3:
        result.add(short_parts[0] + short_parts[-1])
    if admin:
        result.add(admin)
    if short:
        result.add(short)
    result.discard('')
    return result


def expected_table(cities):
    groups = defaultdict(list)
    ranks = []
    names = []
    ids = []
    for index, row in enumerate(cities):
        short_parts = parts(row["C"])
        ranks.append(len(short_parts))
        names.append(short_parts[-1])
        ids.append(row["A"].strip())
        for alias in legacy_aliases(row["B"], row["C"]):
            groups[alias].append(index)
    oracle = {}
    for alias, rows_at in groups.items():
        distinct = {ids[row] for row in rows_at}
        if len(distinct) > 1:
            oracle[alias] = (1, "", "")
            continue
        best = rows_at[0]
        for row in rows_at[1:]:
            if ranks[row] < ranks[best] or (ranks[row] == ranks[best] and row < best):
                best = row
        oracle[alias] = (0, ids[best], names[best])
    return oracle


def compiler():
    tcc = ROOT / ".tools" / "host-tcc" / "tcc" / "tcc.exe"
    if tcc.exists():
        return str(tcc)
    for name in ("cl", "gcc", "cc", "tcc"):
        found = shutil.which(name)
        if found:
            return found
    raise SystemExit("C compiler required")


def compile_runner(cc):
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / "packed_runner.c"
    source.write_text(RUNNER, encoding="ascii", newline="\n")
    exe = OUT / "packed_runner.exe"
    sources = [str(ROOT / "src" / "WeatherCity.c"), str(source)]
    if Path(cc).stem.lower() == "cl":
        cmd = [cc, "/nologo", "/utf-8", "/I" + str(ROOT / "src"), *sources, "/Fe:" + str(exe), "/Fo" + str(OUT) + "\\"]
    else:
        cmd = [cc, "-I" + str(ROOT / "src"), *sources, "-o", str(exe)]
    subprocess.run(cmd, check=True)
    return exe


def run_queries(exe, queries):
    fixture = OUT / "queries.bin"
    result = OUT / "results.bin"
    blob = bytearray(struct.pack("<I", len(queries)))
    for query in queries:
        raw = query.encode("utf-8")
        if len(raw) > 95:
            raise SystemExit("query exceeds lookup stack cap")
        blob += struct.pack("<H", len(raw))
        blob += raw
    fixture.write_bytes(blob)
    subprocess.run([str(exe), str(fixture), str(result)], check=True, timeout=180)
    data = result.read_bytes()
    cursor = 0
    got = []
    for _query in queries:
        if cursor >= len(data):
            raise SystemExit("short resolver output")
        code = data[cursor]
        cursor += 1
        if code == 0:
            city_id = data[cursor:cursor + 12].decode("ascii")
            cursor += 12
            name_len = struct.unpack_from("<H", data, cursor)[0]
            cursor += 2
            name = data[cursor:cursor + name_len].decode("utf-8")
            cursor += name_len
            got.append((0, city_id, name))
        else:
            got.append((code, "", ""))
    if cursor != len(data):
        raise SystemExit("trailing resolver output")
    return got


def rare_alias(aliases):
    counts = defaultdict(int)
    for alias in aliases:
        for char in alias:
            if ord(char) > 127:
                counts[char] += 1
    rare = min(counts, key=lambda char: (counts[char], char))
    for alias in sorted(aliases):
        if rare in alias:
            return alias, rare, counts[rare]
    raise SystemExit("no rare character")


def province_alias(cities, oracle):
    for row in cities:
        short = parts(row["C"])
        admin = row.get("B") or ""
        if len(short) >= 3:
            joined = short[0] + short[-1]
            if joined in oracle and oracle[joined][0] == 0:
                return joined
        if "/" in admin and admin in oracle and oracle[admin][0] == 0:
            return admin
    raise SystemExit("no province alias")


def main():
    cities = load_rows()
    oracle = expected_table(cities)
    aliases = sorted(oracle, key=lambda item: item.encode("utf-8"))
    province = province_alias(cities, oracle)
    rare, rare_char, rare_count = rare_alias(aliases)
    ambiguous = next(alias for alias, value in oracle.items() if value[0] == 1)
    missing = "不存在的县"
    if missing in oracle:
        raise SystemExit("missing fixture collided")
    focused = [
        ("province", province, oracle[province]),
        ("rare", rare, oracle[rare]),
        ("ambiguous", ambiguous, (1, "", "")),
        ("missing", missing, (2, "", "")),
    ]
    exe = compile_runner(compiler())
    focused_got = run_queries(exe, [item[1] for item in focused])
    for (label, query, expect), got in zip(focused, focused_got):
        if got != expect:
            raise SystemExit("%s %r expected %r got %r" % (label, query, expect, got))
    got_all = run_queries(exe, aliases)
    mismatches = 0
    for alias, got in zip(aliases, got_all):
        if got != oracle[alias]:
            mismatches += 1
            if mismatches <= 8:
                print("MISMATCH", alias, "expected", oracle[alias], "got", got, file=sys.stderr)
    if mismatches:
        raise SystemExit("%d alias mismatches" % mismatches)
    print(
        "packed city PASS: %d unique aliases, province %s, rare %s (count %d), ambiguous %s"
        % (len(aliases), province, rare_char, rare_count, ambiguous)
    )


if __name__ == "__main__":
    main()
