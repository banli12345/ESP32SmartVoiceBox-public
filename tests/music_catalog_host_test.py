"""Exercise stable music slots, concurrent publication, deletion and slot reuse."""
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
    cases = r'''
int main(){
 assert(sdcar_register_music_track("/music/anchor.wav")==0);const char*anchor=sdcar_get_music_track_path(0);
 assert(sdcar_register_music_track("/music/anchor.wav")==0);
 assert(sdcar_register_music_track("/music/../evil.wav")==-1);
 assert(sdcar_register_music_track("/music/nested/file.wav")==-1);
 assert(sdcar_register_music_track("/music/file.mp3")==-1);
 std::atomic<bool>done{false};std::atomic<unsigned>observations{0};
 std::thread reader([&](){while(!done.load()){unsigned count=sdcar_get_music_track_count();for(unsigned i=0;i<count;++i){
  const char*p=sdcar_get_music_track_path(i);const char*n=sdcar_get_music_track_name(i);
  assert(p&&strlen(p)<64&&!strncmp(p,"/music/",7)&&!strcmp(n,p+7));
 }++observations;}});
 auto writer=[](int base){for(int i=0;i<40;++i){char path[64];snprintf(path,sizeof(path),"/music/track-%03d.wav",base+i);assert(sdcar_register_music_track(path)>=0);assert(sdcar_register_music_track(path)>=0);}};
 std::thread a(writer,0),b(writer,40),c(writer,80);a.join();b.join();c.join();done=true;reader.join();
 assert(sdcar_get_music_track_count()==121&&anchor==sdcar_get_music_track_path(0)&&!strcmp(anchor,"/music/anchor.wav"));
 for(int i=0;i<7;++i){char path[64];snprintf(path,sizeof(path),"/music/final-%d.wav",i);assert(sdcar_register_music_track(path)==121+i);}
 assert(sdcar_get_music_track_count()==128&&sdcar_register_music_track("/music/overflow.wav")==-1);
 assert(sdcar_register_music_track("/music/anchor.wav")==0);
 assert(sdcar_get_music_track_path(128)==nullptr&&!strcmp(sdcar_get_music_track_name(128),""));
 uint32_t generation;char path[64];
 assert(sdcar_copy_music_track(0,path,sizeof(path),&generation));
 files.insert("/music/anchor.wav");files.insert("/music/anchor.lrc");files.insert("/music/other.wav");
 failed_remove="/music/anchor.lrc";
 assert(!sdcar_delete_music_track(0,generation)&&sdcar_music_track_valid(0)&&files.count("/music/anchor.wav"));
 failed_remove.clear();assert(sdcar_delete_music_track(0,generation));
 assert(!files.count("/music/anchor.wav")&&!files.count("/music/anchor.lrc")&&files.count("/music/other.wav"));
 assert(!sdcar_music_track_valid(0)&&sdcar_next_music_track(127,1,true)==1&&sdcar_next_music_track(127,1,false)==-1);
 assert(sdcar_register_music_track("/music/replacement.wav")==0);
 assert(!sdcar_delete_music_track(0,generation));
 assert(sdcar_copy_music_track(0,path,sizeof(path),&generation)&&!strcmp(path,"/music/replacement.wav"));
 files.insert(path);files.insert("/music/replacement.LRC");failed_remove=path;
 assert(!sdcar_delete_music_track(0,generation)&&files.count(path)&&sdcar_music_track_valid(0));
 failed_remove.clear();assert(sdcar_delete_music_track(0,generation));
 assert(!files.count(path)&&!files.count("/music/replacement.LRC"));
 for(int n=0;n<500;++n){assert(sdcar_register_music_track("/music/repeat.wav")==0);assert(sdcar_copy_music_track(0,path,sizeof(path),&generation));assert(sdcar_delete_music_track(0,generation));}
 printf("Music catalog passed: concurrent publication, immutable existing indices, dedup/path bounds/capacity; reader observations=%u\n",observations.load());
}
'''
    with tempfile.TemporaryDirectory(prefix='music-catalog-', ignore_cleanup_errors=True) as tmp:
        directory = Path(tmp)
        unit, exe = directory/'test.cpp', directory/'test.exe'
        unit.write_text(scaffold+declarations+bodies+cases, encoding='utf-8')
        subprocess.run([compiler, '/nologo', '/std:c++17', '/EHsc', '/utf-8', '/D_CRT_SECURE_NO_WARNINGS',
                        str(unit), f'/Fe:{exe}', f'/Fo{directory}\\'], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
