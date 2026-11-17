"""Verify real local AI song selection after catalog slots are deleted."""
from pathlib import Path
import tempfile,subprocess,shutil
from music_catalog_host_test import function
ROOT=Path(__file__).resolve().parents[1]
def main():
 src=(ROOT/'src/Web_ai.cpp').read_text(encoding='utf-8')
 bodies='\n'.join(function(src,n) for n in ['music_copy_display_name','music_find_track_in_text','music_find_ordinal_track','handle_music_command'])
 with tempfile.TemporaryDirectory(prefix='music-local-ai-') as tmp:
  d=Path(tmp);unit=d/'test.cpp';exe=d/'test.exe';unit.write_text(FAKES+bodies+CASES,encoding='utf-8')
  subprocess.run([shutil.which('cl'),'/nologo','/std:c++17','/utf-8','/EHsc','/D_CRT_SECURE_NO_WARNINGS',f'/I{ROOT/"src"}',str(unit),str(ROOT/'src/music_intent.c'),f'/Fe:{exe}',f'/Fo{d}\\'],check=True)
  subprocess.run([str(exe)],check=True)
FAKES=r'''#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <string>
#include "music_intent.h"
#define strcasecmp _stricmp
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
static bool active[6]={false,true,false,false,true,false};
static int current=1,selected=-1,plays;static std::string reply;
static size_t strlcpy(char*d,const char*s,size_t n){size_t k=strlen(s);if(n)snprintf(d,n,"%s",s);return k;}
static uint8_t sdcar_get_music_track_count(){return 6;}
static bool sdcar_copy_music_track(uint8_t i,char*p,size_t n,uint32_t*g){if(i>=6||!active[i])return false;if(p)snprintf(p,n,i==1?"/music/First - Artist.wav":"/music/Second - Artist.wav");if(g)*g=1;return true;}
static int sdcar_next_music_track(int i,int dir,bool wrap){for(int n=0;n<6;++n){i+=dir;if(i<0||i>=6){if(!wrap)return -1;i=i<0?5:0;}if(active[i])return i;}return -1;}
static int music_next_local_track(int i,int dir){return sdcar_next_music_track(i,dir,true);}
static void music_set_ai_reply(const char*s){reply=s;}
static bool online_music_handle_command(const char*){return false;}
static bool sound_is_ready(){return true;}
static void music_audio_play(){++plays;}
static void music_audio_stop(){}
static void music_audio_pause(){}
static uint8_t get_music_track_index(){return current;}
static void music_select_track(uint8_t i){assert(active[i]);selected=current=i;}
'''
CASES=r'''int main(){
 assert(handle_music_command("播放第一首")&&selected==1);
 assert(handle_music_command("播放第二首")&&selected==4);
 assert(handle_music_command("上一首")&&selected==1);
 assert(handle_music_command("下一首")&&selected==4);
 assert(handle_music_command("下一首")&&selected==1);
 int n=plays;assert(handle_music_command("播放第三首")&&plays==n&&reply.find("不存在")!=std::string::npos);
 assert(handle_music_command("播放 First - Artist")&&selected==1);
 active[1]=active[4]=false;n=plays;assert(handle_music_command("播放音乐")&&plays==n&&reply.find("没有可播放")!=std::string::npos);
 puts("Local AI playlist passed: ordinals, previous/next/wrap and empty catalog skip deleted slots");
}
'''
if __name__=='__main__':main()
