"""Run the real WiFi initialization/cleanup against injected SDK failures."""
from pathlib import Path
import argparse,re,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]

def extract(text,name):
 m=re.search(r'static (?:bool|void) '+name+r'\([^;{}]*\)\s*\{',text)
 assert m,name
 end,depth=m.end(),1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[m.start():end]

SDK=r'''
#include <cassert>
#include <cstddef>
#include <cstdio>
typedef int esp_err_t;
typedef int esp_event_base_t;
typedef void *esp_event_handler_instance_t;
struct esp_netif_t {};
struct wifi_init_config_t {int nvs_enable;};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
enum {ESP_OK,ESP_FAIL,ESP_ERR_NO_MEM,ESP_ERR_INVALID_STATE,WIFI_EVENT,IP_EVENT,ESP_EVENT_ANY_ID,IP_EVENT_STA_GOT_IP,IP_EVENT_STA_LOST_IP,WIFI_STORAGE_RAM,WIFI_MODE_STA};
static bool netif_ready,wifi_started,wifi_driver_ready;
static esp_netif_t *sta_netif;
static esp_event_handler_instance_t wifi_any_instance,ip_got_instance,ip_lost_instance;
static int fault,netifs,handlers,registrations,driver_inits,loop_creates,netif_creates;
static bool driver;
static bool nvs_ready(){return true;}
static void log_internal_heap(const char *,esp_err_t){}
static int esp_netif_init(){return fault==1?ESP_FAIL:ESP_OK;}
static int esp_event_loop_create_default(){++loop_creates;return fault==2?ESP_FAIL:ESP_OK;}
static void wifi_event_handler(void *,esp_event_base_t,int,void *){}
static int esp_event_handler_instance_unregister(esp_event_base_t,int,void *h){assert(h && handlers>0);--handlers;return ESP_OK;}
static int esp_event_handler_instance_register(esp_event_base_t,int,void (*)(void *,esp_event_base_t,int,void *),void *,void **out){
 ++registrations;if(fault==4+registrations)return ESP_FAIL;
 *out=reinterpret_cast<void *>(static_cast<size_t>(registrations));++handlers;return ESP_OK;}
static esp_netif_t *esp_netif_create_default_wifi_sta(){++netif_creates;++netifs;return new esp_netif_t;}
static void esp_netif_destroy_default_wifi(esp_netif_t *p){assert(p && netifs==1);delete p;--netifs;}
static int esp_wifi_init(const wifi_init_config_t *){++driver_inits;if(fault==3)return ESP_FAIL;driver=true;return ESP_OK;}
static int esp_wifi_set_storage(int){return fault==4?ESP_FAIL:ESP_OK;}
static int esp_wifi_set_mode(int){return fault==8?ESP_FAIL:ESP_OK;}
static int esp_wifi_start(){return fault==9?ESP_FAIL:ESP_OK;}
static int esp_wifi_stop(){return ESP_OK;}
static int esp_wifi_deinit(){assert(driver);driver=false;return ESP_OK;}
'''
CASES=r'''
int main(){
 for(int error=1;error<=9;++error){
  fault=error;registrations=driver_inits=loop_creates=netif_creates=0;netif_ready=false;
  assert(!ensure_wifi_started());
  assert(!wifi_started && !wifi_driver_ready && !driver && netifs==0 && handlers==0);
  fault=0;registrations=0;
  assert(ensure_wifi_started());assert(wifi_started && driver && netifs==1 && handlers==3);
  const int created=netif_creates,initialized=driver_inits,registered=registrations;
  assert(ensure_wifi_started());
  assert(netif_creates==created && driver_inits==initialized && registrations==registered);
  release_wifi_attempt();assert(!wifi_started && !driver && netifs==0 && handlers==0);
 }
 puts("WiFi lifecycle passed: 9 initialization faults clean up, retry succeeds, repeated start creates no duplicates");
}
'''
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cxx');args=p.parse_args()
 cc=args.cxx or shutil.which('cl') or shutil.which('c++') or shutil.which('g++')
 if not cc:p.error('Run in a C++ developer terminal or pass --cxx')
 source=(ROOT/'src/setwifi.cpp').read_text(encoding='utf-8')
 names=['unregister_wifi_handlers','release_wifi_attempt','ensure_netif','register_wifi_handlers','ensure_wifi_started']
 body='\n'.join(extract(source,n) for n in names)
 with tempfile.TemporaryDirectory(prefix='wifi-lifecycle-',ignore_cleanup_errors=True) as temporary:
  unit=Path(temporary)/'wifi.cpp';exe=Path(temporary)/'wifi.exe'
  unit.write_text(SDK+body+CASES,encoding='utf-8')
  if Path(cc).stem.lower()=='cl':cmd=[cc,'/nologo','/std:c++17','/EHsc','/utf-8',str(unit),'/Fe:'+str(exe),'/Fo:'+str(Path(temporary)/'wifi.obj')]
  else:cmd=[cc,'-std=c++17',str(unit),'-o',str(exe)]
  subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
