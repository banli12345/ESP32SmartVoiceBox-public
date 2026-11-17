"""Production SDK adapter: reserved local home works offline, live AP/STA roots stay strict."""
from pathlib import Path
import argparse, subprocess, shutil
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/browser-local-open-fix-20261115/host-adapter'
SDK=r'''
#pragma once
#include <stdint.h>
struct esp_ip4_addr_t{uint32_t addr;};
struct esp_netif_t{};
struct esp_netif_ip_info_t{esp_ip4_addr_t ip;};
extern uint32_t sta_ip;extern unsigned snapshots;
#define ESP_OK 0
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) ((p)->addr>>24)&255,((p)->addr>>16)&255,((p)->addr>>8)&255,(p)->addr&255
static esp_netif_t*esp_netif_get_handle_from_ifkey(const char*){static esp_netif_t n;return sta_ip?&n:nullptr;}
static int esp_netif_get_ip_info(esp_netif_t*,esp_netif_ip_info_t*p){p->ip.addr=sta_ip;return ESP_OK;}
'''
CPP=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include "esp_netif.h"
#include "@ROOT@/src/BrowserLocalPage.cpp"
uint32_t sta_ip;unsigned snapshots;static const char*ap_ip="0.0.0.0";
extern "C" void wifi_hotspot_copy_info(char*,size_t,char*,size_t,char*out,size_t size,uint8_t*,bool*){snapshots++;if(out&&size)snprintf(out,size,"%s",ap_ip);}
int main(){
 const char*local[]={"http://172.31.42.1/","172.31.42.1","localhost","http://127.0.0.1/"," HTTP://LOCALHOST:80/?x=1#music \r\n"};
 for(auto s:local)assert(browser_is_own_music_page(s));assert(snapshots==0);
 const char*other[]={"https://172.31.42.1/","http://172.31.42.1:8080/","http://172.31.42.1/api/status","http://172.31.42.10/","http://localhost.evil/","http://localhost@evil/","http://evil@localhost/","http://example.com/","http://0.0.0.0/"};
 for(auto s:other)assert(!browser_is_own_music_page(s));
 ap_ip="10.42.0.1";assert(browser_is_own_music_page("http://10.42.0.1/"));assert(!browser_is_own_music_page("http://10.42.0.10/"));
 sta_ip=0xc0a8012b;assert(browser_is_own_music_page("http://192.168.1.43/"));assert(!browser_is_own_music_page("http://192.168.1.44/"));
 sta_ip=0;ap_ip="0.0.0.0";assert(!browser_is_own_music_page("http://192.168.1.43/"));assert(!browser_is_own_music_page("http://10.42.0.1/"));assert(browser_is_own_music_page("http://172.31.42.1/"));
 puts("Local page adapter PASS: offline default/loopback without driver reads, live AP/STA, host/auth/path/port/HTTPS boundaries");
}
'''
def main():
    p=argparse.ArgumentParser();p.add_argument('--cxx',default=shutil.which('cl'));a=p.parse_args();assert a.cxx,'Run from MSVC developer environment'
    OUT.mkdir(parents=True,exist_ok=True);(OUT/'esp_netif.h').write_text(SDK,encoding='utf-8');unit=OUT/'test.cpp';unit.write_text(CPP.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8');exe=OUT/'adapter.exe'
    cmd=[a.cxx,'/nologo','/std:c++17','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/I'+str(OUT),'/I'+str(ROOT/'src'),str(unit),str(ROOT/'src/browser_local_url.c'),str(ROOT/'src/browser_document.c'),'/Fe:'+str(exe),'/Fo'+str(OUT)+'\\']
    r=subprocess.run(cmd,capture_output=True);assert not r.returncode,(r.stdout+r.stderr).decode(errors='replace');subprocess.run([str(exe)],check=True,timeout=15)
if __name__=='__main__':main()
