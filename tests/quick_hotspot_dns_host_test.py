"""Exercise real DHCP/DNS setup with absent upstream and injected SDK failures."""
from pathlib import Path
import argparse,re,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
SDK=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
using esp_err_t=int;
struct esp_netif_t{};static esp_netif_t ap,sta;
struct ip4{uint32_t addr;};
struct esp_netif_dns_info_t{struct {int type;struct {ip4 ip4;} u_addr;} ip;};
enum {ESP_OK,ESP_FAIL,ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED,ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED};
enum {ESP_NETIF_DNS_MAIN,ESP_IPADDR_TYPE_V4=0,OFFER_DNS=2,ESP_NETIF_OP_SET=3,ESP_NETIF_DOMAIN_NAME_SERVER=4};
static esp_netif_t *ap_netif=&ap,*sta_netif=&sta;
static bool ap_active=true,sta_has_ip,ap_dns_dirty=true;
static ip4 ap_dns_applied{};
static int ap_clients,sets,starts,stops,errors,deauths,fault;
static uint8_t offered;
static uint32_t upstream_dns=0x01010101;
static void log_internal_heap(const char*,int){errors++;}
static int esp_netif_get_dns_info(esp_netif_t*,int,esp_netif_dns_info_t *out){out->ip.type=0;out->ip.u_addr.ip4.addr=upstream_dns;return 0;}
static int esp_netif_dhcps_stop(esp_netif_t*){stops++;return fault==1?ESP_FAIL:ESP_OK;}
static int esp_netif_dhcps_start(esp_netif_t*){starts++;return fault==3?ESP_FAIL:ESP_OK;}
static int esp_netif_dhcps_option(esp_netif_t*,int,int,void *value,unsigned){offered=*(uint8_t*)value;return fault==2?ESP_FAIL:ESP_OK;}
static int esp_netif_set_dns_info(esp_netif_t*,int,esp_netif_dns_info_t *dns){assert(dns->ip.u_addr.ip4.addr!=0);sets++;return ESP_OK;}
static int esp_netif_set_default_netif(esp_netif_t*){return 0;}
static int esp_wifi_deauth_sta(int aid){assert(aid==0);deauths++;return 0;}
'''
CASES=r'''
int main(){
 wifi_hotspot_refresh_dns_locked();
 assert(!ap_dns_dirty && !offered && !sets && !errors && starts==1);
 wifi_hotspot_refresh_dns_locked();assert(starts==1);
 sta_has_ip=true;ap_dns_dirty=true;ap_clients=1;
 wifi_hotspot_refresh_dns_locked();
 assert(!ap_dns_dirty && offered==OFFER_DNS && sets==1 && deauths==1);
 assert(ap_dns_applied.addr==upstream_dns);
 wifi_hotspot_refresh_dns_locked();assert(sets==1);
 sta_has_ip=false;ap_dns_dirty=true;
 wifi_hotspot_refresh_dns_locked();
 assert(!ap_dns_dirty && !offered && sets==1 && !ap_dns_applied.addr && !errors);
 for(int failure=1;failure<=3;failure++){
  sta_has_ip=true;ap_dns_dirty=true;fault=failure;upstream_dns++;
  const uint32_t old=ap_dns_applied.addr;const int old_starts=starts;
  wifi_hotspot_refresh_dns_locked();
  assert(ap_dns_dirty && ap_dns_applied.addr==old);
  if(failure==2)assert(starts==old_starts+1);
  fault=0;wifi_hotspot_refresh_dns_locked();
  assert(!ap_dns_dirty && ap_dns_applied.addr==upstream_dns);
 }
 assert(errors==3);
 puts("Hotspot DNS passed: absent uplink never sets DNS 0.0.0.0, unchanged settings do not restart DHCP, failures retain dirty state and retry safely");
}
'''
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--cxx');args=parser.parse_args()
    compiler=args.cxx or shutil.which('cl') or shutil.which('c++')
    if not compiler:parser.error('Use a C++ developer terminal or --cxx')
    source=(ROOT/'src/setwifi.cpp').read_text(encoding='utf-8')
    match=re.search(r'static void wifi_hotspot_refresh_dns_locked\(void\)\s*\{',source);assert match
    end,depth=match.end(),1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    with tempfile.TemporaryDirectory(prefix='hotspot-dns-',ignore_cleanup_errors=True) as temporary:
        folder=Path(temporary);unit=folder/'dns.cpp';exe=folder/'dns.exe'
        unit.write_text(SDK+source[match.start():end]+CASES,encoding='utf-8')
        if Path(compiler).stem.lower()=='cl':cmd=[compiler,'/nologo','/std:c++17','/EHsc',str(unit),'/Fe:'+str(exe),'/Fo:'+str(folder/'dns.obj')]
        else:cmd=[compiler,'-std=c++17',str(unit),'-o',str(exe)]
        subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
