"""Exercise actual OnlineMusic.cpp against HTTP, SD and playback fault fakes.
Run from a C++ developer terminal: python tests/online_music_host_test.py
No external network, credentials or real device are used by this test.
"""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADERS = {
    "esp_attr.h": "#pragma once\n#define EXT_RAM_BSS_ATTR\n",
    "esp_crt_bundle.h": "#pragma once\ninline int esp_crt_bundle_attach(void*) {return 0;}\n",
    "esp_heap_caps.h": """#pragma once
#include <cstdlib>
enum {MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2, MALLOC_CAP_INTERNAL=4};
extern bool fail_alloc;
inline void *heap_caps_malloc(size_t n, int) {return fail_alloc ? nullptr : malloc(n);}
inline void *heap_caps_calloc(size_t n, size_t s, int) {return fail_alloc ? nullptr : calloc(n,s);}
inline size_t heap_caps_get_free_size(int) {return 100000;}
inline size_t heap_caps_get_largest_free_block(int) {return 100000;}
""",
    "strings.h": "#pragma once\n#define strcasecmp _stricmp\n#define strncasecmp _strnicmp\n",
    "freertos/FreeRTOS.h": """#pragma once
#include <cstdint>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdPASS 1
""",
    "freertos/task.h": """#pragma once
extern bool fail_task;
extern void (*pending_task)(void*);
inline int xTaskCreatePinnedToCore(void(*f)(void*),const char*,unsigned,void*,int,void*,int) {
 if(fail_task)return 0; pending_task=f; return 1;
}
inline unsigned uxTaskGetStackHighWaterMark(void*) {return 4096;}
inline void vTaskDelete(void*) {}
""",
    "esp_http_client.h": """#pragma once
#include <cstdint>
typedef int esp_err_t;
enum {ESP_OK=0, ESP_ERR_HTTP_EAGAIN=1001, HTTP_EVENT_ON_HEADER=1, HTTP_ADDR_TYPE_INET=2};
struct esp_http_client_event_t {int event_id; void *user_data; char *header_key; char *header_value;};
struct esp_http_client_config_t {
 const char *url; int timeout_ms,buffer_size,buffer_size_tx,addr_type; bool disable_auto_redirect;
 esp_err_t(*event_handler)(esp_http_client_event_t*); void *user_data; int(*crt_bundle_attach)(void*);
};
struct Client;
typedef Client *esp_http_client_handle_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*);
int esp_http_client_set_header(Client*,const char*,const char*);
int esp_http_client_open(Client*,int);
int64_t esp_http_client_fetch_headers(Client*);
int esp_http_client_get_status_code(Client*);
int esp_http_client_set_timeout_ms(Client*,int);
int esp_http_client_read(Client*,char*,int);
bool esp_http_client_is_complete_data_received(Client*);
int esp_http_client_close(Client*);
int esp_http_client_cleanup(Client*);
""",
}
HARNESS = r'''
#define SDCAR_H
#define SMARTVOICEBOX_IDF_RUNTIME_H
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <algorithm>
extern "C" bool sdcar_is_ready(void);
extern "C" int sdcar_register_music_track(const char*);
extern "C" bool sdcar_copy_music_track(uint8_t,char*,size_t,uint32_t*);
extern "C" bool sdcar_music_track_valid(uint8_t);
static uint32_t milliseconds=1000;
static uint32_t app_millis() {return milliseconds;}
static void app_delay_ms(uint32_t n) {milliseconds+=n;}
static size_t strlcpy(char *dst,const char *src,size_t size) {
 size_t n=strlen(src); if(size){size_t k=std::min(n,size-1);memcpy(dst,src,k);dst[k]=0;} return n;
}
static size_t strlcat(char *dst,const char *src,size_t size) {
 size_t used=strlen(dst);return used<size?used+strlcpy(dst+used,src,size-used):size+strlen(src);
}
bool fail_alloc,fail_task;
void (*pending_task)(void*);
#include "@ROOT@/src/MusicLibraryLock.cpp"
#include "@ROOT@/src/OnlineMusic.cpp"
namespace fs=std::filesystem;
static fs::path sdroot;
static bool wifi=true,sd=true,fail_flush=false,truncate_audio=false,redirect_evil=false;
static bool html_source=false,fail_mkdir=false,fail_open=false,fail_write=false,fail_commit=false,read_error=false,connect_error=false;
static int source_http_error=0;
static uint64_t free_budget=100u*1024u*1024u;
static int64_t declared_audio_length=-1;
static std::string source_type="audio/mpeg";
static bool cancel_read=false,fail_queue=false,online=false;
static bool queued_paused;
static bool accepted_online=false;
static unsigned save_calls, play_requests;
static std::string accepted_path, saved_id, saved_title, saved_mp3;
static int selected_local=-1;
extern "C" uint8_t get_music_track_count(void) {return 8;}
extern "C" void music_select_track(uint8_t index) {online_music_cancel_pending();selected_local=index;}
static bool save_failed, cancel_save, lyric_network_fail, lyric_absent, saw_lyrics;
extern "C" int sdcar_register_music_track(const char*) {return 3;}
extern "C" bool sdcar_music_track_valid(uint8_t i) {return i<8;}
extern "C" bool sdcar_copy_music_track(uint8_t i,char*p,size_t n,uint32_t*g) {if(i!=3)return false;if(p)strlcpy(p,"/music/test.wav",n);if(g)*g=1;return true;}
extern "C" bool music_save_mp3(const char *path,const char *title,const char*,const char *id,const char*lyrics,uint32_t job,
 music_save_current_t current,music_save_progress_t progress,music_save_result_t*result,char*error,size_t cap) {
 ++save_calls; saved_mp3=path?path:""; saved_title=title?title:""; saved_id=id?id:""; saw_lyrics=lyrics&&strstr(lyrics,"[00:");
 if(cancel_save){online_music_cancel_pending();return false;}
 if(progress)progress(job,1024*1024);
 if(save_failed){strlcpy(error,"fake SD failure",cap);return false;}
 *result={};strcpy(result->wav_path,"/music/test.wav");strcpy(result->lrc_path,"/music/test.lrc");result->lyrics_saved=saw_lyrics;result->reused=save_calls>1;
 return current(job);
}
static unsigned queued=0,woken=0,clients=0,source_opens=0;
static std::string active_path,queued_title;
static std::string search_body=R"({"code":200,"result":{"songs":[{"id":123,"name":"茉莉花","artists":[{"name":"歌手A"}]},{"id":456,"name":"茉莉花 第二版","artists":[{"name":"歌手B"}]}]}})";
static std::string audio_body(8192,'A');
static fs::path file_path(const char *p) {return sdroot / (p[0]=='/'?p+1:p);}
extern "C" bool sdcar_is_ready(void) {return sd;}
extern "C" bool get_wifi_connected(void) {return wifi;}
extern "C" bool music_audio_is_online(void) {return online;}
extern "C" bool music_audio_online_uses_path(const char *p) {return active_path==p;}
extern "C" bool music_audio_copy_active_online_path(char *p,size_t n) {
 if(!accepted_online||accepted_path.empty()){if(p&&n)p[0]=0;return false;}
 strlcpy(p,accepted_path.c_str(),n);return true;
}
extern "C" void music_audio_discard_online_queue(void) {}
extern "C" bool music_audio_queue_online(const char *p,const char *title,uint32_t,bool paused) {
 if(fail_queue)return false; ++queued;active_path=p;online=true;queued_title=title;queued_paused=paused; return true;
}
extern "C" void music_audio_wake(void) {++woken;}
extern "C" void music_audio_stop(void) {online_music_cancel_pending();online=false;active_path.clear();accepted_online=false;accepted_path.clear();}
extern "C" void music_audio_pause(void) {online_music_note_pause(true);}
extern "C" void music_audio_play(void) {++play_requests;online_music_note_pause(false);assert(online||selected_local>=0);}
extern "C" bool storage_mkdir(const char *p) {if(fail_mkdir)return false;fs::create_directories(file_path(p));return true;}
extern "C" bool storage_is_file(const char *p) {return fs::is_regular_file(file_path(p));}
extern "C" bool storage_exists(const char *p) {return fs::exists(file_path(p));}
extern "C" bool storage_remove(const char *p) {return fs::remove(file_path(p));}
extern "C" bool storage_rename(const char *from,const char *to) {if(fail_commit)return false;fs::rename(file_path(from),file_path(to));return true;}
extern "C" uint64_t storage_free_bytes(void) {return free_budget;}
StorageStream::~StorageStream() {close();}
bool StorageStream::open(const char *p,const char *mode) {close();if(fail_open)return false;file_=fopen(file_path(p).string().c_str(),mode);return file_!=nullptr;}
void StorageStream::close() {if(file_)fclose(file_);file_=nullptr;}
size_t StorageStream::write(const void *p,size_t n) {return fwrite(p,1,fail_write&&n?n-1:n,file_);}
bool StorageStream::flush() {return !fail_flush && !fflush(file_);}
struct Client {esp_http_client_config_t cfg;std::string url,body,type,location;size_t pos=0;int status=200;bool truncated=false;int64_t declared=-1;};
Client *esp_http_client_init(const esp_http_client_config_t *cfg) {
 auto *c=new Client; c->cfg=*cfg;c->url=cfg->url;++clients;
 if(c->url.find("/search/get/web")!=std::string::npos){c->body=search_body;c->type="application/json";}
 else if(c->url.find("/api/song/lyric")!=std::string::npos){
  c->status=lyric_network_fail?503:200;c->type="application/json";
  c->body=lyric_absent?R"({"code":200,"nolyric":true})":R"({"code":200,"lrc":{"lyric":"[00:00.00]原创测试歌词\n[00:01.00]第二行"}})";
 }
 else if(c->url.find("/song/media/outer/url")!=std::string::npos) {
  ++source_opens;c->status=source_http_error?source_http_error:302;c->location=html_source?"http://music.163.com/404":redirect_evil?"https://evil.test/song.mp3":"https://m801.music.126.net/song.mp3";
 } else if(c->url=="http://music.163.com/404"){c->status=302;c->location="https://music.163.com/404";}
 else if(c->url=="https://music.163.com/404"){c->body="<!DOCTYPE html><html>Unavailable</html>";c->type="text/html;charset=utf8";c->declared=0;}
 else {assert(c->url.find("m801.music.126.net")!=std::string::npos);c->body=audio_body;c->type=source_type;c->truncated=truncate_audio;c->declared=declared_audio_length;}
 return c;
}
int esp_http_client_set_header(Client*,const char*,const char*) {return 0;}
int esp_http_client_open(Client*c,int) {return connect_error&&c->url.find("m801.music.126.net")!=std::string::npos?-123:0;}
int64_t esp_http_client_fetch_headers(Client *c) {
 for(auto &h:std::vector<std::pair<std::string,std::string>>{{"Content-Type",c->type},{"Location",c->location}}) {
  esp_http_client_event_t event{HTTP_EVENT_ON_HEADER,c->cfg.user_data,h.first.data(),h.second.data()};c->cfg.event_handler(&event);
 }
 return c->declared>=0?c->declared:c->body.size()+(c->truncated?100:0);
}
int esp_http_client_get_status_code(Client *c) {return c->status;}
int esp_http_client_set_timeout_ms(Client*,int) {return 0;}
int esp_http_client_read(Client *c,char *dst,int n) {
 if(read_error&&c->type=="audio/mpeg")return -123;
 if(cancel_read && c->type=="audio/mpeg"){cancel_read=false;online_music_cancel_pending();}
 size_t count=std::min({(size_t)n,c->body.size()-c->pos,(size_t)3072});
 memcpy(dst,c->body.data()+c->pos,count);c->pos+=count;++milliseconds;return (int)count;
}
bool esp_http_client_is_complete_data_received(Client *c) {return c->pos==c->body.size()&&!c->truncated;}
int esp_http_client_close(Client*) {return 0;}
int esp_http_client_cleanup(Client *c) {delete c;--clients;return 0;}
static std::string reply() {char text[1536];online_music_get_result(text,sizeof(text));return text;}
static void run() {milliseconds+=200;online_music_service_poll();assert(pending_task);auto f=pending_task;pending_task=nullptr;f(nullptr);assert(clients==0);}
static void command(const char *text) {assert(online_music_handle_command(text));run();}
int main(int argc,char **argv) {
 assert(argc==2);sdroot=argv[1];
 assert(netease_url("https://music.163.com/a"));
 assert(netease_url("https://m801.music.126.net/a"));
 assert(!netease_url("https://music.163.com.evil.test/a"));
 assert(!netease_url("https://evil.test@music.163.com/a"));
 assert(!netease_url("file:///etc/passwd"));
 char encoded[1280];assert(query_url("茉莉花 &x=1",encoded,sizeof(encoded)));
 assert(strstr(encoded,"%26x%3D1") && !strstr(encoded,"&x=1"));
 command("搜歌 茉莉花");assert(queued==0 && reply().find("1. 茉莉花")!=std::string::npos);
 assert(online_music_handle_command("播放第二首"));
 auto preparing_job=generation.load();
 assert(online_music_handle_command("暂停"));assert(generation.load()==preparing_job);
 run();assert(queued==1&&woken==1&&queued_title.find("第二版")!=std::string::npos&&queued_paused);
 assert(active_path=="/.online/song0.mp3");
 auto first=fs::file_size(file_path(active_path.c_str()));
 command("上一首");assert(queued==2&&active_path=="/.online/song1.mp3"&&!queued_paused);
 assert(fs::file_size(file_path("/.online/song0.mp3"))==first);
 command("播放第6首");assert(queued==2 && reply().find("序号")!=std::string::npos);
 assert(online_music_handle_command("暂停"));assert(reply().find("暂停")!=std::string::npos);
 assert(online_music_handle_command("继续播放"));assert(reply().find("继续")!=std::string::npos);
 assert(online_music_handle_command("停止"));assert(!online);
 assert(online_music_handle_command("播放 茉莉花"));
 assert(online_music_handle_command("停止"));milliseconds+=200;online_music_service_poll();assert(!pending_task&&queued==2);
 /* Latest queued request wins without spawning a second worker. */
 assert(online_music_handle_command("搜歌 A"));assert(online_music_handle_command("搜歌 B"));run();assert(queued==2);
 search_body=R"({"code":200,"result":{"songs":[{"id":789,"name":"新歌","artists":[]}]}})";
 truncate_audio=true;command("播放 新歌");assert(queued==2&&!storage_exists("/.online/download.part"));
 truncate_audio=false;cancel_read=true;command("播放 新歌");assert(queued==2&&reply().find("取消")!=std::string::npos);
 redirect_evil=true;command("播放 新歌");assert(queued==2);redirect_evil=false;
 fail_flush=true;command("播放 新歌");assert(queued==2&&!storage_exists("/.online/download.part"));fail_flush=false;
 command("播放 新歌");assert(queued==3&&woken==3);
 wifi=false;command("播放第1首");assert(queued==4);wifi=true;
 wifi=false;command("搜歌 新歌");assert(reply().find("WiFi")!=std::string::npos);wifi=true;
 sd=false;command("播放 新歌");assert(queued==4&&reply().find("SD 卡")!=std::string::npos);sd=true;
 search_body="not JSON";command("搜歌 失败");assert(reply().find("搜索失败")!=std::string::npos);
 assert(!online_music_handle_command("播放第一首")); /* No stale results after failed search. */
 fail_alloc=true;command("搜歌 新歌");assert(reply().find("内存不足")!=std::string::npos);fail_alloc=false;
 fail_task=true;assert(online_music_handle_command("搜歌 新歌"));milliseconds+=200;online_music_service_poll();
 assert(!pending_task&&reply().find("启动失败")!=std::string::npos);fail_task=false;
 auto upload_job=online_music_begin_upload();assert(online_music_job_current(upload_job));
 auto old_version=reply_version;
 online_music_playback_error(upload_job-1,"old decoder error");assert(reply_version==old_version);
 assert(online_music_handle_command("停止"));assert(!online_music_job_current(upload_job));
 assert(!online_music_finish_upload(upload_job,"/.online/phone0.mp3","phone"));
 upload_job=online_music_begin_upload();assert(online_music_handle_command("暂停"));
 assert(online_music_finish_upload(upload_job,"/.online/phone0.mp3","phone")&&queued_paused);
 search_body=R"({"code":200,"result":{"songs":[{"id":123,"name":"保存测试","artists":[{"name":"歌手A"}]},{"id":456,"name":"另一首","artists":[{"name":"歌手B"}]}]}})";
 assert(online_music_handle_web_command("播放 保存测试",true));run();assert(save_calls==0&&queued>=1);
 assert(!online_music_can_download_current()&&!online_music_download_current()); /* queued, not yet the audio task */
 accepted_online=true;accepted_path=active_path;auto playing_path=active_path;auto queued_at_play=queued;auto woken_at_play=woken;
 assert(online_music_can_download_current());
 music_audio_pause();auto plays_before=play_requests;assert(queued_paused==false);
 assert(online_music_download_current());assert(!online_music_can_download_current()&&!online_music_download_current());
 run();assert(save_calls==1&&saved_id=="123"&&saved_title=="保存测试"&&saved_mp3==playing_path&&saw_lyrics&&reply().find("LRC")!=std::string::npos);
 assert(queued==queued_at_play&&woken==woken_at_play&&play_requests==plays_before);
 assert(online_music_download_current());run();assert(save_calls==2&&reply().find("已存在")!=std::string::npos&&queued==queued_at_play);
 save_failed=true;assert(online_music_download_current());run();assert(save_calls==3&&reply().find("保存失败")!=std::string::npos&&queued==queued_at_play);save_failed=false;
 lyric_network_fail=true;assert(online_music_download_current());run();assert(save_calls==4&&!saw_lyrics&&reply().find("获取失败")!=std::string::npos);lyric_network_fail=false;
 lyric_absent=true;assert(online_music_download_current());run();assert(save_calls==5&&!saw_lyrics&&reply().find("未提供")!=std::string::npos);lyric_absent=false;
 search_body=R"({"code":200,"result":{"songs":[{"id":456,"name":"另一首","artists":[{"name":"歌手B"}]}]}})";
 assert(online_music_handle_web_command("搜歌 另一首",true));run();assert(save_calls==5);
 online_music_song_t searched[5];assert(online_music_copy_songs(searched,5)==1&&!strcmp(searched[0].id,"456"));
 assert(online_music_can_download_current());assert(online_music_download_current());run();
 assert(save_calls==6&&saved_id=="123"&&saved_title=="保存测试"&&saved_mp3==playing_path&&queued==queued_at_play);
 accepted_path="/.online/phone0.mp3";assert(!online_music_can_download_current());accepted_path=playing_path;
 assert(online_music_download_current());accepted_path="/.online/other.mp3";run();assert(reply().find("已切换")!=std::string::npos&&save_calls==6);accepted_path=playing_path;
 cancel_save=true;assert(online_music_download_current());run();assert(reply().find("取消")!=std::string::npos);cancel_save=false;
 assert(online_music_download_current());assert(online_music_handle_command("停止"));
 milliseconds+=200;online_music_service_poll();assert(!pending_task&&save_calls==7&&!accepted_online);
 online_music_song_t snapshot[5];assert(online_music_copy_songs(snapshot,5)==1&&!strcmp(snapshot[0].id,"456"));assert(!online_music_is_preparing());
 /* Distinguish the real unavailable-song redirect from cache/network failures. */
 auto failure_case=[&](const char*first,const char*second=nullptr){
  cached_ids[0][0]=cached_ids[1][0]=0;unsigned q=queued,save=save_calls;
  command("播放 故障测试");auto text=reply();
  assert(queued==q&&save_calls==save&&!storage_exists("/.online/download.part"));
  if(text.find(first)==std::string::npos&&(!second||text.find(second)==std::string::npos)){fprintf(stderr,"expected stage %s; actual %s\n",first,text.c_str());abort();}
 };
 html_source=true;failure_case("未提供可播放音源");html_source=false;
 source_http_error=404;failure_case("未提供可播放音源");source_http_error=410;failure_case("未提供可播放音源");source_http_error=403;failure_case("拒绝");source_http_error=500;failure_case("请求");source_http_error=0;
 source_type="application/json";failure_case("未提供可播放音源");source_type="audio/flac";failure_case("格式");source_type="audio/mpeg";
 declared_audio_length=21*1024*1024;failure_case("过大","超");declared_audio_length=-1;
 free_budget=1;failure_case("空间");free_budget=0;failure_case("容量");free_budget=100u*1024u*1024u;
 fail_mkdir=true;failure_case("SD");fail_mkdir=false;fail_open=true;failure_case("SD");fail_open=false;
 fail_write=true;failure_case("SD");fail_write=false;fail_flush=true;failure_case("SD");fail_flush=false;fail_commit=true;failure_case("SD");fail_commit=false;
 read_error=true;failure_case("下载","中断");read_error=false;connect_error=true;failure_case("连接","请求");connect_error=false;
 auto q=queued;command("播放 故障恢复");assert(queued==q+1);
 source_type="application/octet-stream";cached_ids[0][0]=cached_ids[1][0]=0;q=queued;command("播放 二进制音源");assert(queued==q+1);source_type="audio/mpeg";
 puts("online music: search/select/cache/save/lyrics/cancel and source HTML/404/410/403/MIME/size/network versus SD mkdir/open/write/flush/commit failures passed");
 return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=shutil.which("cl"))
    args = parser.parse_args()
    if not args.cxx:
        parser.error("Run from a Visual Studio C++ developer terminal")
    with tempfile.TemporaryDirectory(prefix="online-music-", ignore_cleanup_errors=True) as tmp:
        directory = Path(tmp)
        for name, content in HEADERS.items():
            path = directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        harness = directory / "test.cpp"
        harness.write_text(HARNESS.replace("@ROOT@", ROOT.as_posix()), encoding="utf-8")
        sdk = Path("C:/Espressif/frameworks/esp-idf-v5.5.6/components/json/cJSON")
        exe = directory / "online_music_test.exe"
        subprocess.run([args.cxx, "/nologo", "/std:c++17", "/EHsc", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
                        f"/I{directory}", f"/I{ROOT / 'src'}", f"/I{sdk}", str(harness),
                        str(ROOT / "src/music_intent.c"), str(sdk / "cJSON.c"),
                        f"/Fe:{exe}", f"/Fo{directory}\\"], check=True)
        subprocess.run([str(exe), str(directory / "sd")], check=True)


if __name__ == "__main__":
    main()
