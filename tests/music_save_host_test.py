"""Actual MP3 decoding, resampling, WAV/LRC files and Storage fault paths.
Run in a VS developer terminal with --fixture pointing to a real MP3.
"""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#define SMARTVOICEBOX_IDF_RUNTIME_H
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <string>
#include <thread>
#include <algorithm>
#include "MusicSave.h"
#include "Storage.h"
#include "Mp3Source.h"
#include "PcmResampler.h"
extern "C" {int fail_alloc=0;int fail_mp3_alloc=0;}
uint64_t real_storage_free_bytes(void);
static uint32_t milliseconds;
static uint64_t forced_free=UINT64_MAX;
uint64_t storage_free_bytes(void){return forced_free==UINT64_MAX?real_storage_free_bytes():forced_free;}
static uint32_t app_millis(){return milliseconds;}
static void app_delay_ms(uint32_t n){milliseconds+=n;}
static size_t strlcpy(char*d,const char*s,size_t cap){size_t n=strlen(s);if(cap){size_t k=std::min(n,cap-1);memcpy(d,s,k);d[k]=0;}return n;}
#include "@ROOT@/src/MusicSave.cpp"
static bool collect(void *user,const int16_t *p,size_t frames){auto*v=(std::vector<int16_t>*)user;v->insert(v->end(),p,p+frames*2);return true;}
static bool reject(void*,const int16_t*,size_t){return false;}
static void resampler_tests(){
 for(unsigned rate:{8000u,11025u,16000u,22050u,24000u,32000u,44100u,48000u})for(unsigned channels:{1u,2u}){
  const size_t count=4099;std::vector<int16_t> input(count*channels);
  for(size_t i=0;i<input.size();++i)input[i]=(int16_t)((i*11713u)%65536u-32768);
  std::vector<int16_t> expected;
  const size_t outputs=(count*44100ull+rate-1)/rate;
  for(size_t k=0;k<outputs;++k){uint64_t pos=k*(uint64_t)rate;size_t left=pos/44100;int64_t fraction=pos%44100;
   for(unsigned c=0;c<2;++c){unsigned ch=channels==1?0:c;int16_t value;
    if(left+1>=count)value=input[(count-1)*channels+ch];
    else{int64_t delta=(int64_t)((int32_t)input[(left+1)*channels+ch]-input[left*channels+ch])*fraction;
     value=input[left*channels+ch]+(delta+(delta>=0?22050:-22050))/44100;}
    expected.push_back(value);
   }
  }
  for(size_t chunk:{1u,7u,1152u,5000u})for(size_t capacity:{1u,1024u}){
   std::vector<int16_t> output,buffer(capacity*2);pcm_resampler_t r;
   assert(pcm_resampler_init(&r,rate,channels,buffer.data(),capacity,collect,&output));
   for(size_t at=0;at<count;at+=chunk)assert(pcm_resampler_push(&r,input.data()+at*channels,std::min(chunk,count-at)));
   assert(pcm_resampler_finish(&r)&&pcm_resampler_finish(&r)&&r.output_frames==outputs&&output==expected);
   assert(!pcm_resampler_push(&r,input.data(),1));
  }
 }
 int16_t buffer[4],sample[2]={-32768,32767};pcm_resampler_t r;std::vector<int16_t> out;
 assert(!pcm_resampler_init(&r,0,2,buffer,2,collect,&out));assert(!pcm_resampler_push(&r,sample,1));
 assert(!pcm_resampler_init(&r,44100,3,buffer,2,collect,&out));
 assert(pcm_resampler_init(&r,44100,2,buffer,1,reject,nullptr));assert(!pcm_resampler_push(&r,sample,1)&&!pcm_resampler_finish(&r));
 assert(pcm_resampler_init(&r,8000,1,buffer,2,collect,&out));assert(!pcm_resampler_finish(&r));
 for(unsigned rate:{8000u,48000u}){out.clear();assert(pcm_resampler_init(&r,rate,1,buffer,2,collect,&out));assert(pcm_resampler_push(&r,sample,1)&&pcm_resampler_finish(&r));assert(out.size()/2==(44100+rate-1)/rate);}
 assert(pcm_resampler_init(&r,44100,2,buffer,2,collect,&out));r.input_frames=UINT64_MAX;assert(!pcm_resampler_push(&r,sample,1));
}
static uint64_t pcm_hash(const char *path,uint64_t *frames=nullptr,uint32_t *input_rate=nullptr){
 auto*d=mp3_source_open(path);assert(d);mp3_source_frame_t f;int rc;uint64_t hash=1469598103934665603ull,n=0;
 while((rc=mp3_source_read_frame(d,&f))==1){if(input_rate)*input_rate=f.sample_rate;n+=f.frames;for(unsigned i=0;i<f.frames*f.channels;++i){hash^=(uint16_t)f.pcm[i];hash*=1099511628211ull;}}
 assert(rc==0);mp3_source_close(d);if(frames)*frames=n;return hash;
}
static int active_calls, cancel_at;
static bool current_job(uint32_t){return !cancel_at||++active_calls<cancel_at;}
static std::string read_file(const char *path){StorageStream file;assert(file.open(path,"rb"));std::string s(file.byte_count(),'\0');assert(file.read(s.data(),s.size())==s.size());return s;}
static void write_file(const char *path,const std::string &text){StorageStream file;assert(file.open(path,"wb")&&file.write(text.data(),text.size())==text.size()&&file.flush());}
int main(int argc,char**argv){
 assert(argc==3);storage_host_set_root(argv[1]);assert(storage_mkdir("/.online")&&storage_mkdir("/music"));
 resampler_tests();
 char native[320];assert(storage_vfs_path("/.online/input.mp3",native,sizeof(native)));
 FILE*in=fopen(argv[2],"rb");FILE*out=fopen(native,"wb");assert(in&&out);char chunk[8192];size_t n;while((n=fread(chunk,1,sizeof(chunk),in)))assert(fwrite(chunk,1,n,out)==n);fclose(in);fclose(out);
 uint64_t input_frames;uint32_t input_rate=0;const uint64_t expected_hash=pcm_hash("/.online/input.mp3",&input_frames,&input_rate);uint64_t a,b;
 std::thread first([&](){a=pcm_hash("/.online/input.mp3");}),second([&](){b=pcm_hash("/.online/input.mp3");});first.join();second.join();assert(a==expected_hash&&b==expected_hash);
 music_save_result_t result;char error[128];const char *lrc="[00:00.00]Original test line\n[00:01.00]Second line\n";
 assert(music_save_mp3("/.online/input.mp3","Test song","Artist","123",lrc,1,current_job,nullptr,&result,error,sizeof(error)));
 assert(result.lyrics_saved&&!result.reused&&result.pcm_bytes>100000&&strlen(result.wav_path)<64);
 assert(!strchr(result.wav_path,'[')&&!strchr(result.wav_path,']'));
 const auto wave=read_file(result.wav_path);assert(wave.size()==result.pcm_bytes+44&&wave.substr(0,4)=="RIFF"&&get32((const uint8_t*)wave.data()+24)==44100);
 /* Independent duration oracle, including the final input interval. */
 assert(result.pcm_bytes/4==(input_frames*44100+input_rate-1)/input_rate);
 assert(read_file(result.lrc_path)==lrc);const std::string original_path=result.wav_path,original_lrc=result.lrc_path;
 assert(music_save_mp3("missing.mp3","Test song","Artist","123","[00:00.00]do not replace",1,current_job,nullptr,&result,error,sizeof(error))&&result.reused&&result.lyrics_saved);
 assert(read_file(original_path.c_str())==wave&&read_file(original_lrc.c_str())==lrc);
 /* Short real MPEG stream keeps fault tests quick. */
 auto mp3=read_file("/.online/input.mp3");write_file("/.online/short.mp3",mp3.substr(0,65536));
 assert(paths("Original file","","222",&result));write_file(result.wav_path,"user file");
 assert(!music_save_mp3("/.online/short.mp3","Original file","","222",lrc,1,current_job,nullptr,&result,error,sizeof(error))&&read_file(result.wav_path)=="user file");
 forced_free=1024;assert(!music_save_mp3("/.online/short.mp3","Low space","","333",lrc,1,current_job,nullptr,&result,error,sizeof(error)));forced_free=UINT64_MAX;
 active_calls=0;cancel_at=6;assert(!music_save_mp3("/.online/input.mp3","Cancel","","444",lrc,1,current_job,nullptr,&result,error,sizeof(error)));cancel_at=0;
 assert(!storage_exists(result.wav_path)&&!storage_exists("/.online/save.wav.part")&&!storage_exists("/.online/save.lrc.part"));
 fail_alloc=1;assert(!music_save_mp3("/.online/short.mp3","OOM","","555",lrc,1,current_job,nullptr,&result,error,sizeof(error)));fail_alloc=0;
 fail_mp3_alloc=1;assert(!music_save_mp3("/.online/short.mp3","OOM","","556",lrc,1,current_job,nullptr,&result,error,sizeof(error)));fail_mp3_alloc=0;
 storage_host_hooks.flush_count=0;storage_host_hooks.fail_flush_at=1;
 assert(!music_save_mp3("/.online/short.mp3","Flush fail","","666",lrc,1,current_job,nullptr,&result,error,sizeof(error)));storage_host_hooks.fail_flush_at=0;
 assert(!storage_exists(result.wav_path)&&!storage_exists("/.online/save.wav.part"));
 assert(paths("Rename fail","","777",&result));std::string failed_dest=result.wav_path;storage_host_hooks.fail_rename_dest=failed_dest.c_str();
 assert(!music_save_mp3("/.online/short.mp3","Rename fail","","777",lrc,1,current_job,nullptr,&result,error,sizeof(error)));storage_host_hooks.fail_rename_dest=nullptr;
 assert(!storage_exists(result.wav_path)&&!storage_exists("/.online/save.wav.part"));
 assert(paths("Lyric fail","","888",&result));failed_dest=result.lrc_path;storage_host_hooks.fail_rename_dest=failed_dest.c_str();
 assert(music_save_mp3("/.online/short.mp3","Lyric fail","","888",lrc,1,current_job,nullptr,&result,error,sizeof(error))&&!result.lyrics_saved&&error[0]&&storage_is_file(result.wav_path));storage_host_hooks.fail_rename_dest=nullptr;
 assert(music_save_mp3("missing.mp3","Lyric fail","","888",lrc,1,current_job,nullptr,&result,error,sizeof(error))&&result.reused&&result.lyrics_saved);
 assert(music_save_mp3("/.online/short.mp3","Instrumental","","999",nullptr,1,current_job,nullptr,&result,error,sizeof(error))&&!result.lyrics_saved&&!storage_exists(result.lrc_path));
 assert(paths("../../很长很长的歌名/\\:*?\"<>|.曲目","很长很长歌手名","12345678901234567890",&result));
 assert(strlen(result.wav_path)<64&&!strstr(result.wav_path,"..")&&!strchr(result.wav_path+7,'/'));
 assert(!paths("a","b","../1",&result));
 assert(paths("茉莉花","银临 / 黄诗扶","2013109760",&result));
 assert(!strcmp(result.wav_path,"/music/茉莉花 - 银临 _ 黄诗扶.wav"));
 assert(!strcmp(result.lrc_path,"/music/茉莉花 - 银临 _ 黄诗扶.lrc"));
 assert(!storage_exists("/.online/save.wav.part")&&!storage_exists("/.online/save.lrc.part"));
 write_file("/result-path.txt",original_path);
 printf("Music save passed: all input rates/chunks vs rational PCM oracle, concurrent real MP3 hashes, full WAV duration/header/LRC, dedup/cancel/OOM/SD flush/rename/lyric isolation. PCM bytes=%llu\n",(unsigned long long)(wave.size()-44));
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    args = parser.parse_args()
    compiler = shutil.which('cl')
    if not compiler:
        parser.error('Use a Visual Studio C++ developer terminal')
    with tempfile.TemporaryDirectory(prefix='music-save-', ignore_cleanup_errors=True) as tmp:
        directory = Path(tmp)
        sd = directory/'sd'
        sd.mkdir()
        (directory/'esp_attr.h').write_text('#pragma once\n#define EXT_RAM_BSS_ATTR\n')
        (directory/'esp_heap_caps.h').write_text('''#pragma once
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
extern int fail_alloc,fail_mp3_alloc;
#ifdef __cplusplus
}
#endif
enum {MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2};
static inline void*heap_caps_malloc(size_t n,int c){(void)c;return fail_alloc?NULL:malloc(n);}
static inline void*heap_caps_calloc(size_t n,size_t s,int c){(void)c;return fail_mp3_alloc?NULL:calloc(n,s);}
#define heap_caps_free(p) free(p)
''')
        unit = directory/'test.cpp'
        unit.write_text(HARNESS.replace('@ROOT@', ROOT.as_posix()), encoding='utf-8')
        storage = directory/'storage.cpp'
        storage.write_text('#define storage_free_bytes real_storage_free_bytes\n#include "'+(ROOT/'src/Storage.cpp').as_posix()+'"\n')
        exe = directory/'save.exe'
        subprocess.run([compiler, '/nologo', '/TP', '/std:c++17', '/EHsc', '/utf-8', '/DSTORAGE_HOST_TEST',
                        '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS', f'/I{directory}', f'/I{ROOT/"src"}',
                        f'/I{ROOT/"components/minimp3/include"}', str(unit), str(storage),
                        str(ROOT/'src/PcmResampler.c'), str(ROOT/'src/Mp3Source.c'),
                        f'/Fe:{exe}', f'/Fo{directory}\\'], check=True)
        subprocess.run([str(exe), str(sd), str(args.fixture.resolve())], check=True)
        logical = (sd/'result-path.txt').read_text()
        with wave.open(str(sd/logical.lstrip('/')), 'rb') as audio:
            assert audio.getnchannels() == 2 and audio.getsampwidth() == 2 and audio.getframerate() == 44100
            assert audio.getnframes() > 44100
            print('Python wave independently validated:', audio.getnframes(), 'frames at 44100 Hz, stereo PCM16')


if __name__ == '__main__':
    main()
