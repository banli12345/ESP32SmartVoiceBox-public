"""Actual web handlers + AI/weather NVS modules; no SD/network/real keys."""
from pathlib import Path
import shutil,subprocess,tempfile
from weather_config_host_test import HEADERS as BASE
ROOT=Path(__file__).resolve().parents[1]
SDK=Path('C:/Espressif/frameworks/esp-idf-v5.5.6/components/json/cJSON')
HEADERS=dict(BASE)
HEADERS['esp_heap_caps.h']='''#pragma once
#include <cstdlib>
enum {MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2};extern bool fail_alloc;
inline void*heap_caps_malloc(size_t n,int){return fail_alloc?nullptr:malloc(n);}
'''
HEADERS['esp_random.h']='''#pragma once
#include <stdint.h>
#include <stddef.h>
inline void esp_fill_random(void*p,size_t n){static unsigned v=1;auto*b=(uint8_t*)p;while(n--)*b++=(uint8_t)v++;}
'''
HEADERS['esp_system.h']='#pragma once\nextern "C" void esp_restart(void);\n'
HEADERS['esp_http_server.h']='''#pragma once
#include <map>
#include <string>
#include <stddef.h>
typedef int esp_err_t;typedef void*httpd_handle_t;
#define ESP_OK 0
struct httpd_req_t {std::map<std::string,std::string> headers;std::string body,response,status,type;size_t content_len=0,pos=0,chunk=3,disconnect=0;bool fail_send=false;};
struct httpd_uri_t {const char*uri;int method;esp_err_t(*handler)(httpd_req_t*);void*user_ctx;};
enum{HTTP_GET,HTTP_POST,HTTPD_500_INTERNAL_SERVER_ERROR=500,HTTPD_RESP_USE_STRLEN=-1};
int httpd_resp_send_err(httpd_req_t*,int,const char*);int httpd_resp_set_status(httpd_req_t*,const char*);
int httpd_resp_set_type(httpd_req_t*,const char*);int httpd_resp_set_hdr(httpd_req_t*,const char*,const char*);
int httpd_resp_send(httpd_req_t*,const char*,int);int httpd_req_recv(httpd_req_t*,char*,size_t);
size_t httpd_req_get_hdr_value_len(httpd_req_t*,const char*);int httpd_req_get_hdr_value_str(httpd_req_t*,const char*,char*,size_t);
int httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t*);
'''
HARNESS=r'''
#include "compat.h"
#include "AiConfig.h"
#include "WeatherConfig.h"
#include "ServiceConfigWeb.h"
#include "Storage.h"
#include "nvs.h"
#include "cJSON.h"
#include <cassert>
#include <vector>
#include <map>
#include <cstring>
#include <cstdio>
#include <algorithm>
bool fail_alloc=false;
static bool ap=true,busy=false,fail_write=false,fail_set=false,fail_commit=false,fail_read=false;
static unsigned clock_ms=5000,restarts=0,writes=0;
static std::map<int,std::vector<unsigned char>> saved,staged;
static std::vector<httpd_uri_t> routes;
uint32_t app_millis(){return clock_ms;}void app_delay_ms(uint32_t n){clock_ms+=n;}
extern "C" bool sdcar_is_ready(){return false;}
extern "C" bool storage_is_file(const char*){return false;}
extern "C" bool storage_remove(const char*){return false;}
StorageStream::~StorageStream(){}bool StorageStream::open(const char*,const char*){return false;}
void StorageStream::close(){}uint64_t StorageStream::byte_count()const{return 0;}size_t StorageStream::read(void*,size_t){return 0;}
extern "C" bool wifi_hotspot_get_enabled(){return ap;}
extern "C" void wifi_hotspot_copy_info(char*,size_t,char*,size_t,char*ip,size_t n,uint8_t*,bool*){strlcpy(ip,"172.31.42.1",n);}
extern "C" bool system_update_is_busy(){return busy;}
extern "C" void esp_restart(){++restarts;}
extern "C" int nvs_open(const char*ns,int mode,int*h){
 if((mode==NVS_READONLY&&fail_read)||(mode==NVS_READWRITE&&fail_write))return 1;
 assert(!strcmp(ns,"ai_direct")||!strcmp(ns,"weather_api"));*h=!strcmp(ns,"ai_direct")?1:2;return 0;
}
extern "C" int nvs_get_blob(int h,const char*k,void*p,size_t*n){assert(!strcmp(k,"config"));auto i=saved.find(h);if(i==saved.end()||i->second.size()>*n)return 1;*n=i->second.size();memcpy(p,i->second.data(),*n);return 0;}
extern "C" int nvs_set_blob(int h,const char*k,const void*p,size_t n){assert(!strcmp(k,"config"));if(fail_set)return 1;staged[h].assign((const unsigned char*)p,(const unsigned char*)p+n);return 0;}
extern "C" int nvs_commit(int h){if(fail_commit)return 1;saved[h]=staged[h];++writes;return 0;}
extern "C" void nvs_close(int h){staged.erase(h);}
int httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t*r){routes.push_back(*r);return 0;}
int httpd_resp_send_err(httpd_req_t*r,int,const char*s){r->response=s;return 1;}
int httpd_resp_set_status(httpd_req_t*r,const char*s){r->status=s;return 0;}
int httpd_resp_set_type(httpd_req_t*r,const char*s){r->type=s;return 0;}
int httpd_resp_set_hdr(httpd_req_t*,const char*,const char*){return 0;}
int httpd_resp_send(httpd_req_t*r,const char*p,int n){if(r->fail_send)return 1;r->response.assign(p,n<0?strlen(p):n);return 0;}
int httpd_req_recv(httpd_req_t*r,char*p,size_t n){if(r->disconnect&&r->pos>=r->disconnect)return -1;size_t k=std::min({n,r->chunk,r->body.size()-r->pos});memcpy(p,r->body.data()+r->pos,k);r->pos+=k;return (int)k;}
size_t httpd_req_get_hdr_value_len(httpd_req_t*r,const char*k){return r->headers[k].size();}
int httpd_req_get_hdr_value_str(httpd_req_t*r,const char*k,char*p,size_t n){auto i=r->headers.find(k);if(i==r->headers.end()||i->second.size()>=n)return 1;memcpy(p,i->second.c_str(),i->second.size()+1);return 0;}
static std::string token;
static httpd_req_t request(std::string body="{}"){httpd_req_t r;r.body=body;r.content_len=body.size();r.headers={{"Host","172.31.42.1"},{"Origin","http://172.31.42.1"},{"X-Config-Token",token},{"Content-Type","application/json"}};return r;}
static void call(const char*path,int method,httpd_req_t&r){for(const auto&route:routes)if(!strcmp(route.uri,path)&&route.method==method){route.handler(&r);return;}assert(false);}
static bool ok(const httpd_req_t&r){return r.response.find("\"ok\":true")!=std::string::npos;}
static void private_response(const httpd_req_t&r){for(const char*s:{"web-ai-secret-13579","web-weather-secret-2468","web-life-secret-1357"})assert(r.response.find(s)==std::string::npos);}
static void metadata(){auto r=request();call("/api/config/status",HTTP_GET,r);assert(ok(r));private_response(r);assert(r.response.find("\"api_key\"")==std::string::npos);cJSON*j=cJSON_Parse(r.response.c_str());assert(j);auto*t=cJSON_GetObjectItem(j,"token");assert(cJSON_IsString(t)&&strlen(t->valuestring)==32);token=t->valuestring;cJSON_Delete(j);}
static void save(const char*route,const std::string&body,bool success=true){auto r=request(body);call(route,HTTP_POST,r);private_response(r);assert(ok(r)==success);}
int main(){
 ai_config_init();weather_config_init();assert(!ai_config_is_ready()&&!weather_config_is_ready());
 assert(service_config_web_register((void*)1)&&routes.size()==5);metadata();
 auto page=request();call("/settings",HTTP_GET,page);assert(page.response.find("服务配置")!=std::string::npos);
 save("/api/config/ai",R"({"api_key":"web-ai-secret-13579","model":"deepseek-flash","speak":true})");
 save("/api/config/weather",R"({"api_key":"web-weather-secret-2468","life_use_weather_key":true})");
 assert(!ai_config_is_ready()&&!weather_config_is_ready());metadata();
 ai_api_config_t ai{};weather_api_config_t weather{};
 assert(ai_config_read_saved(&ai)&&!strcmp(ai.api_key,"web-ai-secret-13579"));
 assert(weather_config_read_saved(&weather)&&!strcmp(weather.api_key,weather.life_api_key));
 save("/api/config/ai",R"({"api_base":"https://example.test/v1","api_key":"","model":"new-model","speak":false})");
 assert(ai_config_read_saved(&ai)&&!strcmp(ai.api_key,"web-ai-secret-13579")&&!strcmp(ai.model,"new-model")&&!ai.speak);
 save("/api/config/weather",R"({"api_key":"","life_use_weather_key":false,"life_api_key":"web-life-secret-1357","life_url":"https://life.example.test/custom"})");
 save("/api/config/weather",R"({"api_key":"","life_api_key":"","life_use_weather_key":false,"now_url":"https://weather.example.test/custom"})");
 assert(weather_config_read_saved(&weather)&&!strcmp(weather.life_api_key,"web-life-secret-1357"));
 metadata();ai_config_init();weather_config_init();assert(ai_config_is_ready()&&weather_config_is_ready());
 assert(ai_config_get(&ai)&&!ai.speak&&!strcmp(ai.model,"new-model"));
 assert(weather_config_get(&weather)&&!strcmp(weather.now_url,"https://weather.example.test/custom"));
 auto old=saved;
 for(const char*body:{"{",R"({"api_key":"bad key"})",R"({"api_key":"YOUR_KEY"})",R"({"api_key":"x\u0000y"})",R"({"api_key":"a","api_key":"b"})",R"({"now_url":"https://user:pass@host/x"})",R"({"now_url":"http://host/x?key=secret"})"})save("/api/config/weather",body,false);
 assert(saved==old);
 for(bool*f:{&fail_write,&fail_set,&fail_commit,&fail_read,&fail_alloc}){*f=true;save("/api/config/weather",R"({"now_url":"https://other.example.test/x"})",false);*f=false;assert(saved==old);}
 auto r=request();r.headers["Host"]="attacker.example";call("/api/config/status",HTTP_GET,r);assert(!ok(r));
 r=request();r.headers["Origin"]="http://attacker.example";call("/api/config/weather",HTTP_POST,r);assert(!ok(r)&&saved==old);
 r=request();r.headers["X-Config-Token"]="bad";call("/api/config/ai",HTTP_POST,r);assert(!ok(r));
 r=request();r.headers["Content-Type"]="text/plain";call("/api/config/ai",HTTP_POST,r);assert(!ok(r));
 r=request(std::string(3073,'x'));call("/api/config/weather",HTTP_POST,r);assert(!ok(r));
 r=request(R"({"api_key":"web-weather-secret-2468"})");r.chunk=1;r.disconnect=4;call("/api/config/weather",HTTP_POST,r);assert(!ok(r)&&saved==old);
 busy=true;save("/api/config/ai",R"({"model":"blocked"})",false);r=request();call("/api/config/reboot",HTTP_POST,r);assert(!ok(r));busy=false;
 ap=false;r=request();call("/api/config/status",HTTP_GET,r);assert(!ok(r));ap=true;
 r=request();r.fail_send=true;call("/api/config/reboot",HTTP_POST,r);clock_ms+=2000;service_config_web_poll();assert(restarts==0);
 r=request();call("/api/config/reboot",HTTP_POST,r);assert(ok(r));save("/api/config/ai",R"({"model":"blocked"})",false);
 clock_ms+=1499;service_config_web_poll();assert(restarts==0);++clock_ms;busy=true;service_config_web_poll();assert(restarts==0);busy=false;service_config_web_poll();assert(restarts==1);
 auto previous=token;routes.clear();service_config_web_register((void*)1);metadata();assert(token!=previous);
 r=request();r.headers["X-Config-Token"]=previous;call("/api/config/ai",HTTP_POST,r);assert(!ok(r));
 puts("Service config web PASS: no-SD NVS provisioning/reboot, blank-key retention, no key responses, endpoints, faults, Host/Origin/token protection and delayed restart");
}
'''
def main():
 cc=shutil.which('cl');assert cc,'Use VS developer shell'
 with tempfile.TemporaryDirectory(prefix='web-config-',ignore_cleanup_errors=True)as tmp:
  d=Path(tmp)
  for name,body in HEADERS.items():
   p=d/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(body,encoding='utf8')
  unit=d/'test.cpp';unit.write_text(HARNESS,encoding='utf8');exe=d/'test.exe'
  args=[cc,'/nologo','/TP','/std:c++20','/EHsc','/utf-8','/DNOMINMAX','/D_CRT_SECURE_NO_WARNINGS','/DLV_CONF_INCLUDE_SIMPLE','/DLV_KCONFIG_IGNORE',f'/FI{d/"compat.h"}',f'/I{d}',f'/I{ROOT/"src"}',f'/I{ROOT/"include"}',f'/I{ROOT/"components"}',f'/I{ROOT/"components/lvgl"}',f'/I{ROOT/"components/lvgl/src/extra/others/ime"}',f'/I{SDK}',str(unit),str(ROOT/'src/ServiceConfigWeb.cpp'),str(ROOT/'src/AiConfig.cpp'),str(ROOT/'src/WeatherConfig.cpp'),str(SDK/'cJSON.c'),f'/Fe:{exe}',f'/Fo{d}\\']
  subprocess.run(args,check=True)
  p=subprocess.run([str(exe)],capture_output=True,text=True,encoding='utf8',check=True)
  for secret in ['web-ai-secret-13579','web-weather-secret-2468','web-life-secret-1357']:assert secret not in p.stdout+p.stderr
  print(p.stdout,end='')
if __name__=='__main__':main()
