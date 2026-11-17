"""Render the production LVGL panel and drive real pointer/long-press events."""
from pathlib import Path
import argparse,subprocess,re

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc')
    parser.add_argument('--output')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    output=Path(args.output).resolve() if args.output else root/'.tools/quick-panel-20261114/host'
    output.mkdir(parents=True,exist_ok=True)
    config=(root/'include/lv_conf.h').read_text(encoding='utf-8')
    config=config.replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0')
    config=config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>')
    config=config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)')
    (output/'lv_conf.h').write_text(config,encoding='utf-8')
    (output/'host_time.h').write_text('''#include <time.h>
#include <string.h>
static struct tm *panel_localtime_r(const time_t *timer, struct tm *result) {
    struct tm *value=localtime(timer); if(!value)return 0;
    memcpy(result,value,sizeof(*result));return result;
}
#define localtime_r panel_localtime_r
''',encoding='utf-8')
    main=(root/'src/main.cpp').read_text(encoding='utf-8')
    match=re.search(r'static void touch_read\([^{}]+\)\s*\{',main);assert match
    end,depth=match.end(),1
    while depth:depth+=(main[end]=='{')-(main[end]=='}');end+=1
    read=main[match.start():end].replace('lv_indev_drv_t *,','lv_indev_drv_t *driver,').replace('nullptr','NULL')
    (output/'production_input.c').write_text('''#include "lvgl.h"
#include "QuickPanel.h"
#include "EdgeBack.h"
#include "AppLauncher.h"
#include "UiTransition.h"
bool custom_navigation_can_back(void);
#include <stdlib.h>
static struct {lv_obj_t *screen;} guider_ui;
static bool touch_pressed;
static int touch_point_x,touch_point_y,current_page;
static int8_t swipe_dir;
static uint32_t app_millis(void){return lv_tick_get();}
static uint32_t touch_snapshot_load(void){return (touch_pressed?1u:0u)|((uint32_t)touch_point_x<<1)|((uint32_t)touch_point_y<<10);}
static bool boot_splash_busy(void){return false;}
static bool alarm_ui_alert_visible(void){return false;}
static bool screen_idle_pointer(bool down,int x,int y,uint32_t now){(void)down;(void)x;(void)y;(void)now;return false;}
'''+read+'''
void panel_test_set_home(lv_obj_t *screen){guider_ui.screen=screen;}
int panel_test_take_swipe(void){int result=swipe_dir;swipe_dir=0;return result;}
void panel_test_read(lv_indev_drv_t *driver,lv_indev_data_t *data,bool down,int x,int y){
touch_pressed=down;touch_point_x=x;touch_point_y=y;touch_read(driver,data);}
''',encoding='utf-8')
    compiler=Path(args.cc) if args.cc else root/'.tools/host-tcc/tcc/tcc.exe'
    lvgl=root/'components/lvgl'
    sources=sorted(str(p) for p in (lvgl/'src').rglob('*.c'))
    units=[str(root/'tests/quick_panel_host_test.c'),
             str(root/'src/UiTransition.c'),str(root/'src/QuickPanel.c'),str(root/'src/HotspotIcon.c'),str(root/'src/EdgeBack.c'),str(output/'production_input.c'),*sources]
    if compiler.name.lower() in ('cl','cl.exe'):
        # Native rounded arcs use signed int16 trigonometry. MSVC matches the
        # target GCC here; the bundled TCC miscompiles negative-angle sine.
        arguments=['/nologo','/std:c11','/O2','/utf-8','/D_CRT_SECURE_NO_WARNINGS',
                   '/DLV_CONF_INCLUDE_SIMPLE','/DLV_KCONFIG_IGNORE',
                   f'/I{output}',f'/I{lvgl}',f'/I{root / "src"}',
                   f'/FI{output / "host_time.h"}',*units,
                   f'/Fe:{output / "panel-test.exe"}',f'/Fo{output}\\']
        response=output/'msvc.rsp'
        response.write_text('\n'.join(subprocess.list2cmdline([argument]) for argument in arguments),encoding='utf-8')
        command=[str(compiler),'@'+str(response)]
    else:
        command=[str(compiler),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE',
             '-I',str(output),'-I',str(lvgl),'-I',str(root/'src'),
             '-include',str(output/'host_time.h'),*units,'-o',str(output/'panel-test.exe')]
    result=subprocess.run(command,cwd=root,capture_output=True,text=True,encoding='utf-8',errors='replace')
    (output/'compile.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode:
        print((result.stdout+result.stderr)[-5000:]);raise SystemExit(result.returncode)
    run=[str(output/'panel-test.exe'),str(output)]
    font=root/'sd_card_files/fonts/chinese_20.bin'
    if font.is_file():run.append(str(font))
    subprocess.run(run,check=True,timeout=45)
    try:
        from PIL import Image
    except ImportError:
        return
    for path in output.glob('*.rgb565'):
        raw=path.read_bytes();rgb=bytearray()
        for i in range(0,len(raw),2):
            color=raw[i]|raw[i+1]<<8
            rgb.extend((((color>>11)&31)*255//31,((color>>5)&63)*255//63,(color&31)*255//31))
        image=Image.frombytes('RGB',(480,480),bytes(rgb));image.save(path.with_suffix('.png'))
    print('Actual LVGL renders:',output)

if __name__=='__main__':main()
