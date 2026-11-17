"""Actual voice preferences and LVGL page: touch, async save, errors and return."""
from pathlib import Path
import shutil
import subprocess
from PIL import Image
from music_playlist_ui_host_test import HARNESS

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'.tools/ai-speech-20261116/ui-host'
SOURCE = r'''
#include "lvgl.h"
#include "AiVoice.h"
#include "EdgeBack.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_color_t pixels[480*480];
static lv_font_t *chinese_font_20;
static bool saved_exists,fail_save;
static uint8_t saved;
static unsigned saves;
bool storage_nvs_get_u8(const char *ns,const char *key,uint8_t *value){assert(!strcmp(ns,"ai_voice")&&!strcmp(key,"voice"));if(!saved_exists)return false;*value=saved;return true;}
bool storage_nvs_set_u8(const char *ns,const char *key,uint8_t value){assert(!strcmp(ns,"ai_voice")&&!strcmp(key,"voice"));++saves;if(fail_save)return false;saved=value;saved_exists=true;return true;}
#include "@ROOT@/src/AiVoiceUI.c"
@DRIVER@
static void swipe_back(void){edge_back_pointer(true,1,220,true);edge_back_pointer(true,88,220,true);edge_back_pointer(false,88,220,true);advance(80);}
int main(int argc,char **argv){
@INIT@
    ai_voice_init();edge_back_init();
    lv_obj_t *settings=lv_obj_create(NULL);lv_scr_load(settings);
    ai_voice_ui_open(settings,chinese_font_20);advance(200);
    assert(lv_scr_act()==screen && !lv_obj_has_flag(checks[0],LV_OBJ_FLAG_HIDDEN));
    assert(strstr(lv_label_get_text(current),"女声"));screenshot(argv[2],"voice-female");
    tap(210,273);assert(selected==AI_VOICE_TIDE && saves==0 && !strcmp(ai_voice_get_id(),"stella"));
    tap(230,381);ai_voice_state_t state;ai_voice_get_state(&state);
    assert(state.busy && lv_obj_has_state(save_button,LV_STATE_DISABLED) && saves==0);
    advance(250);assert(!strcmp(ai_voice_get_id(),"stella") && strstr(lv_label_get_text(status),"正在"));
    lv_event_send(save_button,LV_EVENT_CLICKED,NULL);assert(saves==0);
    ai_voice_poll();advance(150);assert(saved==AI_VOICE_TIDE && saves==1);
    assert(!strcmp(ai_voice_get_id(),"tide") && strstr(lv_label_get_text(status),"已保存"));
    assert(strstr(lv_label_get_text(current),"男声"));screenshot(argv[2],"voice-male-saved");
    swipe_back();assert(lv_scr_act()==settings && timer->paused);
    ai_voice_ui_open(settings,chinese_font_20);assert(selected==AI_VOICE_TIDE);
    fail_save=true;tap(210,167);tap(210,381);ai_voice_poll();advance(150);
    assert(!strcmp(ai_voice_get_id(),"tide") && strstr(lv_label_get_text(status),"失败"));
    assert(!lv_obj_has_state(save_button,LV_STATE_DISABLED));screenshot(argv[2],"voice-save-failed");
    fail_save=false;tap(210,381);ai_voice_poll();advance(150);
    assert(!strcmp(ai_voice_get_id(),"stella"));ai_voice_init();assert(!strcmp(ai_voice_get_id(),"stella"));
    tap(210,273);swipe_back();ai_voice_ui_open(settings,chinese_font_20);
    assert(selected==AI_VOICE_STELLA && saves==3);
    lv_mem_monitor_t before,after;lv_mem_monitor(&before);
    for(int i=0;i<20;i++){swipe_back();ai_voice_ui_open(settings,chinese_font_20);advance(140);}
    lv_mem_monitor(&after);assert(after.free_size+128>=before.free_size);
    swipe_back();lv_obj_del(screen);assert(!screen && !timer);
    ai_voice_ui_open(settings,NULL);advance(150);assert(!chinese && selected==AI_VOICE_STELLA);
    screenshot(argv[2],"voice-english");swipe_back();lv_obj_del(screen);
    assert(lv_mem_test()==LV_RES_OK);
    puts("voice UI: real touch selection, pending/committed/error state, reboot value, unsaved reset, edge return, delete/reopen and memory PASS");
}
'''


def main():
    compiler = shutil.which('cl')
    if not compiler:
        raise SystemExit('Use the Visual Studio developer environment.')
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT/'freertos').mkdir(exist_ok=True)
    (OUT/'freertos/FreeRTOS.h').write_text('''#pragma once
#include <mutex>
typedef std::mutex portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) ((p)->lock())
#define portEXIT_CRITICAL(p) ((p)->unlock())
''', encoding='utf-8')
    config = (ROOT/'include/lv_conf.h').read_text(encoding='utf-8')
    config = config.replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0')
    config = config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>')
    config = config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
    (OUT/'lv_conf.h').write_text(config, encoding='utf-8')
    driver = HARNESS[HARNESS.index('static bool down;'):HARNESS.index('int main(')]
    init = HARNESS[HARNESS.index(' assert(argc==3);'):HARNESS.index(' for(int i=0;i<12;')]
    unit = OUT/'test.c'
    unit.write_text(SOURCE.replace('@ROOT@',ROOT.as_posix()).replace('@DRIVER@',driver).replace('@INIT@',init), encoding='utf-8')
    arguments = ['/nologo','/O2','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/DLV_CONF_INCLUDE_SIMPLE','/DLV_KCONFIG_IGNORE',
                 '/I'+str(OUT),'/I'+str(ROOT/'src'),'/I'+str(ROOT/'components/lvgl'),
                 str(unit),str(ROOT/'src/AiVoice.cpp'),str(ROOT/'src/EdgeBack.c'),str(ROOT/'src/UiTransition.c'),
                 *[str(p) for p in (ROOT/'components/lvgl/src').rglob('*.c')],
                 '/Fe:'+str(OUT/'test.exe'),'/Fo'+str(OUT)+'\\']
    response=OUT/'compile.rsp'
    response.write_text('\n'.join(subprocess.list2cmdline([a]) for a in arguments),encoding='utf-8')
    result=subprocess.run([compiler,'@'+str(response)],capture_output=True,timeout=120)
    (OUT/'compile.log').write_bytes(result.stdout+result.stderr)
    if result.returncode:
        print((result.stdout+result.stderr).decode(errors='replace')[-4500:])
        raise SystemExit(result.returncode)
    subprocess.run([str(OUT/'test.exe'),str(ROOT/'sd_card_files/fonts/chinese_20.bin'),str(OUT)],check=True,timeout=45)
    for file in OUT.glob('*.rgb565'):
        raw=file.read_bytes();rgb=bytearray()
        for i in range(0,len(raw),2):
            c=raw[i]|raw[i+1]<<8
            rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
        Image.frombytes('RGB',(480,480),bytes(rgb)).save(file.with_suffix('.png'))


if __name__=='__main__':
    main()
