"""Run actual direct chat, speech and configuration sources with fault fakes.
Uses real cJSON; requires a Visual Studio developer terminal. No credentials,
device or network access. Checks protocol, persistence and audio cleanup.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
from online_music_host_test import HEADERS as MUSIC_HEADERS

ROOT = Path(__file__).resolve().parents[1]
HEADERS = dict(MUSIC_HEADERS)
HEADERS['esp_http_client.h'] = HEADERS['esp_http_client.h'].replace(
    'HTTP_ADDR_TYPE_INET=2', 'HTTP_ADDR_TYPE_INET=2, HTTP_METHOD_POST=3').replace(
    'const char *url; int timeout_ms', 'const char *url; int method; int timeout_ms') + '''
int esp_http_client_write(Client*,const char*,int);
'''
HEADERS['nvs.h'] = '''#pragma once
#include <cstddef>
#include <cstdint>
typedef int nvs_handle_t;
enum {NVS_READONLY, NVS_READWRITE};
int nvs_open(const char*,int,nvs_handle_t*);
int nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);
int nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
'''
HEADERS['esp_wifi.h'] = '''#pragma once
typedef enum { WIFI_PS_NONE=0, WIFI_PS_MIN_MODEM=1, WIFI_PS_MAX_MODEM=2 } wifi_ps_type_t;
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *type);
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type);
'''
HEADERS['esp_heap_caps.h'] = '''#pragma once
#include <cstdlib>
enum {MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2, MALLOC_CAP_INTERNAL=4};
extern bool fail_alloc, fail_large;
extern int heap_live;
extern bool saw_internal_stack;
extern int allocation_attempts, fail_at;
inline void *heap_caps_malloc(size_t n, unsigned caps) {
 if (++allocation_attempts == fail_at) return nullptr;
 if (fail_alloc) return nullptr;
 if (fail_large && n >= 128u * 1024u) return nullptr;
 if ((caps & MALLOC_CAP_INTERNAL) && n == 4096) saw_internal_stack = true;
 void *p = std::malloc(n);
 if (p) ++heap_live;
 return p;
}
inline void heap_caps_free(void *p) { if (!p) return; --heap_live; ::free(p); }
'''
HEADERS['freertos/FreeRTOS.h'] = '''#pragma once
#include <cstdint>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef uint8_t StackType_t;
typedef struct { void *opaque[32]; } StaticTask_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) (ms)
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
'''
HEADERS['freertos/task.h'] = '''#pragma once
#include "freertos/FreeRTOS.h"
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
inline BaseType_t xPortGetCoreID(void) {return 1;}
TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t fn, const char *name, uint32_t depth,
 void *param, UBaseType_t prio, StackType_t *stack, StaticTask_t *tcb, BaseType_t core);
'''
HARNESS = r'''
#define SMARTVOICEBOX_IDF_RUNTIME_H
#define SDCAR_H
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "Storage.h"
#include "AiConfig.h"
#include "AiSpeech.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
bool fail_alloc=false, fail_large=false, saw_internal_stack=false;
int heap_live=0;
int allocation_attempts=0,fail_at=0;
static wifi_ps_type_t wifi_ps_mode=WIFI_PS_NONE;
static int wifi_get_error=0, wifi_set_error=0, wifi_restore_error=0, wifi_get_calls=0, wifi_set_calls=0;
static int wifi_restore_transient=0;
static int wifi_ps_at_prepare=-1, wifi_ps_at_read=-1, wifi_ps_at_i2s=-1, wifi_sets_at_i2s=-1;
static bool sample_on_dir=false, sample_on_flush=false, sample_on_begin=false, flush_sampled=false;
static uint32_t stall_in_flush_ms=0;
static ai_speech_status_t status_at_dir{}, status_flush_before{}, status_flush_after{}, status_at_begin{};
static std::atomic<uint32_t> now{0};
static std::atomic<bool> sound_active{false};
static std::atomic<unsigned> reads_without_wait{0};
static bool fail_header,fail_auth_header;
static bool begin_deadline_stall;
static bool cancel_read,cancel_play;
extern "C" void ai_speech_cancel(void);
extern "C" bool ai_speech_clear_cache(void);
static uint32_t app_millis(){return now.load();}
static void app_delay_ms(uint32_t n){now.fetch_add(n); if(n) std::this_thread::sleep_for(std::chrono::milliseconds(n>5?1:n));}
static std::string log_capture;
static int test_printf(const char *fmt, ...) {
 char buf[768]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
 log_capture += buf; fputs(buf, stdout); fflush(stdout); return (int)strlen(buf);
}
#define printf test_printf
struct TaskNote {
 std::mutex mu; std::condition_variable cv; unsigned signals=0; bool alive=true; std::thread thread;
};
static std::mutex notes_mu, start_mu;
static std::condition_variable start_cv;
static bool workers_go=false;
static std::unordered_map<std::thread::id, TaskNote*> notes;
static std::atomic<int> live_tasks{0};
extern bool fail_task;
static void release_workers(){std::lock_guard<std::mutex> lk(start_mu); workers_go=true; start_cv.notify_all();}
static TaskNote *current_note(){
 std::lock_guard<std::mutex> g(notes_mu);
 auto id=std::this_thread::get_id();
 auto it=notes.find(id);
 if(it!=notes.end()) return it->second;
 auto *n=new TaskNote; notes[id]=n; return n;
}
TaskHandle_t xTaskGetCurrentTaskHandle(void){return current_note();}
void xTaskNotifyGive(TaskHandle_t h){auto *n=(TaskNote*)h; if(!n) return; std::lock_guard<std::mutex> lk(n->mu); ++n->signals; n->cv.notify_all();}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks){
 reads_without_wait=0;
 release_workers();
 auto *n=current_note();
 std::unique_lock<std::mutex> lk(n->mu);
 auto ready=[&]{return n->signals || !n->alive;};
 if(ticks==portMAX_DELAY) n->cv.wait(lk, ready);
 else n->cv.wait_for(lk, std::chrono::milliseconds(2), ready);
 uint32_t c=n->signals;
 if(clear && n->alive) n->signals=0;
 if(!c && n->alive && ticks!=portMAX_DELAY) now.fetch_add(ticks);
 return c;
}
void vTaskDelay(TickType_t ticks){
 auto *n=current_note();
 std::unique_lock<std::mutex> lk(n->mu);
 if(ticks==portMAX_DELAY) n->cv.wait(lk, [&]{return !n->alive;});
 else n->cv.wait_for(lk, std::chrono::milliseconds(ticks>2?2:ticks), [&]{return !n->alive;});
}
void vTaskDelete(TaskHandle_t task){
 auto *n=(TaskNote*)task; if(!n) return;
 assert(!sound_active.load()); /* Never terminate the owner before I2S cleanup. */
 release_workers();
 {std::lock_guard<std::mutex> lk(n->mu); n->alive=false;}
 n->cv.notify_all();
 if(n->thread.joinable() && n->thread.get_id()!=std::this_thread::get_id()) n->thread.join();
 {std::lock_guard<std::mutex> g(notes_mu); for(auto it=notes.begin(); it!=notes.end();){ if(it->second==n) it=notes.erase(it); else ++it; }}
 delete n;
 if(live_tasks.load()>0) live_tasks.fetch_sub(1);
}
TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t fn, const char *name, uint32_t depth, void *param, UBaseType_t, StackType_t *stack, StaticTask_t *tcb, BaseType_t core){
 assert(fn && name && stack && tcb && depth==4096);
 assert(core==1); /* Forward the caller's core, rather than hardcoding core 0. */
 if(fail_task) return nullptr;
 auto *n=new TaskNote;
 live_tasks.fetch_add(1);
 n->thread=std::thread([=]{
  {std::lock_guard<std::mutex> g(notes_mu); notes[std::this_thread::get_id()]=n;}
  {std::unique_lock<std::mutex> lk(start_mu); start_cv.wait(lk, [&]{return workers_go || !n->alive;});}
  if(n->alive) fn(param);
 });
 return n;
}
static size_t strlcpy(char*d,const char*s,size_t n){size_t k=strlen(s);if(n){size_t c=std::min(k,n-1);memcpy(d,s,c);d[c]=0;}return k;}
static void app_log_memory(const char*){}
static std::string response, sent, auth, content_type, url;
static std::atomic<size_t> pos{0};
static size_t read_limit=7;
static int http_delay_ms=0, header_delay_ms=0, play_time_add=0, read_request_max=0;
static bool incomplete, eagain, fail_open, fail_audio, fail_begin, fail_finish, flip_voice, fail_client;
static size_t stall_at=131072;
static int status=200, initialized, cleaned, audio_finish, begin_calls, eagain_every, stalls_left, cut_at=-1, read_calls;
static int64_t declared=-2;
static std::vector<int16_t> played;
struct Client {esp_http_client_config_t config;std::string authorization,mime;bool opened=false;};
Client *esp_http_client_init(const esp_http_client_config_t*c){
 assert(c->addr_type==HTTP_ADDR_TYPE_INET&&c->crt_bundle_attach&&c->disable_auto_redirect);
 if(fail_client)return nullptr;
 ++initialized;url=c->url;
 if(c->url && std::strstr(c->url,"/tools/tts")){
  assert(c->buffer_size==4096 && c->buffer_size_tx==1024);
 }
 return new Client{*c};
}
int esp_http_client_set_header(Client*c,const char*k,const char*v){
 assert(!c->opened);if(fail_header||(fail_auth_header&&!strcmp(k,"Authorization")))return 1;
 if(!strcmp(k,"Authorization")){auth=v;c->authorization=v;}
 if(!strcmp(k,"Content-Type"))c->mime=v;
 return 0;
}
int esp_http_client_open(Client*c,int){
 // Independent per-client contract: stale chat headers cannot authenticate TTS.
 assert(c->authorization.rfind("Bearer ",0)==0&&c->authorization.size()>8);
 assert(c->mime=="application/json");c->opened=true;
 return fail_open?1:0;
}
int esp_http_client_write(Client*,const char*s,int n){int k=std::min(n,11);sent.append(s,k);return k;}
int64_t esp_http_client_fetch_headers(Client*c){
 if(header_delay_ms) now.fetch_add((uint32_t)header_delay_ms);
 if(c->config.event_handler){char key[]="Content-Type";esp_http_client_event_t e={HTTP_EVENT_ON_HEADER,c->config.user_data,key,content_type.data()};c->config.event_handler(&e);}
 return declared==-2?(int64_t)response.size():declared;
}
int esp_http_client_get_status_code(Client*){return status;}
int esp_http_client_set_timeout_ms(Client*,int){return 0;}
int esp_http_client_read(Client*,char*d,int n){
 if(url.find("/tools/tts")!=std::string::npos){
  assert(!ai_speech_clear_cache());
  if(wifi_ps_at_read<0) wifi_ps_at_read=(int)wifi_ps_mode;
  assert(n>0 && n<=4096);
  if(n>read_request_max) read_request_max=n;
  if(http_delay_ms) now.fetch_add((uint32_t)http_delay_ms);
 }
 if(cancel_read&&pos.load()>=40000){ai_speech_cancel();cancel_read=false;}
 if(eagain){now.fetch_add(1000);return -ESP_ERR_HTTP_EAGAIN;}
 if(cut_at>=0 && (int)pos.load()>=cut_at) return -1;
 if(stalls_left>0 && pos.load()>=stall_at){--stalls_left; release_workers(); std::this_thread::sleep_for(std::chrono::milliseconds(200)); now.fetch_add(200);}
 if(eagain_every && (++read_calls % eagain_every)==0){now.fetch_add(25); return -ESP_ERR_HTTP_EAGAIN;}
 size_t at=pos.load();
 size_t k=std::min({(size_t)n,response.size()-at,read_limit});memcpy(d,response.data()+at,k);pos.store(at+k);return (int)k;
}
bool esp_http_client_is_complete_data_received(Client*){return pos.load()==response.size()&&!incomplete;}
int esp_http_client_close(Client*){return 0;}
int esp_http_client_cleanup(Client*c){++cleaned;delete c;return 0;}
static std::thread::id main_id, audio_id;
static const char *current_voice="stella";
static int voice_gets=0;
extern "C" const char *ai_voice_get_id(void){++voice_gets; return current_voice;}
extern "C" bool sound_ai_output_begin_rate(uint32_t rate){
 assert(rate==24000); assert(std::this_thread::get_id()!=main_id);
 if(!begin_calls) audio_id=std::this_thread::get_id(); else assert(std::this_thread::get_id()==audio_id);
 ++begin_calls; if(flip_voice) current_voice="tide";
 assert(log_capture.find("cache_pace ")!=std::string::npos);
 if(wifi_ps_at_i2s<0){wifi_ps_at_i2s=(int)wifi_ps_mode; wifi_sets_at_i2s=wifi_set_calls;}
 if(play_time_add){now.fetch_add((uint32_t)play_time_add); play_time_add=0;}
 if(sample_on_begin) ai_speech_get_status(&status_at_begin);
 size_t at=pos.load(); assert(at==response.size() && initialized==cleaned);
 if(!fail_begin) sound_active=true;
 if(begin_deadline_stall){now.fetch_add(606000);std::this_thread::sleep_for(std::chrono::milliseconds(20));}
 return !fail_begin;
}
extern "C" bool sound_ai_output_write(int16_t*p,size_t n){
 assert(std::this_thread::get_id()==audio_id); assert(begin_calls==1); assert(n%4==0);
 played.insert(played.end(),p,p+n/2); if(cancel_play&&played.size()>4096)ai_speech_cancel();return !fail_audio;
}
extern "C" bool sound_ai_output_finish(bool ok){
 assert(std::this_thread::get_id()==audio_id); assert(begin_calls==1); ++audio_finish; sound_active=false; return ok && !fail_finish;
}
static bool sd=true, import_exists=false, fail_commit=false, removed=false;
static std::string import_json;
static std::vector<char> committed, pending;
extern "C" bool sdcar_is_ready(){return sd;}

int esp_wifi_get_ps(wifi_ps_type_t *type){++wifi_get_calls; if(wifi_get_error) return wifi_get_error; *type=wifi_ps_mode; return 0;}
int esp_wifi_set_ps(wifi_ps_type_t type){
 ++wifi_set_calls;
 if(type==WIFI_PS_NONE){ if(wifi_set_error) return wifi_set_error; }
 else if(wifi_restore_transient>0){--wifi_restore_transient;return 11;}
 else if(wifi_restore_error) return wifi_restore_error;
 wifi_ps_mode=type; return 0;
}
static std::string cached_pcm;
static std::string committed_pcm;
static size_t cache_read_pos;
static bool cache_exists,fail_cache_write,fail_cache_flush,fail_cache_open,fail_cache_read,corrupt_cache,fail_cache_reopen,cache_dir=true,fail_cache_short=false;
static int writes_before_fail=-1, flush_delay_ms=0, sd_delay_ms=0;
static std::vector<size_t> cache_write_sizes, cache_read_sizes;
static unsigned cache_removes;
extern "C" bool storage_is_file(const char*p){assert(!strcmp(p,"/config/ai.json"));return import_exists;}
extern "C" bool storage_is_dir(const char*p){assert(!strcmp(p,"/.cache")); if(wifi_ps_at_prepare<0) wifi_ps_at_prepare=(int)wifi_ps_mode; if(sample_on_dir){sample_on_dir=false; ai_speech_get_status(&status_at_dir);} return cache_dir;}
extern "C" bool storage_mkdir(const char*p){assert(!strcmp(p,"/.cache"));return cache_dir=true;}
extern "C" bool storage_exists(const char*p){assert(!strcmp(p,"/.cache/breeze-ai-speech.pcm"));return cache_exists;}
extern "C" bool storage_remove(const char*p){
 if(!strcmp(p,"/config/ai.json")){removed=true;import_exists=false;return true;}
 assert(!strcmp(p,"/.cache/breeze-ai-speech.pcm"));++cache_removes;cache_exists=false;cached_pcm.clear();return true;
}
StorageStream::~StorageStream(){close();}
bool StorageStream::open(const char*p,const char*mode){
 close();
 if(!strcmp(p,"/config/ai.json")){if(import_exists)file_=(FILE*)1;return import_exists;}
 assert(!strcmp(p,"/.cache/breeze-ai-speech.pcm"));
 if(!strcmp(mode,"wb")){
  if(fail_cache_open)return false;cached_pcm.clear();cache_exists=true;file_=(FILE*)2;
 }else{
  assert(!strcmp(mode,"rb"));if(fail_cache_reopen||!cache_exists)return false;file_=(FILE*)3;cache_read_pos=0;committed_pcm=cached_pcm;
 }return true;
}
void StorageStream::close(){file_=nullptr;}
uint64_t StorageStream::byte_count()const{
 if(file_==(FILE*)1)return import_json.size();assert(file_==(FILE*)3);
 return corrupt_cache?cached_pcm.size()-2:cached_pcm.size();
}
size_t StorageStream::read(void*d,size_t n){
 if(file_==(FILE*)1){memcpy(d,import_json.data(),n);return n;}
 assert(file_==(FILE*)3);
 if(fail_cache_read)return 0;
 assert(n<=4096);
 n=std::min(n,cached_pcm.size()-cache_read_pos);cache_read_sizes.push_back(n);memcpy(d,cached_pcm.data()+cache_read_pos,n);cache_read_pos+=n;return n;
}
size_t StorageStream::write(const void*d,size_t n){
 assert(file_==(FILE*)2);assert(n>0 && n<=16384);
 if(sd_delay_ms) now.fetch_add((uint32_t)sd_delay_ms);
 cache_write_sizes.push_back(n);
 if(fail_cache_write || writes_before_fail==0) return 0;
 if(writes_before_fail>0) --writes_before_fail;
 if(fail_cache_short) return n-1;
 cached_pcm.append((const char*)d,n);return n;
}
bool StorageStream::flush(){
 assert(file_==(FILE*)2);
 if(sample_on_flush && !flush_sampled){
  flush_sampled=true; ai_speech_get_status(&status_flush_before);
  if(stall_in_flush_ms) now.fetch_add(stall_in_flush_ms);
  ai_speech_get_status(&status_flush_after);
 }
 if(flush_delay_ms) now.fetch_add((uint32_t)flush_delay_ms); return !fail_cache_flush;
}
int nvs_open(const char*n,int,int*h){assert(!strcmp(n,"ai_direct"));*h=1;return 0;}
int nvs_get_blob(int,const char*,void*d,size_t*n){if(committed.empty())return 1;assert(*n>=committed.size());*n=committed.size();memcpy(d,committed.data(),*n);return 0;}
int nvs_set_blob(int,const char*,const void*d,size_t n){pending.assign((const char*)d,(const char*)d+n);return 0;}
int nvs_commit(int){if(fail_commit)return 1;committed=pending;return 0;}
void nvs_close(int){}
#define free heap_caps_free
#include "@ROOT@/src/AiConfig.cpp"
#include "@ROOT@/src/AiDirectClient.cpp"
#include "@ROOT@/src/AiSpeechBuffer.h"
static size_t audited_ring_read(AiSpeechRing *r,uint8_t *p,size_t n){
 size_t got=ai_speech_ring_read(r,p,n);
 if(!got)assert(++reads_without_wait<4);else reads_without_wait=0;
 return got;
}
#define ai_speech_ring_read audited_ring_read
#include "@ROOT@/src/AiSpeech.cpp"
#undef ai_speech_ring_read
#include "@ROOT@/src/AiSpeechBuffer.cpp"
bool fail_task=false;
static void reset(){
 assert(!sound_active.load());reads_without_wait=0;fail_auth_header=fail_header=cancel_play=cancel_read=begin_deadline_stall=fail_client=false;
 allocation_attempts=fail_at=0;stall_at=131072;
 assert(!cache_exists);cache_read_pos=cache_removes=0;cached_pcm.clear();committed_pcm.clear();cache_write_sizes.clear();cache_read_sizes.clear();cache_dir=sd=true;
 fail_cache_write=fail_cache_flush=fail_cache_open=fail_cache_read=corrupt_cache=fail_cache_reopen=fail_cache_short=false;
 writes_before_fail=-1;flush_delay_ms=sd_delay_ms=0;
 now.store(0);pos.store(0);read_limit=7;http_delay_ms=header_delay_ms=play_time_add=read_request_max=0;incomplete=eagain=fail_open=fail_audio=fail_begin=fail_finish=fail_alloc=fail_large=fail_task=false;
 status=200;declared=-2;initialized=cleaned=audio_finish=begin_calls=0;sent.clear();auth.clear();played.clear();
 eagain_every=stalls_left=read_calls=0; cut_at=-1; workers_go=false; log_capture.clear();
 wifi_ps_mode=WIFI_PS_NONE; wifi_get_error=wifi_set_error=wifi_restore_error=wifi_get_calls=wifi_set_calls=wifi_restore_transient=0;
 wifi_ps_at_prepare=wifi_ps_at_read=wifi_ps_at_i2s=wifi_sets_at_i2s=-1;
 sample_on_dir=sample_on_flush=sample_on_begin=flush_sampled=false; stall_in_flush_ms=0;
 status_at_dir=status_flush_before=status_flush_after=status_at_begin={};
 response=R"({"choices":[{"message":{"content":"你好，直连成功"}}]})";
 content_type="audio/pcm; rate=24000; bits=16; channels=1";
}
static int log_field(const char *name){
 auto at=log_capture.rfind(name); assert(at!=std::string::npos); return std::atoi(log_capture.c_str()+at+std::strlen(name));
}
static std::vector<int16_t> mono_played(){
 std::vector<int16_t> out;
 for(size_t i=0;i+1<played.size();i+=2){assert(played[i]==played[i+1]); if(played[i]) out.push_back(played[i]);}
 return out;
}
int main(){
 setvbuf(stdout, nullptr, _IONBF, 0); setvbuf(stderr, nullptr, _IONBF, 0);
 main_id=std::this_thread::get_id();
 ai_api_config_t cfg={};strcpy(cfg.api_base,"https://api.deepseek.com/v1/");strcpy(cfg.api_key,"test-key-not-real");strcpy(cfg.model,"deepseek-flash");cfg.speak=true;
 char reply[128],error[128];
 cache_exists=true;cached_pcm="stale";assert(ai_speech_clear_cache()&&!cache_exists&&cached_pcm.empty());
 auto chat=[&](){return ai_direct_chat(&cfg,"你好",reply,sizeof(reply),error,sizeof(error));};
 reset();declared=0;assert(chat()&&!strcmp(reply,"你好，直连成功")&&initialized==cleaned);
 assert(url=="https://api.deepseek.com/v1/chat/completions"&&auth=="Bearer test-key-not-real");
 auto root=cJSON_Parse(sent.c_str());assert(root);
 assert(!strcmp(cJSON_GetObjectItem(root,"model")->valuestring,"deepseek-flash"));
 assert(cJSON_GetObjectItem(root,"max_tokens")->valueint==1024&&cJSON_IsFalse(cJSON_GetObjectItem(root,"stream")));
 assert(!strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(root,"thinking"),"type")->valuestring,"disabled"));
 assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(root,"messages"),1),"content")->valuestring,"你好"));cJSON_Delete(root);
 reset();strcpy(cfg.api_base,"https://example.com/v1");assert(chat()&&sent.find("thinking")==std::string::npos);strcpy(cfg.api_base,"https://api.deepseek.com");
 for(auto bad:{"http://api.deepseek.com","https://evil@api.deepseek.com","https:///bad","https://api.deepseek.com?q=x","https://api.deepseek.com\r\n"}){
  reset();strcpy(cfg.api_base,bad);assert(!chat()&&initialized==0);
 }strcpy(cfg.api_base,"https://api.deepseek.com");
 for(int code:{400,401,402,403,404,429,500,503}){reset();status=code;response=cfg.api_key;assert(!chat()&&error[0]&&!strstr(error,cfg.api_key)&&!reply[0]&&initialized==cleaned);}
 for(auto bad:{"", "not JSON", "{}", "{\"choices\":[]}","{\"choices\":[{\"message\":{\"content\":\"\"}}]}"}){reset();response=bad;assert(!chat());}
 reset();response+="garbage";assert(!chat());
 reset();response=" \n\r"+response+" \n";assert(chat());
 reset();incomplete=true;assert(!chat());reset();eagain=true;assert(!chat()&&now.load()>=60000&&now.load()<125000);
 reset();declared=30000;assert(!chat());reset();declared=0;response.assign(17000,' ');assert(!chat());
 reset();fail_open=true;assert(!chat()&&cleaned==1);reset();fail_alloc=true;assert(!chat()&&initialized==0);
 reset();char short_reply[5];assert(ai_direct_chat(&cfg,"x",short_reply,sizeof(short_reply),error,sizeof(error))&&!strcmp(short_reply,"你"));
 reset();response=R"({"choices":[{"message":{"content":"\ud83d\ude00中文"}}]})";
 char emoji[6];assert(ai_direct_chat(&cfg,"x",emoji,sizeof(emoji),error,sizeof(error))&&strlen(emoji)==4);
 reset();char story[8193];assert(ai_direct_chat_context(&cfg,"继续讲故事","讲一个森林的故事","小鹿走进森林。",story,sizeof(story),error,sizeof(error)));
 root=cJSON_Parse(sent.c_str());assert(root&&cJSON_GetObjectItem(root,"max_tokens")->valueint==2048);
 auto msgs=cJSON_GetObjectItem(root,"messages");assert(cJSON_GetArraySize(msgs)==4);
 assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(msgs,1),"content")->valuestring,"讲一个森林的故事"));
 assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(msgs,2),"role")->valuestring,"assistant"));cJSON_Delete(root);
 reset();response=R"({"choices":[{"finish_reason":"length","message":{"content":"故事仍在继续。"}}]})";
 assert(ai_direct_chat(&cfg,"讲故事",story,sizeof(story),error,sizeof(error))&&strstr(story,"继续")&&strstr(story,"接着讲"));
 reset();response=R"({"choices":[{"message":{"content":"中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容中文故事内容"}}]})";
 assert(ai_direct_chat(&cfg,"讲故事",reply,sizeof(reply),error,sizeof(error))&&strlen(reply)<sizeof(reply)&&strstr(reply,"接着讲"));
 /* Streaming PCM accepts one-byte chunks without dropping sample halves. */
 auto speech=[&](){int heap_before=heap_live; int tasks=live_tasks.load(); bool ok=ai_direct_speak(&cfg,"测试语音",error,sizeof(error));
  assert(live_tasks.load()==tasks && heap_live==heap_before && !cache_exists); ai_speech_status_t state;ai_speech_get_status(&state);assert(state.phase==AI_SPEECH_IDLE && state.cache_elapsed_ms==0 && state.cache_bytes_per_second==0); assert(log_capture.find("Bearer")==std::string::npos);
  assert(log_capture.find(cfg.api_key)==std::string::npos); assert(log_capture.find("测试语音")==std::string::npos);
  assert(log_capture.find("voice=")!=std::string::npos); return ok;};
 reset();response=std::string("\x01\x00\xff\x7f\x00\x80",6);read_limit=1;
 assert(speech()&&played==std::vector<int16_t>({1,1,32767,32767,-32768,-32768})&&audio_finish==1&&begin_calls==1&&saw_internal_stack);
 assert(cache_write_sizes==std::vector<size_t>({6})&&committed_pcm==std::string("\x01\x00\xff\x7f\x00\x80",6)&&cache_read_sizes==std::vector<size_t>({6})&&read_request_max<=4096);
 assert(log_field("peak=")==6&&log_field("refill=")==0&&log_field("ok=")==1);
 root=cJSON_Parse(sent.c_str());assert(root&&url=="https://api.deepseek.com/v1/tools/tts");
 assert(auth=="Bearer test-key-not-real");
 assert(!strcmp(cJSON_GetObjectItem(root,"format")->valuestring,"pcm"));
 assert(!strcmp(cJSON_GetObjectItem(root,"voice_id")->valuestring,"stella"));cJSON_Delete(root);
 reset();response=std::string("\x02\x00",2);assert(speech()&&played==std::vector<int16_t>({2,2})&&audio_finish==1&&begin_calls==1);
 reset();response=std::string("\x00\x00\x01\x00",4);assert(speech()&&played==std::vector<int16_t>({0,0,1,1}));
 reset();response="x";assert(!speech()&&audio_finish==0&&begin_calls==0);
 reset();response=std::string("\x01\x00\x02",3);assert(!speech()&&audio_finish==0&&begin_calls==0);
 reset();response="xx";incomplete=true;assert(!speech()&&audio_finish==0&&begin_calls==0);
 reset();status=401;assert(!speech()&&audio_finish==0&&begin_calls==0);
 reset();content_type="application/json";assert(!speech()&&audio_finish==0);
 for(auto type:{"audio/pcm; rate=16000","audio/pcm; rate=240000","audio/pcm; channels=2","audio/pcm; bits=8","audio/pcm-bad"}){
  reset();content_type=type;assert(!speech()&&audio_finish==0);
 }
 reset();response="xx";fail_begin=true;assert(!speech()&&audio_finish==0&&begin_calls==1);
 reset();response="xx";fail_audio=true;assert(!speech()&&audio_finish==1&&begin_calls==1);
 reset();response="xx";fail_finish=true;assert(!speech()&&audio_finish==1);
 reset();eagain=true;assert(!speech()&&now.load()>=30000&&cleaned==1&&audio_finish==0);
 reset();fail_alloc=true;assert(!speech()&&initialized==0&&heap_live==0);
 for(int at:{1,4,5,6,7}){
  reset();fail_at=at;response="xx";assert(!speech()&&initialized==cleaned&&audio_finish==0);
  if(at==4||at==5)assert(strstr(error,"播放缓冲内存不足")&&log_capture.find("[AI MEMORY]")!=std::string::npos);
  if(at==6||at==7)assert(strstr(error,"播放任务内存不足")&&log_capture.find("[AI MEMORY]")!=std::string::npos);
 }
 reset();response="xx";fail_header=true;assert(!speech()&&initialized==cleaned&&begin_calls==0);
 reset();response="xx";fail_auth_header=true;assert(!speech()&&initialized==cleaned&&begin_calls==0);
 reset();fail_open=true;response="xx";assert(!speech()&&initialized==cleaned&&audio_finish==0);
 reset();fail_client=true;response="xx";assert(!speech()&&initialized==0&&audio_finish==0);
 reset();response="xx";begin_deadline_stall=true;
 assert(!speech()&&audio_finish==1&&!sound_active.load());
 reset();response=std::string("\x03\x00",2);eagain_every=2;read_limit=1;assert(speech()&&played==std::vector<int16_t>({3,3})&&log_field("eagain=")>0);
 {
  std::string bytes; std::vector<int16_t> expect;
  for(int i=0;i<100*1024;++i){int16_t s=(int16_t)((i%1000)+1); expect.push_back(s); bytes.push_back((char)(s&255)); bytes.push_back((char)((s>>8)&255));}
  reset();response=bytes;read_limit=4096;stalls_left=1;
  assert(speech()&&mono_played()==expect&&audio_finish==1&&log_field("peak=")==131072&&log_field("read_max_ms=")>=200);
  assert(read_request_max==4096&&cache_write_sizes.size()==13&&cache_write_sizes.back()==8192&&committed_pcm==bytes);
  assert(std::count(cache_write_sizes.begin(),cache_write_sizes.end()-1,16384)==12);
  assert(std::count(cache_read_sizes.begin(),cache_read_sizes.end(),4096)==50);
  // Stop the source after an odd byte past the startup threshold. Waiting for
  // the next sample byte must yield, even at the consumer's higher priority.
  reset();response=bytes.substr(0,80000);read_limit=4001;stall_at=36009;stalls_left=1;
  assert(speech()&&mono_played()==std::vector<int16_t>(expect.begin(),expect.begin()+40000));
  reset();response=bytes;cancel_play=true;assert(!speech()&&audio_finish==1&&strstr(error,"停止"));
  reset();response=bytes;read_limit=4096;cancel_read=true;assert(!speech()&&strstr(error,"停止")&&!sound_active.load());
  reset();response=bytes;read_limit=4096;fail_audio=true;assert(!speech()&&audio_finish==1&&begin_calls==1&&pos.load()==bytes.size());
  reset();response=bytes;cut_at=150000;assert(!speech()&&audio_finish==0&&begin_calls==0);
  bytes.push_back('\x01'); reset();response=bytes;assert(!speech()&&audio_finish==0);
  reset();fail_large=true;response=bytes.substr(0,80*1024);assert(speech()&&log_field("peak=")==65536);
  reset();fail_task=true;response=std::string("\x01\x00",2);assert(!speech()&&initialized==cleaned&&audio_finish==0&&live_tasks.load()==0);
  reset();response=std::string("\x01\x00",2);assert(speech()&&audio_finish==1);
  int gets=voice_gets; current_voice="stella"; flip_voice=true; reset();response=std::string("\x04\x00",2);
  assert(speech()&&voice_gets==gets+1);
  root=cJSON_Parse(sent.c_str());assert(root&&!strcmp(cJSON_GetObjectItem(root,"voice_id")->valuestring,"stella"));cJSON_Delete(root);
  assert(!strcmp(current_voice,"tide")); reset();response=std::string("\x05\x00",2);
  assert(speech()&&voice_gets==gets+2);
  root=cJSON_Parse(sent.c_str());assert(root&&!strcmp(cJSON_GetObjectItem(root,"voice_id")->valuestring,"tide"));cJSON_Delete(root);
  flip_voice=false;
 }
 reset();cache_exists=true;cached_pcm="previous request";response="xx";assert(speech()&&cache_removes==2);
 reset();sd=false;response="xx";assert(!speech()&&initialized==0&&begin_calls==0&&strstr(error,"SD"));
 for(int fault=0;fault<5;fault++){
  reset();response="xx";fail_cache_open=fault==0;fail_cache_write=fault==1;fail_cache_flush=fault==2;corrupt_cache=fault==3;fail_cache_reopen=fault==4;
  assert(!speech()&&begin_calls==0);
 }
 reset();response="xx";fail_cache_read=true;assert(!speech()&&begin_calls==0);
 reset();response="xx";declared=24u*1024u*1024u+2;assert(!speech()&&begin_calls==0);
 reset();response="xx";declared=4;assert(!speech()&&begin_calls==0);
 reset();response="xx";declared=0;assert(speech()&&audio_finish==1);
 reset();response=std::string(24u*1024u*1024u+2,'x');read_limit=4096;declared=0;assert(!speech()&&begin_calls==0);
 assert(heap_live==0&&live_tasks.load()==0);
 /* A failed replacement must retain the previously committed configuration. */
 ready=false;ai_config_init();assert(!ai_config_is_ready());
 import_json=R"({"version":1,"api_base":"https://api.deepseek.com","api_key":"test-key-not-real","model":"deepseek-flash"})";
 import_exists=true;ai_config_init();assert(ai_config_get(&cfg)&&cfg.speak&&removed&&!import_exists&&!committed.empty());
 auto original=committed;ready=false;ai_config_init();assert(ai_config_get(&cfg));
 removed=false;import_exists=true;import_json=R"({"version":1,"api_base":"http://bad","api_key":"test-key-not-real","model":"deepseek-flash"})";
 ai_config_init();assert(ai_config_get(&cfg)&&committed==original&&!removed&&import_exists);
 import_json=R"({"version":1,"api_base":"https://api.deepseek.com","api_key":"replacement-key","model":"deepseek-flash","speak":false})";
 fail_commit=true;ai_config_init();assert(ai_config_get(&cfg)&&!strcmp(cfg.api_key,"test-key-not-real")&&committed==original&&!removed);
 fail_commit=false;ai_config_init();assert(ai_config_get(&cfg)&&!cfg.speak&&!strcmp(cfg.api_key,"replacement-key")&&removed);
 puts("direct AI: actual JSON/HTTPS contract, HTTP/timeout/OOM/UTF-8, complete SD cache before I2S / cleanup, NVS import preservation passed");
}
'''


def main():
    compiler = shutil.which('cl')
    if not compiler:
        raise SystemExit('Use a Visual Studio C++ developer terminal')
    with tempfile.TemporaryDirectory(prefix='ai-direct-', ignore_cleanup_errors=True) as tmp:
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
                        f'/I{sdk}', str(unit), str(sdk / 'cJSON.c'), f'/Fe:{exe}', f'/Fo{directory}\\'], check=True)
        result = subprocess.run([str(exe)], check=False, timeout=90, capture_output=True, text=True)
        print(result.stdout)
        print(result.stderr)
        if result.returncode:
            raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
