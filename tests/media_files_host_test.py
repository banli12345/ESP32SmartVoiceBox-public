from pathlib import Path
import subprocess,tempfile,shutil
from media_ota_host_test import HEADERS,SDK,ROOT
HARNESS=r'''#include "compat.h"
#include "Storage.h"
#include "FileTransfer.h"
#include "FirmwareImage.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_app_desc.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <filesystem>
#include <vector>
#include <fstream>
uint32_t clock_now=5000;uint32_t app_millis(){return clock_now;}void app_delay_ms(uint32_t n){clock_now+=n;}
bool fail_task,fail_alloc;void(*task_fn)(void*);void*task_arg;
void(*sem_taken_hook)(SemaphoreHandle_t,int)=nullptr;void(*sem_before_hook)(SemaphoreHandle_t,int)=nullptr;
static bool updating,reserved,video_open,video_audio;static int photo_state;
extern "C" bool system_update_is_busy(){return updating;}
extern "C" bool system_update_reserve_upload(){if(reserved)return false;return reserved=true;}
extern "C" void system_update_release_upload(){reserved=false;}
extern "C" int get_photo_load_status(){return photo_state;}
extern "C" bool sdcar_video_file_is_open(){return video_open;}
extern "C" bool video_audio_is_active(){return video_audio;}
static esp_app_desc_t app;const esp_app_desc_t*esp_app_get_description(){return &app;}
extern "C" void mbedtls_platform_zeroize(void*p,size_t n){volatile unsigned char*b=(volatile unsigned char*)p;while(n--)*b++=0;}
#include "@ROOT@/src/MediaFiles.cpp"
static std::string root_storage;
static void setup(const char*base,int number){root_storage=std::string(base)+"/case-"+std::to_string(number);std::filesystem::create_directories(root_storage);storage_host_hooks={};storage_host_set_root(root_storage.c_str());assert(storage_mkdir("/images")&&storage_mkdir("/imges")&&storage_mkdir("/videos"));assert(media_files_recover_boot());}
static void put(const char*p,const char*s){StorageStream f;assert(f.open(p,"wb")&&f.write(s,strlen(s))==strlen(s)&&f.flush());}
static std::string read(const char*p){StorageStream f;assert(f.open(p,"rb"));std::string s(f.byte_count(),'\0');assert(f.read(s.data(),s.size())==s.size());return s;}
static media_file_status_t state(){media_file_status_t s;media_files_get_status(&s);return s;}
static void run(){auto f=task_fn;auto a=task_arg;task_fn=nullptr;assert(f);try{f(a);}catch(TaskDone&){} }
static bool edit(media_kind_t k,const char*p,const char*n,bool erase=false){if(!media_files_request(k,p,n,erase))return false;run();return state().state==2;}
static bool crash_edit(int step,bool erase,const char*name="Renamed"){storage_host_hooks.rename_count=0;storage_host_hooks.crash_after_rename=step;assert(media_files_request(MEDIA_VIDEO,"/videos/Clip.rvf",erase?nullptr:name,erase));try{run();return false;}catch(StorageHostCrash&){file_transfer_end_media_edit();busy=false;storage_host_hooks.crash_after_rename=0;return true;}}
static void clean_private(){assert(!storage_exists("/.media-edit/pending")&&!storage_exists("/.media-edit/committed")&&!storage_exists("/.media-edit/primary.saved")&&!storage_exists("/.media-edit/audio.saved"));}
int main(int argc,char**argv){assert(argc==2);setup(argv[1],0);file_transfer_poll();
 put("/imges/photo.bin","picture");assert(edit(MEDIA_PHOTO,"S:/imges/photo.bin","holiday"));assert(read("/imges/holiday.bin")=="picture"&&!storage_exists("/imges/photo.bin"));
 put("/images/image.bin","second");assert(edit(MEDIA_PHOTO,"S:/images/image.bin","new.bin"));assert(read("/images/new.bin")=="second");assert(edit(MEDIA_PHOTO,"/images/new.bin",nullptr,true));assert(!storage_exists("/images/new.bin"));
 put("/videos/Clip.rvf","video-data");put("/videos/CLIP.WAV","audio-data");
 assert(edit(MEDIA_VIDEO,"/videos/Clip.rvf","Movie"));assert(read("/videos/Movie.rvf")=="video-data"&&read("/videos/Movie.wav")=="audio-data");assert(edit(MEDIA_VIDEO,"/videos/Movie.rvf","movie"));
 char actual[96];assert(actual_path("/videos/movie.rvf",actual)&&!strcmp(actual,"/videos/movie.rvf"));assert(edit(MEDIA_VIDEO,"/videos/movie.rvf",nullptr,true));assert(!storage_exists("/videos/movie.rvf")&&!storage_exists("/videos/movie.wav"));clean_private();
 put("/videos/Silent.rvf","silent");assert(edit(MEDIA_VIDEO,"/videos/Silent.rvf","Quiet"));assert(!storage_exists("/videos/Quiet.wav"));
 put("/videos/Taken.wav","orphan");assert(!edit(MEDIA_VIDEO,"/videos/Quiet.rvf","Taken"));assert(read("/videos/Quiet.rvf")=="silent"&&read("/videos/Taken.wav")=="orphan");
 assert(!edit(MEDIA_VIDEO,"/videos/Quiet.rvf","../bad"));assert(!edit(MEDIA_VIDEO,"/videos/Quiet.rvf","bad/thing"));assert(!edit(MEDIA_VIDEO,"/videos/Quiet.rvf"," "));assert(!media_files_request(MEDIA_VIDEO,"/music/song.wav","bad",false));assert(valid_path("/images/测试.bin",MEDIA_PHOTO));
 photo_state=1;assert(!edit(MEDIA_VIDEO,"/videos/Quiet.rvf","Busy"));photo_state=0;video_open=true;assert(!edit(MEDIA_VIDEO,"/videos/Quiet.rvf","Busy"));video_open=false;video_audio=true;assert(!edit(MEDIA_VIDEO,"/videos/Quiet.rvf","Busy"));video_audio=false;
 char token[24],error[192]={};assert(file_transfer_begin("photo","upload.bin",12,token,sizeof(token),error,sizeof(error)));assert(!edit(MEDIA_VIDEO,"/videos/Quiet.rvf","Busy"));file_transfer_cancel(token);
 assert(edit(MEDIA_VIDEO,"/videos/Quiet.rvf","Allowed"));clean_private();
 for(int step=1;step<=6;step++){
  setup(argv[1],step);put("/videos/Clip.rvf","V");put("/videos/CLIP.WAV","A");assert(crash_edit(step,false));assert(media_files_recover_boot());
  const bool committed=step==6;assert(read(committed?"/videos/Renamed.rvf":"/videos/Clip.rvf")=="V");assert(read(committed?"/videos/Renamed.wav":"/videos/CLIP.WAV")=="A");clean_private();
 }
 for(int step=1;step<=4;step++){
  setup(argv[1],10+step);put("/videos/Clip.rvf","V");put("/videos/CLIP.WAV","A");assert(crash_edit(step,true));assert(media_files_recover_boot());
  if(step==4){assert(!storage_exists("/videos/Clip.rvf")&&!storage_exists("/videos/CLIP.WAV"));}else assert(read("/videos/Clip.rvf")=="V"&&read("/videos/CLIP.WAV")=="A");clean_private();
 }
 for(int point=1;point<=5;point++){
  setup(argv[1],20+point);put("/videos/Clip.rvf","V");put("/videos/CLIP.WAV","A");
  const char*dests[]={"/.media-edit/pending","/.media-edit/primary.saved","/.media-edit/audio.saved","/videos/Renamed.rvf","/videos/Renamed.wav"};storage_host_hooks.fail_rename_dest=dests[point-1];
  assert(!edit(MEDIA_VIDEO,"/videos/Clip.rvf","Renamed"));assert(read("/videos/Clip.rvf")=="V"&&read("/videos/CLIP.WAV")=="A");clean_private();
 }

 for(int step=1;step<=6;step++){
  setup(argv[1],40+step);put("/videos/Clip.rvf","V");put("/videos/CLIP.WAV","A");assert(crash_edit(step,false,"clip"));assert(media_files_recover_boot());
  const char*expected=step==6?"/videos/clip.rvf":"/videos/Clip.rvf";const char*audio=step==6?"/videos/clip.wav":"/videos/CLIP.WAV";
  assert(actual_path(expected,actual)&&!strcmp(actual,expected));assert(actual_path(audio,actual)&&!strcmp(actual,audio));assert(read(expected)=="V"&&read(audio)=="A");clean_private();
 }
 for(int flush=1;flush<=2;flush++){
  setup(argv[1],50+flush);put("/videos/Clip.rvf","V");put("/videos/CLIP.WAV","A");storage_host_hooks.flush_count=0;storage_host_hooks.fail_flush_at=flush;
  assert(!edit(MEDIA_VIDEO,"/videos/Clip.rvf","Renamed"));assert(media_files_recover_boot());assert(read("/videos/Clip.rvf")=="V"&&read("/videos/CLIP.WAV")=="A");clean_private();
 }
 setup(argv[1],60);put("/videos/Clip.rvf","V");put("/videos/CLIP.WAV","A");storage_host_hooks.fail_rename_dest="/.media-edit/committed";
 assert(!edit(MEDIA_VIDEO,"/videos/Clip.rvf","Renamed"));assert(media_files_recover_boot());assert(read("/videos/Clip.rvf")=="V"&&read("/videos/CLIP.WAV")=="A");clean_private();
 setup(argv[1],61);put("/videos/Clip.rvf","V");put("/videos/CLIP.WAV","A");assert(crash_edit(4,true));assert(storage_remove("/.media-edit/primary.saved"));assert(media_files_recover_boot());assert(!storage_exists("/videos/Clip.rvf")&&!storage_exists("/videos/CLIP.WAV"));clean_private();
 setup(argv[1],62);put("/videos/Clip.rvf","V");put("/videos/CLIP.WAV","A");assert(crash_edit(6,false));assert(storage_remove("/.media-edit/pending"));assert(media_files_recover_boot());assert(read("/videos/Renamed.rvf")=="V"&&read("/videos/Renamed.wav")=="A");clean_private();
 setup(argv[1],63);put("/videos/Clip.rvf","V");put("/videos/CLIP.WAV","A");assert(crash_edit(1,false));put("/.media-edit/primary.saved","V");assert(!media_files_recover_boot());assert(read("/videos/Clip.rvf")=="V"&&read("/.media-edit/primary.saved")=="V");
 setup(argv[1],30);put("/videos/Clip.rvf","V");put("/videos/CLIP.WAV","A");assert(crash_edit(3,false));put("/.media-edit/pending","corrupt");assert(!media_files_recover_boot());assert(storage_exists("/.media-edit/primary.saved")&&storage_exists("/.media-edit/audio.saved"));assert(!file_transfer_begin("photo","blocked.bin",12,token,sizeof(token),error,sizeof(error)));
 puts("Media files passed: photo/video/WAV rename/delete, case-only spelling, no-overwrite/path/busy guards, crash recovery at every rename boundary, rollback failures and corrupt-journal preservation");
}
'''
def main():
 compiler=shutil.which('cl');assert compiler,'Use VS developer shell'
 with tempfile.TemporaryDirectory(prefix='media-files-',ignore_cleanup_errors=True)as temp:
  d=Path(temp);headers=dict(HEADERS);headers['compat.h']+='#define SDCAR_H\nextern "C" int get_photo_load_status();\nextern "C" bool sdcar_video_file_is_open();\n';headers['strings.h']='#pragma once\n#define strcasecmp _stricmp\n'
  for name,body in headers.items():
   p=d/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(body,encoding='utf-8')
  unit=d/'test.cpp';unit.write_text(HARNESS.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8');exe=d/'test.exe'
  cmd=[compiler,'/nologo','/TP','/std:c++17','/utf-8','/EHsc','/DNOMINMAX','/D_CRT_SECURE_NO_WARNINGS','/DSTORAGE_HOST_TEST',f'/FI{d/"compat.h"}',f'/I{d}',f'/I{ROOT/"src"}',f'/I{SDK/"include"}',f'/I{SDK/"library"}',str(unit),*[str(ROOT/'src'/n)for n in['FirmwareImage.cpp','FileTransfer.cpp','Storage.cpp','serial_upload_protocol.c']],str(SDK/'library/sha256.c'),f'/Fe:{exe}',f'/Fo{d}\\']
  subprocess.run(cmd,check=True);subprocess.run([str(exe),str(d/'sd')],check=True)
if __name__=='__main__':main()
