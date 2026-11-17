"""Compare the native ST7701 GPIO waveform with the previous working init table."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r'static\s+(?:bool|void)\s+' + re.escape(name) + r'\([^;{}]*\)\s*\{', source)
    if not match:
        raise AssertionError('Missing production function: ' + name)
    at, depth = match.end(), 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[match.start():at]


def reference():
    old = (ROOT / 'include/Arduino_GFX-master/src/display/Arduino_RGB_Display.h').read_text(encoding='utf-8')
    data = re.search(r'st7701_type1_init_operations\[\]\s*=\s*\{(.*?)\};', old, re.S)[1]
    data = re.sub(r'//[^\n]*|/\*.*?\*/', '', data, flags=re.S)
    tokens = [x.strip() for x in data.split(',') if x.strip()]
    # The previous begin() issues software reset because this board has no RST pin.
    events = ['B', 'C01', 'E', 'L120']
    i = 0
    while i < len(tokens):
        op = tokens[i]
        i += 1
        if op in ('BEGIN_WRITE', 'END_WRITE'):
            events.append('B' if op == 'BEGIN_WRITE' else 'E')
        elif op == 'DELAY':
            events.append('L' + str(int(tokens[i], 0)))
            i += 1
        elif op == 'WRITE_COMMAND_8':
            events.append('C%02X' % int(tokens[i], 0))
            i += 1
        elif op == 'WRITE_BYTES':
            count = int(tokens[i], 0)
            i += 1
            events += ['D%02X' % int(x, 0) for x in tokens[i:i + count]]
            i += count
        elif op in ('WRITE_C8_D8', 'WRITE_C8_D16'):
            events.append('C%02X' % int(tokens[i], 0))
            i += 1
            count = 1 if op == 'WRITE_C8_D8' else 2
            events += ['D%02X' % int(x, 0) for x in tokens[i:i + count]]
            i += count
        else:
            raise AssertionError('Unhandled original operation: ' + op)
    return events


FAKE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "board_pins.h"
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
enum { ESP_OK, ESP_FAIL, GPIO_MODE_OUTPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE, GPIO_INTR_DISABLE };
static int cs=1, sck=0, mosi=0, bits=0, word=0;
static bool fail_config;
static int gpio_config(const gpio_config_t *io) {
    assert(io->pin_bit_mask == ((1ULL<<39)|(1ULL<<48)|(1ULL<<47)));
    return fail_config ? ESP_FAIL : ESP_OK;
}
static void gpio_set_level(int pin,int value) {
    if (pin==BOARD_LCD_SPI_CS) {
        if (value != cs) { assert(bits==0); puts(value ? "E" : "B"); }
        cs=value;
    } else if(pin==BOARD_LCD_SPI_MOSI) mosi=value;
    else if(pin==BOARD_LCD_SPI_SCK) {
        if(value && !sck && !cs) {
            word=(word<<1)|mosi;
            if(++bits==9) { printf("%c%02X\n",(word&256)?'D':'C',word&255); word=bits=0; }
        }
        sck=value;
    } else assert(false);
}
static void app_delay_ms(uint32_t n) { assert(bits==0 && cs); printf("L%u\n",n); }
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc')
    args=parser.parse_args()
    bundled=ROOT/'.tools/host-tcc/tcc/tcc.exe'
    compiler=args.cc or (str(bundled) if bundled.is_file() else shutil.which('cc'))
    if not compiler:
        parser.error('Pass --cc with a C compiler')
    source=(ROOT/'src/board_lcd.c').read_text(encoding='utf-8')
    names=['spi_cs','spi_sck','spi_mosi','spi_write9','spi_begin','spi_end','spi_cmd','spi_data','spi_cmd_bytes','st7701_type1_init']
    body='\n'.join(function(source,n) for n in names)
    with tempfile.TemporaryDirectory(prefix='st7701-init-', ignore_cleanup_errors=True) as temp:
        unit=Path(temp)/'init.c'
        exe=Path(temp)/'init.exe'
        unit.write_text(FAKE+body+'\nint main(void){fail_config=true;assert(!st7701_type1_init());fail_config=false;assert(st7701_type1_init());return 0;}\n',encoding='utf-8')
        subprocess.run([compiler,'-I',str(ROOT/'src'),str(unit),'-o',str(exe)],check=True)
        actual=subprocess.check_output([str(exe)],text=True).splitlines()
    expected=reference()
    assert actual==expected, f'Native sequence differs: {len(actual)} actual vs {len(expected)} expected events'
    print(f'ST7701 init passed: {len(actual)} exact GPIO-derived command/data/delay events; config failure stops safely')


if __name__=='__main__':
    main()
