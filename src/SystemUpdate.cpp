#include "SystemUpdate.h"
#include "FirmwareImage.h"
#include "Storage.h"
#include "Sound.h"
#include "OnlineMusic.h"
#include "IdfRuntime.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <algorithm>
namespace {
constexpr const char *IMAGE="/updates/firmware.bin",*BACKUP="/updates/firmware.prev";
std::atomic_flag file_lock=ATOMIC_FLAG_INIT;
std::atomic<bool> working{false};
portMUX_TYPE status_mux=portMUX_INITIALIZER_UNLOCKED;
system_update_status_t status={};firmware_image_info_t checked={};bool initialized,boot_checked;
void publish(int state,const char *message,unsigned progress=0,bool available=false){
 portENTER_CRITICAL(&status_mux);status.state=state;status.progress=progress;status.available=available;
 strlcpy(status.message,message,sizeof(status.message));portEXIT_CRITICAL(&status_mux);
}
int compare_version(const char*a,const char*b){
 unsigned av[3]={},bv[3]={};
 if(sscanf(a,"%u.%u.%u",&av[0],&av[1],&av[2])!=3||sscanf(b,"%u.%u.%u",&bv[0],&bv[1],&bv[2])!=3)return strcmp(a,b);
 for(int i=0;i<3;i++){if(av[i]!=bv[i])return av[i]>bv[i]?1:-1;}
 return 0;
}
uint64_t build_time(const char*date,const char*time){
 static const char*months="JanFebMarAprMayJunJulAugSepOctNovDec";
 char month[4]={};unsigned day=0,year=0,h=0,m=0,s=0;
 if(sscanf(date,"%3s %u %u",month,&day,&year)!=3||sscanf(time,"%u:%u:%u",&h,&m,&s)!=3)return 0;
 const char*p=strstr(months,month);if(!p)return 0;
 return (((((uint64_t)year*12+(p-months)/3)*32+day)*24+h)*60+m)*60+s;
}
void worker(void *argument){
 const bool install=argument!=nullptr;
 if(file_lock.test_and_set(std::memory_order_acquire)){publish(-1,"固件正在上传，请上传完成后再检查");working=false;vTaskDelete(nullptr);return;}
 auto finish=[&](){file_lock.clear(std::memory_order_release);working=false;};
 const esp_app_desc_t *app=esp_app_get_description();
 const esp_partition_t *target=esp_ota_get_next_update_partition(nullptr);
 firmware_image_info_t image={};char error[192]={};
 bool ok=target&&firmware_image_validate(IMAGE,app->project_name,target->size,&image,error,sizeof(error));
 if(!ok){publish(-1,target?error:"没有可用的 OTA 备用分区");finish();vTaskDelete(nullptr);return;}
 if(!install){
  const int version=compare_version(image.version,app->version);
  bool available=memcmp(image.elf_sha,app->app_elf_sha256,32)&&
   (version>0||(version==0&&build_time(image.date,image.time)>build_time(app->date,app->time)));
  portENTER_CRITICAL(&status_mux);checked=image;strlcpy(status.candidate_version,image.version,sizeof(status.candidate_version));portEXIT_CRITICAL(&status_mux);
  publish(available?2:0,available?"发现新固件，点击安装后将重启设备":version<0?"上传固件版本较旧，已保留当前系统":"当前已是最新上传的固件",0,available);
  finish();vTaskDelete(nullptr);return;
 }
 firmware_image_info_t expected;
 portENTER_CRITICAL(&status_mux);expected=checked;portEXIT_CRITICAL(&status_mux);
 if(memcmp(expected.elf_sha,image.elf_sha,32)||expected.size!=image.size){publish(-1,"固件已变更，请重新检查更新");finish();vTaskDelete(nullptr);return;}
 if(online_music_is_preparing()||sound_ai_audio_is_playing()||video_audio_is_active()){
  publish(-1,"请等待音乐保存、AI 语音或视频播放结束后重试");finish();vTaskDelete(nullptr);return;
 }
 music_audio_stop();app_delay_ms(150);
 StorageStream file;auto *buffer=static_cast<uint8_t*>(heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 esp_ota_handle_t handle=0;
 esp_err_t result=buffer&&file.open(IMAGE,"rb")?esp_ota_begin(target,image.size,&handle):ESP_ERR_NO_MEM;
 uint32_t used=0;publish(3,"正在写入 OTA 备用分区，请保持供电");
 while(result==ESP_OK&&used<image.size){
  size_t n=std::min<size_t>(8192,image.size-used);
  if(file.read(buffer,n)!=n){result=ESP_FAIL;break;}
  result=esp_ota_write(handle,buffer,n);used+=result==ESP_OK?n:0;
  if(result==ESP_OK)publish(3,"正在写入 OTA 备用分区，请保持供电",used*100ULL/image.size);
  app_delay_ms(1);
 }
 file.close();free(buffer);
 if(result==ESP_OK){result=esp_ota_end(handle);handle=0;}
 if(handle)esp_ota_abort(handle);
 if(result==ESP_OK)result=esp_ota_set_boot_partition(target);
 if(result!=ESP_OK){snprintf(error,sizeof(error),"更新未完成（%s），当前系统保持可用",esp_err_to_name(result));publish(-1,error);finish();vTaskDelete(nullptr);return;}
 publish(4,"更新已校验，即将重启…",100);app_delay_ms(1200);esp_restart();
}
bool start(bool install){
 bool expected=false;if(!working.compare_exchange_strong(expected,true))return false;
 if(install){portENTER_CRITICAL(&status_mux);bool ready=status.available;portEXIT_CRITICAL(&status_mux);if(!ready){working=false;return false;}}
 publish(install?3:1,install?"正在验证并准备安装固件…":"正在检查 SD 卡中的已上传固件…");
 if(xTaskCreatePinnedToCore(worker,"SystemUpdate",8192,install?(void*)1:nullptr,1,nullptr,0)!=pdPASS){working=false;publish(-1,"更新任务内存不足，请稍后重试");return false;}
 return true;
}
}
extern "C" void system_update_poll(bool healthy){
 if(!initialized){
  initialized=true;portENTER_CRITICAL(&status_mux);strlcpy(status.current_version,esp_app_get_description()->version,sizeof(status.current_version));strlcpy(status.message,"请先在热点网页上传应用固件，再检查更新",sizeof(status.message));portEXIT_CRITICAL(&status_mux);
  if(storage_exists(BACKUP)){if(!storage_exists(IMAGE))storage_rename(BACKUP,IMAGE);else storage_remove(BACKUP);}
 }
 if(!boot_checked&&healthy&&app_millis()>20000){
  boot_checked=true;esp_ota_img_states_t state;
  if(esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)==ESP_OK&&state==ESP_OTA_IMG_PENDING_VERIFY){
   esp_err_t result=esp_ota_mark_app_valid_cancel_rollback();printf("[SYSTEM UPDATE] boot health confirmed=%d\n",result==ESP_OK);
  }
 }
}
extern "C" void system_update_get_status(system_update_status_t*out){if(!out)return;portENTER_CRITICAL(&status_mux);*out=status;portEXIT_CRITICAL(&status_mux);out->busy=working.load();}
extern "C" bool system_update_check(void){return start(false);}
extern "C" bool system_update_install(void){return start(true);}
extern "C" bool system_update_is_busy(void){return working.load();}
extern "C" bool system_update_reserve_upload(void){if(working||file_lock.test_and_set(std::memory_order_acquire))return false;publish(0,"固件正在上传，完成后可检查更新");portENTER_CRITICAL(&status_mux);status.candidate_version[0]=0;portEXIT_CRITICAL(&status_mux);return true;}
extern "C" void system_update_release_upload(void){file_lock.clear(std::memory_order_release);}
