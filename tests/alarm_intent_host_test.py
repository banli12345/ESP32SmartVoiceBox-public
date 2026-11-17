"""Host-compile src/AlarmIntent.cpp and check the Chinese alarm parser.

Run: py -3 tests/alarm_intent_host_test.py
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
VS = r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
NONE, SET, QUERY, DISABLE, STOP, INVALID = range(6)

DRIVER = r"""
#include "AlarmIntent.h"
#include <stdio.h>
#include <string.h>
static void dump(alarm_intent_t a) {
    printf("%d %u %u %u %u %d %u\n", (int)a.action, (unsigned)a.hour, (unsigned)a.minute,
           (unsigned)a.weekdays, (unsigned)a.ring_minutes, (int)a.day_offset, (unsigned)a.delay_minutes);
}
int main(void) {
    char buf[1024];
    while (fgets(buf, sizeof buf, stdin)) {
        size_t n = strlen(buf);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
        if (strcmp(buf, "@@NULL@@") == 0) dump(alarm_intent_parse(NULL));
        else if (strcmp(buf, "@@EMPTY@@") == 0) dump(alarm_intent_parse(""));
        else dump(alarm_intent_parse(buf));
    }
    return 0;
}
"""


def expect(text, action, hour=0, minute=0, weekdays=0, ring=0, day=-1, delay=0):
    return (text, (action, hour, minute, weekdays, ring, day, delay))


CASES = [
    expect("@@NULL@@", NONE),
    expect("@@EMPTY@@", NONE),
    expect("   \t  ", NONE),
    expect("帮我设置明天8点的闹钟", SET, 8, 0, 0, 0, 1, 0),
    expect("设一个每天早上7点半的闹钟", SET, 7, 30, 127, 0, -1, 0),
    expect("设置周一到周五8:30的闹钟", SET, 8, 30, 31, 0, -1, 0),
    expect("每周一、周三、周五7点闹钟", SET, 7, 0, 21, 0, -1, 0),
    expect("每天8点响铃5分钟", SET, 8, 0, 127, 5, -1, 0),
    expect("设置八点的闹钟", SET, 8, 0, 0, 0, -1, 0),
    expect("每天八点响铃八分钟", SET, 8, 0, 127, 8, -1, 0),
    expect("设置十二点的闹钟", SET, 12, 0),
    expect("设置两点的闹钟", SET, 2, 0),
    expect("设置两点半的闹钟", SET, 2, 30),
    expect("设置八点半的闹钟", SET, 8, 30),
    expect("设置二十三点四十五分的闹钟", SET, 23, 45),
    expect("设置零点三十分的闹钟", SET, 0, 30),
    expect("设置今天00:00的闹钟", SET, 0, 0, 0, 0, 0, 0),
    expect("设置后天八点半的闹钟", SET, 8, 30, 0, 0, 2, 0),
    expect("设置23:59的闹钟", SET, 23, 59),
    expect("设置中午12点的闹钟", SET, 12, 0),
    expect("设置中午十二点半的闹钟", SET, 12, 30),
    expect("设置下午8点半的闹钟", SET, 20, 30),
    expect("设置晚上11点的闹钟", SET, 23, 0),
    expect("设置晚上12点的闹钟", SET, 0, 0),
    expect("设置凌晨3点的闹钟", SET, 3, 0),
    expect("设置上午8:30的闹钟", SET, 8, 30),
    expect("每周日晚上9点闹钟", SET, 21, 0, 64),
    expect("设置周六8点的闹钟", SET, 8, 0, 32),
    expect("设置周日8点闹钟", SET, 8, 0, 64),
    expect("周一到周五8:30的闹钟", SET, 8, 30, 31),
    expect("设置星期一至星期五8:30闹钟", SET, 8, 30, 31),
    expect("每天8点响5分钟", SET, 8, 0, 127, 5),
    expect("每天8点铃声持续60分钟", SET, 8, 0, 127, 60),
    expect("设置8点响铃1分钟的闹钟", SET, 8, 0, 0, 1),
    expect("设置８：３０的闹钟", SET, 8, 30),
    expect("20分钟后提醒我", SET, 0, 0, 0, 0, -1, 20),
    expect("二十分钟后叫我", SET, 0, 0, 0, 0, -1, 20),
    expect("20分钟后闹钟", SET, 0, 0, 0, 0, -1, 20),
    expect("1分钟后提醒我", SET, 0, 0, 0, 0, -1, 1),
    expect("10080分钟后叫我", SET, 0, 0, 0, 0, -1, 10080),
    expect("设置20分钟后的闹钟", SET, 0, 0, 0, 0, -1, 20),
    expect("六十分钟后提醒我", SET, 0, 0, 0, 0, -1, 60),
    expect("8分钟后提醒我", SET, 0, 0, 0, 0, -1, 8),
    expect("查看闹钟", QUERY),
    expect("帮我查看一下闹钟", QUERY),
    expect("闹钟还有多久", QUERY),
    expect("我的闹钟", QUERY),
    expect("闹钟还有多久？", QUERY),
    expect("关闭闹钟", DISABLE),
    expect("取消闹钟", DISABLE),
    expect("请关闭我的闹钟", DISABLE),
    expect("停止响铃", STOP),
    expect("别响了", STOP),
    expect("停止闹钟", STOP),
    expect("query alarm", QUERY),
    expect("Please show my alarms", QUERY),
    expect("cancel alarm", DISABLE),
    expect("please turn off the alarm", DISABLE),
    expect("stop ringing", STOP),
    expect("STOP ALARM", STOP),
    expect("hello", NONE),
    expect("set an alarm", NONE),
    expect("从前有一个闹钟在桌子上", NONE),
    expect("请给我讲一个关于闹钟的故事", NONE),
    expect("教我怎么设置明天8点的闹钟", NONE),
    expect("闹钟是什么", NONE),
    expect("如果设置明天8点的闹钟", NONE),
    expect("比如每天8点的闹钟", NONE),
    expect("设置明天8点的闹钟吗", NONE),
    expect("桌子上的闹钟", NONE),
    expect("不要设置明天8点的闹钟", INVALID),
    expect("别设置明天7点的闹钟", INVALID),
    expect("设置两个明天8点的闹钟", INVALID),
    expect("设置明天8点和9点的闹钟", INVALID),
    expect("设置明天每天8点的闹钟", INVALID),
    expect("设置后天下周一8:00的闹钟", INVALID),
    expect("设置上午12点的闹钟", INVALID),
    expect("设置中午1点的闹钟", INVALID),
    expect("设置凌晨8点的闹钟", INVALID),
    expect("设置24:00的闹钟", INVALID),
    expect("设置8:60的闹钟", INVALID),
    expect("设置二十五点的闹钟", INVALID),
    expect("设置8点70分的闹钟", INVALID),
    expect("设置六十点的闹钟", INVALID),
    expect("每天8点响铃61分钟", INVALID),
    expect("每天8点响铃0分钟", INVALID),
    expect("0分钟后提醒我", INVALID),
    expect("10081分钟后闹钟", INVALID),
    expect("设置8分钟的闹钟", INVALID),
    expect("设置闹钟", INVALID),
    expect("8点闹钟", INVALID),
    expect("设置明天8点闹钟查看闹钟", INVALID),
    expect("设置8点20分钟后提醒我", INVALID),
    expect("20分钟后", INVALID),
    expect("设置8点左右的闹钟", INVALID),
    expect("设置昨天8点的闹钟", INVALID),
    expect("设置周五到周一8点的闹钟", INVALID),
    expect("闹钟", INVALID),
    expect("设置大后天8点的闹钟", INVALID),
]


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        driver = tmp / "driver.cpp"
        driver.write_text(DRIVER, encoding="utf-8")
        exe = tmp / "alarm_intent_test.exe"
        cmd = (
            f'call "{VS}" -arch=x64 -host_arch=x64 >nul && '
            f'cl /nologo /EHsc /utf-8 /std:c++17 /I "{ROOT / "src"}" '
            f'"{driver}" "{ROOT / "src" / "AlarmIntent.cpp"}" '
            f'/Fe:"{exe}"'
        )
        build = subprocess.run(cmd, cwd=tmp, capture_output=True, shell=True)
        if build.returncode != 0:
            raise SystemExit(build.stdout.decode("utf-8", "replace") + build.stderr.decode("utf-8", "replace"))
        blob = ("\n".join(text for text, _ in CASES) + "\n").encode("utf-8")
        run = subprocess.run([str(exe)], input=blob, capture_output=True)
        if run.returncode != 0:
            raise SystemExit(run.stderr.decode("utf-8", "replace"))
        lines = run.stdout.decode("ascii", "replace").splitlines()
        if len(lines) != len(CASES):
            raise SystemExit(f"line count {len(lines)} != {len(CASES)}\n{lines}")
        failed = 0
        for (text, want), line in zip(CASES, lines):
            got = tuple(int(x) for x in line.split())
            if got != want:
                failed += 1
                print(f"FAIL {text!r}\n  want {want}\n  got  {got}")
        if failed:
            raise SystemExit(f"{failed} failed")
        print(f"ok {len(CASES)} cases")


if __name__ == "__main__":
    main()
