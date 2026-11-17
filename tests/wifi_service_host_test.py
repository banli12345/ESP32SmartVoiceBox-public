"""Run actual WiFi event handlers, requests and service poll against an asynchronous fake SDK."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r'^[^;\n]*\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


SDK = r'''
#include "WifiProfiles.h"
#include "setwifi.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using esp_err_t=int;using esp_event_base_t=int;using wifi_mode_t=int;
using esp_event_handler_instance_t=void*;using SemaphoreHandle_t=void*;using StaticSemaphore_t=int;using portMUX_TYPE=int;
struct esp_ip4_addr_t{uint32_t addr;};
struct esp_netif_t{int unused;};
struct esp_netif_ip_info_t{esp_ip4_addr_t ip,netmask,gw;};
struct wifi_event_sta_connected_t{uint8_t ssid[32];uint8_t ssid_len;};
struct wifi_event_sta_disconnected_t{uint8_t ssid[32];uint8_t ssid_len;uint16_t reason;};
struct wifi_event_sta_scan_done_t{uint32_t status;uint8_t number,scan_id;};
struct ip_event_got_ip_t{esp_netif_t*esp_netif;esp_netif_ip_info_t ip_info;bool ip_changed;};
struct wifi_ap_record_t{uint8_t ssid[33];int authmode;};
struct wifi_scan_config_t{bool show_hidden;int scan_type;struct{struct{int min,max;}active;}scan_time;int home_chan_dwell_time;};
struct wifi_config_t{struct{uint8_t ssid[32],password[64];int scan_method,failure_retry_cnt;struct{bool capable,required;}pmf_cfg;int sae_pwe_h2e;struct{int authmode;}threshold;}sta;};
enum{ESP_OK=0,ESP_FAIL=-1,ESP_ERR_WIFI_NOT_STARTED=-2,ESP_ERR_WIFI_STATE=-3,ESP_ERR_WIFI_NOT_CONNECT=-4};
enum{WIFI_EVENT=1,IP_EVENT=2,WIFI_EVENT_AP_STACONNECTED=1,WIFI_EVENT_AP_STADISCONNECTED,WIFI_EVENT_AP_START,WIFI_EVENT_STA_CONNECTED,WIFI_EVENT_STA_DISCONNECTED,WIFI_EVENT_SCAN_DONE,WIFI_EVENT_STA_STOP,IP_EVENT_STA_GOT_IP,IP_EVENT_STA_LOST_IP};
enum{WIFI_MODE_STA=1,WIFI_MODE_AP=2,WIFI_MODE_APSTA=3,WIFI_AUTH_OPEN=0,WIFI_AUTH_WEP=1,WIFI_ALL_CHANNEL_SCAN=0,WPA3_SAE_PWE_BOTH=0,WIFI_IF_STA=0,WIFI_SCAN_TYPE_ACTIVE=0,pdTRUE=1};
#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY -1
static int critical;
#define portENTER_CRITICAL(m) (++critical)
#define portEXIT_CRITICAL(m) (--critical)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
static bool gui,driver_scanning,driver_associated,driver_joining;
static unsigned scans,stops,connects,disconnects,sets,stores;
static uint32_t now_ms=100;
static uint8_t driver_scan_id;
static int mode=WIFI_MODE_APSTA;
static wifi_config_t last_config;
static esp_netif_t netif;
static esp_netif_ip_info_t live_ip;
static char driver_ssid[33];
static std::vector<wifi_ap_record_t> results;
static std::vector<uint8_t> persisted,pending;
static bool fail_commit,fail_next_connect;
static std::atomic<uint32_t> weather_retry_at{0};
static char wifi_ssid_storage[33],wifi_password_storage[64];
const char *WIFI_SSID=wifi_ssid_storage,*WIFI_PASSWORD=wifi_password_storage;
static uint32_t app_millis(){return now_ms;}
static void sdk(){assert(!gui&&critical==0);}
static SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t*){sdk();return (void*)1;}
static int xSemaphoreTakeRecursive(SemaphoreHandle_t,int){sdk();return 1;}
static void xSemaphoreGiveRecursive(SemaphoreHandle_t){sdk();}
static bool ensure_wifi_started(){sdk();return true;}
static void log_internal_heap(const char*,esp_err_t){}
static void esp_netif_set_default_netif(esp_netif_t*){}
static bool esp_netif_is_netif_up(esp_netif_t*){return driver_associated;}
static esp_err_t esp_netif_get_ip_info(esp_netif_t*,esp_netif_ip_info_t*out){*out=live_ip;return ESP_OK;}
static esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t*out){if(!driver_associated)return ESP_FAIL;memset(out,0,sizeof(*out));memcpy(out->ssid,driver_ssid,strlen(driver_ssid));return ESP_OK;}
static esp_err_t esp_wifi_disconnect(){sdk();disconnects++;if(!driver_associated&&!driver_joining)return ESP_ERR_WIFI_NOT_CONNECT;driver_associated=driver_joining=false;live_ip={};return ESP_OK;}
static esp_err_t esp_wifi_set_config(int,const wifi_config_t*c){sdk();sets++;last_config=*c;return ESP_OK;}
static esp_err_t esp_wifi_connect(){sdk();connects++;if(fail_next_connect){fail_next_connect=false;return ESP_FAIL;}driver_joining=true;return ESP_OK;}
static esp_err_t esp_wifi_stop(){sdk();driver_scanning=driver_associated=driver_joining=false;return ESP_OK;}
static esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*,bool block){sdk();assert(!block&&!driver_scanning);scans++;driver_scanning=true;++driver_scan_id;return ESP_OK;}
static esp_err_t esp_wifi_scan_stop(){sdk();stops++;driver_scanning=false;return ESP_OK;}
static esp_err_t esp_wifi_scan_get_ap_records(uint16_t*n,wifi_ap_record_t*out){sdk();if(driver_scanning)return ESP_ERR_WIFI_STATE;*n=(uint16_t)(results.size()<*n?results.size():*n);for(unsigned i=0;i<*n;i++)out[i]=results[i];return ESP_OK;}
static void esp_wifi_clear_ap_list(){sdk();}
static bool nvs_ready(){sdk();return true;}
static int io_open(void*,int){sdk();pending=persisted;return WIFI_NVS_OK;}
static void io_close(void*){sdk();pending.clear();}
static int io_get(void*,const char*,void*b,size_t*n){sdk();if(persisted.empty())return WIFI_NVS_NOT_FOUND;if(*n<persisted.size())return WIFI_NVS_ERR;*n=persisted.size();memcpy(b,persisted.data(),*n);return WIFI_NVS_OK;}
static int io_set(void*,const char*,const void*b,size_t n){sdk();stores++;pending.assign((const uint8_t*)b,(const uint8_t*)b+n);return WIFI_NVS_OK;}
static int io_commit(void*){sdk();if(fail_commit)return WIFI_NVS_ERR;persisted=pending;return WIFI_NVS_OK;}
static wifi_nvs_io_t profile_nvs_io(){wifi_nvs_io_t io={};io.open_ns=io_open;io.close_ns=io_close;io.get_blob=io_get;io.set_blob=io_set;io.commit=io_commit;return io;}
static void load_wifi_profiles(){}
'''

FAKES = r'''
static bool wifi_commit_mode(wifi_mode_t m,bool with_ap){sdk();assert(with_ap);mode=m;wifi_started=true;return true;}
static void wifi_refresh_on_gui_tick(bool){assert(gui);}
void quick_radio_adopt(quick_radio_t,bool){}
'''

CASES = r'''
static void reset(){
 gui=false;driver_scanning=driver_associated=driver_joining=false;scans=stops=connects=disconnects=sets=stores=0;now_ms+=30000;driver_scan_id=0;results.clear();persisted.clear();pending.clear();fail_commit=fail_next_connect=false;live_ip={};driver_ssid[0]=0;
 wifi_started=wifi_driver_ready=sta_desired=sta_user_enabled=true;ap_active=true;sta_netif=&netif;wifi_api_lock=(void*)1;sta_has_ip=false;have_sta_config=false;auto_reconnect=false;scan_active=false;
 wifi_scan_results=0;wifi_scan_revision_value=1;scan_epoch=link_epoch=1;scan_event_pending=ip_event_pending=disc_event_pending=false;connected_len=0;connected_ssid[0]=0;
 wifi_profile_store_clear(&wifi_profiles);memset(&wifi_requests,0,sizeof(wifi_requests));memset(&wifi_attempt,0,sizeof(wifi_attempt));wifi_attempt_wait_disc=false;wifi_last_failed=false;wifi_target[0]=0;wifi_fail_mask=0;wifi_visible_fresh=false;wifi_auto_armed=false;wifi_auto_due=now_ms;mode=WIFI_MODE_APSTA;wifi_profiles_ready=true;
 @RESET_EXTRA@
 wifi_service_poll();
}
static void known(const char*s,const char*p){bool change;assert(wifi_profile_note_success(&wifi_profiles,s,strlen(s),p,strlen(p),&change));}
static void visible(const char*s){wifi_ap_record_t ap={};memcpy(ap.ssid,s,strlen(s));ap.authmode=WIFI_AUTH_WEP;results.push_back(ap);}
static void scan_done(uint8_t id){wifi_event_sta_scan_done_t e={};e.scan_id=id;wifi_event_handler(nullptr,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&e);}
static void connected_event(const char*s){driver_associated=true;driver_joining=false;snprintf(driver_ssid,sizeof(driver_ssid),"%s",s);wifi_event_sta_connected_t e={};e.ssid_len=(uint8_t)strlen(s);memcpy(e.ssid,s,e.ssid_len);wifi_event_handler(nullptr,WIFI_EVENT,WIFI_EVENT_STA_CONNECTED,&e);}
static void disconnected_event(const char*s){wifi_event_sta_disconnected_t e={};e.ssid_len=(uint8_t)strlen(s);memcpy(e.ssid,s,e.ssid_len);e.reason=202;wifi_event_handler(nullptr,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&e);}
static void ip_event(uint32_t addr,bool live){if(live)live_ip.ip.addr=addr;ip_event_got_ip_t e={};e.esp_netif=&netif;e.ip_info.ip.addr=addr;wifi_event_handler(nullptr,IP_EVENT,IP_EVENT_STA_GOT_IP,&e);}
static void gui_scan(){gui=true;do_wifi_scan();gui=false;}
static void gui_saved(const char*s){gui=true;assert(do_wifi_connect_saved(s));gui=false;}
static void gui_password(const char*s,const char*p){gui=true;assert(do_wifi_connect(s,p));gui=false;}
int main(){
 reset();known("Alpha","Alpha!123");known("Bravo","Bravo!123");visible("Alpha");visible("Bravo");wifi_auto_armed=true;
 gui_scan();assert(!scans&&!connects);wifi_service_poll();assert(scans==1&&driver_scanning);
 driver_scanning=false;scan_done(driver_scan_id);wifi_service_poll();assert(connects==1&&!strcmp(wifi_connection_target(),"Bravo"));
 connected_event("Bravo");ip_event(0x0200000a,true);wifi_service_poll();assert(get_wifi_connected()&&!wifi_connection_is_busy()&&stores==0);
 gui_password("New Demo","New!Pass9");wifi_service_poll();assert(wifi_connection_is_busy());disconnected_event("Bravo");wifi_service_poll();assert(!get_wifi_connected());
 ip_event(0x0200000a,false);wifi_service_poll();assert(!wifi_network_is_saved("New Demo")&&!get_wifi_connected());
 connected_event("New Demo");wifi_service_poll();assert(!wifi_network_is_saved("New Demo"));ip_event(0x0300000a,true);wifi_service_poll();assert(wifi_network_is_saved("New Demo")&&stores==1);
 disconnected_event("Bravo");wifi_service_poll();assert(get_wifi_connected());
 wifi_event_handler(nullptr,IP_EVENT,IP_EVENT_STA_LOST_IP,nullptr);assert(get_wifi_connected());
 unsigned before=connects;gui_scan();wifi_service_poll();assert(driver_scanning);uint8_t retired=driver_scan_id;
 gui_saved("Alpha");wifi_service_poll();assert(!driver_scanning&&wifi_connection_is_busy());disconnected_event("New Demo");wifi_service_poll();connected_event("Alpha");ip_event(0x0400000a,true);wifi_service_poll();
 gui_scan();wifi_service_poll();unsigned waiting=scans;assert(!driver_scanning);scan_done(retired);wifi_service_poll();
 if(!(driver_scanning&&scans==waiting+1&&wifi_scan_is_busy())){fprintf(stderr,"scan fence: driver=%d scans=%u before=%u busy=%d pending=%d attempt=%d\n",driver_scanning,scans,waiting,wifi_scan_is_busy(),wifi_requests.scan_pending,wifi_attempt.active);}
 assert(driver_scanning&&scans==waiting+1&&wifi_scan_is_busy());
 driver_scanning=false;scan_done(driver_scan_id);wifi_service_poll();assert(connects==before+1);
 gui_password("Fresh","Fresh!123");wifi_service_poll();disconnected_event("Alpha");wifi_service_poll();connected_event("Fresh");fail_commit=true;ip_event(0x0500000a,true);wifi_service_poll();assert(!wifi_network_is_saved("Fresh")&&wifi_network_is_saved("Alpha"));fail_commit=false;
 gui_saved("Alpha");wifi_service_poll();assert(wifi_radio_apply(false)&&mode==WIFI_MODE_AP&&ap_active&&!wifi_connection_is_busy());unsigned off_connects=connects;
 connected_event("Alpha");ip_event(0x0600000a,true);wifi_service_poll();assert(!get_wifi_connected()&&connects==off_connects);
 reset();known("Alpha","Alpha!123");visible("Alpha");gui_scan();wifi_service_poll();uint8_t canceled=driver_scan_id;assert(wifi_radio_apply(false));scan_done(canceled);assert(wifi_radio_apply(true));wifi_service_poll();assert(ap_active&&mode==WIFI_MODE_APSTA&&driver_scanning);
 reset();known("Alpha","Alpha!123");known("Bravo","Bravo!123");visible("Alpha");visible("Bravo");wifi_auto_armed=true;gui_scan();wifi_service_poll();driver_scanning=false;fail_next_connect=true;scan_done(driver_scan_id);wifi_service_poll();assert(connects==1);wifi_service_poll();assert(connects==2&&!strcmp(wifi_connection_target(),"Alpha"));
 reset();known("Alpha","Alpha!123");gui_saved("Alpha");for(int i=0;i<8;i++){char ssid[12];snprintf(ssid,sizeof(ssid),"Example%d",i);known(ssid,"Sample!123");}assert(!wifi_network_is_saved("Alpha"));wifi_service_poll();assert(connects==1&&!memcmp(last_config.sta.ssid,"Alpha",5)&&!strcmp((const char*)last_config.sta.password,"Alpha!123"));
 puts("WiFi service passed: real callbacks/poll, async UI queue, matching DHCP persistence, stale events, canceled-scan drain, manual switch, no roam, NVS failure and AP-only off");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx')
    args = parser.parse_args()
    compiler = args.cxx or shutil.which('cl') or shutil.which('g++')
    if not compiler:
        parser.error('Run in a C++ developer terminal or pass --cxx')
    source = (ROOT / 'src/setwifi.cpp').read_text(encoding='utf-8')
    a = source.index('static const int WIFI_SCAN_LIMIT')
    state = source[a:source.index('static void copy_text', a)]
    names = ['copy_text', 'wifi_event_handler', 'apply_sta_config', 'publish_preferred_profile',
             'persist_profiles_copy', 'wifi_driver_lock', 'wifi_driver_unlock', 'clear_sta_link_state',
             'remember_sta_restore', 'wifi_radio_note_desired', 'publish_join_target', 'sta_radio_ready',
             'retire_scan', 'clear_attempt_secret', 'issue_sta_connect', 'fail_attempt', 'complete_attempt',
             'start_background_scan', 'publish_scan_results', 'profile_index_of_attempt', 'maybe_join_visible',
             'wifi_radio_apply', 'wifi_service_start_connect', 'wifi_service_poll', 'wifi_service_tick', 'get_wifi_connected',
             'get_wifi_connected_ssid', 'do_wifi_scan', 'get_wifi_scan_count', 'get_wifi_ssid', 'do_wifi_connect',
             'wifi_network_is_saved', 'do_wifi_connect_saved', 'wifi_scan_is_busy', 'wifi_scan_revision',
             'wifi_connection_is_busy', 'wifi_connection_target', 'wifi_scan_network_is_open', 'wifi_last_connection_failed']
    methods = '\n'.join(function(source, n) for n in names)
    constants = '\n'.join(re.findall(r'^static const uint32_t WIFI_(?:JOIN_TIMEOUT|DISC_WAIT|AUTO_COOLDOWN)_MS.*$', source, re.M))
    reset_extra = 'scan_draining = false;' if 'static bool scan_draining' in source else ''
    declarations = '#include "QuickSettingsService.h"\nstatic void retire_scan(void);\n'
    with tempfile.TemporaryDirectory(prefix='wifi-service-', ignore_cleanup_errors=True) as folder_name:
        folder = Path(folder_name)
        unit = folder / 'service.cpp'
        exe = folder / 'service.exe'
        unit.write_text(SDK + state + declarations + FAKES + constants + methods + CASES.replace('@RESET_EXTRA@', reset_extra), encoding='utf-8')
        if Path(compiler).stem.lower() == 'cl':
            command = [compiler, '/nologo', '/std:c++17', '/EHsc', '/utf-8', f'/I{ROOT / "src"}', str(unit),
                       str(ROOT / 'src/WifiProfiles.cpp'), f'/Fe:{exe}', f'/Fo{folder}\\']
        else:
            command = [compiler, '-std=c++17', f'-I{ROOT / "src"}', str(unit), str(ROOT / 'src/WifiProfiles.cpp'), '-o', str(exe)]
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True, timeout=20)


if __name__ == '__main__':
    main()
