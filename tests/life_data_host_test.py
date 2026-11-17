"""Exercise the actual four-index worker with real cJSON and SDK/HTTP faults.
Run from a Visual Studio developer terminal; no credentials/network/device.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
from online_music_host_test import HEADERS as BASE_HEADERS

ROOT = Path(__file__).resolve().parents[1]
HEADERS = dict(BASE_HEADERS)
HEADERS['esp_heap_caps.h'] += '\ninline void heap_caps_free(void *p){free(p);}\n'
HEADERS['esp_http_client.h'] = HEADERS['esp_http_client.h'].replace(
    'HTTP_ADDR_TYPE_INET=2', 'HTTP_ADDR_TYPE_INET=2, HTTP_METHOD_GET=3').replace(
    'const char *url; int timeout_ms', 'const char *url; int method; int timeout_ms')
HEADERS['freertos/FreeRTOS.h'] += '''
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(n) (n)
'''
HEADERS['freertos/task.h'] += '''
uint32_t xTaskGetTickCount();
void vTaskDelay(uint32_t);
'''

HARNESS = r'''
#define SMARTVOICEBOX_IDF_RUNTIME_H
#define SETWIFI_H
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include "Storage.h"
#include "LifeData.h"
#include "esp_http_client.h"
namespace fs=std::filesystem;
static fs::path sdroot;
static uint32_t milliseconds;
static uint32_t app_millis(){return milliseconds;}
uint32_t xTaskGetTickCount(){return milliseconds;}
void vTaskDelay(uint32_t n){milliseconds+=n;}
static time_t clock_value;
static time_t fake_time(time_t *p){if(p)*p=clock_value;return clock_value;}
static tm *localtime_r(const time_t*p,tm*out){return gmtime_s(out,p)==0?out:nullptr;}
static bool wifi=true,fail_io=false;
bool get_wifi_connected(){return wifi;}
bool fail_alloc=false,fail_task=false;
void (*pending_task)(void*)=nullptr;
static int initialized,cleaned,status=200,task_runs;
static bool incomplete,eagain,fail_open,fail_init;
static int64_t declared=-2;
static size_t pos,read_limit=7;
static std::string response,url;
static int response_hook;
static void date(int,int,int);
struct Client{};
Client *esp_http_client_init(const esp_http_client_config_t*c){
 assert(c->method==HTTP_METHOD_GET&&c->addr_type==HTTP_ADDR_TYPE_INET&&c->crt_bundle_attach&&c->disable_auto_redirect&&c->timeout_ms==6000);
 url=c->url;if(fail_init)return nullptr;pos=0;++initialized;return new Client;
}
int esp_http_client_open(Client*,int){return fail_open?1:0;}
int64_t esp_http_client_fetch_headers(Client*){return declared==-2?(int64_t)response.size():declared;}
int esp_http_client_get_status_code(Client*){return status;}
int esp_http_client_read(Client*,char*d,int n){
 if(response_hook==1){response_hook=0;life_data_set_city("北京");}
 if(response_hook==2){response_hook=0;date(2026,11,15);}
 if(eagain)return -ESP_ERR_HTTP_EAGAIN;
 size_t k=std::min({(size_t)n,response.size()-pos,read_limit});memcpy(d,response.data()+pos,k);pos+=k;return (int)k;
}
bool esp_http_client_is_complete_data_received(Client*){return !incomplete&&pos==response.size();}
int esp_http_client_close(Client*){return 0;}
int esp_http_client_cleanup(Client*c){++cleaned;delete c;return 0;}
static fs::path path(const char*p){return sdroot/(p+1);}
extern "C" bool storage_exists(const char*p){return fs::exists(path(p));}
extern "C" bool storage_is_dir(const char*p){return fs::is_directory(path(p));}
extern "C" bool storage_mkdir(const char*p){return fs::create_directories(path(p));}
extern "C" bool storage_remove(const char*p){return fs::remove(path(p));}
extern "C" bool storage_rename(const char*a,const char*b){std::error_code ec;fs::rename(path(a),path(b),ec);return !ec;}
StorageStream::~StorageStream(){close();}
bool StorageStream::open(const char*p,const char*m){file_=fopen(path(p).string().c_str(),m);return file_!=nullptr;}
void StorageStream::close(){if(file_){fclose(file_);file_=nullptr;}}
uint64_t StorageStream::byte_count()const{long p=ftell(file_);fseek(file_,0,SEEK_END);long n=ftell(file_);fseek(file_,p,SEEK_SET);return n;}
size_t StorageStream::read(void*p,size_t n){return fail_io?0:fread(p,1,n,file_);}
size_t StorageStream::write(const void*p,size_t n){return fail_io?0:fwrite(p,1,n,file_);}
bool StorageStream::flush(){return !fail_io&&fflush(file_)==0;}
#define time fake_time
#include "@ROOT@/src/LifeData.cpp"
#undef time
static void date(int y,int m,int d){tm t={};t.tm_year=y-1900;t.tm_mon=m-1;t.tm_mday=d;t.tm_hour=12;clock_value=_mkgmtime(&t);}
static void reset(){
 assert(!pending_task);fs::remove_all(sdroot/".calendar");
 milliseconds=0;date(2026,11,14);response_hook=0;wifi=true;fail_alloc=fail_task=fail_io=false;
 incomplete=eagain=fail_open=fail_init=false;initialized=cleaned=task_runs=0;status=200;declared=-2;pos=0;read_limit=7;
 response=R"({"results":[{"suggestion":{"dressing":{"brief":"舒适"}},"last_update":"2026-11-14T08:00:00+08:00"}]})";
 s_slot=s_seen_gen=s_attempt_at=0;s_attempted=false;s_today[0]=0;s_gen=1;s_rev=0;s_busy=false;
 life_data_init("unit-key?not-real","上海","https://api.seniverse.com/v3/life/suggestion.json",false);
}
static void poll(unsigned dt=1000){milliseconds+=dt;life_data_poll();}
static void run(){assert(pending_task);auto f=pending_task;pending_task=nullptr;++task_runs;f(nullptr);assert(!s_busy&&initialized==cleaned);}
static bool got(){life_dressing_t out;bool ok=life_data_get_dressing(&out);if(!ok)assert(!out.date[0]&&!out.brief[0]);return ok;}
static void cache(const std::string&s,const char*name="indices.json"){
 std::string payload=s;cJSON*obj=cJSON_Parse(s.c_str());
 cJSON*v=obj?cJSON_GetObjectItem(obj,"v"):nullptr;
 if(cJSON_IsObject(obj)&&cJSON_IsNumber(v)&&v->valuedouble==2){
  cJSON_SetNumberValue(v,3);cJSON_AddNumberToObject(obj,"source",s_source_tag);
  char*encoded=cJSON_PrintUnformatted(obj);assert(encoded);payload=encoded;cJSON_free(encoded);
 }
 cJSON_Delete(obj);fs::create_directories(sdroot/".calendar");
 auto f=fopen((sdroot/".calendar"/name).string().c_str(),"wb");assert(f);fwrite(payload.data(),1,payload.size(),f);fclose(f);
}
int main(int argc,char**argv){assert(argc==3);sdroot=argv[1];fs::create_directories(sdroot);
 // Captured real response shapes, retaining only date and dressing fields.
 for(const char*name:{"basic","daily"}){
  reset();std::ifstream f(fs::path(argv[2])/(std::string("seniverse_dressing_")+name+".json"));assert(f);
  response.assign(std::istreambuf_iterator<char>(f),{});poll();run();assert(got()&&initialized==1);
  life_indices_t all;assert(life_data_get_indices(&all));
  assert(!strcmp(all.dressing,"舒适")&&!strcmp(all.uv,"弱")&&!strcmp(all.flu,"少发"));
  assert(!strcmp(all.sport,!strcmp(name,"basic")?"较不宜":"适宜"));
  assert(url.find("days=")==std::string::npos); // basic/free contract, no daily parameter
 }
 reset();response=R"({"results":[{"last_update":"2026-11-14T12:00:00+08:00","suggestion":{"uv":{"brief":"强"},"flu":{"brief":"暂无"},"sport":{"brief":42}}}]})";
 poll();run();life_indices_t partial;assert(life_data_get_indices(&partial)&&!strcmp(partial.uv,"强")&&!partial.dressing[0]&&!partial.flu[0]&&!partial.sport[0]&&!got());
 life_data_set_city("杭州");memset(&partial,0x41,sizeof partial);assert(!life_data_get_indices(&partial));for(auto c:std::string((char*)&partial,sizeof partial))assert(c==0);
 reset();cache(R"({"v":1,"date":"2026-11-14","city":"上海","brief":"旧缓存"})","dressing.json");poll();run();assert(got()&&initialized==1);
 reset();response=R"({"results":[{"suggestion":[{"date":"2026-11-13","dressing":{"brief":"较冷"}},{"date":"2026-11-14","dressing":{"brief":"舒适"}}],"last_update":"2026-11-13T20:00:00+08:00"}]})";
 poll();run();life_dressing_t selected;assert(life_data_get_dressing(&selected)&&!strcmp(selected.brief,"舒适"));
 for(auto rows:{"[]",R"([{"date":"2026-11-13","dressing":{"brief":"舒适"}}])",R"([{"date":"2026-11-14x","dressing":{"brief":"舒适"}}])",R"([{"date":20261114,"dressing":{"brief":"舒适"}}])",R"([{"date":"2026-11-14","dressing":{"brief":""}}])"}){
  reset();response=std::string("{\"results\":[{\"suggestion\":")+rows+"}]}";poll();run();assert(!got());
 }
 reset();response=R"({"results":[{"suggestion":{"dressing":{"brief":"舒适"}}}]})";poll();run();assert(!got());
 reset();wifi=false;poll();assert(!pending_task&&!got());life_calendar_day_t day;
 assert(life_data_get_day(2026,2,17,&day)&&!strcmp(day.festival,"春节"));assert(initialized==0);
 wifi=true;poll();assert(pending_task);poll();run();assert(got()&&initialized==1);
 assert(url.find("key=unit-key%3Fnot-real")!=std::string::npos&&url.find("location=%E4%B8%8A%E6%B5%B7")!=std::string::npos);
 for(int i=0;i<20;++i)poll();assert(!pending_task&&task_runs==1);
 s_have=false;poll(300000);run();assert(got()&&initialized==1); // saved cache, no second HTTP
 date(2026,11,15);assert(!got());poll();run();assert(!got()); // old cache/API date rejected
 reset();poll();response_hook=1;run();assert(!got());poll();run();assert(got());
 reset();poll();response_hook=2;run();assert(!got()); // midnight response cannot publish yesterday
 reset();fail_task=true;poll();assert(!pending_task&&!s_busy);fail_task=false;poll();assert(!pending_task);poll(300000);run();assert(got());
 reset();fail_alloc=true;poll();run();assert(!got());fail_alloc=false;poll();assert(!pending_task);poll(300000);run();assert(got());
 for(int code:{403,429,500}){reset();status=code;poll();run();assert(!got());poll(299000);assert(!pending_task);status=200;poll(1000);run();assert(got());}
 for(const auto&bad:{std::string("{}"),std::string("not json"),std::string("{"),std::string("[[]]]"),std::string(20,'[')+std::string(20,']')}){
  reset();response=bad;poll();run();assert(!got());
 }
 for(const auto&brief:{std::string(""),std::string("暂无"),std::string(48,'x'),std::string("x\\n") ,std::string("\xC0\xAF")}){
  reset();response="{\"results\":[{\"last_update\":\"2026-11-14T12:00:00+08:00\",\"suggestion\":{\"dressing\":{\"brief\":\""+brief+"\"}}}]}";poll();run();assert(!got());
 }
 reset();response+="garbage";poll();run();assert(!got());
 reset();response=" \r\n"+response+" \n";declared=0;poll();run();assert(got()); // chunked transfer
 reset();incomplete=true;poll();run();assert(!got());
 reset();declared=999;poll();run();assert(!got());
 reset();declared=17000;poll();run();assert(!got());
 reset();declared=0;response.assign(17000,' ');poll();run();assert(!got());
 reset();eagain=true;poll();run();assert(!got()&&milliseconds>=7000&&milliseconds<7100);
 reset();fail_open=true;poll();run();assert(!got());
 reset();fail_init=true;poll();run();assert(!got());
 for(const auto&bad:{std::string("[]"),std::string("{}"),std::string("{"),std::string(1025,'x'),
  std::string(R"({"v":2,"date":"2026-11-13","city":"上海","dressing":"旧数据","uv":"弱","flu":"少发","sport":"适宜"})"),
  std::string(R"({"v":2,"date":"2026-11-14","city":"杭州","dressing":"其他城市","uv":"弱","flu":"少发","sport":"适宜"})"),
  std::string(R"({"v":2.5,"date":"2026-11-14","city":"上海","dressing":"舒适","uv":"弱","flu":"少发","sport":"适宜"})"),
  std::string(R"({"v":2,"date":"2026-11-14","city":"上海","dressing":"","uv":"","flu":"","sport":""})"),
  std::string(R"({"v":2,"date":"2026-11-14","city":"上海","dressing":"舒适","uv":3,"flu":"少发","sport":"适宜"})")}){
  reset();cache(bad);poll();run();assert(got()&&initialized==1); // bad disk data falls back to HTTPS
 }
 reset();cache(R"({"v":2,"date":"2026-11-14","city":"上海","dressing":"舒适","uv":"弱","flu":"少发","sport":"适宜"})");poll();run();assert(got()&&initialized==0);
 life_indices_t stored;assert(life_data_get_indices(&stored)&&!strcmp(stored.uv,"弱")&&!strcmp(stored.flu,"少发")&&!strcmp(stored.sport,"适宜"));
 date(2026,11,15);memset(&stored,0x41,sizeof stored);assert(!life_data_get_indices(&stored));for(auto c:std::string((char*)&stored,sizeof stored))assert(c==0);
 reset();cache(R"({"v":2,"date":"2026-11-14","city":"上海","dressing":"旧缓存","uv":"弱","flu":"少发","sport":"适宜"})");
 life_data_init("new-life-key","上海","https://life.example.test:8443/custom",true);
 poll();run();assert(got()&&initialized==1&&url.find("https://life.example.test:8443/custom?key=new-life-key&")==0);
 assert(!s_ignore_cache); // a successful new-source response allows its own cache
 reset();life_data_init("new-life-key","上海","https://life.example.test:8443/custom",true);
 status=403;poll();run();assert(!got()&&s_ignore_cache); // failures must not fall back to old provider cache
 reset();poll();run();assert(got()&&initialized==1);
 // A saved result from a different key/endpoint cannot mask the new request, even without SD-import flags.
 life_data_init("different-key","上海","https://life.example.test/new",false);s_attempted=false;
 poll();run();assert(got()&&initialized==2&&url.find("https://life.example.test/new?key=different-key")==0);
 reset();fail_io=true;poll();run();assert(got()); // failed derived cache cannot discard RAM result
 puts("LifeData PASS: four real indices, both API schemas, partial availability, source-scoped v3 cache/migration, offline lunar, date/city, retry/task/OOM, bounded JSON/UTF-8/partial/timeout cleanup");
}
'''


def main():
    compiler = shutil.which('cl')
    if not compiler:
        raise SystemExit('Use a Visual Studio C++ developer terminal')
    with tempfile.TemporaryDirectory(prefix='life-data-', ignore_cleanup_errors=True) as tmp:
        directory = Path(tmp)
        for name, content in HEADERS.items():
            file = directory / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(content, encoding='utf-8')
        unit = directory / 'test.cpp'
        unit.write_text(HARNESS.replace('@ROOT@', ROOT.as_posix()), encoding='utf-8')
        sdk = Path('C:/Espressif/frameworks/esp-idf-v5.5.6/components/json/cJSON')
        exe = directory / 'test.exe'
        subprocess.run([compiler, '/nologo', '/std:c++17', '/EHsc', '/utf-8',
                        '/D_CRT_SECURE_NO_WARNINGS', f'/I{directory}', f'/I{ROOT / "src"}',
                        f'/I{sdk}', str(unit), str(ROOT / 'src/LocalLunar.c'),
                        str(sdk / 'cJSON.c'), f'/Fe:{exe}', f'/Fo{directory}\\'], check=True)
        subprocess.run([str(exe), str(directory / 'sd'), str(ROOT / 'tests/fixtures')], check=True)


if __name__ == '__main__':
    main()
