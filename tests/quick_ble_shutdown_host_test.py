"""Inject BLE stop/deinit failures into the production shutdown function."""
from pathlib import Path
import argparse,re,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
SDK=r'''
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdint>
using esp_err_t=int;
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdMS_TO_TICKS(x) (x)
#define BLE_ERR_REM_USER_CONN_TERM 0x13
static bool ble_initialized,ble_host_started;
static std::atomic<bool> ble_stack_up,ble_cancel,ble_synced,ble_adv_ok,ble_host_loop_exited;
static std::atomic<int> conn_count;
static int conn_handle,ble_mux,fault,owned,deinits;
static unsigned elapsed;
static uint32_t app_millis(){return elapsed;}
static void vTaskDelay(unsigned ms){elapsed+=ms;}
static int ble_gap_terminate(int,int){return 0;}
static int ble_gap_adv_stop(){return 0;}
static const char *esp_err_to_name(int){return "injected";}
static int nimble_port_stop(){if(fault==1)return 1;ble_host_loop_exited=true;return 0;}
static int esp_nimble_disable(){assert(ble_host_loop_exited);return 0;}
static int nimble_port_deinit(){assert(!ble_host_started);deinits++;if(fault==2)return 1;owned=0;return 0;}
'''
CASES=r'''
int main(){
 for(int error=0;error<=2;error++){
  ble_initialized=ble_host_started=ble_stack_up=ble_synced=ble_adv_ok=true;
  ble_host_loop_exited=ble_cancel=false;conn_handle=-1;owned=1;deinits=0;fault=error;
  bool result=quick_ble_force_down();
  if(error){
   assert(!result && owned && ble_initialized);
   if(error==1)assert(deinits==0 && ble_host_started);
   fault=0;assert(quick_ble_force_down());
  }else assert(result);
  assert(!owned && !ble_initialized && !ble_host_started && !ble_stack_up);
  assert(quick_ble_force_down());
 }
 puts("BLE shutdown passed: stop timeout never frees a live host; deinit failure retains ownership; retries and repeated close succeed");
}
'''
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--cxx');args=parser.parse_args()
    compiler=args.cxx or shutil.which('cl') or shutil.which('c++')
    if not compiler:parser.error('Use a C++ developer terminal or --cxx')
    source=(ROOT/'src/QuickBluetooth.cpp').read_text(encoding='utf-8')
    match=re.search(r'static bool quick_ble_force_down\(void\)\s*\{',source);assert match
    end,depth=match.end(),1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    with tempfile.TemporaryDirectory(prefix='ble-shutdown-',ignore_cleanup_errors=True) as temporary:
        folder=Path(temporary);unit=folder/'ble.cpp';exe=folder/'ble.exe'
        unit.write_text(SDK+source[match.start():end]+CASES,encoding='utf-8')
        if Path(compiler).stem.lower()=='cl':cmd=[compiler,'/nologo','/std:c++17','/EHsc',str(unit),'/Fe:'+str(exe),'/Fo:'+str(folder/'ble.obj')]
        else:cmd=[compiler,'-std=c++17',str(unit),'-o',str(exe)]
        subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
