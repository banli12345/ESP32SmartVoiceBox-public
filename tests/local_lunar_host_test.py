"""Compare the actual C conversion with every day in the source HKO tables."""
from pathlib import Path
import ctypes
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'.tools/lunar-life-20261114/lunar-host'


class Day(ctypes.Structure):
    _fields_ = [(name, ctypes.c_char*size) for name, size in (
        ('date', 11), ('lunar_month', 24), ('lunar_day', 20),
        ('zodiac', 16), ('solar_term', 32), ('festival', 128))]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    wrapper = OUT/'wrapper.c'
    wrapper.write_text('#include "LocalLunar.h"\n__declspec(dllexport) int query(int y,int m,int d,life_calendar_day_t*out){return local_lunar_get(y,m,d,out);}\n', encoding='utf-8')
    dll = OUT/'lunar.dll'
    subprocess.run([str(ROOT/'.tools/host-tcc/tcc/tcc.exe'), '-shared', '-I', str(ROOT/'src'),
                    str(wrapper), str(ROOT/'src/LocalLunar.c'), '-o', str(dll)], check=True)
    lib = ctypes.CDLL(str(dll))
    lib.query.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.POINTER(Day)]
    lib.query.restype = ctypes.c_int
    records = json.loads((ROOT/'.tools/lunar-life-20261114/hko/reference-days.json').read_text(encoding='utf-8'))
    months = ['', '正月', '二月', '三月', '四月', '五月', '六月', '七月', '八月', '九月', '十月', '冬月', '腊月']
    days = '初一 初二 初三 初四 初五 初六 初七 初八 初九 初十 十一 十二 十三 十四 十五 十六 十七 十八 十九 二十 廿一 廿二 廿三 廿四 廿五 廿六 廿七 廿八 廿九 三十'.split()
    animals = '鼠牛虎兔龙蛇马羊猴鸡狗猪'
    for date, ly, lm, ld, leap, term in records:
        out = Day()
        assert lib.query(*map(int, date.split('-')), ctypes.byref(out)), date
        assert out.date.decode() == date
        assert out.lunar_month.decode() == ('闰' if leap else '') + months[lm], date
        assert out.lunar_day.decode() == days[ld-1], date
        assert out.zodiac.decode() == animals[(ly-4) % 12], date
        assert out.solar_term.decode() == term, date
    for date, festival in [('2024-02-09','除夕'), ('2024-02-10','春节'), ('2025-01-28','除夕'),
                           ('2025-01-29','春节'), ('2026-02-16','除夕'), ('2026-02-17','春节'),
                           ('2020-10-01','国庆节 / 中秋节')]:
        out = Day()
        assert lib.query(*map(int,date.split('-')), ctypes.byref(out))
        assert out.festival.decode() == festival, (date,out.festival)
    for y,m,d in [(1900,12,31),(2100,1,1),(2023,2,29),(2024,13,1),(2024,0,1),(2024,1,0),(2024,1,32)]:
        out = Day(); ctypes.memset(ctypes.byref(out),0x41,ctypes.sizeof(out))
        assert not lib.query(y,m,d,ctypes.byref(out))
        assert bytes(out) == bytes(ctypes.sizeof(out))
    for date,ly,lm,ld,leap,term in records:
        if leap and (lm,ld)==(5,5):
            out=Day();assert lib.query(*map(int,date.split('-')),ctypes.byref(out))
            assert '端午' not in out.festival.decode()
    print(f'Local lunar C PASS: {len(records):,} HKO dates, leap months, all solar terms, zodiac rollover, 29/30-day New Year eves, overlapping festivals, bounds and invalid dates')


if __name__ == '__main__':
    main()
