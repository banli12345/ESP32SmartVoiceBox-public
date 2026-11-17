"""Run actual alarm playback/control code with timed I2S and SD fault fakes."""
from pathlib import Path
import subprocess, tempfile, shutil
import re
def function(source,name):
 match=re.search(r'((?:static\s+)?(?:extern\s+"C"\s+)?(?:bool|void|uint32_t|int)\s+'+name+r'\s*\([^;{}]*\)\s*)\{',source)
 assert match,name
 end,depth=match.end(),1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[match.start():end]

ROOT=Path(__file__).resolve().parents[1]
FAKE=r'''
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cassert>
static std::atomic<bool> alarm_clock_running{false};
static std::atomic<uint32_t> alarm_clock_started{0},alarm_clock_duration{0};
static bool alarm_audio_requested,alarm_audio_active,music_i2s_enabled;
static bool have_wav,fail_i2s,fail_setup,partial,stop_early;
static unsigned opens,closes,reads,writes,tones,notifications,acks;
static bool ai_release_request,ai_i2s_held,video_cmd_ok,video_running,video_i2s_count,video_prepared,video_need_rearm;
static bool music_audio_playing,video_session,music_resume_after_ai_audio,inject_ai;
static int music_control_mux,video_i2s_mux,audio_cmd_done;
static struct {int fd;} video_wav_file;
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
static void stop_wav_prefetch(){}
static void mute_music_i2s(){}
static void xSemaphoreGive(int){++acks;}
static uint32_t ticks;
static uint8_t music_audio_buffer[8192];
static int music_i2s_tx=1,music_audio_task_handle=1;
#define MUSIC_SAMPLE_RATE 44100u
#define ESP_OK 0
#define pdMS_TO_TICKS(x) (x)
static uint32_t app_millis(){return ticks;}
static void xTaskNotifyGive(int){++notifications;}
static bool disable_music_i2s_for_ai(){music_i2s_enabled=false;return !fail_setup;}
static bool set_output_sample_rate(uint32_t n){assert(n==44100);return true;}
static bool enable_music_i2s_channel(){music_i2s_enabled=true;return true;}
static bool open_alarm_wav(){++opens;return have_wav;}
static void sdcar_close_alarm(){++closes;}
static size_t sdcar_read_alarm(uint8_t *p,size_t n){++reads;memset(p,1,n);return reads%2 ? n : 0;}
static void apply_audio_volume(uint8_t*,size_t){}
extern "C" void sound_alarm_stop(void);
static int i2s_channel_write(int,const void *p,size_t n,size_t *done,int){
 ++writes;ticks+=100;if(inject_ai&&writes==1)ai_release_request=true;*done=partial?n/2:n;
 if(!have_wav){const int16_t *s=(const int16_t*)p;bool audible=false;
  for(size_t i=0;i<n/2;i+=2){assert(s[i]==s[i+1]);audible|=s[i]!=0;}tones+=audible;}
 if(stop_early&&writes==3)sound_alarm_stop();
 return fail_i2s?1:ESP_OK;
}
static void reset(){alarm_clock_running=false;alarm_audio_requested=alarm_audio_active=false;have_wav=fail_i2s=fail_setup=partial=stop_early=false;opens=closes=reads=writes=tones=notifications=acks=0;inject_ai=ai_release_request=ai_i2s_held=false;ticks=1000;}
'''
TAIL=r'''
int main(){
 reset();sound_alarm_start(1);assert(alarm_preempts_ai()&&notifications==1);play_alarm_blocking();
 assert(writes==600&&tones>300&&!sound_alarm_is_active()&&!alarm_audio_active&&!alarm_audio_requested);
 reset();have_wav=true;sound_alarm_start(2);play_alarm_blocking();assert(writes==1200&&opens>500&&closes>500&&!sound_alarm_is_active());
 reset();inject_ai=true;sound_alarm_start(1);play_alarm_blocking();assert(acks==1&&!ai_i2s_held&&!ai_release_request&&writes==600);
 reset();sound_alarm_start(60);stop_early=true;play_alarm_blocking();assert(writes==3&&!sound_alarm_is_active());
 reset();ticks=UINT32_MAX-1000;sound_alarm_start(1);play_alarm_blocking();assert(writes==600&&!sound_alarm_is_active());
 reset();sound_alarm_start(0);assert(!sound_alarm_is_active());sound_alarm_start(61);assert(!sound_alarm_is_active());
 for(int f=0;f<3;f++){reset();fail_setup=f==0;fail_i2s=f==1;partial=f==2;sound_alarm_start(1);play_alarm_blocking();assert(!sound_alarm_is_active()&&writes<=1);}
 reset();sound_alarm_start(1);sound_alarm_stop();play_alarm_blocking();assert(!writes&&!sound_alarm_is_active());
 puts("Alarm audio: duration/repeated WAV/no-SD tone/stop/wrap/I2S failures and priority PASS");
}
'''
def main():
 cc=shutil.which('cl');assert cc
 sound=(ROOT/'src/Sound.cpp').read_text(encoding='utf-8').replace('extern "C" ', '')
 video=(ROOT/'src/video_audio.inc').read_text(encoding='utf-8')
 source=FAKE+'\n'+'\n'.join(function(sound,n)for n in ['alarm_preempts_ai','sound_alarm_start','sound_alarm_stop','sound_alarm_is_active'])+'\n'+function(video,'handle_ai_release')+'\n'+function(video,'play_alarm_blocking')+TAIL
 with tempfile.TemporaryDirectory(prefix='alarm-audio-')as temp:
  d=Path(temp);unit=d/'test.cpp';unit.write_text(source,encoding='utf-8');exe=d/'test.exe'
  r=subprocess.run([cc,'/nologo','/std:c++17','/O2','/EHsc','/utf-8',str(unit),'/Fe:'+str(exe),'/Fo'+str(d)+'\\'],capture_output=True)
  if r.returncode:print((r.stdout+r.stderr).decode(errors='replace'));raise SystemExit(r.returncode)
  subprocess.run([str(exe)],check=True,timeout=20)
if __name__=='__main__':main()
