from pathlib import Path
import subprocess,tempfile,shutil
ROOT=Path(__file__).resolve().parents[1]
SDK=Path('C:/Espressif/frameworks/esp-idf-v5.5.6/components/mbedtls/mbedtls')
HEADERS={
'compat.h':'''#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
extern uint32_t app_millis();extern void app_delay_ms(uint32_t);
inline size_t strlcpy(char*d,const char*s,size_t n){size_t k=strlen(s);if(n){size_t c=std::min(k,n-1);memcpy(d,s,c);d[c]=0;}return k;}
#define SMARTVOICEBOX_IDF_RUNTIME_H
''',
'freertos/FreeRTOS.h':'''#pragma once
#include <mutex>
typedef std::mutex portMUX_TYPE;
typedef unsigned TickType_t;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) ((p)->lock())
#define portEXIT_CRITICAL(p) ((p)->unlock())
#define pdPASS 1
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
''',
'freertos/task.h':'''#pragma once
#include <cstdint>
extern bool fail_task;extern void(*task_fn)(void*);extern void*task_arg;
struct TaskDone{};
inline int xTaskCreatePinnedToCore(void(*f)(void*),const char*,unsigned,void*a,int,void*,int){if(fail_task)return 0;task_fn=f;task_arg=a;return 1;}
inline void vTaskDelete(void*){throw TaskDone{};}
''',
'freertos/semphr.h':'''#pragma once
#include <mutex>
#include <chrono>
typedef std::timed_mutex* SemaphoreHandle_t;
extern void(*sem_taken_hook)(SemaphoreHandle_t,int);
extern void(*sem_before_hook)(SemaphoreHandle_t,int);
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return new std::timed_mutex;}
inline int xSemaphoreTake(SemaphoreHandle_t p,int ticks){if(sem_before_hook)sem_before_hook(p,ticks);bool ok=ticks?p->try_lock_for(std::chrono::milliseconds(ticks)):p->try_lock();if(ok&&sem_taken_hook)sem_taken_hook(p,ticks);return ok;}
inline void xSemaphoreGive(SemaphoreHandle_t p){p->unlock();}
''',
'esp_app_desc.h':'''#pragma once
#include <cstdint>
struct esp_app_desc_t{uint32_t magic_word,secure_version,reserv1[2];char version[32],project_name[32],time[16],date[16],idf_ver[32];uint8_t app_elf_sha256[32];};
extern const esp_app_desc_t*esp_app_get_description();
''',
'esp_random.h':'#pragma once\n#include <cstdint>\ninline uint32_t esp_random(){static uint32_t n=12345;return ++n;}\n',
'esp_heap_caps.h':'''#pragma once
#include <cstdlib>
enum{MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2};extern bool fail_alloc;
inline void*heap_caps_malloc(size_t n,int){return fail_alloc?nullptr:malloc(n);}
''',
'esp_ota_ops.h':'''#pragma once
#include <cstdint>
#include <cstddef>
typedef int esp_err_t;typedef unsigned esp_ota_handle_t;typedef int esp_ota_img_states_t;
enum{ESP_OK=0,ESP_FAIL=1,ESP_ERR_NO_MEM=2,ESP_OTA_IMG_PENDING_VERIFY=1};
struct esp_partition_t{uint32_t size;};
const esp_partition_t*esp_ota_get_next_update_partition(const void*);
const esp_partition_t*esp_ota_get_running_partition();
esp_err_t esp_ota_begin(const esp_partition_t*,size_t,esp_ota_handle_t*);
esp_err_t esp_ota_write(esp_ota_handle_t,const void*,size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
esp_err_t esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t*);
esp_err_t esp_ota_mark_app_valid_cancel_rollback();
const char*esp_err_to_name(int);
''',
'esp_system.h':'#pragma once\nvoid esp_restart();\n',
'mbedtls/mbedtls_config.h':'#define MBEDTLS_SHA256_C\n',
}
HARNESS=r'''#include "compat.h"
#include "Storage.h"
#include "FirmwareImage.h"
#include "SystemUpdate.h"
#include "FileTransfer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include <vector>
#include <filesystem>
#include <fstream>
#include <cassert>
#include <cstdio>
#include <string>
#include <atomic>
#include <thread>
#include <condition_variable>
void(*sem_taken_hook)(SemaphoreHandle_t,int)=nullptr;
void(*sem_before_hook)(SemaphoreHandle_t,int)=nullptr;
std::atomic<uint32_t> now{1000};uint32_t app_millis(){return now;}void app_delay_ms(uint32_t n){now+=n;}
bool fail_task,fail_alloc;void(*task_fn)(void*);void*task_arg;
static bool fail_begin,fail_write,fail_end,fail_boot,pending_boot,video_busy,ai_busy,saving;
static unsigned aborts,boots,marks,stops,writes;static esp_app_desc_t app;
static esp_partition_t partition{4*1024*1024};static std::vector<uint8_t>flashed;
const esp_app_desc_t*esp_app_get_description(){return &app;}
const esp_partition_t*esp_ota_get_next_update_partition(const void*){return &partition;}
const esp_partition_t*esp_ota_get_running_partition(){return &partition;}
int esp_ota_begin(const esp_partition_t*,size_t,esp_ota_handle_t*h){if(fail_begin)return ESP_FAIL;*h=1;flashed.clear();return ESP_OK;}
int esp_ota_write(esp_ota_handle_t,const void*p,size_t n){++writes;if(fail_write)return ESP_FAIL;auto*b=(const uint8_t*)p;flashed.insert(flashed.end(),b,b+n);return ESP_OK;}
int esp_ota_end(esp_ota_handle_t){return fail_end?ESP_FAIL:ESP_OK;}
int esp_ota_abort(esp_ota_handle_t){++aborts;return ESP_OK;}
int esp_ota_set_boot_partition(const esp_partition_t*){if(fail_boot)return ESP_FAIL;++boots;return ESP_OK;}
int esp_ota_get_state_partition(const esp_partition_t*,int*s){*s=pending_boot?ESP_OTA_IMG_PENDING_VERIFY:0;return ESP_OK;}
int esp_ota_mark_app_valid_cancel_rollback(){++marks;return ESP_OK;}
const char*esp_err_to_name(int){return "fault";}
struct Reboot{};void esp_restart(){throw Reboot{};}
extern "C" bool online_music_is_preparing(){return saving;}
extern "C" bool sound_ai_audio_is_playing(){return ai_busy;}
extern "C" bool video_audio_is_active(){return video_busy;}
extern "C" void music_audio_stop(){++stops;}
extern "C" void mbedtls_platform_zeroize(void*p,size_t n){volatile unsigned char*b=(volatile unsigned char*)p;while(n--)*b++=0;}
static void run(){auto f=task_fn;auto a=task_arg;task_fn=nullptr;assert(f);try{f(a);}catch(TaskDone&){} }
static system_update_status_t state(){system_update_status_t s;system_update_get_status(&s);return s;}
static void put(const char*path,const std::vector<uint8_t>&data){StorageStream f;assert(f.open(path,"wb")&&f.write(data.data(),data.size())==data.size()&&f.flush());}
static std::vector<uint8_t>read(const char*p){StorageStream f;assert(f.open(p,"rb"));std::vector<uint8_t>b(f.byte_count());assert(f.read(b.data(),b.size())==b.size());return b;}
static std::string upload(const char*kind,const char*name,const std::vector<uint8_t>&data,bool success=true){char token[24],error[192]={},path[96];assert(file_transfer_begin(kind,name,data.size(),token,sizeof(token),error,sizeof(error)));for(size_t i=0;i<data.size();i+=4093){assert(file_transfer_append(token,i,data.data()+i,std::min<size_t>(4093,data.size()-i),error,sizeof(error)));}assert(file_transfer_finish(token,path,sizeof(path),error,sizeof(error))==success);return success?path:"";}

static std::mutex hook_mutex;static std::condition_variable hook_cv;
static bool hook_entered,hook_release;static int hook_wait;
static void hold_lock(SemaphoreHandle_t,int wait){
 if(wait!=hook_wait)return;std::unique_lock<std::mutex> lock(hook_mutex);
 hook_entered=true;hook_cv.notify_all();hook_cv.wait(lock,[]{return hook_release;});
}
static void wait_hook(){std::unique_lock<std::mutex> l(hook_mutex);assert(hook_cv.wait_for(l,std::chrono::seconds(2),[]{return hook_entered;}));}
static void release_hook(){{std::lock_guard<std::mutex> l(hook_mutex);hook_release=true;}hook_cv.notify_all();}
static char refreshed_token[24];
static const uint8_t refreshed_photo[]={4,8,64,0,0,248,224,7,31,0,255,255};
static void refresh_before_poll_lock(SemaphoreHandle_t,int wait){
 if(wait)return;sem_before_hook=nullptr;now+=5;char error[192]={};
 assert(file_transfer_append(refreshed_token,0,refreshed_photo,sizeof(refreshed_photo),error,sizeof(error)));
}
static void contention_test(){
 const uint8_t image[]={4,8,64,0,0,248,224,7,31,0,255,255};char token[24],error[192]={},path[96];
 assert(file_transfer_begin("photo","contention.bin",sizeof(image),token,sizeof(token),error,sizeof(error)));
 now+=1001;hook_wait=0;hook_entered=hook_release=false;sem_taken_hook=hold_lock;
 std::thread poller([]{file_transfer_poll();});wait_hook();
 std::atomic<bool> done{false},attempting{false};bool accepted=false;
 std::thread writer([&]{attempting=true;accepted=file_transfer_append(token,0,image,sizeof(image),error,sizeof(error));done=true;});
 while(!attempting.load())std::this_thread::yield();
 std::this_thread::sleep_for(std::chrono::milliseconds(25));const bool waited=!done.load();release_hook();poller.join();writer.join();sem_taken_hook=nullptr;
 assert(waited&&accepted);assert(file_transfer_finish(token,path,sizeof(path),error,sizeof(error)));
 assert(file_transfer_begin("photo","poll-skip.bin",sizeof(image),token,sizeof(token),error,sizeof(error)));
 hook_wait=1000;hook_entered=hook_release=false;sem_taken_hook=hold_lock;
 std::thread writing([&]{accepted=file_transfer_append(token,0,image,sizeof(image),error,sizeof(error));});wait_hook();
 now+=1001;auto began=std::chrono::steady_clock::now();file_transfer_poll();auto took=std::chrono::steady_clock::now()-began;
 release_hook();writing.join();sem_taken_hook=nullptr;assert(took<std::chrono::milliseconds(50)&&accepted);
 file_transfer_cancel(token);assert(!storage_exists("/.web-upload/file.part"));
 assert(file_transfer_begin("photo","fresh-clock.bin",sizeof(refreshed_photo),refreshed_token,sizeof(refreshed_token),error,sizeof(error)));
 now+=1001;sem_before_hook=refresh_before_poll_lock;file_transfer_poll();sem_before_hook=nullptr;
 assert(file_transfer_finish(refreshed_token,path,sizeof(path),error,sizeof(error)));
 puts("Upload contention: writer waits for polling owner; poll skips a busy writer; cancellation clears the live session");
}
int main(int argc,char**argv){assert(argc==3);std::filesystem::create_directories(argv[1]);storage_host_set_root(argv[1]);strcpy(app.project_name,"esp32_smart_voice_box");strcpy(app.version,"1.6.0");strcpy(app.date,"Nov 14 2026");strcpy(app.time,"00:00:00");system_update_poll(false);file_transfer_poll();
 contention_test();
 std::ifstream f(argv[2],std::ios::binary);std::vector<uint8_t>image((std::istreambuf_iterator<char>(f)),{});assert(image.size()>1000000);put("/fixture.bin",image);
 firmware_image_info_t info;char error[192]={};assert(firmware_image_validate("/fixture.bin",app.project_name,partition.size,&info,error,sizeof(error))&&info.size==image.size());assert(!firmware_image_validate("/fixture.bin","other_project",partition.size,&info,error,sizeof(error)));
 for(int test=0;test<6;++test){auto bad=image;if(test==0)bad[1024]^=1;if(test==1)bad[12]=0;if(test==2)bad[28]=255;if(test==3)bad.pop_back();if(test==4)bad.push_back(0);if(test==5)bad.back()^=1;put("/bad.bin",bad);assert(!firmware_image_validate("/bad.bin",app.project_name,partition.size,&info,error,sizeof(error)));}
 std::vector<uint8_t>photo={4,8,64,0,0,248,224,7,31,0,255,255};
 assert(upload("photo","test.bin",photo)=="/imges/test.bin");char token[24],path[96];assert(!file_transfer_begin("photo","test.bin",photo.size(),token,sizeof(token),error,sizeof(error)));assert(!file_transfer_begin("photo","../evil.bin",10,token,sizeof(token),error,sizeof(error)));
 assert(file_transfer_begin("photo","cancel.bin",photo.size(),token,sizeof(token),error,sizeof(error)));assert(!file_transfer_append("wrong",0,photo.data(),3,error,sizeof(error)));assert(!file_transfer_append(token,1,photo.data(),3,error,sizeof(error)));assert(file_transfer_append(token,0,photo.data(),3,error,sizeof(error)));file_transfer_cancel(token);assert(!storage_exists("/imges/cancel.bin")&&!storage_exists("/.web-upload/file.part"));
 assert(file_transfer_begin("photo","timeout.bin",10,token,sizeof(token),error,sizeof(error)));now+=60001;file_transfer_poll();assert(!file_transfer_append(token,0,photo.data(),3,error,sizeof(error)));
 upload("photo","invalid.bin",std::vector<uint8_t>(12,0),false);
 std::ifstream rvf_file(".tools/media-ota-20261114/js-test/clip.rvf",std::ios::binary),wav_file(".tools/media-ota-20261114/js-test/clip.wav",std::ios::binary);
 std::vector<uint8_t>rvf((std::istreambuf_iterator<char>(rvf_file)),{}),wav((std::istreambuf_iterator<char>(wav_file)),{});
 if(!rvf.empty()){assert(upload("video","clip.rvf",rvf)=="/videos/clip.rvf");assert(upload("soundtrack","clip.wav",wav)=="/videos/clip.wav");rvf[24]=3;upload("video","bad.rvf",rvf,false);}
 assert(upload("firmware","update.bin",image)=="/updates/firmware.bin");assert(read("/updates/firmware.bin")==image);auto bad=image;bad.back()^=1;upload("firmware","bad.bin",bad,false);assert(read("/updates/firmware.bin")==image);
 assert(system_update_check());run();assert(state().available&&!state().busy);
 for(int test=0;test<4;++test){fail_begin=test==0;fail_write=test==1;fail_end=test==2;fail_boot=test==3;assert(system_update_install());run();assert(state().state==-1&&boots==0);fail_begin=fail_write=fail_end=fail_boot=false;assert(system_update_check());run();assert(state().available);}
 saving=true;assert(system_update_install());run();assert(state().state==-1&&boots==0);saving=false;assert(system_update_check());run();assert(state().available);
 assert(system_update_install());bool rebooted=false;try{run();}catch(Reboot&){rebooted=true;}assert(rebooted&&boots==1&&flashed==image&&state().progress==100&&writes>1&&aborts>0);
 pending_boot=true;now=21000;system_update_poll(true);assert(marks==1);system_update_poll(true);assert(marks==1);
 puts("Media/OTA passed: real SHA/segments/corrupt/truncated/wrong-chip, staging/cancel/timeout/no-overwrite, OTA failure isolation/exact image/boot confirmation");
}
'''
def main():
 compiler=shutil.which('cl');assert compiler,'Use VS developer shell'
 with tempfile.TemporaryDirectory(prefix='media-ota-',ignore_cleanup_errors=True) as tmp:
  d=Path(tmp)
  for name,body in HEADERS.items():
   p=d/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(body,encoding='utf-8')
  unit=d/'test.cpp';unit.write_text(HARNESS,encoding='utf-8');exe=d/'test.exe'
  cmd=[compiler,'/nologo','/TP','/std:c++17','/utf-8','/EHsc','/DNOMINMAX','/D_CRT_SECURE_NO_WARNINGS','/DSTORAGE_HOST_TEST',f'/FI{d/"compat.h"}',f'/I{d}',f'/I{ROOT/"src"}',f'/I{SDK/"include"}',f'/I{SDK/"library"}',str(unit),*[str(ROOT/'src'/n) for n in ['FirmwareImage.cpp','FileTransfer.cpp','SystemUpdate.cpp','Storage.cpp','serial_upload_protocol.c']],str(SDK/'library/sha256.c'),f'/Fe:{exe}',f'/Fo{d}\\']
  subprocess.run(cmd,check=True);subprocess.run([str(exe),str(d/'sd'),str(ROOT/'build/esp32_smart_voice_box.bin')],check=True)
if __name__=='__main__':main()
