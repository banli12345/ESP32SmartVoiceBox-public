#include "FileTransfer.h"
#include "Storage.h"
#include "FirmwareImage.h"
#include "SystemUpdate.h"
#include "IdfRuntime.h"
#include "serial_upload_protocol.h"
#include "esp_app_desc.h"
#include "esp_random.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <atomic>
namespace {
SemaphoreHandle_t mutex;
std::atomic<bool> media_recovery{false};
struct Session {char token[24],path[96];uint32_t total,used,last;bool firmware;StorageStream file;} session;
constexpr const char*PART="/.web-upload/file.part";
/* An HTTP upload can preempt the lower-priority housekeeping task while it
 * owns this mutex. Waiting here allows FreeRTOS priority inheritance to finish
 * that short critical section instead of failing a partially written upload. */
struct Guard {
 bool held;
 explicit Guard(TickType_t wait=pdMS_TO_TICKS(1000))
  :held(mutex&&xSemaphoreTake(mutex,wait)==pdTRUE){}
 ~Guard(){if(held)xSemaphoreGive(mutex);}
};
bool fail(char*e,size_t n,const char*t){if(e&&n)snprintf(e,n,"%s",t);return false;}
void clear(){session.file.close();if(session.token[0])storage_remove(PART);session.token[0]=0;if(session.firmware)system_update_release_upload();session.firmware=false;}
uint32_t u32(const uint8_t*p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
uint16_t u16(const uint8_t*p){return p[0]|(uint16_t)p[1]<<8;}
bool validate(const char*path){
 StorageStream file;uint8_t h[44]={};if(!file.open(PART,"rb"))return false;
 const size_t n=file.read(h,sizeof(h));const char*ext=strrchr(path,'.');
 if(!ext)return false;
 if(!strcmp(ext,".bin")){
  uint32_t value=u32(h);unsigned cf=value&31,w=(value>>10)&2047,height=value>>21;
  return n>=4&&(cf==4||cf==5)&&w&&w<=480&&height&&height<=480&&file.byte_count()==4+(uint64_t)w*height*(cf==4?2:3);
 }
 if(!strcmp(ext,".rvf")){
  unsigned w=u16(h+8),height=u16(h+10),header_size=u16(h+6);uint32_t frames=u32(h+16),frame_size=u32(h+20);
  return n>=32&&!memcmp(h,"RVF1",4)&&u16(h+4)==1&&header_size>=32&&header_size<=4096&&w&&w<=480&&height&&height<=480&&
   u32(h+12)>=1000&&u32(h+12)<=30000&&frames&&frame_size==w*height*2&&u32(h+24)==1&&u32(h+28)==0&&
   file.byte_count()==header_size+(uint64_t)frames*frame_size;
 }
 return n==44&&!memcmp(h,"RIFF",4)&&!memcmp(h+8,"WAVEfmt ",8)&&u32(h+16)==16&&u16(h+20)==1&&u16(h+22)==2&&u32(h+24)==44100&&u32(h+28)==176400&&u16(h+32)==4&&u16(h+34)==16&&!memcmp(h+36,"data",4)&&u32(h+40)>0&&u32(h+40)%4==0&&u32(h+4)==(uint64_t)u32(h+40)+36&&file.byte_count()==44+(uint64_t)u32(h+40);
}
}
extern "C" bool file_transfer_begin(const char*kind,const char*name,uint32_t size,char*token,size_t cap,char*error,size_t n){
 Guard guard;if(!guard.held)return fail(error,n,"上传服务正忙，请稍后重试");
 if(media_recovery)return fail(error,n,"媒体文件需要恢复，请重启设备后重试");
 if(session.token[0]){if(app_millis()-session.last<60000)return fail(error,n,"另一项上传尚未结束，请先取消");clear();}
 if(!kind||!name||!*name||!size||system_update_is_busy())return fail(error,n,"文件信息无效或正在系统更新");
 const bool firmware=!strcmp(kind,"firmware"),photo=!strcmp(kind,"photo"),video=!strcmp(kind,"video"),audio=!strcmp(kind,"soundtrack");
 const char*ext=firmware||photo?".bin":video?".rvf":audio?".wav":"";
 const uint32_t limit=firmware?4u*1024*1024:photo?1024*1024:video?256u*1024*1024:audio?64u*1024*1024:0;
 const char*dot=strrchr(name,'.');char check[96];
 if(!limit||size>limit||!dot||strcmp(dot,ext)||snprintf(check,sizeof(check),"%s%s",video||audio?"/videos/":"/imges/",name)>=(int)sizeof(check)||!sdup_valid_path(check))return fail(error,n,"文件名、格式或大小不支持，请使用网页转换或选择对应文件");
 const char*directory=firmware?"/updates":photo?"/imges":"/videos";
 if(firmware&&!system_update_reserve_upload())return fail(error,n,"系统更新正忙，请稍后重试");
 session.firmware=firmware;
 snprintf(session.path,sizeof(session.path),"%s/%s",directory,firmware?"firmware.bin":name);
 if((!firmware&&storage_exists(session.path))||storage_free_bytes()<size+1024ULL*1024||!storage_mkdir(directory)||!storage_mkdir("/.web-upload")){
  clear();return fail(error,n,"同名文件已存在或 SD 卡空间不足，原文件已保留");
 }
 snprintf(session.token,sizeof(session.token),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
 if(!session.file.open(PART,"wb")){clear();return fail(error,n,"无法写入 SD 卡，请检查存储卡");}
 session.file.use_buffer(32768);session.total=size;session.used=0;session.last=app_millis();
 snprintf(token,cap,"%s",session.token);return true;
}
extern "C" bool file_transfer_append(const char*token,uint32_t offset,const void*bytes,size_t length,char*e,size_t n){
 Guard guard;if(!guard.held)return fail(e,n,"上传服务正忙");
 if(!token||!session.token[0]||strcmp(token,session.token))return fail(e,n,"上传会话已失效，请重新上传");
 if(offset!=session.used||!length||length>65536||length>session.total-session.used)return fail(e,n,"上传顺序或长度错误");
 if(session.file.write(bytes,length)!=length){clear();return fail(e,n,"SD 卡写入失败，未完成文件已清理");}
 session.used+=length;session.last=app_millis();return true;
}
extern "C" bool file_transfer_finish(const char*token,char*path,size_t cap,char*e,size_t n){
 Guard guard;if(!guard.held)return fail(e,n,"上传服务正忙");
 if(!token||!session.token[0]||strcmp(token,session.token))return fail(e,n,"上传会话已失效");
 bool ok=session.used==session.total&&session.file.flush();session.file.close();
 if(ok&&session.firmware){firmware_image_info_t info;ok=firmware_image_validate(PART,esp_app_get_description()->project_name,4u*1024*1024,&info,e,n);}
 else if(ok)ok=validate(session.path);
 if(!ok){clear();return fail(e,n,"文件不完整或格式校验失败，请选择本工程应用固件或网页转换后的媒体");}
 if(session.firmware){
  const char*backup="/updates/firmware.prev";
  bool had=storage_exists(session.path);
  ok=(!storage_exists(backup)||storage_remove(backup))&&(!had||storage_rename(session.path,backup));
  if(ok&&!storage_rename(PART,session.path)){if(had)storage_rename(backup,session.path);ok=false;}
  if(ok&&had)storage_remove(backup);
 }else ok=!storage_exists(session.path)&&storage_rename(PART,session.path);
 if(ok)snprintf(path,cap,"%s",session.path);
 clear();return ok||fail(e,n,"文件提交失败，原文件已保留");
}
extern "C" void file_transfer_cancel(const char*token){Guard guard;if(guard.held&&token&&session.token[0]&&!strcmp(token,session.token))clear();}
extern "C" void file_transfer_reset(void){Guard guard;if(guard.held)clear();}
extern "C" void file_transfer_poll(void){
 if(!mutex){mutex=xSemaphoreCreateMutex();if(!mutex)return;storage_mkdir("/.web-upload");storage_remove(PART);}
 /* Only housekeeping uses a try-lock. Never wait behind a live upload, and
  * avoid taking its mutex on every 2 ms AppServices iteration. */
 static uint32_t last_check;
 const uint32_t now=app_millis();
 if(now-last_check<1000)return;
 last_check=now;
 Guard guard(0);if(guard.held&&session.token[0]&&app_millis()-session.last>60000)clear();
}

extern "C" bool file_transfer_begin_media_edit(void){
 if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(1000))!=pdTRUE)return false;
 if(session.token[0]||system_update_is_busy()){xSemaphoreGive(mutex);return false;}
 return true;
}
extern "C" void file_transfer_end_media_edit(void){xSemaphoreGive(mutex);}
extern "C" void file_transfer_set_media_recovery(bool pending){media_recovery=pending;}
