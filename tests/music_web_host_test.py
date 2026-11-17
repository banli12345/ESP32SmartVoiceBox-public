"""Actual local HTTP handlers plus OnlineMusic, with bounded request/SD fakes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from online_music_host_test import HEADERS, HARNESS, ROOT

HTTPD = r'''
#pragma once
#include <string>
#include <cstring>
#include "esp_http_client.h"
typedef void *httpd_handle_t;
struct httpd_req_t {
 size_t content_len=0,position=0,chunk=4096,disconnect_at=0;
 std::string incoming,type="application/json",filename="song.mp3",response,status,content_type;
 bool cancel_on_read=false;
 std::string upload_token,offset="0";
};
struct httpd_config_t {unsigned stack_size=0;int core_id=0,max_open_sockets=0,max_uri_handlers=0,recv_wait_timeout=0,send_wait_timeout=0;bool lru_purge_enable=false;int(*open_fn)(httpd_handle_t,int)=nullptr;};
struct httpd_uri_t {const char *uri;int method;int(*handler)(httpd_req_t*);void *user_ctx;};
#define HTTPD_DEFAULT_CONFIG() httpd_config_t{}
enum {HTTP_GET,HTTP_POST,HTTPD_500_INTERNAL_SERVER_ERROR=500,HTTPD_RESP_USE_STRLEN=-1};
int httpd_resp_send_err(httpd_req_t*,int,const char*);
int httpd_resp_set_status(httpd_req_t*,const char*);
int httpd_resp_set_type(httpd_req_t*,const char*);
int httpd_resp_set_hdr(httpd_req_t*,const char*,const char*);
int httpd_resp_send(httpd_req_t*,const char*,int);
int httpd_resp_send_chunk(httpd_req_t*,const char*,int);
int httpd_req_recv(httpd_req_t*,char*,size_t);
size_t httpd_req_get_hdr_value_len(httpd_req_t*,const char*);
int httpd_req_get_hdr_value_str(httpd_req_t*,const char*,char*,size_t);
int httpd_start(httpd_handle_t*,const httpd_config_t*);
int httpd_stop(httpd_handle_t);
int httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t*);
'''
EXTRA = r'''
#define ESP_FAIL 1
#define CONFIG_LWIP_IPV6 0
#include "@ROOT@/src/MusicWeb.cpp"
static bool ap_on=true;
static unsigned starts,stops,volume_saves;
static int audio_volume=50;
static std::vector<httpd_uri_t> registered;
bool service_config_web_register(httpd_handle_t){return true;}
void service_config_web_poll(){}
extern "C" bool wifi_hotspot_get_enabled(void) {return ap_on;}
extern "C" void wifi_hotspot_copy_info(char*,size_t,char*,size_t,char *ip,size_t cap,uint8_t*,bool*) {strlcpy(ip,"172.31.42.1",cap);}
extern "C" void music_audio_copy_title(char *out,size_t n) {strlcpy(out,queued_title.c_str(),n);}
extern "C" bool music_audio_is_playing(void) {return online&&!queued_paused;}
extern "C" uint8_t get_audio_volume(void) {return (uint8_t)audio_volume;}
extern "C" void set_audio_volume(int value) {audio_volume=value;}
extern "C" void save_audio_volume(void) {++volume_saves;}
int httpd_resp_send_err(httpd_req_t *r,int,const char *text){r->response=text;return 0;}
int httpd_resp_set_status(httpd_req_t *r,const char *text){r->status=text;return 0;}
int httpd_resp_set_type(httpd_req_t *r,const char *text){r->content_type=text;return 0;}
int httpd_resp_set_hdr(httpd_req_t*,const char*,const char*){return 0;}
int httpd_resp_send(httpd_req_t *r,const char *text,int n){r->response.assign(text,n<0?strlen(text):n);return 0;}
int httpd_resp_send_chunk(httpd_req_t*r,const char*p,int n){if(p)r->response.append(p,n);return 0;}
static std::string transfer_bytes;static uint32_t transfer_total;static unsigned transfer_cancels;static bool checked_update,installable;
extern "C" bool file_transfer_begin(const char*k,const char*name,uint32_t size,char*t,size_t cap,char*,size_t){if(!k||!name)return false;transfer_bytes.clear();transfer_total=size;strlcpy(t,"transfer-token",cap);return true;}
extern "C" bool file_transfer_append(const char*t,uint32_t at,const void*p,size_t n,char*,size_t){if(strcmp(t,"transfer-token")||at!=transfer_bytes.size())return false;transfer_bytes.append((const char*)p,n);return true;}
extern "C" bool file_transfer_finish(const char*t,char*p,size_t cap,char*,size_t){if(!t||strcmp(t,"transfer-token")||transfer_bytes.size()!=transfer_total)return false;strlcpy(p,"/imges/test.bin",cap);return true;}
extern "C" void file_transfer_cancel(const char*){++transfer_cancels;transfer_bytes.clear();}
extern "C" void file_transfer_reset(){}
extern "C" bool system_update_check(){checked_update=true;return true;}
extern "C" bool system_update_install(){return installable;}
extern "C" void system_update_get_status(system_update_status_t*s){*s={};strcpy(s->current_version,"1.7.0");strcpy(s->candidate_version,"1.7.1");s->available=installable;}
int httpd_req_recv(httpd_req_t *r,char *out,size_t n){
 if(r->disconnect_at&&r->position>=r->disconnect_at)return -1;
 size_t got=std::min({n,r->chunk,r->incoming.size()-r->position});
 memcpy(out,r->incoming.data()+r->position,got);r->position+=got;++milliseconds;
 if(r->cancel_on_read){r->cancel_on_read=false;online_music_cancel_pending();}
 return (int)got;
}
size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *key){return !strcmp(key,"X-Filename")?r->filename.size():r->type.size();}
int httpd_req_get_hdr_value_str(httpd_req_t *r,const char *key,char *out,size_t n){
 auto &value=!strcmp(key,"X-Filename")?r->filename:!strcmp(key,"X-Upload-Token")?r->upload_token:!strcmp(key,"X-Offset")?r->offset:r->type;return strlcpy(out,value.c_str(),n)<n?0:1;
}
int httpd_start(httpd_handle_t *out,const httpd_config_t*){*out=(void*)1;++starts;return 0;}
int httpd_stop(httpd_handle_t){++stops;return 0;}
int httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t *uri){registered.push_back(*uri);return 0;}
static bool ok(const httpd_req_t &r){return r.response.find("\"ok\":true")!=std::string::npos;}
static httpd_req_t make_upload(){httpd_req_t r;r.type="application/octet-stream";r.incoming="ID3"+std::string(4093,'X');r.content_len=r.incoming.size();return r;}
int main(int argc,char **argv){
 assert(argc==2);sdroot=argv[1];milliseconds=5000;music_web_service_poll();
 assert(starts==1&&registered.size()==12&&music_web_is_running());
 httpd_req_t req;page(&req);assert(req.response.find("随身音乐")!=std::string::npos&&req.response.find("上传照片")!=std::string::npos&&req.response.find("mediaFirmwareSend")!=std::string::npos);
 req={};status(&req);assert(ok(req)&&req.content_type.find("application/json")!=std::string::npos);
 req={};req.incoming=R"({"volume":31})";req.content_len=req.incoming.size();volume(&req);assert(ok(req)&&audio_volume==31&&volume_saves==1);
 req={};req.incoming=R"({"volume":999})";req.content_len=req.incoming.size();volume(&req);assert(!ok(req)&&audio_volume==31&&volume_saves==1);
 req=make_upload();req.chunk=1;req.filename="%E6%89%8B%E6%9C%BA%E6%AD%8C%E6%9B%B2.mp3";
 upload(&req);assert(ok(req)&&queued==1&&active_path=="/.online/phone0.mp3"&&queued_title=="手机歌曲.mp3");
 assert(fs::file_size(file_path(active_path.c_str()))==4096);
 req=make_upload();upload(&req);assert(ok(req)&&queued==2&&active_path=="/.online/phone1.mp3");
 assert(fs::file_size(file_path("/.online/phone0.mp3"))==4096);
 req=make_upload();req.disconnect_at=1024;req.chunk=512;upload(&req);assert(!ok(req)&&queued==2&&!storage_exists("/.online/phone.part"));
 req=make_upload();req.cancel_on_read=true;upload(&req);assert(!ok(req)&&queued==2&&!storage_exists("/.online/phone.part"));
 req=make_upload();req.incoming.assign(4096,'X');upload(&req);assert(!ok(req)&&queued==2);
 req=make_upload();fail_flush=true;upload(&req);fail_flush=false;assert(!ok(req)&&queued==2);
 req=make_upload();req.content_len=21u*1024u*1024u;upload(&req);assert(!ok(req)&&queued==2);
 req=make_upload();req.type="multipart/form-data";upload(&req);assert(!ok(req));
 req=make_upload();fail_alloc=true;upload(&req);fail_alloc=false;assert(!ok(req)&&queued==2);
 req={};req.incoming=R"({"command":"停止"})";req.content_len=req.incoming.size();music_command(&req);assert(ok(req)&&!online);
 req={};page(&req);assert(req.response.find("id=\"download\"")!=std::string::npos&&req.response.find("saveLibrary")==std::string::npos&&req.response.find("save:false")!=std::string::npos&&req.response.find("download:true")!=std::string::npos);
 req={};req.incoming=R"({"command":"播放 保存测试","save":false})";req.content_len=req.incoming.size();music_command(&req);assert(ok(req)&&!pending_request.save_library&&!pending_request.save_only);run();assert(save_calls==0);
 req={};req.incoming=R"({"command":"播放 保存测试","save":true})";req.content_len=req.incoming.size();music_command(&req);assert(ok(req)&&!pending_request.save_library);run();assert(save_calls==0);
 req={};status(&req);assert(req.response.find("\"can_download\":false")!=std::string::npos);
 accepted_online=true;accepted_path=active_path;auto q=queued;req={};req.incoming=R"({"download":true})";req.content_len=req.incoming.size();music_command(&req);assert(ok(req)&&pending_request.save_only);
 httpd_req_t again;again.incoming=R"({"download":true})";again.content_len=again.incoming.size();music_command(&again);assert(!ok(again));
 run();assert(save_calls==1&&queued==q&&saved_id=="123");
 accepted_online=false;req={};req.incoming=R"({"download":true})";req.content_len=req.incoming.size();music_command(&req);assert(!ok(req)&&save_calls==1);
 req={};status(&req);assert(req.response.find("\"songs\":[{")!=std::string::npos&&req.response.find("\"preparing\":false")!=std::string::npos);
 req={};req.incoming=R"({"local_track":3})";req.content_len=req.incoming.size();music_command(&req);assert(ok(req)&&selected_local==3);
 req={};req.incoming=R"({"local_track":999})";req.content_len=req.incoming.size();music_command(&req);assert(!ok(req)&&selected_local==3);

 req={};req.incoming=R"({"kind":"photo","name":"test.bin","size":12})";req.content_len=req.incoming.size();file_begin(&req);assert(ok(req)&&req.response.find("transfer-token")!=std::string::npos);
 req={};req.incoming="abcdefghijkl";req.content_len=req.incoming.size();req.chunk=1;req.upload_token="transfer-token";file_chunk(&req);assert(ok(req)&&transfer_bytes=="abcdefghijkl");
 req={};req.incoming=R"({"token":"transfer-token"})";req.content_len=req.incoming.size();file_finish(&req);assert(ok(req)&&req.response.find("/imges/test.bin")!=std::string::npos);
 req={};req.incoming="abcdef";req.content_len=6;req.chunk=1;req.disconnect_at=3;req.upload_token="transfer-token";transfer_bytes.clear();unsigned cancels=transfer_cancels;file_chunk(&req);assert(!ok(req)&&transfer_cancels>cancels&&transfer_bytes.empty());
 req={};req.content_len=65537;file_chunk(&req);assert(!ok(req));
 req={};req.incoming=R"({"kind":"photo","name":"bad.bin","size":2.5})";req.content_len=req.incoming.size();file_begin(&req);assert(!ok(req));
 req={};update_check(&req);assert(ok(req)&&checked_update&&req.response.find("1.7.0")!=std::string::npos);req={};update_install(&req);assert(!ok(req));installable=true;req={};update_install(&req);assert(ok(req));
 ap_on=false;music_web_service_poll();assert(stops==1&&!music_web_is_running());
 puts("music web: actual handlers, one-byte upload chunks, slot isolation, disconnect/cancel/format/size/SD/OOM and volume passed");
}
'''


def main():
    compiler = shutil.which("cl")
    if not compiler:
        raise SystemExit("Run from a Visual Studio C++ developer terminal")
    with tempfile.TemporaryDirectory(prefix="music-web-", ignore_cleanup_errors=True) as tmp:
        directory = Path(tmp)
        headers = dict(HEADERS)
        headers["esp_http_server.h"] = HTTPD
        headers["lwip/sockets.h"] = "#pragma once\n#include <winsock2.h>\ntypedef int socklen_t;\n#pragma comment(lib,\"Ws2_32.lib\")\n"
        headers["lwip/inet.h"] = "#pragma once\n#include <ws2tcpip.h>\n"
        for name, content in headers.items():
            path = directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        source = HARNESS.split("int main(int argc,char **argv)", 1)[0] + EXTRA
        unit = directory / "test.cpp"
        unit.write_text(source.replace("@ROOT@", ROOT.as_posix()), encoding="utf-8")
        sdk = Path("C:/Espressif/frameworks/esp-idf-v5.5.6/components/json/cJSON")
        exe = directory / "web_test.exe"
        subprocess.run([compiler, "/nologo", "/std:c++20", "/EHsc", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
                        "/D_WINSOCK_DEPRECATED_NO_WARNINGS", "/DNOMINMAX", f"/I{directory}", f"/I{ROOT / 'src'}", f"/I{sdk}",
                        str(unit), str(ROOT / "src/music_intent.c"), str(sdk / "cJSON.c"),
                        f"/Fe:{exe}", f"/Fo{directory}\\"], check=True)
        subprocess.run([str(exe), str(directory / "sd")], check=True)


if __name__ == "__main__":
    main()
