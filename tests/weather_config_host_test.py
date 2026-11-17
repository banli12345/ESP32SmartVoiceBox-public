"""Actual WeatherConfig + cJSON with filesystem and NVS fault boundaries."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from photo_loader_host_test import HEADERS as BASE_HEADERS

ROOT=Path(__file__).resolve().parents[1]
SDK=Path('C:/Espressif/frameworks/esp-idf-v5.5.6/components/json/cJSON')
HEADERS=dict(BASE_HEADERS)
HEADERS['esp_attr.h']='#pragma once\n#define EXT_RAM_BSS_ATTR\n'
HEADERS['nvs.h']='''#pragma once
#include <stddef.h>
typedef int nvs_handle_t; typedef int esp_err_t;
#define ESP_OK 0
#define NVS_READONLY 0
#define NVS_READWRITE 1
extern "C" {
int nvs_open(const char*,int,nvs_handle_t*);int nvs_get_blob(int,const char*,void*,size_t*);
int nvs_set_blob(int,const char*,const void*,size_t);int nvs_commit(int);void nvs_close(int);
}
'''
HARNESS=r'''
#include "WeatherConfig.h"
#include "Storage.h"
#include "nvs.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
static std::filesystem::path root;
static bool card=true,fail_open,fail_read,fail_remove,fail_nvs_open,fail_set,fail_commit;
static std::vector<unsigned char> committed,pending;
static std::filesystem::path path(const char*p){return root/(p+1);}
extern "C" bool sdcar_is_ready(){return card;}
extern "C" bool storage_is_file(const char*p){return std::filesystem::is_regular_file(path(p));}
extern "C" bool storage_remove(const char*p){return !fail_remove&&std::filesystem::remove(path(p));}
StorageStream::~StorageStream(){close();}
bool StorageStream::open(const char*p,const char*m){close();file_=fail_open?nullptr:fopen(path(p).string().c_str(),m);return file_!=nullptr;}
void StorageStream::close(){if(file_){fclose(file_);file_=nullptr;}}
uint64_t StorageStream::byte_count()const{long at=ftell(file_);fseek(file_,0,SEEK_END);long size=ftell(file_);fseek(file_,at,SEEK_SET);return size;}
size_t StorageStream::read(void*p,size_t n){return fail_read?0:fread(p,1,n,file_);}
extern "C" int nvs_open(const char*ns,int mode,int*out){assert(!strcmp(ns,"weather_api"));if(mode==NVS_READWRITE&&fail_nvs_open)return 1;*out=1;return 0;}
extern "C" int nvs_get_blob(int,const char*k,void*p,size_t*n){assert(!strcmp(k,"config"));if(committed.empty()||*n<committed.size())return 1;*n=committed.size();memcpy(p,committed.data(),*n);return 0;}
extern "C" int nvs_set_blob(int,const char*k,const void*p,size_t n){assert(!strcmp(k,"config"));if(fail_set)return 1;pending.assign((const unsigned char*)p,(const unsigned char*)p+n);return 0;}
extern "C" int nvs_commit(int){if(fail_commit)return 1;committed=pending;return 0;}
extern "C" void nvs_close(int){pending.clear();}
static void put(const std::string&s){std::ofstream f(root/"config/weather.json",std::ios::binary);f.write(s.data(),s.size());}
static bool exists(){return std::filesystem::exists(root/"config/weather.json");}
static void remove(){std::filesystem::remove(root/"config/weather.json");}
static weather_api_config_t get(){weather_api_config_t c;assert(weather_config_get(&c));return c;}
int main(int argc,char**argv){
 assert(argc==2);root=argv[1];std::filesystem::create_directories(root/"config");
 weather_api_config_t c;memset(&c,1,sizeof c);weather_config_init();assert(!weather_config_is_ready()&&!weather_config_get(&c));
 for(unsigned char v:std::string((char*)&c,sizeof c))assert(v==0);
 assert(!weather_config_get(nullptr));
 const std::string first=R"({"version":1,"api_key":"unit-weather-secret"})";
 put(first);weather_config_init();assert(weather_config_is_ready()&&weather_config_imported()&&!exists());
 c=get();assert(!strcmp(c.now_url,WEATHER_DEFAULT_NOW_URL)&&!strcmp(c.daily_url,WEATHER_DEFAULT_DAILY_URL)&&!strcmp(c.life_url,WEATHER_DEFAULT_LIFE_URL));
 assert(!strcmp(c.api_key,"unit-weather-secret")&&!strcmp(c.life_api_key,c.api_key));
 auto baseline=committed;weather_config_init();assert(weather_config_is_ready()&&!weather_config_imported());auto restored=get();assert(!memcmp(&c,&restored,sizeof c));
 for(const std::string&bad:std::vector<std::string>{"[]","{",first+"garbage",std::string(2049,'x'),
  R"({"version":true,"api_key":"unit-weather-secret"})",R"({"version":2,"api_key":"unit-weather-secret"})",
  R"({"version":1,"api_key":""})",R"({"version":1,"api_key":"YOUR_KEY"})",
  R"({"version":1,"api_key":"unit-weather-secret","api_key":"second"})",
  R"({"version":1,"api_key":"unit-weather-secret\u0000suffix"})",
  R"({"version":1,"api_key":"unit-weather-secret","now_url":"http://"})",
  R"({"version":1,"api_key":"unit-weather-secret","now_url":"https://user:pass@host/x"})",
  R"({"version":1,"api_key":"unit-weather-secret","daily_url":"https://host/x?q=1"})",
  R"({"version":1,"api_key":"unit-weather-secret","life_url":"https://host:0/x"})",
  R"({"version":1,"api_key":"unit-weather-secret","life_url":"https://host:65536/x"})",
  R"({"version":1,"api_key":"unit-weather-secret","life_url":"https://host:abc/x"})",
  R"({"version":1,"api_key":"unit-weather-secret","life_url":"https://host:/x"})",
  R"({"version":1,"api_key":"unit-weather-secret","life_url":""})",
  R"({"version":1,"api_key":"unit-weather-secret","life_api_key":false})",
  std::string("{\"version\":1,\"api_key\":\"")+std::string(96,'x')+"\"}",
  std::string("{\"version\":1,\"api_key\":\"unit-weather-secret\",\"now_url\":\"https://host/")+std::string(192,'x')+"\"}",
  std::string("{\"version\":1,\"api_key\":\"unit-weather-secret\",\"extra\":")+std::string(16,'[')+"0"+std::string(16,']')+"}"}){
  put(bad);weather_config_init();assert(exists()&&!weather_config_imported()&&committed==baseline);auto old=get();assert(!memcmp(&c,&old,sizeof c));
 }
 put(first+std::string(1,'\0'));weather_config_init();assert(exists()&&committed==baseline);remove();
 const std::string changed=R"({"version":1,"api_key":"new-weather-value","life_api_key":"separate-life-secret","now_url":"https://weather.example.test:8443/custom-now","daily_url":"http://192.0.2.1:8080/custom-daily","life_url":"https://life.example.test/indices"})";
 for(bool*failure:{&fail_open,&fail_read,&fail_nvs_open,&fail_set,&fail_commit}){
  put(changed);*failure=true;weather_config_init();*failure=false;
  assert(exists()&&committed==baseline&&!weather_config_imported());auto old=get();assert(!memcmp(&c,&old,sizeof c));
 }
 put(std::string("\xef\xbb\xbf")+changed);weather_config_init();assert(weather_config_imported()&&!exists());
 c=get();assert(!strcmp(c.now_url,"https://weather.example.test:8443/custom-now")&&!strcmp(c.daily_url,"http://192.0.2.1:8080/custom-daily")&&!strcmp(c.life_url,"https://life.example.test/indices")&&!strcmp(c.life_api_key,"separate-life-secret"));
 card=false;weather_config_init();restored=get();assert(!memcmp(&c,&restored,sizeof c)&&!weather_config_imported());card=true;
 fail_remove=true;put(changed);weather_config_init();assert(exists()&&weather_config_is_ready()&&weather_config_imported());fail_remove=false;weather_config_init();assert(!exists());
 auto good=committed;committed[0]^=1;weather_config_init();assert(!weather_config_is_ready());
 committed.assign(10,0);weather_config_init();assert(!weather_config_is_ready());committed=good;
 weather_config_init();assert(weather_config_is_ready()&&!weather_config_imported());
 puts("WeatherConfig PASS: import/reboot, independent endpoints/keys, invalid JSON/URL/NUL/duplicate, NVS/file faults, cleanup and previous-config retention");
}
'''

def main():
    cc=shutil.which('cl');assert cc,'Use VS developer shell'
    with tempfile.TemporaryDirectory(prefix='weather-config-',ignore_cleanup_errors=True)as tmp:
        d=Path(tmp)
        for name,body in HEADERS.items():
            p=d/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(body,encoding='utf8')
        unit=d/'test.cpp';unit.write_text(HARNESS,encoding='utf8');exe=d/'test.exe'
        args=[cc,'/nologo','/TP','/std:c++17','/EHsc','/utf-8','/DNOMINMAX','/D_CRT_SECURE_NO_WARNINGS',
            '/DLV_CONF_INCLUDE_SIMPLE','/DLV_KCONFIG_IGNORE',f'/FI{d/"compat.h"}',f'/I{d}',f'/I{ROOT/"src"}',
            f'/I{ROOT/"include"}',f'/I{ROOT/"components"}',f'/I{ROOT/"components/lvgl"}',
            f'/I{ROOT/"components/lvgl/src/extra/others/ime"}',f'/I{SDK}',str(unit),
            str(ROOT/'src/WeatherConfig.cpp'),str(SDK/'cJSON.c'),f'/Fe:{exe}',f'/Fo{d}\\']
        subprocess.run(args,check=True)
        result=subprocess.run([str(exe),str(d/'sd')],capture_output=True,text=True,encoding='utf8',check=True)
        for secret in ['unit-weather-secret','separate-life-secret','new-weather-value']:
            assert secret not in result.stdout+result.stderr
        print(result.stdout,end='')

if __name__=='__main__':main()
