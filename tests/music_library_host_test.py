"""Verify real audio-owner deletion and file-close ordering with the real catalog."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r'^(?:static )?(?:void|bool|int|uint8_t|uint32_t|const char \*)\s*' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    compiler = shutil.which('cl')
    if not compiler:
        raise SystemExit('Use a Visual Studio C++ developer terminal')
    source = (ROOT/'src/SDcar.cpp').read_text(encoding='utf-8')
    declarations = source[source.index('static constexpr uint8_t MUSIC_MAX_TRACKS'):source.index('static uint8_t current_lyric_track_index')]
    bodies = '\n'.join(function(source, name) for name in ['music_is_wav_name', 'sdcar_get_music_track_count',
        'sdcar_music_track_valid', 'sdcar_copy_music_track', 'sdcar_music_catalog_revision', 'sdcar_next_music_track', 'sdcar_get_music_track_name', 'sdcar_get_music_track_path', 'sdcar_register_music_track', 'sdcar_delete_music_track'])
    scaffold = r'''
#include <atomic>
#include <mutex>
#include <thread>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <set>
#include <string>
static unsigned current_lyric_track_index, music_lyric_count;
static std::set<std::string> files;
static std::string failed_remove, open_path;
static bool storage_exists(const char*p){return files.count(p);}
static bool storage_remove(const char*p){assert(open_path!=p);if(failed_remove==p)return false;files.erase(p);return true;}

#define strcasecmp _stricmp
#define EXT_RAM_BSS_ATTR
#define portMUX_TYPE std::mutex
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) ((p)->lock())
#define portEXIT_CRITICAL(p) ((p)->unlock())
static bool sd_available=true;
static size_t strlcpy(char*d,const char*s,size_t n){size_t k=strlen(s);if(n){size_t c=std::min(k,n-1);memcpy(d,s,c);d[c]=0;}return k;}
'''
    sound = (ROOT/'src/Sound.cpp').read_text(encoding='utf-8')
    audio_bodies = '\n'.join(function(sound.replace('extern "C" ', ''), name) for name in ['music_delete_track', 'music_delete_status', 'service_music_delete'])
    scaffold += r'''static std::mutex music_control_mux;
static int music_delete_state, music_audio_task_handle=1;
static uint8_t music_delete_index, music_track_index;
static uint32_t music_delete_generation, music_elapsed_ms=500;
static int music_track_request=-1;
static bool music_audio_playing=true,music_audio_stop_requested,music_audio_pause_requested,music_play_requested;
static bool music_resume_after_ai_audio=true,music_resume_after_video=true,online;
static bool music_audio_is_online(){return online;}
static unsigned closes,notifications;
static void sdcar_close_music(){++closes;open_path.clear();}
static void xTaskNotifyGive(int){++notifications;}
#include "MusicLibraryLock.cpp"
'''
    cases = r''' 
int main(){
 uint32_t gen; char path[64];
 assert(sdcar_register_music_track("/music/first.wav")==0);
 assert(sdcar_register_music_track("/music/second.wav")==1);
 assert(sdcar_copy_music_track(0,path,sizeof(path),&gen));
 files={"/music/first.wav","/music/first.lrc","/music/second.wav","/music/second.lrc"};
 open_path="/music/first.wav";
 assert(music_library_try_lock());
 assert(music_delete_track(0,gen));service_music_delete();assert(music_delete_status()==-1&&closes==0&&files.count(open_path));
 music_library_unlock();
 assert(music_delete_track(0,gen));assert(!music_delete_track(1,1));
 service_music_delete();assert(music_delete_status()==2&&closes==1&&!music_audio_playing&&music_audio_stop_requested);
 assert(music_track_index==1&&!music_resume_after_ai_audio&&!music_resume_after_video&&music_elapsed_ms==0);
 assert(!files.count("/music/first.wav")&&!files.count("/music/first.lrc")&&files.count("/music/second.wav"));
 assert(sdcar_register_music_track("/music/reused.wav")==0);
 assert(music_delete_track(0,gen));service_music_delete();assert(music_delete_status()==-1&&sdcar_music_track_valid(0));
 assert(sdcar_copy_music_track(0,path,sizeof(path),&gen));files.insert(path);open_path="/music/second.wav";
 music_audio_playing=true;music_audio_stop_requested=false;
 assert(music_delete_track(0,gen));service_music_delete();assert(music_delete_status()==2&&closes==1&&music_audio_playing&&open_path=="/music/second.wav");
 assert(sdcar_copy_music_track(1,path,sizeof(path),&gen));music_audio_playing=false;music_audio_pause_requested=true;
 assert(music_delete_track(1,gen));service_music_delete();assert(music_delete_status()==2&&closes==2&&!music_audio_pause_requested);
 assert(sdcar_next_music_track(-1,1,false)==-1);
 puts("Music library audio owner: close-before-unlink, paused/current/other/last-song deletion, stable slots, stale generation and concurrent-save rejection passed");
}

'''
    with tempfile.TemporaryDirectory(prefix='music-catalog-', ignore_cleanup_errors=True) as tmp:
        directory = Path(tmp)
        unit, exe = directory/'test.cpp', directory/'test.exe'
        unit.write_text(scaffold+declarations+bodies+audio_bodies+cases, encoding='utf-8')
        subprocess.run([compiler, '/nologo', '/std:c++17', '/EHsc', '/utf-8', '/D_CRT_SECURE_NO_WARNINGS',
                        f'/I{ROOT/"src"}', str(unit), f'/Fe:{exe}', f'/Fo{directory}\\'], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
