"""Production single-alarm scheduler and persistence with deterministic clock/NVS."""
from pathlib import Path
import shutil, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
COMPAT = r'''
#pragma once
#include <cstdint>
#include <ctime>
#define SMARTVOICEBOX_IDF_RUNTIME_H
extern uint32_t alarm_test_ticks;
inline uint32_t app_millis(){return alarm_test_ticks;}
extern "C" time_t alarm_test_time(time_t*);
#define time(x) alarm_test_time(x)
'''
SOURCE = r'''
#include "AlarmService.h"
#include "nvs.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdio>
#include <mutex>
uint32_t alarm_test_ticks;
static int64_t wall;
static std::vector<unsigned char> stored,pending;
static bool legacy,fail_open,fail_set,fail_commit,audio_on,dismiss_before_start;
static uint8_t old_hour=6,old_minute=15,old_enabled=1;
static unsigned writes,starts,stops,wakes,ring_minutes;
extern "C" time_t alarm_test_time(time_t*p){if(p)*p=(time_t)wall;return (time_t)wall;}
extern "C" void sound_alarm_start(uint16_t minutes){++starts;ring_minutes=minutes;if(dismiss_before_start)alarm_dismiss();audio_on=true;}
extern "C" void sound_alarm_stop(){++stops;audio_on=false;}
extern "C" bool sound_alarm_is_active(){return audio_on;}
extern "C" void screen_idle_request_wake(){++wakes;}
int nvs_open(const char *ns,int mode,nvs_handle_t *h){
 if(!strcmp(ns,"alarm")){if(mode==NVS_READONLY&&stored.empty())return ESP_ERR_NVS_NOT_FOUND;if(mode==NVS_READWRITE&&fail_open)return ESP_FAIL;*h=1;return ESP_OK;}
 assert(!strcmp(ns,"clock"));if(!legacy)return ESP_ERR_NVS_NOT_FOUND;*h=2;return ESP_OK;
}
int nvs_get_blob(nvs_handle_t h,const char *key,void *p,size_t *n){assert(h==1&&!strcmp(key,"config"));if(stored.empty())return ESP_ERR_NVS_NOT_FOUND;if(*n<stored.size())return ESP_FAIL;*n=stored.size();memcpy(p,stored.data(),*n);return ESP_OK;}
int nvs_set_blob(nvs_handle_t h,const char *key,const void *p,size_t n){assert(h==1&&!strcmp(key,"config")&&n==32);++writes;if(fail_set)return ESP_FAIL;pending.assign((const unsigned char*)p,(const unsigned char*)p+n);return ESP_OK;}
int nvs_commit(nvs_handle_t h){assert(h==1);if(fail_commit)return ESP_FAIL;stored=pending;return ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==1||h==2);}
int nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out){assert(h==2);if(!strcmp(key,"hour"))*out=old_hour;else if(!strcmp(key,"minute"))*out=old_minute;else {assert(!strcmp(key,"enabled"));*out=old_enabled;}return ESP_OK;}
static int64_t at(int y,int month,int d,int h,int m,int sec=0){tm t={};t.tm_year=y-1900;t.tm_mon=month-1;t.tm_mday=d;t.tm_hour=h;t.tm_min=m;t.tm_sec=sec;t.tm_isdst=-1;return (int64_t)mktime(&t);}
static alarm_snapshot_t snapshot(){alarm_snapshot_t s;alarm_get_snapshot(&s);return s;}
static void advance(unsigned seconds){wall+=seconds;alarm_test_ticks+=seconds*1000u;alarm_service_poll();}
static void fresh(){stored.clear();pending.clear();legacy=fail_open=fail_set=fail_commit=audio_on=dismiss_before_start=false;writes=starts=stops=wakes=0;alarm_test_ticks=1000;wall=1800000000;alarm_service_init();wall=at(2026,11,16,7,59,50);alarm_service_init();}
static uint32_t save(alarm_config_t c){uint32_t id=0;assert(alarm_request_save(&c,&id)&&id);assert(snapshot().save_busy);alarm_service_poll();assert(snapshot().saved_request==id&&!snapshot().save_busy);return id;}
int main(){
 fresh();assert(!snapshot().config.enabled&&!snapshot().next_epoch);
 alarm_config_t c={8,0,127,2,true,0};save(c);assert(snapshot().seconds_to_alarm==10&&writes==1);
 assert(!starts);advance(10);assert(starts==1&&audio_on&&wakes==1&&ring_minutes==2);
 assert(snapshot().ringing&&snapshot().config.enabled&&snapshot().next_epoch==at(2026,11,17,8,0));
 advance(119);assert(snapshot().ringing);advance(1);assert(!snapshot().ringing&&!audio_on);
 wall=at(2026,11,16,8,0,20);audio_on=false;alarm_service_init();alarm_service_poll();assert(starts==1);
 assert(snapshot().next_epoch==at(2026,11,17,8,0));
 // Once fires, disables durably and does not replay after reboot in the same minute.
 fresh();c={8,0,0,1,true,at(2026,11,17,8,0)};save(c);assert(snapshot().next_epoch==c.once_epoch);
 wall=c.once_epoch;alarm_test_ticks+=86410000;alarm_service_poll();assert(starts==1&&!snapshot().config.enabled&&snapshot().ringing);
 alarm_dismiss();assert(!audio_on&&!snapshot().ringing);alarm_service_init();alarm_service_poll();assert(starts==1&&!snapshot().config.enabled);
 // Daily, weekdays, custom Sunday and leap/month rollover use local dates.
 fresh();c={8,30,31,1,true,0};assert(alarm_next_occurrence(&c,at(2026,11,20,9,0),0)==at(2026,11,23,8,30));
 c.weekdays=64;assert(alarm_next_occurrence(&c,at(2026,11,16,9,0),0)==at(2026,11,22,8,30));
 c.weekdays=127;assert(alarm_next_occurrence(&c,at(2028,2,28,9,0),0)==at(2028,2,29,8,30));
 c={8,0,0,1,true,0};wall=at(2026,12,31,9,0);save(c);assert(snapshot().next_epoch==at(2027,1,1,8,0));
 // Failed saves never alter active or committed configuration.
 const auto original=stored;const auto before=snapshot().config;c.hour=9;
 for(int mode=0;mode<3;mode++){
  fail_open=mode==0;fail_set=mode==1;fail_commit=mode==2;uint32_t id;
  assert(alarm_request_save(&c,&id));assert(!alarm_request_save(&c,nullptr));alarm_service_poll();
  assert(snapshot().failed_request==id&&snapshot().save_failed&&stored==original&&snapshot().config.hour==before.hour);
 }
 fail_open=fail_set=fail_commit=false;save(c);
 for(int field=0;field<4;field++){auto bad=c;if(field==0)bad.hour=24;if(field==1)bad.minute=60;if(field==2)bad.weekdays=128;if(field==3)bad.ring_minutes=0;assert(!alarm_request_save(&bad,nullptr));}
 // Invalid clock cannot resolve a one-shot; recurring schedules can await sync.
 fresh();wall=0;c={8,0,0,1,true,0};assert(!alarm_request_save(&c,nullptr));c.weekdays=127;save(c);assert(!snapshot().time_valid&&!snapshot().next_epoch);
 wall=at(2026,11,16,7,59);alarm_service_poll();assert(snapshot().next_epoch==at(2026,11,16,8,0));
 // Existing clock/hour/minute/enabled migration and corrupt new data isolation.
 fresh();legacy=true;alarm_service_init();assert(snapshot().config.enabled&&snapshot().config.weekdays==127&&snapshot().config.hour==6&&stored.size()==32);
 stored.assign(12,0);alarm_service_init();assert(!snapshot().config.enabled&&snapshot().storage_warning);
 // Monotonic ringing duration survives wall-clock rollback and tick wrap.
 fresh();c={8,0,127,1,true,0};save(c);alarm_test_ticks=UINT32_MAX-5000;wall=at(2026,11,16,8,0);alarm_service_poll();assert(audio_on);
 wall-=3600;alarm_test_ticks+=59000;alarm_service_poll();assert(audio_on&&snapshot().ringing);
 alarm_test_ticks+=1000;alarm_service_poll();assert(!audio_on&&!snapshot().ringing&&starts==1);
 // A dismiss racing the start dispatch must not leave an invisible ringing alarm.
 fresh();save(c);dismiss_before_start=true;advance(10);assert(!snapshot().ringing&&!audio_on);
 // Persisting a fired one-shot retries without duplicating the current occurrence.
 fresh();c={8,0,0,1,true,0};save(c);fail_commit=true;advance(10);assert(starts==1&&snapshot().storage_warning&&!snapshot().config.enabled);
 fail_commit=false;advance(6);assert(starts==1&&!snapshot().storage_warning);audio_on=false;alarm_service_init();alarm_service_poll();assert(starts==1&&!snapshot().config.enabled);
 alarm_get_snapshot(nullptr);
 puts("alarm scheduler: once/daily/weekdays/custom, countdown, legacy migration, NVS failures, no replay, clock/tick changes, duration and dismiss race PASS");
}
'''
def main():
 cc=shutil.which('cl');assert cc,'Use a Visual Studio developer environment'
 with tempfile.TemporaryDirectory(prefix='alarm-service-') as temp:
  d=Path(temp);(d/'freertos').mkdir()
  (d/'freertos/FreeRTOS.h').write_text('#pragma once\n#include <mutex>\ntypedef std::mutex portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED {}\n#define portENTER_CRITICAL(p) ((p)->lock())\n#define portEXIT_CRITICAL(p) ((p)->unlock())\n')
  (d/'compat.h').write_text(COMPAT)
  (d/'nvs.h').write_text('''#pragma once
#include <cstdint>
#include <cstddef>
typedef int esp_err_t;typedef int nvs_handle_t;
enum {ESP_OK=0,ESP_ERR_NVS_NOT_FOUND=1,ESP_FAIL=2,NVS_READONLY,NVS_READWRITE};
int nvs_open(const char*,int,nvs_handle_t*);int nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);
int nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);int nvs_commit(nvs_handle_t);void nvs_close(nvs_handle_t);
int nvs_get_u8(nvs_handle_t,const char*,uint8_t*);
''')
  unit=d/'test.cpp';unit.write_text(SOURCE,encoding='utf-8');exe=d/'test.exe'
  cmd=[cc,'/nologo','/std:c++17','/EHsc','/O2','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/I'+str(d),'/I'+str(ROOT/'src'),'/FI'+str(d/'compat.h'),str(unit),str(ROOT/'src/AlarmService.cpp'),'/Fe:'+str(exe),'/Fo'+str(d)+'\\']
  r=subprocess.run(cmd,capture_output=True)
  if r.returncode:print((r.stdout+r.stderr).decode(errors='replace')[-4500:]);raise SystemExit(r.returncode)
  subprocess.run([str(exe)],check=True,timeout=20)
if __name__=='__main__':main()
