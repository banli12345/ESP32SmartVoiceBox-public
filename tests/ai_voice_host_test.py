"""Run production voice preference persistence, including concurrent readers.

Uses production NVS wrappers with injected failures and a real reader mutex.
The existing AI credential blob is not changed by this feature.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
from audio_handoff_host_test import function

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <thread>
#include <atomic>
#include "AiVoice.h"
#include "Storage.h"
typedef int nvs_handle_t;
typedef int esp_err_t;
enum {ESP_OK=0,NVS_READONLY,NVS_READWRITE};
static bool exists,fail_open,fail_get,fail_set,fail_save;
static uint8_t persisted,pending;
static unsigned writes;
static std::atomic<bool> hold_commit{false},in_commit{false};
static int nvs_open(const char *ns,int mode,nvs_handle_t *handle){
    assert(!strcmp(ns,"ai_voice"));if(fail_open||(mode==NVS_READONLY&&!exists))return 1;*handle=1;return 0;
}
static int nvs_get_u8(nvs_handle_t handle,const char *key,uint8_t *out){
    assert(handle==1&&!strcmp(key,"voice"));if(fail_get)return 1;*out=persisted;return 0;
}
static int nvs_set_u8(nvs_handle_t handle,const char *key,uint8_t value){
    assert(handle==1&&!strcmp(key,"voice"));++writes;if(fail_set)return 1;pending=value;return 0;
}
static int nvs_commit(nvs_handle_t handle){
    assert(handle==1);
    in_commit=true;while(hold_commit.load())std::this_thread::yield();
    if(fail_save)return 1;persisted=pending;exists=true;return 0;
}
static void nvs_close(nvs_handle_t handle){assert(handle==1);}
@STORAGE@
static ai_voice_state_t snapshot(){ai_voice_state_t s;ai_voice_get_state(&s);return s;}
int main(){
    ai_voice_init();assert(!strcmp(ai_voice_get_id(),"stella"));
    assert(!snapshot().busy && !snapshot().failed);
    assert(!ai_voice_request((ai_voice_t)-1)&&!ai_voice_request((ai_voice_t)2));
    assert(ai_voice_request(AI_VOICE_STELLA));ai_voice_poll();assert(!writes);
    assert(ai_voice_request(AI_VOICE_TIDE));assert(snapshot().busy&&!strcmp(ai_voice_get_id(),"stella"));
    assert(!ai_voice_request(AI_VOICE_STELLA));ai_voice_poll();
    assert(writes==1&&persisted==1&&!snapshot().busy&&!snapshot().failed&&!strcmp(ai_voice_get_id(),"tide"));
    ai_voice_init();assert(!strcmp(ai_voice_get_id(),"tide"));
    fail_save=true;assert(ai_voice_request(AI_VOICE_STELLA));ai_voice_poll();
    assert(snapshot().failed&&!snapshot().busy&&!strcmp(ai_voice_get_id(),"tide")&&persisted==1);
    fail_save=false;assert(ai_voice_request(AI_VOICE_STELLA));ai_voice_poll();
    assert(!strcmp(ai_voice_get_id(),"stella")&&!snapshot().failed);
    fail_open=true;assert(ai_voice_request(AI_VOICE_TIDE));ai_voice_poll();
    assert(snapshot().failed&&!strcmp(ai_voice_get_id(),"stella"));fail_open=false;
    fail_set=true;assert(ai_voice_request(AI_VOICE_TIDE));ai_voice_poll();
    assert(snapshot().failed&&!strcmp(ai_voice_get_id(),"stella"));fail_set=false;
    persisted=255;ai_voice_init();assert(!strcmp(ai_voice_get_id(),"stella"));
    persisted=1;fail_get=true;ai_voice_init();assert(!strcmp(ai_voice_get_id(),"stella"));fail_get=false;
    // While flash commit is blocked, readers continue to see the old voice,
    // duplicate saves and a second service poll cannot start another commit.
    hold_commit=true;in_commit=false;assert(ai_voice_request(AI_VOICE_TIDE));
    unsigned before=writes;std::thread writer(ai_voice_poll);
    while(!in_commit.load())std::this_thread::yield();
    for(int i=0;i<10000;i++)assert(!strcmp(ai_voice_get_id(),"stella")&&snapshot().busy);
    ai_voice_poll();assert(writes==before+1&&!ai_voice_request(AI_VOICE_STELLA));
    hold_commit=false;writer.join();assert(!strcmp(ai_voice_get_id(),"tide"));
    const char *old_id=ai_voice_get_id();assert(ai_voice_request(AI_VOICE_STELLA));ai_voice_poll();
    assert(!strcmp(old_id,"tide")&&!strcmp(ai_voice_get_id(),"stella"));
    ai_voice_get_state(nullptr);
    puts("voice preferences: defaults, saved reboot value, failed commit preservation, no UI flash write, concurrency and immutable request snapshot PASS");
}
'''


def main():
    compiler = shutil.which('cl')
    if not compiler:
        raise SystemExit('Use the Visual Studio developer environment.')
    with tempfile.TemporaryDirectory(prefix='ai-voice-') as temp:
        path = Path(temp)
        (path/'freertos').mkdir()
        (path/'freertos/FreeRTOS.h').write_text('''#pragma once
#include <mutex>
typedef std::mutex portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) ((p)->lock())
#define portEXIT_CRITICAL(p) ((p)->unlock())
''', encoding='utf-8')
        unit = path/'test.cpp'
        storage = (ROOT/'src/Storage.cpp').read_text(encoding='utf-8')
        wrappers = '\n'.join(function(storage, name) for name in ['storage_nvs_get_u8','storage_nvs_set_u8'])
        unit.write_text(SOURCE.replace('@STORAGE@', wrappers), encoding='utf-8')
        exe = path/'test.exe'
        result = subprocess.run([compiler, '/nologo', '/std:c++17', '/EHsc', '/O2', '/utf-8',
                                 '/I'+str(path), '/I'+str(ROOT/'src'), str(unit), str(ROOT/'src/AiVoice.cpp'),
                                 '/Fe:'+str(exe), '/Fo'+str(path)+'\\'], capture_output=True)
        if result.returncode:
            print((result.stdout+result.stderr).decode(errors='replace')[-4000:])
            raise SystemExit(result.returncode)
        subprocess.run([str(exe)], check=True, timeout=15)


if __name__ == '__main__':
    main()
