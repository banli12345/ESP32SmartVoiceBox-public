"""Compile the real parser + AI bridge + scheduler, including NVS failures."""
from pathlib import Path
import subprocess, shutil, tempfile
import alarm_service_host_test as backend
ROOT=backend.ROOT
EXTRA=r"""
static bool pause_service;
void app_delay_ms(uint32_t ms){alarm_test_ticks+=ms;if(!pause_service)alarm_service_poll();}
static void ask(const char *q,const char *contains){char out[512];assert(alarm_ai_handle(q,out,sizeof(out)));assert(strstr(out,contains));}
int main(){
 fresh();ask("帮我设置明天8点的闹钟","已保存");
 assert(snapshot().next_epoch==at(2026,11,17,8,0)&&snapshot().config.weekdays==0);
 ask("查看闹钟","2026年11月17日 08:00");ask("设置每天7点半的闹钟，响铃5分钟","已保存");
 assert(snapshot().config.weekdays==127&&snapshot().config.ring_minutes==5);
 ask("帮我设置周一、周三、周五8点的闹钟","已保存");
 assert(snapshot().config.weekdays==21&&snapshot().config.ring_minutes==5);
 ask("20分钟后提醒我","已保存");assert(snapshot().next_epoch==wall+1200&&snapshot().config.weekdays==0);
 ask("关闭闹钟","已关闭并保存");assert(!snapshot().config.enabled);
 ask("帮我设置今天6点的闹钟","已经过去");assert(!snapshot().config.enabled);
 fail_commit=true;ask("帮我设置明天8点的闹钟","保存失败");assert(!snapshot().config.enabled);fail_commit=false;
 auto before=stored;ask("讲一个设置闹钟的故事","INVALID"); // replaced below; stories must bypass local actions.
 return 0;
}
"""
def main():
 cc=shutil.which('cl');assert cc
 with tempfile.TemporaryDirectory(prefix='alarm-ai-') as temp:
  d=Path(temp);(d/'freertos').mkdir()
  (d/'freertos/FreeRTOS.h').write_text('#pragma once\n#include <mutex>\ntypedef std::mutex portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED {}\n#define portENTER_CRITICAL(p) ((p)->lock())\n#define portEXIT_CRITICAL(p) ((p)->unlock())\n')
  (d/'compat.h').write_text(backend.COMPAT+'\nvoid app_delay_ms(uint32_t);\n')
  # Keep the NVS ABI identical to the backend fault-injection harness.
  script=(ROOT/'tests/alarm_service_host_test.py').read_text()
  nvs=script.split("(d/'nvs.h').write_text("+chr(39)*3,1)[1].split(chr(39)*3+')',1)[0]
  (d/'nvs.h').write_text(nvs)
  source=backend.SOURCE.split('int main(){',1)[0]+'\n#include "AlarmAI.h"\n'+EXTRA
  source=source.replace('ask("讲一个设置闹钟的故事","INVALID"); // replaced below; stories must bypass local actions.',"""
 char reply[512];assert(!alarm_ai_handle("讲一个设置闹钟的故事",reply,sizeof(reply)));
 assert(!alarm_ai_handle("设置音量50",reply,sizeof(reply))&&stored==before);
 ask("设置8点和9点的两个闹钟","没有执行");assert(stored==before);
 wall=0;ask("帮我设置明天8点的闹钟","尚未校准");assert(stored==before);
 wall=at(2026,11,16,7,59,50);pause_service=true;
 ask("帮我设置明天8点的闹钟","正在保存");assert(snapshot().save_busy&&stored==before);
 pause_service=false;alarm_service_poll();assert(snapshot().config.enabled);
 ask("设置每天8点的闹钟","已保存");advance(10);assert(snapshot().ringing);
 ask("停止响铃","已停止");assert(!snapshot().ringing&&snapshot().config.enabled&&!audio_on);
 puts("AI alarm bridge: tomorrow/custom/daily/relative/query/off/stop, invalid intent, clock, commit failure and timeout PASS");
 """)
  unit=d/'test.cpp';unit.write_text(source,encoding='utf-8');exe=d/'test.exe'
  cmd=[cc,'/nologo','/std:c++17','/EHsc','/O2','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/I'+str(d),'/I'+str(ROOT/'src'),'/FI'+str(d/'compat.h'),str(unit),*[str(ROOT/'src'/n)for n in ['AlarmService.cpp','AlarmIntent.cpp','AlarmAI.cpp']],'/Fe:'+str(exe),'/Fo'+str(d)+'\\\\']
  r=subprocess.run(cmd,capture_output=True)
  if r.returncode:print((r.stdout+r.stderr).decode(errors='replace')[-4500:]);raise SystemExit(r.returncode)
  subprocess.run([str(exe)],check=True,timeout=30)
if __name__=='__main__':main()
