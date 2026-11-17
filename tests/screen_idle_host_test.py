"""Run actual idle/backlight code against deterministic clock/NVS/LEDC fakes."""
from pathlib import Path
import subprocess
import shutil
import tempfile
from desktop_ui_host_test import function

ROOT = Path(__file__).resolve().parents[1]
HEADERS = {
    'esp_err.h': '#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n',
    'esp_timer.h': '#pragma once\n#include <stdint.h>\nextern "C" int64_t esp_timer_get_time(void);\n',
    'freertos/FreeRTOS.h': '#pragma once\n#include <stdint.h>\ntypedef uint32_t TickType_t;\n#define pdMS_TO_TICKS(x) (x)\n',
    'freertos/task.h': '#pragma once\nstatic inline void vTaskDelay(unsigned){}\n#define taskYIELD() ((void)0)\n',
    'nvs.h': '''#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef int nvs_handle_t;
enum{NVS_READONLY,NVS_READWRITE};
extern "C" {
esp_err_t nvs_open(const char*,int,nvs_handle_t*);
esp_err_t nvs_get_u16(nvs_handle_t,const char*,uint16_t*);
esp_err_t nvs_set_u16(nvs_handle_t,const char*,uint16_t);
esp_err_t nvs_commit(nvs_handle_t);void nvs_close(nvs_handle_t);
}
''',
    'driver/ledc.h': '''#pragma once
#include "esp_err.h"
#include <stdint.h>
enum{LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,LEDC_TIMER_8_BIT,LEDC_TIMER_0,LEDC_AUTO_CLK,LEDC_INTR_DISABLE,LEDC_SLEEP_MODE_NO_ALIVE_NO_PD};
typedef struct {int speed_mode,duty_resolution,timer_num,freq_hz,clk_cfg;bool deconfigure;}ledc_timer_config_t;
typedef struct {int gpio_num,speed_mode,channel,intr_type,timer_sel;uint32_t duty;int hpoint,sleep_mode;struct{unsigned output_invert;}flags;}ledc_channel_config_t;
extern "C" {
esp_err_t ledc_timer_config(const ledc_timer_config_t*);
esp_err_t ledc_channel_config(const ledc_channel_config_t*);
esp_err_t ledc_set_duty(int,int,uint32_t);esp_err_t ledc_update_duty(int,int);
uint32_t ledc_get_duty(int,int);uint32_t ledc_get_freq(int,int);
}
''',
}


def main():
    cc = shutil.which('cl')
    if not cc:
        raise SystemExit('Use Visual Studio developer shell')
    with tempfile.TemporaryDirectory(prefix='screen-idle-', ignore_cleanup_errors=True) as temp:
        out = Path(temp)
        for name, text in HEADERS.items():
            file = out/name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(text, encoding='utf-8')
        sensor = function((ROOT/'src/main.cpp').read_text(encoding='utf8'), 'mpu_update')
        (out/'screen_idle_sensor_host.inc').write_text(sensor, encoding='utf8')
        exe = out/'test.exe'
        command = [cc, '/nologo', '/std:c++20', '/EHsc', '/utf-8', f'/I{out}', f'/I{ROOT/"src"}',
                   str(ROOT/'tests/screen_idle_host_test.cpp'), f'/Fe:{exe}', f'/Fo{out}\\']
        result = subprocess.run(command, capture_output=True)
        if result.returncode:
            print(result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
            raise SystemExit(result.returncode)
        result = subprocess.run([str(exe)], capture_output=True)
        if result.returncode:
            print(result.stdout.decode(errors='replace')[-3000:] + result.stderr.decode(errors='replace'))
            raise SystemExit(result.returncode)
        print(result.stdout.decode(errors='replace').splitlines()[-1])


if __name__ == '__main__':
    main()
