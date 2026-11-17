"""Production weather scheduler against slow/failing network and task boundary fakes.

The GUI tick and result publication are production code. HTTP/NVS/FreeRTOS are
explicit boundary fakes; this is not an ESP32 network or frame-rate test.
"""
from pathlib import Path
import argparse,re,shutil,subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/ui-smooth-20261115/host-weather'

def function(source,name):
    m=re.search(r'^(?:static |extern "C" )?(?:void|bool) '+name+r'\([^;{}]*\)\s*\{',source,re.M)
    assert m,name
    end,depth=m.end(),1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end].replace('extern "C" ','')

SDK=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define WEATHER_FORECAST_DAYS 3
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define portMUX_INITIALIZER_UNLOCKED 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef int portMUX_TYPE;typedef int BaseType_t;typedef void*TaskHandle_t;
static unsigned locks;
#define portENTER_CRITICAL(x) (++locks)
#define portEXIT_CRITICAL(x) (--locks)
static uint32_t now_ms;
static bool gui=true,network_ok=true,forecast_ok=true,create_fail,radio_lock_busy;
static unsigned task_creates,notifications,http_calls,ntp_calls,cache_saves,ui_publishes,heartbeats;
static unsigned allocation_count,fail_allocation;
static void(*during_network)(void);
static char cached_city[32],index_city[32];
static uint32_t app_millis(){return now_ms;}
static int xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t bytes,void*,unsigned,TaskHandle_t*out,int core){assert(gui&&bytes>=6144&&core==0);task_creates++;if(create_fail)return 0;*out=(void*)1;return pdPASS;}
static void xTaskNotifyGive(TaskHandle_t task){assert(gui&&task);notifications++;}
static int ulTaskNotifyTake(int,uint32_t){return 0;}
static unsigned uxTaskGetStackHighWaterMark(void*){return 4096;}
static void *wifi_api_lock=(void*)1;
static int xSemaphoreTakeRecursive(void*,uint32_t timeout){assert(gui&&timeout==0);return !radio_lock_busy;}
static void wifi_driver_unlock(){assert(gui);}
static bool sta_user_enabled=true,sta_desired=true,sta_has_ip=true;
static bool service_restore_pending,service_connecting,scan_active,have_sta_config,wifi_started=true,ap_active,service_was_connected=true;
static uint32_t service_next_retry,service_deadline;
static const char*WIFI_SSID="test";static const char*WIFI_PASSWORD="test-password";
static char service_ssid[33],service_password[64];
static bool wifi_service_start_connect(const char*,const char*){return true;}
static void saveWiFiCredentials(const char*,const char*){}
static bool ensure_wifi_started(){return true;}
static void esp_wifi_connect(){}
static char WEATHER_CITY[32]="nanning",WEATHER_CITY_DISPLAY[96]="南宁";
static int s_weather_code=-1;static char s_weather_status[48];static const char *s_city_error="";
#include "WeatherCity.h"
static char *weather,*temp,*updateTime;
static char WEATHER_FORECAST_DATE[3][12],WEATHER_FORECAST_DAY_TEXT[3][32],WEATHER_FORECAST_NIGHT_TEXT[3][32],WEATHER_FORECAST_HIGH[3][8],WEATHER_FORECAST_LOW[3][8];
static uint8_t WEATHER_FORECAST_COUNT;
static void cleanupWeatherData(){assert(gui);free(weather);free(temp);free(updateTime);weather=temp=updateTime=nullptr;}
static const char*pinyin_to_chinese(const char*city){if(!strcmp(city,"beijing"))return "北京";if(!strcmp(city,"shanghai"))return "上海";return city;}
static void life_data_set_city(const char*c){assert(gui);snprintf(index_city,sizeof(index_city),"%s",c);}
static void set_weather(const char*w,const char*t){assert(gui);assert(w&&t);ui_publishes++;}
static void save_weather_nvs(const char*city,const char*,const char*,const char*,const char*,const char[][12],const char[][32],const char[][32],const char[][8],const char[][8],uint8_t,int,bool){assert(!gui);cache_saves++;snprintf(cached_city,sizeof(cached_city),"%s",city);}
static void*allocate(size_t n){allocation_count++;return allocation_count==fail_allocation?nullptr:malloc(n);}
'''
NETWORK=r'''
static void wifi_service_tick(bool);
static void fake_slow_io(){
 assert(!gui);now_ms+=5000;
 gui=true;for(int i=0;i<100;i++){wifi_service_tick(false);heartbeats++;}if(during_network){auto cb=during_network;during_network=nullptr;cb();}gui=false;
}
static bool syncNTPTime(){assert(!gui);ntp_calls++;fake_slow_io();return network_ok;}
static bool fetch_weather_now_into(const char*city,wifi_refresh_result*out){assert(!gui);http_calls++;fake_slow_io();if(!network_ok)return false;snprintf(out->weather,sizeof(out->weather),"%s-sunny",city);strcpy(out->temp,"25");strcpy(out->update_time,"test-time");return true;}
static bool fetch_forecast_into(const char*,wifi_refresh_result*out){assert(!gui);http_calls++;fake_slow_io();if(!forecast_ok)return false;out->forecast_count=1;strcpy(out->forecast_date[0],"2026-11-15");strcpy(out->forecast_day_text[0],"sunny");return true;}
'''
CASES=r'''
static void run_worker(){assert(gui&&refresh_state==WIFI_REFRESH_RUNNING);gui=false;wifi_refresh_worker_run_once();gui=true;}
static void change_twice(){assert(updateWeatherCity("shanghai"));assert(updateWeatherCity("beijing"));}
int main(){
 weather=strdup("cached");temp=strdup("20");updateTime=strdup("old");WEATHER_FORECAST_COUNT=1;
 assert(!updateWeatherCity("")&&!updateWeatherCity("abcdefghijklmnopqrstuvwxyzabcdefghi"));
 unsigned h=http_calls;for(unsigned i=0;i<500;i++)wifi_service_tick(false);assert(http_calls==h&&task_creates==0&&ui_publishes==0);
 wifi_service_tick(true);assert(task_creates==1&&notifications==1&&refresh_state==WIFI_REFRESH_RUNNING&&http_calls==0);
 for(unsigned i=0;i<500;i++)wifi_service_tick(true);assert(task_creates==1&&notifications==1&&http_calls==0);
 run_worker();assert(!strcmp(weather,"cached")&&ui_publishes==0&&cache_saves==1&&heartbeats>=300);
 wifi_service_tick(false);assert(!strcmp(weather,"nanning-sunny")&&ui_publishes==1&&refresh_state==WIFI_REFRESH_IDLE);
 /* One-hour refresh reuses the worker; success must not disable the timer. */
 now_ms=weather_retry_at.load()-1;wifi_service_tick(true);assert(notifications==1);now_ms++;wifi_service_tick(true);assert(notifications==2&&task_creates==1);run_worker();wifi_service_tick(false);
 /* Slow request interleaves 300 GUI heartbeats and two city edits. Its stale
  * result may neither touch UI globals nor overwrite the cache for the city. */
 assert(updateWeatherCity("nanning"));unsigned before=ui_publishes,saved=cache_saves;during_network=change_twice;run_worker();assert(cache_saves==saved&&ui_publishes==before);wifi_service_tick(true);assert(ui_publishes==before&&refresh_state==WIFI_REFRESH_RUNNING&&!strcmp(refresh_job.city,"beijing"));run_worker();wifi_service_tick(false);assert(!strcmp(weather,"beijing-sunny")&&!strcmp(cached_city,"beijing")&&!strcmp(index_city,"beijing"));
 /* HTTP failure retains all already-published strings/forecast and retries
  * after 30 seconds, while the GUI still handles every polling iteration. */
 unsigned old_count=WEATHER_FORECAST_COUNT;network_ok=false;assert(updateWeatherCity("shanghai"));run_worker();before=ui_publishes;saved=cache_saves;wifi_service_tick(true);assert(ui_publishes==before&&cache_saves==saved&&!strcmp(weather,"beijing-sunny")&&WEATHER_FORECAST_COUNT==old_count);unsigned n=notifications;now_ms=weather_retry_at.load()-1;wifi_service_tick(true);assert(notifications==n);now_ms++;wifi_service_tick(true);assert(notifications==n+1);network_ok=true;run_worker();wifi_service_tick(false);assert(!strcmp(weather,"shanghai-sunny"));
 /* Atomic publication failure preserves the old strings and schedules retry. */
 assert(updateWeatherCity("beijing"));run_worker();fail_allocation=allocation_count+2;before=ui_publishes;wifi_service_tick(false);assert(ui_publishes==before&&!strcmp(weather,"shanghai-sunny"));fail_allocation=0;
 /* Creation failure is throttled, not retried/logged on each 5 ms GUI tick. */
 refresh_task=nullptr;refresh_state=WIFI_REFRESH_IDLE;refresh_task_retry_at=0;create_fail=true;assert(updateWeatherCity("beijing"));n=task_creates;assert(n>=2);for(unsigned i=0;i<500;i++)wifi_service_tick(true);assert(task_creates==n);now_ms=refresh_task_retry_at-1;wifi_service_tick(true);assert(task_creates==n);now_ms++;create_fail=false;wifi_service_tick(true);assert(task_creates==n+1&&refresh_state==WIFI_REFRESH_RUNNING);run_worker();wifi_service_tick(false);
 /* UI never waits for the driver mutex, even if a radio operation is busy. */
 assert(updateWeatherCity("nanning"));run_worker();radio_lock_busy=true;before=ui_publishes;wifi_service_tick(false);assert(ui_publishes==before+1&&refresh_state==WIFI_REFRESH_IDLE);radio_lock_busy=false;
 sta_has_ip=false;assert(updateWeatherCity("beijing"));n=notifications;wifi_service_tick(true);assert(notifications==n);sta_has_ip=true;wifi_service_tick(true);assert(notifications==n+1);forecast_ok=false;run_worker();wifi_service_tick(false);assert(WEATHER_FORECAST_COUNT==0&&!strcmp(weather,"beijing-sunny"));
 /* Deadlines continue across the 32-bit millisecond clock rollover. */
 refresh_task=nullptr;refresh_state=WIFI_REFRESH_IDLE;refresh_task_retry_at=0;create_fail=true;now_ms=0xfffffc17u;assert(updateWeatherCity("beijing"));n=task_creates;now_ms=0xffffffffu;wifi_service_tick(true);assert(task_creates==n);now_ms=refresh_task_retry_at;create_fail=false;wifi_service_tick(true);assert(task_creates==n+1);run_worker();wifi_service_tick(false);
 /* Link the real official-city resolver to the production GUI scheduler. */
 assert(updateWeatherCity("四川五通桥"));assert(!strcmp(WEATHER_CITY,"WM1Z63UKTHX2")&&!strcmp(WEATHER_CITY_DISPLAY,"五通桥")&&!strcmp(index_city,"WM1Z63UKTHX2"));
 run_worker();wifi_service_tick(false);assert(!strcmp(cached_city,"WM1Z63UKTHX2")&&!strcmp(WEATHER_CITY_DISPLAY,"五通桥"));
 uint32_t previous_gen=refresh_generation;assert(!updateWeatherCity("朝阳"));assert(strstr(s_city_error,"重名")&&refresh_generation==previous_gen&&!strcmp(WEATHER_CITY,"WM1Z63UKTHX2"));
 assert(updateWeatherCity("北京市朝阳区"));run_worker();wifi_service_tick(false);assert(!strcmp(WEATHER_CITY,"WX4G17JWZEK7")&&!strcmp(WEATHER_CITY_DISPLAY,"朝阳"));
 assert(locks==0);wifi_refresh_execute(nullptr,nullptr);cleanupWeatherData();
 printf("Weather refresh PASS: GUI keeps running through slow/failing IO, UI-only publication, latest-city wins, 1h/30s timers, allocation rollback, bounded task creation and nonblocking driver lock; %u heartbeats\n",heartbeats);
}
'''

def main():
    p=argparse.ArgumentParser();p.add_argument('--cxx');args=p.parse_args();cc=args.cxx or shutil.which('cl') or shutil.which('g++');assert cc,'C++ compiler required'
    text=(ROOT/'src/setwifi.cpp').read_text(encoding='utf-8');a=text.index('enum {\n    WIFI_REFRESH_IDLE');b=text.index('typedef struct {\n    const char *pinyin;',a);state=text[a:b]
    tick=function(text,'wifi_service_tick')
    assert 'wifi_refresh_on_gui_tick' in tick and 'nvs_' not in tick and 'esp_wifi_' not in tick and 'portMAX_DELAY' not in tick
    parts=[SDK,state,function(text,'copy_text'),NETWORK]
    for name in ['save_weather_cache_from_result','wifi_refresh_execute']:
        parts.append(function(text,name))
    parts+=['#define malloc allocate',function(text,'wifi_refresh_install_strings'),'#undef malloc']
    for name in ['wifi_refresh_publish_job','wifi_refresh_worker_run_once','wifi_refresh_worker','wifi_refresh_kick','wifi_refresh_on_gui_tick','updateWeatherCity','wifi_service_tick']:
        parts.append(function(text,name))
    OUT.mkdir(parents=True,exist_ok=True);unit=OUT/'test.cpp';unit.write_text('\n'.join(parts)+CASES,encoding='utf-8');exe=OUT/'test.exe'
    if Path(cc).stem.lower()=='cl':cmd=[cc,'/nologo','/std:c++17','/EHsc','/utf-8','/I'+str(ROOT/'src'),str(unit),str(ROOT/'src/WeatherCity.c'),'/Fe:'+str(exe),'/Fo'+str(OUT)+'\\']
    else:cmd=[cc,'-std=c++17','-I'+str(ROOT/'src'),str(unit),str(ROOT/'src/WeatherCity.c'),'-o',str(exe)]
    subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,timeout=30)
if __name__=='__main__':main()
