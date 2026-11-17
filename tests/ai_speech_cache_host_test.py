"""Use actual Storage.cpp + AiSpeech.cpp to verify physical temporary-file cleanup."""
from pathlib import Path
import tempfile,subprocess,shutil
from ai_direct_client_host_test import ROOT, HEADERS, HARNESS
TAIL=r"""
#include <filesystem>
static void reset_cache_test(){
 assert(!sound_active.load());
 now=0;pos=0;read_limit=4096;incomplete=eagain=fail_open=fail_audio=fail_begin=fail_finish=fail_alloc=fail_large=fail_task=false;
 status=200;declared=-2;initialized=cleaned=audio_finish=begin_calls=0;sent.clear();auth.clear();played.clear();
 eagain_every=stalls_left=read_calls=0;cut_at=-1;workers_go=false;log_capture.clear();
 http_delay_ms=header_delay_ms=play_time_add=read_request_max=0;
 allocation_attempts=fail_at=0;stall_at=131072;reads_without_wait=0;fail_auth_header=fail_header=cancel_play=cancel_read=begin_deadline_stall=fail_client=false;
 content_type="audio/pcm; rate=24000; bits=16; channels=1";sd=true;
 wifi_ps_mode=WIFI_PS_NONE; wifi_get_error=wifi_set_error=wifi_restore_error=wifi_get_calls=wifi_set_calls=wifi_restore_transient=0;
 wifi_ps_at_prepare=wifi_ps_at_read=wifi_ps_at_i2s=wifi_sets_at_i2s=-1;
 sample_on_dir=sample_on_flush=sample_on_begin=flush_sampled=false; stall_in_flush_ms=0;
 status_at_dir=status_flush_before=status_flush_after=status_at_begin={};
}
int main(int argc,char **argv){
 assert(argc==2);storage_host_set_root(argv[1]);main_id=std::this_thread::get_id();
 ai_api_config_t cfg={};strcpy(cfg.api_base,"https://api.deepseek.com");strcpy(cfg.api_key,"test-not-real");cfg.speak=true;
 char error[128];
 auto speak=[&]{
  bool ok=ai_direct_speak(&cfg,"测试语音",error,sizeof(error));
  assert(!storage_exists("/.cache/breeze-ai-speech.pcm"));
  assert(!sound_active.load()&&!live_tasks.load()&&!heap_live);
  assert(std::filesystem::is_empty(std::filesystem::path(argv[1])/".cache"));
  return ok;
 };
 reset_cache_test();response=std::string(160000,'x');stalls_left=1;
 assert(speak()&&begin_calls==1&&audio_finish==1&&initialized==cleaned);
 reset_cache_test();response=std::string(160000,'x');cut_at=80000;assert(!speak()&&begin_calls==0);
 reset_cache_test();response="xx";fail_header=true;assert(!speak()&&begin_calls==0);
 reset_cache_test();response="xxx";assert(!speak()&&begin_calls==0);
 reset_cache_test();response=std::string(160000,'x');cancel_read=true;assert(!speak()&&begin_calls==0);
 reset_cache_test();response=std::string(160000,'x');cancel_play=true;assert(!speak()&&audio_finish==1);
 reset_cache_test();response="xx";fail_audio=true;assert(!speak()&&audio_finish==1);
 {
  StorageStream old;assert(old.open("/.cache/breeze-ai-speech.pcm","wb"));assert(old.write("stale",5)==5);old.close();
  assert(storage_exists("/.cache/breeze-ai-speech.pcm"));assert(ai_speech_clear_cache()&&!storage_exists("/.cache/breeze-ai-speech.pcm"));
 }
 puts("actual Storage + complete speech: play only after HTTP close; success/cancel/download/PCM/I2S failures and reboot stale-file cleanup PASS");
}
"""
def main():
 cc=shutil.which('cl');assert cc
 with tempfile.TemporaryDirectory(prefix='speech-cache-') as temp:
  d=Path(temp)
  for n,content in HEADERS.items():
   p=d/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(content,encoding='utf-8')
  # Keep endpoint, heap and RTOS fakes; replace all storage substitutes with production Storage.cpp.
  text=HARNESS[:HARNESS.index('static std::string cached_pcm;')]
  text+='#define NOMINMAX'+chr(10)+'#include "@ROOT@/src/Storage.cpp"'+chr(10)
  text+=HARNESS[HARNESS.index('int nvs_open'):HARNESS.index('static void reset(){')]
  text+=TAIL
  unit=d/'test.cpp';unit.write_text(text.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8')
  exe=d/'test.exe';card=d/'card';card.mkdir()
  sdk=Path('C:/Espressif/frameworks/esp-idf-v5.5.6/components/json/cJSON')
  cmd=[cc,'/nologo','/std:c++17','/EHsc','/utf-8','/DSTORAGE_HOST_TEST','/D_CRT_SECURE_NO_WARNINGS','/I'+str(d),'/I'+str(ROOT/'src'),'/I'+str(sdk),str(unit),str(sdk/'cJSON.c'),'/Fe:'+str(exe),'/Fo'+str(d)+'\\\\']
  r=subprocess.run(cmd,capture_output=True)
  if r.returncode:print((r.stdout+r.stderr).decode(errors='replace')[-5000:]);raise SystemExit(r.returncode)
  subprocess.run([str(exe),str(card)],check=True,timeout=60)
if __name__=='__main__':main()
