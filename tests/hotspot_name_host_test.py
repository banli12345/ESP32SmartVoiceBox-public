"""Actual UTF-8, NVS queue and live AP rename implementation with fault fakes."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADERS = {
    'freertos/FreeRTOS.h': '''#pragma once
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
''',
    'nvs.h': '''#pragma once
#include <cstddef>
using esp_err_t=int;using nvs_handle_t=int;
enum {ESP_OK=0,NVS_READONLY=1,NVS_READWRITE=2};
int nvs_open(const char*,int,int*);int nvs_get_str(int,const char*,char*,size_t*);
int nvs_set_str(int,const char*,const char*);int nvs_commit(int);void nvs_close(int);
int nvs_get_blob(int,const char*,void*,size_t*);int nvs_set_blob(int,const char*,const void*,size_t);
''',
}
SOURCE = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "HotspotName.h"
static bool ap_active,wifi_started,napt,hotspot_secret_ready,fail_open,fail_set,fail_commit,fail_get_config,fail_set_config,fail_rollback,persist_failed_commit;
static int opens,closes,gets,sets,blob_sets;
static std::string legacy_name,legacy_password;
static std::vector<unsigned char> committed,pending;
static char hotspot_ssid[33],hotspot_password[64];static int wifi_mux;
struct wifi_config_t {struct {unsigned char ssid[32];unsigned char ssid_len;char password[64];int channel,authmode;} ap;};
static wifi_config_t config;
enum {WIFI_IF_AP=1};
static void wifi_driver_lock(){}static void wifi_driver_unlock(){}static void hotspot_prepare_ssid(){}
static bool nvs_ready(){return true;}static void log_internal_heap(const char*,int){}
static uint32_t esp_random(){static uint32_t n=0;return ++n;}
int esp_wifi_get_config(int id,wifi_config_t*out){assert(id==WIFI_IF_AP);gets++;if(fail_get_config)return 1;*out=config;return 0;}
int esp_wifi_set_config(int id,const wifi_config_t*in){assert(id==WIFI_IF_AP);sets++;if(fail_set_config||(fail_rollback&&sets==2))return 1;config=*in;return 0;}
int nvs_open(const char*ns,int mode,int*h){assert(!strcmp(ns,"hotspot_cfg")||!strcmp(ns,"hotspot"));if(fail_open&&mode==NVS_READWRITE)return 1;*h=!strcmp(ns,"hotspot_cfg")?1:2;opens++;return 0;}
int nvs_get_str(int h,const char*k,char*out,size_t*n){const std::string &v=h==1?legacy_name:legacy_password;assert(!strcmp(k,h==1?"ssid":"psk"));if(v.empty())return 1;size_t need=v.size()+1;if(!out){*n=need;return 0;}if(*n<need)return 1;memcpy(out,v.c_str(),need);*n=need;return 0;}
int nvs_set_str(int h,const char*k,const char*v){assert(h==2&&!strcmp(k,"psk"));if(fail_set)return 1;legacy_password=v;return 0;}
int nvs_get_blob(int h,const char*k,void*out,size_t*n){assert(h==1&&!strcmp(k,"credentials"));if(committed.empty())return 1;if(*n<committed.size())return 1;memcpy(out,committed.data(),committed.size());*n=committed.size();return 0;}
int nvs_set_blob(int h,const char*k,const void*v,size_t n){assert(h==1&&!strcmp(k,"credentials"));blob_sets++;if(fail_set)return 1;const auto*b=(const unsigned char*)v;pending.assign(b,b+n);return 0;}
int nvs_commit(int h){if(h==2)return fail_commit?1:0;if(persist_failed_commit)committed=pending;if(fail_commit)return 1;committed=pending;return 0;}
void nvs_close(int){closes++;}
#include "@ROOT@/src/HotspotName.cpp"
@GENERATE@
@ENSURE@
@APPLY@
static void boot(){g_state={};g_has_job=false;g_pending={};memset(hotspot_password,0,sizeof hotspot_password);hotspot_secret_ready=false;hotspot_name_restore("ESP32S3-TEST",hotspot_ssid);assert(hotspot_ensure_secret());memset(&config,0,sizeof config);memcpy(config.ap.ssid,hotspot_ssid,strlen(hotspot_ssid));config.ap.ssid_len=(unsigned char)strlen(hotspot_ssid);strcpy(config.ap.password,hotspot_password);config.ap.channel=6;config.ap.authmode=3;}
static void reset(){ap_active=wifi_started=napt=fail_open=fail_set=fail_commit=fail_get_config=fail_set_config=fail_rollback=persist_failed_commit=false;committed.clear();pending.clear();legacy_name.clear();legacy_password="ABcd3456EFgh";opens=closes=gets=sets=blob_sets=0;boot();}
static hotspot_name_state_t state(){hotspot_name_state_t s;hotspot_name_get(&s);return s;}
int main(){char normalized[33];
 assert(hotspot_name_normalize("  My Room  ",normalized)&&!strcmp(normalized,"My Room"));
 assert(hotspot_name_normalize(std::string(32,'x').c_str(),normalized)&&strlen(normalized)==32);
 const std::string chinese="一二三四五六七八九十";assert(hotspot_name_normalize((chinese+"12").c_str(),normalized)&&strlen(normalized)==32);
 for(const auto &bad:std::vector<std::string>{"", "   ",std::string(33,'x'),chinese+"123","x\n","\x7f","\xC2\x80","\xC0\xAF","\xED\xA0\x80","\xF4\x90\x80\x80",std::string(128,'x')}){memset(normalized,0x41,33);assert(!hotspot_name_normalize(bad.c_str(),normalized));for(char c:normalized)assert(c==0);}
 for(const auto &bad:std::vector<std::string>{"", "1234567",std::string(64,'x'),"abcdefgh\n","密码123456","abcdefg\x7f"})assert(!hotspot_password_valid(bad.c_str()));
 assert(hotspot_password_valid("12345678")&&hotspot_password_valid(std::string(63,'x').c_str())&&hotspot_password_valid(" space ! "));
 reset();assert(!strcmp(state().password,legacy_password.c_str()));assert(hotspot_credentials_request("  New Room  ","New!Pass8"));assert(state().busy&&!gets&&!sets&&committed.empty());assert(!hotspot_credentials_request("Other","Other!Pwd"));hotspot_name_poll();assert(!state().busy&&state().result==1&&!committed.empty()&&!strcmp(hotspot_ssid,"New Room")&&!strcmp(hotspot_password,"New!Pass8")&&!gets&&!sets&&blob_sets==1);boot();assert(!strcmp(state().name,"New Room")&&!strcmp(state().password,"New!Pass8"));
 reset();ap_active=wifi_started=napt=true;assert(hotspot_credentials_request("我的热点","12345678"));hotspot_name_poll();assert(state().result==1&&ap_active&&wifi_started&&napt&&gets==1&&sets==1&&config.ap.channel==6&&config.ap.authmode==3&&!strcmp(config.ap.password,"12345678")&&config.ap.ssid_len==12&&!memcmp(config.ap.ssid,"我的热点",12));assert(opens==closes);boot();assert(!strcmp(state().name,"我的热点")&&!strcmp(state().password,"12345678"));
 assert(hotspot_name_request("NameOnly"));hotspot_name_poll();assert(!strcmp(state().password,"12345678"));
 for(int fault=0;fault<5;fault++){reset();ap_active=wifi_started=napt=true;fail_open=fault==0;fail_get_config=fault==1;fail_set_config=fault==2;fail_set=fault==3;fail_commit=fault==4;assert(hotspot_credentials_request("Attempt","Other!Pwd"));hotspot_name_poll();assert(!state().busy&&state().result==-1&&!strcmp(state().name,"ESP32S3-TEST")&&!strcmp(state().password,"ABcd3456EFgh")&&!strcmp(hotspot_ssid,"ESP32S3-TEST")&&!strcmp(hotspot_password,"ABcd3456EFgh")&&opens==closes);int count=sets;hotspot_name_poll();assert(sets==count);}
 reset();ap_active=wifi_started=true;fail_commit=persist_failed_commit=true;assert(hotspot_credentials_request("Attempt","Other!Pwd"));hotspot_name_poll();fail_commit=persist_failed_commit=false;boot();assert(!strcmp(state().name,"ESP32S3-TEST")&&!strcmp(state().password,"ABcd3456EFgh"));
 reset();ap_active=wifi_started=true;fail_commit=fail_rollback=true;assert(hotspot_credentials_request("Changed","Changed!Pass"));hotspot_name_poll();assert(!state().busy&&state().result==-1&&!strcmp(state().name,"Changed")&&!strcmp(state().password,"Changed!Pass")&&strstr(state().message,"保存失败"));
 reset();legacy_name="Old custom SSID";boot();assert(!strcmp(state().name,"Old custom SSID")&&!strcmp(state().password,"ABcd3456EFgh"));
 assert(hotspot_credentials_request("Paired","Fresh!Pass"));hotspot_name_poll();auto valid=committed;for(int fault=0;fault<4;fault++){committed=valid;if(fault==0)committed.resize(1);if(fault==1)committed[0]=9;if(fault==2)memset(committed.data()+1,'A',33);if(fault==3)memset(committed.data()+34,'A',64);boot();assert(!strcmp(state().name,"Old custom SSID")&&!strcmp(state().password,"ABcd3456EFgh"));}
 reset();ap_active=wifi_started=true;std::string full_name(32,'z'),full_pass(63,'p');assert(hotspot_credentials_request(full_name.c_str(),full_pass.c_str()));hotspot_name_poll();assert(state().result==1&&config.ap.ssid_len==32&&strlen(config.ap.password)==63);boot();assert(!strcmp(state().name,full_name.c_str())&&!strcmp(state().password,full_pass.c_str()));assert(opens==closes);
 reset();hotspot_secret_ready=false;legacy_password.clear();assert(hotspot_ensure_secret()&&legacy_password.size()==12);std::string generated=state().password;boot();assert(generated==state().password);
 reset();assert(!hotspot_credentials_request("Attempt","short")&&state().result==-1&&!state().busy&&!gets&&!sets&&committed.empty());assert(opens==closes);
 puts("Hotspot credentials PASS: UTF-8/32-byte SSID, 8-63 ASCII password, atomic pair/NVS reboot, legacy migration, AP-off/on, channel/security/NAPT preservation, faults and rollback");
}
'''


def main():
    compiler=shutil.which('cl')
    if not compiler:raise SystemExit('Use a Visual Studio developer terminal')
    code=(ROOT/'src/setwifi.cpp').read_text(encoding='utf-8')
    def extract(signature):
        match=re.search(signature,code);assert match
        end,depth=match.end(),1
        while depth:depth+=(code[end]=='{')-(code[end]=='}');end+=1
        return code[match.start():end]
    apply=extract(r'extern "C" bool wifi_hotspot_apply_credentials\([^;]*?\)\s*\{')
    ensure=extract(r'static bool hotspot_ensure_secret\([^;]*?\)\s*\{')
    generate=extract(r'static bool hotspot_generate_password\([^;]*?\)\s*\{')
    with tempfile.TemporaryDirectory(prefix='hotspot-name-',ignore_cleanup_errors=True) as tmp:
        directory=Path(tmp)
        for name,value in HEADERS.items():
            f=directory/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(value,encoding='utf-8')
        unit=directory/'test.cpp';unit.write_text(SOURCE.replace('@ROOT@',ROOT.as_posix()).replace('@APPLY@',apply).replace('@ENSURE@',ensure).replace('@GENERATE@',generate),encoding='utf-8')
        exe=directory/'test.exe'
        subprocess.run([compiler,'/nologo','/std:c++17','/EHsc','/utf-8','/D_CRT_SECURE_NO_WARNINGS',f'/I{directory}',f'/I{ROOT/"src"}',str(unit),f'/Fe:{exe}',f'/Fo{directory}\\'],check=True)
        subprocess.run([str(exe)],check=True)

if __name__=='__main__':main()
