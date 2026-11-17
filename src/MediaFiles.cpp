#include "MediaFiles.h"
#include "FileTransfer.h"
#include "Storage.h"
#include "SDcar.h"
#include "Sound.h"
#include "serial_upload_protocol.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <cstddef>
namespace {
constexpr const char *DIR="/.media-edit",*JOURNAL="/.media-edit/pending",*TEMP="/.media-edit/pending.tmp";
constexpr const char *COMMITTED="/.media-edit/committed",*COMMIT_TEMP="/.media-edit/commit.tmp";
constexpr const char *PRIMARY="/.media-edit/primary.saved",*AUDIO="/.media-edit/audio.saved";
struct Transaction {uint32_t magic;uint8_t op,audio,reserved[2];char old_path[96],old_audio[96],new_path[96],new_audio[96];uint32_t primary_bytes,audio_bytes,crc;};
static_assert(sizeof(Transaction)==404,"media journal layout");
struct Request {media_kind_t kind;bool remove;char path[96],name[96];};
std::atomic<bool> busy{false};Request request;
portMUX_TYPE status_mux=portMUX_INITIALIZER_UNLOCKED;
media_file_status_t status={};
void publish(int state,const char*message,const char*path=""){
 portENTER_CRITICAL(&status_mux);status.state=state;snprintf(status.message,sizeof(status.message),"%s",message);snprintf(status.path,sizeof(status.path),"%s",path);portEXIT_CRITICAL(&status_mux);
}
bool utf8(const char*text){
 const auto*p=(const unsigned char*)text;
 while(*p){uint32_t cp;unsigned count;unsigned char first=*p++;
  if(first<128){if(first<32||first==127)return false;continue;}
  if(first>=0xc2&&first<=0xdf){cp=first&31;count=1;}
  else if(first>=0xe0&&first<=0xef){cp=first&15;count=2;}
  else if(first>=0xf0&&first<=0xf4){cp=first&7;count=3;}else return false;
  for(unsigned i=0;i<count;i++){if((*p&0xc0)!=0x80)return false;cp=(cp<<6)|(*p++&63);}
  if((count==2&&cp<0x800)||(count==3&&cp<0x10000)||(cp>=0xd800&&cp<=0xdfff)||cp>0x10ffff)return false;
 }return true;
}
bool valid_path(const char*path,media_kind_t kind){
 if(!path||strlen(path)>(kind==MEDIA_PHOTO?93u:95u)||!utf8(path))return false;
 const char*name=nullptr;
 if(kind==MEDIA_PHOTO){if(!strncmp(path,"/imges/",7))name=path+7;else if(!strncmp(path,"/images/",8))name=path+8;}
 else if(kind==MEDIA_VIDEO&&!strncmp(path,"/videos/",8))name=path+8;
 if(!name||!*name||*name=='.'||strstr(name,".."))return false;
 for(const char*p=name;*p;p++)if(strchr("/\\:*?\"<>|",*p))return false;
 const char*dot=strrchr(name,'.');return dot&&dot!=name&&!strcasecmp(dot,kind==MEDIA_PHOTO?".bin":".rvf");
}
void companion(const char*path,char*out){snprintf(out,96,"%s",path);memcpy(out+strlen(out)-3,"wav",3);}
bool journal_valid(const Transaction&t){
 if(t.magic!=0x3144454d||(t.op!=1&&t.op!=2)||t.audio>1||t.reserved[0]||t.reserved[1]||t.crc!=sdup_crc32(0,&t,offsetof(Transaction,crc)))return false;
 const char*paths[]={t.old_path,t.old_audio,t.new_path,t.new_audio};for(const char*p:paths)if(!memchr(p,0,96))return false;
 media_kind_t kind=!strncmp(t.old_path,"/videos/",8)?MEDIA_VIDEO:MEDIA_PHOTO;
 if(!valid_path(t.old_path,kind))return false;
 if(t.op==1){
  if(!valid_path(t.new_path,kind))return false;
  const char*a=strrchr(t.old_path,'/'),*b=strrchr(t.new_path,'/');
  if(a-t.old_path!=b-t.new_path||strncmp(t.old_path,t.new_path,a-t.old_path))return false;
 }else if(t.new_path[0]||t.new_audio[0])return false;
 if(t.audio){char expected[96];if(kind!=MEDIA_VIDEO)return false;companion(t.old_path,expected);if(strcasecmp(expected,t.old_audio))return false;
  if(t.op==1){companion(t.new_path,expected);if(strcasecmp(expected,t.new_audio))return false;}
 }else if(t.old_audio[0]||t.new_audio[0])return false;
 return true;
}
/* Resolve the actual long filename; FAT lookup alone cannot distinguish
 * source spelling from a case-only destination. Preserve the WAV's own case. */
bool actual_path(const char*path,char*out){
 const char*slash=strrchr(path,'/');if(!slash)return false;
 char dir[96],name[256];snprintf(dir,sizeof(dir),"%.*s",(int)(slash-path),path);
 StorageDirectory directory;if(!directory.open(dir))return false;bool is_dir=false;
 while(directory.read_next(name,sizeof(name),&is_dir)){
  const char*leaf=strrchr(name,'/');leaf=leaf?leaf+1:name;
  if(!is_dir&&!strcasecmp(leaf,slash+1)){
   int n=snprintf(out,96,"%s/%s",dir,leaf);return n>=0&&n<96;
  }
 }return false;
}
bool file_size(const char*path,uint32_t*size){StorageStream file;if(!file.open(path,"rb")||file.byte_count()>UINT32_MAX)return false;*size=(uint32_t)file.byte_count();return true;}
bool locate(const char*old,const char*stage,const char*dest,uint32_t expected,char*out){
 unsigned count=0;out[0]=0;const char*paths[]={old,stage,dest};
 for(const char*path:paths){
  if(!path||!*path||!storage_exists(path))continue;
  char actual[96];uint32_t bytes;
  if(!actual_path(path,actual)||!file_size(actual,&bytes)||bytes!=expected)return false;
  if(!out[0]||strcmp(out,actual)){if(++count>1)return false;snprintf(out,96,"%s",actual);}
 }
 return count==1;
}
bool move_to(const char*from,const char*to,const char*stage){
 if(!strcmp(from,to))return true;
 if(!strcasecmp(from,to))return !storage_exists(stage)&&storage_rename(from,stage)&&storage_rename(stage,to);
 return !storage_exists(to)&&storage_rename(from,to);
}
bool recover(){
 if(!storage_exists(JOURNAL)){
  if(storage_exists(PRIMARY)||storage_exists(AUDIO))return false;
  return storage_remove(TEMP)&&storage_remove(COMMIT_TEMP)&&storage_remove(COMMITTED);
 }
 StorageStream file;Transaction t={};
 if(!file.open(JOURNAL,"rb")||file.byte_count()!=sizeof(t)||file.read(&t,sizeof(t))!=sizeof(t)||!journal_valid(t))return false;
 file.close();if(!t.audio&&storage_exists(AUDIO))return false;
 bool committed=false;
 if(storage_exists(COMMITTED)){
  uint32_t marker[3];
  if(!file.open(COMMITTED,"rb")||file.byte_count()!=sizeof(marker)||file.read(marker,sizeof(marker))!=sizeof(marker)||
    marker[0]!=0x31434d4d||marker[1]!=t.crc||marker[2]!=sdup_crc32(0,marker,8))return false;
  committed=true;file.close();
 }
 bool ok=true;
 if(committed&&t.op==2){
  const char*stages[]={PRIMARY,AUDIO};const char*olds[]={t.old_path,t.old_audio};const uint32_t sizes[]={t.primary_bytes,t.audio_bytes};
  for(unsigned i=0;i<(t.audio?2u:1u);i++)if(storage_exists(stages[i])){
   uint32_t bytes;if(storage_exists(olds[i])||!file_size(stages[i],&bytes)||bytes!=sizes[i])return false;
  }
  ok=storage_remove(PRIMARY)&&storage_remove(AUDIO);
 }else{
  char primary[96],audio[96];
  if(!locate(t.old_path,PRIMARY,t.op==1?t.new_path:nullptr,t.primary_bytes,primary)||
    (t.audio&&!locate(t.old_audio,AUDIO,t.op==1?t.new_audio:nullptr,t.audio_bytes,audio)))return false;
  ok=move_to(primary,committed?t.new_path:t.old_path,PRIMARY);
  if(t.audio)ok=move_to(audio,committed?t.new_audio:t.old_audio,AUDIO)&&ok;
 }
 /* Journal is removed first. A remaining commit with no journal or stages
  * only needs private-marker cleanup; it must never trigger a rollback. */
 return ok&&storage_remove(JOURNAL)&&storage_remove(COMMITTED)&&storage_remove(COMMIT_TEMP)&&storage_remove(TEMP);
}
bool prepare(Transaction&t){
 t.magic=0x3144454d;t.crc=sdup_crc32(0,&t,offsetof(Transaction,crc));StorageStream file;
 bool ok=storage_mkdir(DIR)&&file.open(TEMP,"wb")&&file.write(&t,sizeof(t))==sizeof(t)&&file.flush();file.close();
 return ok&&storage_rename(TEMP,JOURNAL);
}
bool commit(const Transaction&t){
 const uint32_t prefix[2]={0x31434d4d,t.crc};uint32_t marker[3]={prefix[0],prefix[1],sdup_crc32(0,prefix,sizeof(prefix))};
 StorageStream file;bool ok=file.open(COMMIT_TEMP,"wb")&&file.write(marker,sizeof(marker))==sizeof(marker)&&file.flush();file.close();
 return ok&&storage_rename(COMMIT_TEMP,COMMITTED);
}
bool make_target(const Request&r,Transaction*t){
 if(!valid_path(r.path,r.kind)||!storage_is_file(r.path))return false;
 if(!actual_path(r.path,t->old_path)||!file_size(t->old_path,&t->primary_bytes))return false;
 t->op=r.remove?2:1;
 if(r.kind==MEDIA_VIDEO){
  char expected[96];companion(t->old_path,expected);
  if(storage_exists(expected)){
   if(!actual_path(expected,t->old_audio)||!file_size(t->old_audio,&t->audio_bytes))return false;
   t->audio=1;
  }
 }
 if(r.remove)return true;
 char name[96];snprintf(name,sizeof(name),"%s",r.name);
 char*start=name;while(*start==' ')start++;size_t n=strlen(start);while(n&&start[n-1]==' ')start[--n]=0;
 const char*extension=strrchr(r.path,'.');if(n>=4&&!strcasecmp(start+n-4,extension))start[n-=4]=0;
 if(!n||start[0]=='.'||start[n-1]=='.'||start[n-1]==' '||strstr(start,"..")||!utf8(start))return false;
 for(const char*p=start;*p;p++)if(strchr("/\\:*?\"<>|",*p))return false;
 const size_t prefix=strrchr(r.path,'/')-r.path+1;
 if(prefix+n+strlen(extension)>(r.kind==MEDIA_PHOTO?93u:95u))return false;
 memcpy(t->new_path,r.path,prefix);snprintf(t->new_path+prefix,sizeof(t->new_path)-prefix,"%s%s",start,extension);
 if(t->audio)companion(t->new_path,t->new_audio);
 return true;
}
void worker(void*){
 const Request r=request;
 if(!file_transfer_begin_media_edit()){publish(-1,"请等待网页上传或系统更新完成后重试");busy=false;vTaskDelete(nullptr);return;}
 auto finish=[](){file_transfer_end_media_edit();busy=false;};
 if(get_photo_load_status()==1||sdcar_video_file_is_open()||video_audio_is_active()){
  publish(-1,"照片或视频仍在读取，请返回列表稍后重试");finish();vTaskDelete(nullptr);return;
 }
 if(!recover()){file_transfer_set_media_recovery(true);publish(-1,"文件需要恢复，请重启设备；原文件已保留");finish();vTaskDelete(nullptr);return;}
 file_transfer_set_media_recovery(false);
 Transaction t={};
 if(!make_target(r,&t)){publish(-1,"文件不存在或名称无效，请缩短名称并保留文件格式");finish();vTaskDelete(nullptr);return;}
 if(!r.remove&&!strcmp(t.old_path,t.new_path)){publish(2,"名称未改变",t.old_path);finish();vTaskDelete(nullptr);return;}
 if(!r.remove){char audio[96];companion(t.new_path,audio);
  bool collision=storage_exists(t.new_path)&&strcasecmp(t.old_path,t.new_path);
  if(r.kind==MEDIA_VIDEO&&storage_exists(audio))collision=collision||!t.audio||strcasecmp(t.old_audio,audio);
  if(collision){publish(-1,"同名文件或配套音频已存在，请换一个名称");finish();vTaskDelete(nullptr);return;}
 }
 file_transfer_set_media_recovery(true);
 bool ok=prepare(t);
 if(ok)ok=storage_rename(t.old_path,PRIMARY);
 if(ok&&t.audio)ok=storage_rename(t.old_audio,AUDIO);
 if(ok&&!r.remove){ok=storage_rename(PRIMARY,t.new_path);if(ok&&t.audio)ok=storage_rename(AUDIO,t.new_audio);}
 if(!ok){bool restored=recover();file_transfer_set_media_recovery(!restored);publish(-1,restored?"操作未完成，原文件已恢复":"操作未完成，请重启设备恢复文件");finish();vTaskDelete(nullptr);return;}
 if(!commit(t)){publish(-1,"操作需要恢复，请重启设备后重试；文件已保留");finish();vTaskDelete(nullptr);return;}
 ok=recover();file_transfer_set_media_recovery(!ok);
 publish(ok?2:-1,ok?(r.remove?"已删除文件及配套音频":"重命名完成") :"操作需要恢复，请重启设备后重试",r.remove?t.old_path:t.new_path);
 printf("[MEDIA FILES] operation=%s ok=%d paired_audio=%u\n",r.remove?"delete":"rename",ok,t.audio);
 finish();vTaskDelete(nullptr);
}
}
extern "C" bool media_files_recover_boot(void){bool ok=recover();file_transfer_set_media_recovery(!ok);if(!ok)puts("[MEDIA FILES] recovery pending; original data preserved");return ok;}
extern "C" bool media_files_request(media_kind_t kind,const char*path,const char*name,bool remove){
 if(!path||(kind!=MEDIA_PHOTO&&kind!=MEDIA_VIDEO)||strlen(path)>=96||(!remove&&(!name||strlen(name)>=96)))return false;
 if(!strncmp(path,"S:",2))path+=2;
 if(!valid_path(path,kind))return false;
 bool expected=false;if(!busy.compare_exchange_strong(expected,true))return false;
 request={};request.kind=kind;request.remove=remove;snprintf(request.path,sizeof(request.path),"%s",path);if(name)snprintf(request.name,sizeof(request.name),"%s",name);
 publish(1,remove?"正在删除文件…":"正在重命名…");
 if(xTaskCreatePinnedToCore(worker,"MediaFiles",6144,nullptr,1,nullptr,0)!=pdPASS){busy=false;publish(-1,"文件任务内存不足，请稍后重试");return false;}return true;
}
extern "C" void media_files_get_status(media_file_status_t*out){if(!out)return;portENTER_CRITICAL(&status_mux);*out=status;portEXIT_CRITICAL(&status_mux);}
