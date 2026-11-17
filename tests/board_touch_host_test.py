"""Compile production board_touch.c against a scripted GT911 I2C fake.

Covers the status-first read, one 8-byte contact, release, bounds, ACK failures,
and init/re-init. This does not exercise a physical GT911.
Run: py -3 tests/board_touch_host_test.py [--cc cl.exe|tcc.exe|cc]
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

SDK = r'''
#ifndef BOARD_TOUCH_FAKE_ESP_ERR_H
#define BOARD_TOUCH_FAKE_ESP_ERR_H
#include <stdint.h>
typedef int esp_err_t;
enum { ESP_OK = 0, ESP_FAIL = 1, ESP_ERR_INVALID_STATE = 2 };
#define pdMS_TO_TICKS(ms) (ms)
void vTaskDelay(int ticks);
int64_t esp_timer_get_time(void);
#endif
'''

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "board_i2c.h"
#include "touch.h"

volatile bool i2c_busy;

static uint8_t mem[0x9000];
static int bus_up, add_fail, fail_read_reg, fail_write_reg;
static int adds, delays, reads, writes, read_bytes, write_regs[8];
static uint16_t read_reg[8];
static size_t read_len[8];
static int64_t micros;

int64_t esp_timer_get_time(void) { return micros; }
void vTaskDelay(int ticks) { delays += ticks; micros += (int64_t)ticks * 1000; }

i2c_master_bus_handle_t board_i2c_bus(void) {
    return bus_up ? (i2c_master_bus_handle_t)mem : NULL;
}
esp_err_t board_i2c_add_device(uint16_t addr, i2c_master_dev_handle_t *dev) {
    ++adds;
    assert(addr == 0x5D && dev);
    if (add_fail) { *dev = NULL; return ESP_FAIL; }
    *dev = (i2c_master_dev_handle_t)(mem + 1);
    return ESP_OK;
}
esp_err_t board_i2c_write(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len) {
    uint16_t reg;
    assert(dev && data && len == 3);
    reg = (uint16_t)((data[0] << 8) | data[1]);
    assert(writes < 8);
    write_regs[writes++] = reg;
    if (fail_write_reg == reg) return ESP_FAIL;
    mem[reg] = data[2];
    return ESP_OK;
}
esp_err_t board_i2c_write_read(i2c_master_dev_handle_t dev, const uint8_t *wr, size_t wr_len,
                               uint8_t *rd, size_t rd_len) {
    uint16_t reg;
    assert(dev && wr && wr_len == 2 && rd && rd_len);
    reg = (uint16_t)((wr[0] << 8) | wr[1]);
    assert(reads < 8);
    read_reg[reads] = reg;
    read_len[reads++] = rd_len;
    read_bytes += (int)rd_len;
    if (fail_read_reg == reg) return ESP_FAIL;
    memcpy(rd, mem + reg, rd_len);
    return ESP_OK;
}

static void wipe(void) {
    memset(mem, 0, sizeof(mem));
    add_fail = fail_read_reg = fail_write_reg = 0;
    adds = delays = reads = writes = read_bytes = 0;
    i2c_busy = false;
    touch_last_x = 11;
    touch_last_y = 22;
}
static void point(int x, int y) {
    mem[0x814F] = 1;
    mem[0x8150] = (uint8_t)x;
    mem[0x8151] = (uint8_t)(x >> 8);
    mem[0x8152] = (uint8_t)y;
    mem[0x8153] = (uint8_t)(y >> 8);
    mem[0x8154] = 0xFF;
}
int main(void) {
    int x = 3, y = 4;

    wipe(); bus_up = 0;
    touch_init();
    assert(adds == 0 && delays == 0 && !reads);
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_ERROR && x == 3 && y == 4);
    assert(!touch_read_point(&x, &y) && !touch_released());

    wipe(); bus_up = 1; add_fail = 1;
    touch_init();
    assert(adds == 1 && delays == 0);
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_ERROR);

    wipe(); bus_up = 1; fail_write_reg = 0x8040;
    touch_init();
    assert(writes == 1 && write_regs[0] == 0x8040 && delays == 0);
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_ERROR && !reads);

    wipe(); bus_up = 1;
    touch_init();
    assert(delays == 200 && write_regs[0] == 0x8040 && mem[0x8040] == 0x02);
    reads = writes = read_bytes = 0;

    mem[0x814E] = 0x03;
    x = 9; y = 8;
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_NONE);
    assert(reads == 1 && read_reg[0] == 0x814E && read_len[0] == 1 && writes == 0);
    assert(x == 9 && y == 8 && touch_last_x == 11 && touch_last_y == 22);
    assert(!touch_released());
    assert(touch_read_point(&x, &y) == false && x == 9);

    reads = writes = read_bytes = 0;
    mem[0x814E] = 0x81;
    point(10, 20);
    x = y = -5;
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_PRESSED);
    assert(x == 10 && y == 20 && touch_last_x == 10 && touch_last_y == 20);
    assert(reads == 2 && read_len[0] == 1 && read_reg[1] == 0x814F && read_len[1] == 8);
    assert(read_bytes == 9 && writes == 1 && write_regs[0] == 0x814E && mem[0x814E] == 0);
    assert(!touch_released());

    reads = writes = read_bytes = 0;
    mem[0x814E] = 0x00;
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_NONE && writes == 0 && read_bytes == 1);
    assert(touch_read_point(&x, &y) && x == 10 && y == 20);
    assert(!touch_released());

    reads = writes = 0;
    mem[0x814E] = 0x00;
    assert(touch_touched());
    assert(touch_touched());
    micros += 30 * 1000;
    i2c_busy = true;
    assert(touch_touched() && reads == 1);
    i2c_busy = false;
    mem[0x814E] = 0x80;
    reads = writes = read_bytes = 0;
    x = 10; y = 20;
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_RELEASED);
    assert(reads == 1 && read_len[0] == 1 && writes == 1 && mem[0x814E] == 0);
    assert(x == 10 && y == 20 && touch_last_x == 10 && touch_last_y == 20);
    assert(touch_released());
    assert(!touch_read_point(&x, &y));

    reads = writes = read_bytes = 0;
    mem[0x814E] = 0x83;
    point(100, 200);
    mem[0x8157] = 9; mem[0x8158] = 1;
    x = y = 0;
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_PRESSED && x == 100 && y == 200);
    assert(reads == 2 && read_len[1] == 8 && read_bytes == 9);

    reads = writes = 0;
    mem[0x814E] = 0x86;
    x = 1; y = 2;
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_ERROR);
    assert(reads == 1 && writes == 1 && mem[0x814E] == 0);
    assert(x == 1 && y == 2 && touch_last_x == 100 && touch_last_y == 200);
    assert(touch_read_point(&x, &y) && x == 100 && y == 200);

    reads = writes = 0;
    mem[0x814E] = 0x81;
    point(480, 10);
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_ERROR && writes == 1 && read_len[1] == 8);
    assert(touch_last_x == 100 && touch_last_y == 200 && x == 100 && y == 200);
    reads = writes = 0;
    mem[0x814E] = 0x81;
    point(1, 480);
    assert(touch_read_sample(NULL, NULL) == TOUCH_SAMPLE_ERROR);
    assert(touch_last_x == 100 && touch_last_y == 200);

    reads = writes = read_bytes = 0;
    fail_read_reg = 0x814E;
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_ERROR && writes == 0 && reads == 1 && read_len[0] == 1);
    assert(touch_last_x == 100 && touch_read_point(&x, &y) && x == 100);

    reads = writes = 0; fail_read_reg = 0x814F;
    mem[0x814E] = 0x81; point(4, 5);
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_ERROR && writes == 0);
    assert(mem[0x814E] == 0x81 && touch_last_x == 100 && x == 100);

    reads = writes = 0; fail_read_reg = 0; fail_write_reg = 0x814E;
    mem[0x814E] = 0x81; point(6, 7);
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_ERROR);
    assert(reads == 2 && writes == 1 && touch_last_x == 100 && touch_last_y == 200);
    reads = writes = 0;
    mem[0x814E] = 0x80;
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_ERROR && writes == 1 && reads == 1);
    assert(touch_released() == false && touch_read_point(&x, &y) && x == 100);

    fail_write_reg = 0; reads = writes = delays = 0;
    touch_init();
    assert(delays == 200 && adds == 0); /* Re-init reuses the already-added device. */
    mem[0x814E] = 0x81; point(7, 9);
    assert(touch_read_sample(&x, &y) == TOUCH_SAMPLE_PRESSED && x == 7 && y == 9);
    assert(touch_has_signal());
    puts("board_touch host test passed");
    return 0;
}
'''


def compile(compiler, temp):
    sdk = Path(temp) / 'sdk'
    (sdk / 'driver').mkdir(parents=True)
    (sdk / 'freertos').mkdir()
    (sdk / 'esp_err.h').write_text(SDK, encoding='utf-8')
    (sdk / 'esp_timer.h').write_text('#include "esp_err.h"\n', encoding='utf-8')
    (sdk / 'driver' / 'i2c_master.h').write_text(
        'typedef struct i2c_dev *i2c_master_dev_handle_t;\n'
        'typedef struct i2c_bus *i2c_master_bus_handle_t;\n', encoding='utf-8')
    (sdk / 'freertos' / 'FreeRTOS.h').write_text('#include "esp_err.h"\ntypedef int TickType_t;\n', encoding='utf-8')
    (sdk / 'freertos' / 'task.h').write_text('#include "esp_err.h"\n#define taskYIELD() ((void)0)\n', encoding='utf-8')
    unit = Path(temp) / 'touch_host.c'
    exe = Path(temp) / 'touch_host.exe'
    unit.write_text(HARNESS, encoding='utf-8')
    sources = [str(ROOT / 'src' / 'board_touch.c'), str(unit)]
    includes = [str(sdk), str(ROOT / 'include'), str(ROOT / 'src')]
    if compiler.name.lower() in ('cl', 'cl.exe'):
        command = [str(compiler), '/nologo', '/std:c11', '/W3', '/utf-8',
                   '/D_CRT_SECURE_NO_WARNINGS', *[f'/I{item}' for item in includes],
                   *sources, f'/Fe:{exe}', f'/Fo{temp}\\']
        if not os.environ.get('INCLUDE'):
            vcvars = Path(compiler).parents[6] / 'Auxiliary' / 'Build' / 'vcvars64.bat'
            if not vcvars.is_file():
                raise SystemExit('MSVC cl.exe needs INCLUDE or vcvars64.bat')
            script = Path(temp) / 'build.bat'
            script.write_text(f'@echo off\ncall "{vcvars}"\n{subprocess.list2cmdline(command)}\n', encoding='utf-8')
            command = ['cmd', '/d', '/c', str(script)]
    else:
        command = [str(compiler), *[f'-I{item}' for item in includes], *sources, '-o', str(exe)]
    result = subprocess.run(command, capture_output=True)
    if result.returncode:
        print((result.stdout + result.stderr).decode(errors='replace')[-4000:])
        raise SystemExit(result.returncode)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc')
    args = parser.parse_args()
    bundled = ROOT / '.tools' / 'host-tcc' / 'tcc' / 'tcc.exe'
    compiler = Path(args.cc) if args.cc else bundled if bundled.is_file() else None
    if compiler is None:
        found = shutil.which('cl') or shutil.which('cc') or shutil.which('gcc')
        if not found:
            parser.error('Pass --cc with cl.exe, tcc.exe, or cc')
        compiler = Path(found)
    with tempfile.TemporaryDirectory(prefix='board-touch-', ignore_cleanup_errors=True) as temp:
        exe = compile(compiler, temp)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
