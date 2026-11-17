"""Execute actual AI worker/routing/publication with bounded SDK endpoint fakes."""
from pathlib import Path
import subprocess, tempfile, shutil
from alarm_audio_host_test import function
ROOT=Path(__file__).resolve().parents[1]
FAKE=r'''
#include "AiConfig.h"
#include "AiSpeech.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <string>
static std::atomic<bool> ai_chat_busy{false},ai_chat_pending{false};
static std::mutex ai_request_mux,result_mux;
static std::mutex *ai_result_mutex=&result_mux;
static uint32_t ai_result_revision;
static char ai_chat_request[513],ai_chat_response[8193],ai_direct_response[8193];
static char ai_previous_question[513],ai_previous_answer[8193],ai_chat_error[128];
static int ai_chat_result_state,ai_chat_task_handle=1,notifications;
static bool connected,configured,local_alarm,cloud_ok=true,music_during_speech;
static int clouds,speeches,alarms,cancels;
static ai_speech_status_t speech_state;
extern "C" void ai_speech_get_status(ai_speech_status_t *s){*s=speech_state;}
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(x) (x)
#define portENTER_CRITICAL(p) ((p)->lock())
#define portEXIT_CRITICAL(p) ((p)->unlock())
static size_t strlcpy(char*d,const char*s,size_t n){size_t k=strlen(s);if(n){size_t take=k<n-1?k:n-1;memcpy(d,s,take);d[take]=0;}return k;}
static int xSemaphoreTake(std::mutex *m,unsigned wait){if(wait){m->lock();return 1;}return m->try_lock();}
static void xSemaphoreGive(std::mutex*m){m->unlock();}
struct End{};
static void ulTaskNotifyTake(int,unsigned){if(!ai_chat_pending.load())throw End{};}
static void xTaskNotifyGive(int){++notifications;}
static uint32_t online_music_get_result(char*,size_t){return 0;}
static bool handle_music_command(const char*){return false;}
static bool handle_volume_command(const char*){return false;}
static bool handle_weather_command(const char*){return false;}
static bool get_wifi_connected(){return connected;}
extern "C" bool ai_config_get(ai_api_config_t *c){c->speak=true;return configured;}
extern "C" const char *ai_config_status(){return "No API key";}
static bool alarm_ai_handle(const char*,char*out,size_t n){++alarms;if(local_alarm)strlcpy(out,"alarm saved",n);return local_alarm;}
static void build_ai_server_prompt(char*out,size_t n){strlcpy(out,ai_chat_request,n);}
static void format_weather_reply(char*out,size_t n){strlcpy(out,"weather",n);}
static bool ai_direct_chat_context(const ai_api_config_t*,const char*,const char*,const char*,char*out,size_t n,char*err,size_t en){
 ++clouds;if(cloud_ok){std::string story(7000,'A');strlcpy(out,story.c_str(),n);}else strlcpy(err,"cloud failed",en);return cloud_ok;
}
static uint32_t publish_result(const char*,const char*,int);
extern "C" bool ai_direct_speak(const ai_api_config_t*,const char*,char*err,size_t n){
 ++speeches;if(music_during_speech)publish_result("music reply","",1);strlcpy(err,"speech failed",n);return false;
}
extern "C" void ai_speech_cancel(){++cancels;}
'''
TAIL=r'''
static void run(const char *q){assert(ai_chat_submit(q));try{ai_chat_task(nullptr);}catch(End&){}assert(!ai_chat_busy&&!ai_chat_pending);}
static void reset(){clouds=speeches=alarms=notifications=0;connected=configured=local_alarm=music_during_speech=false;cloud_ok=true;ai_chat_response[0]=ai_chat_error[0]=0;ai_chat_result_state=0;}
int main(){
 char status[96];assert(!ai_chat_speech_status(status,sizeof(status)));
 speech_state={AI_SPEECH_CACHING,50,200,2500,65536};
 assert(ai_chat_speech_status(status,sizeof(status))&&strstr(status,"25%")&&strstr(status,"64KiB/s")&&strstr(status,"2s"));
 speech_state.expected_bytes=0;speech_state.cached_bytes=1536;
 assert(ai_chat_speech_status(status,sizeof(status))&&strstr(status,"1KiB")&&strstr(status,"64KiB/s"));
 char tiny[8];assert(ai_chat_speech_status(tiny,sizeof(tiny))&&tiny[7]==0);
 speech_state.phase=AI_SPEECH_PLAYING;assert(ai_chat_speech_status(status,sizeof(status))&&strstr(status,"缓存完成"));
 speech_state.phase=AI_SPEECH_IDLE;
 reset();local_alarm=true;run("set alarm");assert(!clouds&&!speeches&&alarms==1&&!strcmp(ai_chat_response,"alarm saved"));
 reset();run("question");assert(!clouds&&ai_chat_result_state==-1&&strstr(ai_chat_error,"WiFi"));
 reset();connected=true;run("question");assert(!clouds&&ai_chat_result_state==-1&&strstr(ai_chat_error,"API"));
 reset();connected=configured=true;run("story");assert(clouds==1&&speeches==1&&strlen(ai_chat_response)==7000&&strlen(ai_previous_answer)==7000&&!strcmp(ai_previous_question,"story"));
 reset();connected=configured=true;music_during_speech=true;run("continue");assert(!strcmp(ai_chat_response,"music reply")&&!ai_chat_error[0]);
 reset();assert(ai_chat_submit("one")&&!ai_chat_submit("two")&&!strcmp(ai_chat_request,"one"));try{ai_chat_task(nullptr);}catch(End&){}
 assert(ai_chat_submit("停止播报")&&cancels==1);ai_chat_stop_speech();assert(cancels==2);
 // Reader must never observe mixed answer/error generations during publication.
 publish_result("0","0",1);
 std::thread publisher([]{for(int i=1;i<=3000;i++){char n[32];snprintf(n,sizeof(n),"%d",i);publish_result(n,n,1);}});
 for(int i=0;i<3000;i++){char answer[8193],error[128];if(ai_chat_get_result(answer,sizeof(answer),error,sizeof(error))==1)assert(!strcmp(answer,error));}
 publisher.join();puts("AI routing: offline alarms, cloud gating, long history, speech cancellation, stale error and atomic snapshot PASS");
}
'''
def main():
 cc=shutil.which('cl');assert cc
 source=(ROOT/'src/Web_ai.cpp').read_text(encoding='utf-8')
 names=['publish_result','publish_speech_error','ai_chat_task','ai_chat_submit','ai_chat_stop_speech','ai_chat_speech_status']
 # Getter returns int; preserve its body with the same brace scanner.
 getter=function(source.replace('extern "C" int ai_chat_get_result','extern "C" bool ai_chat_get_result'),'ai_chat_get_result').replace('extern "C" bool ai_chat_get_result','extern "C" int ai_chat_get_result')
 unit_text=FAKE+'\n'+'\n'.join(function(source,n)for n in names)+'\n'+getter+TAIL
 with tempfile.TemporaryDirectory(prefix='ai-routing-')as temp:
  d=Path(temp);unit=d/'test.cpp';unit.write_text(unit_text,encoding='utf-8');exe=d/'test.exe'
  r=subprocess.run([cc,'/nologo','/std:c++17','/O2','/EHsc','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/I'+str(ROOT/'src'),str(unit),'/Fe:'+str(exe),'/Fo'+str(d)+'\\'],capture_output=True)
  if r.returncode:print((r.stdout+r.stderr).decode(errors='replace'));raise SystemExit(r.returncode)
  subprocess.run([str(exe)],check=True,timeout=20)
if __name__=='__main__':main()
