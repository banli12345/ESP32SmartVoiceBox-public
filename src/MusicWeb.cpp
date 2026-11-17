#include "SDcar.h"
#include "MusicWeb.h"
#include "OnlineMusic.h"
#include "Sound.h"
#include "Storage.h"
#include "setwifi.h"
#include "IdfRuntime.h"
#include "music_web_page.h"
#include "media_web_assets.h"
#include "FileTransfer.h"
#include "SystemUpdate.h"
#include "ServiceConfigWeb.h"
#include <cerrno>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace {
static httpd_handle_t server;
static std::atomic<bool> running{false};
static uint32_t last_retry;
constexpr size_t MAX_UPLOAD = 20u * 1024u * 1024u;
const char *upload_paths[] = {"/.online/phone0.mp3", "/.online/phone1.mp3"};

esp_err_t json_reply(httpd_req_t *req, cJSON *root, const char *status = "200 OK")
{
    char *body = root ? cJSON_PrintUnformatted(root) : nullptr;
    cJSON_Delete(root);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    cJSON_free(body);
    return result;
}
esp_err_t answer(httpd_req_t *req, bool ok, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddBoolToObject(root, "ok", ok);
        if (!ok) cJSON_AddStringToObject(root, "error", message);
    }
    return json_reply(req, root, ok ? "200 OK" : "400 Bad Request");
}
cJSON *read_json(httpd_req_t *req)
{
    char body[512];
    if (!req->content_len || req->content_len >= sizeof(body)) return nullptr;
    size_t used = 0;
    while (used < req->content_len) {
        const int got = httpd_req_recv(req, body + used, req->content_len - used);
        if (got <= 0) return nullptr;
        used += got;
    }
    body[used] = 0;
    return cJSON_Parse(body);
}
esp_err_t page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const char *footer=strstr(MUSIC_WEB_PAGE,"<footer>"),*end=strstr(MUSIC_WEB_PAGE,"</html>");
    if(!footer||!end)return ESP_FAIL;
    if(httpd_resp_send_chunk(req,MUSIC_WEB_PAGE,footer-MUSIC_WEB_PAGE)!=ESP_OK||
       httpd_resp_send_chunk(req,MEDIA_WEB_SECTIONS,sizeof(MEDIA_WEB_SECTIONS)-1)!=ESP_OK||
       httpd_resp_send_chunk(req,footer,end-footer)!=ESP_OK||
       httpd_resp_send_chunk(req,MEDIA_WEB_SCRIPT,sizeof(MEDIA_WEB_SCRIPT)-1)!=ESP_OK||
       httpd_resp_send_chunk(req,"</html>",7)!=ESP_OK)return ESP_FAIL;
    return httpd_resp_send_chunk(req,nullptr,0);
}
esp_err_t status(httpd_req_t *req)
{
    char title[256], reply[1536];
    music_audio_copy_title(title, sizeof(title));
    online_music_get_result(reply, sizeof(reply));
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddBoolToObject(root, "ok", true);
        cJSON_AddBoolToObject(root, "playing", music_audio_is_playing());
        cJSON_AddNumberToObject(root, "volume", get_audio_volume());
        cJSON_AddStringToObject(root, "title", title);
        cJSON_AddStringToObject(root, "reply", reply);
        cJSON_AddBoolToObject(root, "preparing", online_music_is_preparing());
        cJSON_AddBoolToObject(root, "can_download", online_music_can_download_current());
        char saved_path[64];
        cJSON_AddNumberToObject(root, "saved_index", online_music_saved_track(saved_path, sizeof(saved_path)));
        cJSON_AddStringToObject(root, "saved_path", saved_path);
        cJSON *list = cJSON_AddArrayToObject(root, "songs");
        auto *songs = static_cast<online_music_song_t *>(heap_caps_calloc(5, sizeof(online_music_song_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        const unsigned count = songs ? online_music_copy_songs(songs, 5) : 0;
        for (unsigned i = 0; list && i < count; ++i) {
            cJSON *song = cJSON_CreateObject();
            if (!song) break;
            cJSON_AddNumberToObject(song, "index", i + 1);
            cJSON_AddStringToObject(song, "id", songs[i].id);
            cJSON_AddStringToObject(song, "title", songs[i].title);
            cJSON_AddStringToObject(song, "artist", songs[i].artist);
            cJSON_AddItemToArray(list, song);
        }
        free(songs);
    }
    return json_reply(req, root);
}
esp_err_t music_command(httpd_req_t *req)
{
    cJSON *root = read_json(req);
    cJSON *command = root ? cJSON_GetObjectItem(root, "command") : nullptr;
    bool accepted = false;
    const char *error = "请输入“搜歌 歌名”“播放 歌名”或播放控制指令";
    cJSON *download = root ? cJSON_GetObjectItem(root, "download") : nullptr;
    cJSON *local = root ? cJSON_GetObjectItem(root, "local_track") : nullptr;
    if (cJSON_IsTrue(download)) {
        accepted = online_music_download_current();
        error = "没有可保存的当前在线歌曲";
    } else if (cJSON_IsNumber(local) && local->valuedouble == local->valueint &&
        local->valueint >= 0 && local->valueint < get_music_track_count() &&
        sdcar_music_track_valid((uint8_t)local->valueint)) {
        music_select_track((uint8_t)local->valueint);
        music_audio_play();
        accepted = true;
    } else if (cJSON_IsString(command) && command->valuestring && strlen(command->valuestring) < 256) {
        const char *text = command->valuestring;
        accepted = online_music_handle_web_command(text, false);
        if (!accepted && !strcmp(text, "停止")) { music_audio_stop(); accepted = true; }
        else if (!accepted && !strcmp(text, "暂停")) { music_audio_pause(); accepted = true; }
        else if (!accepted && !strcmp(text, "继续播放")) { music_audio_play(); accepted = true; }
    }
    cJSON_Delete(root);
    return answer(req, accepted, error);
}
esp_err_t volume(httpd_req_t *req)
{
    cJSON *root = read_json(req);
    cJSON *value = root ? cJSON_GetObjectItem(root, "volume") : nullptr;
    bool valid = cJSON_IsNumber(value) && value->valuedouble >= 0 && value->valuedouble <= 100;
    if (valid) { set_audio_volume(value->valueint); save_audio_volume(); }
    cJSON_Delete(root);
    return answer(req, valid, "音量需要在 0 到 100 之间");
}
int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
void upload_title(httpd_req_t *req, char *title, size_t size)
{
    char encoded[512];
    strlcpy(title, "手机歌曲.mp3", size);
    if (httpd_req_get_hdr_value_len(req, "X-Filename") >= sizeof(encoded) ||
        httpd_req_get_hdr_value_str(req, "X-Filename", encoded, sizeof(encoded)) != ESP_OK) return;
    size_t n = 0;
    for (size_t i = 0; encoded[i] && n + 1 < size; ++i) {
        unsigned char ch = encoded[i];
        if (ch == '%' && encoded[i + 1] && encoded[i + 2] && hex_value(encoded[i + 1]) >= 0 && hex_value(encoded[i + 2]) >= 0) {
            ch = static_cast<unsigned char>((hex_value(encoded[i + 1]) << 4) | hex_value(encoded[i + 2]));
            i += 2;
        }
        if (ch >= 32 && ch != 127 && ch != '/' && ch != '\\') title[n++] = ch;
    }
    title[n] = 0;
    if (!n) strlcpy(title, "手机歌曲.mp3", size);
}
esp_err_t upload(httpd_req_t *req)
{
    if (!sdcar_is_ready()) return answer(req, false, "请先插入 SD 卡");
    if (req->content_len < 128 || req->content_len > MAX_UPLOAD) return answer(req, false, "请选择 20 MiB 以内的 MP3 文件");
    char type[64];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strcmp(type, "application/octet-stream")) return answer(req, false, "需要上传原始 MP3 文件");
    if (storage_free_bytes() < req->content_len + 1024u * 1024u || !storage_mkdir("/.online")) return answer(req, false, "SD 卡空间不足或无法创建缓存目录");
    const unsigned slot = music_audio_online_uses_path(upload_paths[0]) ? 1 : 0;
    if (music_audio_online_uses_path(upload_paths[slot])) return answer(req, false, "缓存正在播放，请稍后重试");
    uint8_t *buffer = static_cast<uint8_t *>(heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer) return answer(req, false, "外部内存不足");
    StorageStream file;
    if (!file.open("/.online/phone.part", "wb")) { free(buffer); return answer(req, false, "SD 卡写入失败"); }
    const uint32_t job = online_music_begin_upload();
    const uint32_t began = app_millis();
    size_t used = 0;
    bool success = true;
    uint8_t prefix[3] = {};
    while (used < req->content_len && online_music_job_current(job) && wifi_hotspot_get_enabled() && app_millis() - began < 180000) {
        const int got = httpd_req_recv(req, reinterpret_cast<char *>(buffer), std::min<size_t>(4096, req->content_len - used));
        if (got <= 0) { success = false; break; }
        if (used < sizeof(prefix)) memcpy(prefix + used, buffer, std::min<size_t>(got, sizeof(prefix) - used));
        if (file.write(buffer, got) != static_cast<size_t>(got)) { success = false; break; }
        used += got;
        app_delay_ms(1);
    }
    const bool valid_header = !memcmp(prefix, "ID3", 3) ||
        (prefix[0] == 0xff && (prefix[1] & 0xe0) == 0xe0);
    success = success && valid_header && used == req->content_len && online_music_job_current(job);
    if (success) success = file.flush();
    file.close();
    free(buffer);
    if (success && storage_exists(upload_paths[slot])) success = storage_remove(upload_paths[slot]);
    if (success) success = storage_rename("/.online/phone.part", upload_paths[slot]);
    if (!success) {
        storage_remove("/.online/phone.part");
        online_music_abort_upload(job, "手机歌曲上传未完成或已取消，请重试");
        return answer(req, false, "上传未完成、已取消或不是有效 MP3，请重试");
    }
    char title[160];
    upload_title(req, title, sizeof(title));
    success = online_music_finish_upload(job, upload_paths[slot], title);
    return answer(req, success, "歌曲已接收，但播放已取消或音频输出正忙");
}
const char *json_string(cJSON*root,const char*name){cJSON*v=root?cJSON_GetObjectItem(root,name):nullptr;return cJSON_IsString(v)?v->valuestring:nullptr;}
esp_err_t file_begin(httpd_req_t*req){
 cJSON*root=read_json(req);cJSON*size=root?cJSON_GetObjectItem(root,"size"):nullptr;
 char token[24]={},error[192]={};
 bool ok=cJSON_IsNumber(size)&&size->valuedouble>0&&size->valuedouble<=268435456&&size->valuedouble==(uint32_t)size->valuedouble&&
  file_transfer_begin(json_string(root,"kind"),json_string(root,"name"),(uint32_t)size->valuedouble,token,sizeof(token),error,sizeof(error));
 cJSON_Delete(root);if(!ok)return answer(req,false,error[0]?error:"文件信息无效");
 cJSON*reply=cJSON_CreateObject();cJSON_AddBoolToObject(reply,"ok",true);cJSON_AddStringToObject(reply,"token",token);cJSON_AddNumberToObject(reply,"chunk_size",65536);return json_reply(req,reply);
}
esp_err_t file_chunk(httpd_req_t*req){
 char token[24]={},offset[16]={},error[192]={},*end=nullptr;
 bool ok=req->content_len&&req->content_len<=65536&&httpd_req_get_hdr_value_str(req,"X-Upload-Token",token,sizeof(token))==ESP_OK&&
  httpd_req_get_hdr_value_str(req,"X-Offset",offset,sizeof(offset))==ESP_OK;
 errno=0;unsigned long position=strtoul(offset,&end,10);
 ok=ok&&offset[0]>='0'&&offset[0]<='9'&&end&&!*end&&!errno&&position<=UINT32_MAX;
 auto*buffer=ok?static_cast<uint8_t*>(heap_caps_malloc(4096,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)):nullptr;
 if(!buffer)ok=false;
 size_t used=0;
 while(ok&&used<req->content_len){
  int got=httpd_req_recv(req,(char*)buffer,std::min<size_t>(4096,req->content_len-used));
  if(got<=0){ok=false;break;}
  ok=file_transfer_append(token,(uint32_t)position+used,buffer,got,error,sizeof(error));used+=got;app_delay_ms(1);
 }
 free(buffer);if(!ok)file_transfer_cancel(token);
 return answer(req,ok,error[0]?error:"上传中断、顺序错误或内存不足，请重新上传");
}
esp_err_t file_finish(httpd_req_t*req){
 cJSON*root=read_json(req);char path[96]={},error[192]={};
 bool ok=file_transfer_finish(json_string(root,"token"),path,sizeof(path),error,sizeof(error));cJSON_Delete(root);
 if(!ok)return answer(req,false,error[0]?error:"文件提交失败");
 cJSON*reply=cJSON_CreateObject();cJSON_AddBoolToObject(reply,"ok",true);cJSON_AddStringToObject(reply,"path",path);return json_reply(req,reply);
}
esp_err_t file_cancel(httpd_req_t*req){cJSON*root=read_json(req);file_transfer_cancel(json_string(root,"token"));cJSON_Delete(root);return answer(req,true,"");}
esp_err_t update_status(httpd_req_t*req){
 system_update_status_t state;system_update_get_status(&state);cJSON*root=cJSON_CreateObject();
 cJSON_AddBoolToObject(root,"ok",true);cJSON_AddNumberToObject(root,"state",state.state);cJSON_AddNumberToObject(root,"progress",state.progress);
 cJSON_AddBoolToObject(root,"busy",state.busy);cJSON_AddBoolToObject(root,"available",state.available);
 cJSON_AddStringToObject(root,"current_version",state.current_version);cJSON_AddStringToObject(root,"candidate_version",state.candidate_version);cJSON_AddStringToObject(root,"message",state.message);return json_reply(req,root);
}
esp_err_t update_check(httpd_req_t*req){if(!system_update_check())return answer(req,false,"正在检查或安装，请稍后重试");return update_status(req);}
esp_err_t update_install(httpd_req_t*req){return answer(req,system_update_install(),"请先检查到可用的新固件，再安装");}
esp_err_t accept_hotspot_only(httpd_handle_t, int fd)
{
    sockaddr_storage local = {};
    socklen_t size = sizeof(local);
    char ip[16];
    wifi_hotspot_copy_info(nullptr, 0, nullptr, 0, ip, sizeof(ip), nullptr, nullptr);
    if (getsockname(fd, reinterpret_cast<sockaddr *>(&local), &size) != 0 || !wifi_hotspot_get_enabled()) return ESP_FAIL;
    uint32_t address = 0;
    if (local.ss_family == AF_INET) address = reinterpret_cast<sockaddr_in *>(&local)->sin_addr.s_addr;
#if CONFIG_LWIP_IPV6
    else if (local.ss_family == AF_INET6) {
        const uint8_t *bytes = reinterpret_cast<sockaddr_in6 *>(&local)->sin6_addr.s6_addr;
        const uint8_t mapped[12] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff};
        if (memcmp(bytes, mapped, sizeof(mapped))) return ESP_FAIL;
        memcpy(&address, bytes + 12, sizeof(address));
    }
#endif
    return address && address == inet_addr(ip) ? ESP_OK : ESP_FAIL;
}
}

extern "C" bool music_web_is_running(void) { return running.load(); }
extern "C" void music_web_service_poll(void)
{
    service_config_web_poll();
    const bool enabled = wifi_hotspot_get_enabled();
    if (!enabled && server) {
        if (httpd_stop(server) == ESP_OK) { server = nullptr; running.store(false); file_transfer_reset(); }
        return;
    }
    if (!enabled || server || app_millis() - last_retry < 3000) return;
    last_retry = app_millis();
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.core_id = 0;
    cfg.max_open_sockets = 3;
    cfg.max_uri_handlers = 17;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = cfg.send_wait_timeout = 2;
    cfg.open_fn = accept_hotspot_only;
    if (httpd_start(&server, &cfg) != ESP_OK) { server = nullptr; return; }
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = page, .user_ctx = nullptr },
        { .uri = "/api/status", .method = HTTP_GET, .handler = status, .user_ctx = nullptr },
        { .uri = "/api/music", .method = HTTP_POST, .handler = music_command, .user_ctx = nullptr },
        { .uri = "/api/volume", .method = HTTP_POST, .handler = volume, .user_ctx = nullptr },
        { .uri = "/api/upload", .method = HTTP_POST, .handler = upload, .user_ctx = nullptr },
        { .uri = "/api/files/begin", .method = HTTP_POST, .handler = file_begin, .user_ctx = nullptr },
        { .uri = "/api/files/chunk", .method = HTTP_POST, .handler = file_chunk, .user_ctx = nullptr },
        { .uri = "/api/files/finish", .method = HTTP_POST, .handler = file_finish, .user_ctx = nullptr },
        { .uri = "/api/files/cancel", .method = HTTP_POST, .handler = file_cancel, .user_ctx = nullptr },
        { .uri = "/api/update/status", .method = HTTP_GET, .handler = update_status, .user_ctx = nullptr },
        { .uri = "/api/update/check", .method = HTTP_POST, .handler = update_check, .user_ctx = nullptr },
        { .uri = "/api/update/install", .method = HTTP_POST, .handler = update_install, .user_ctx = nullptr }
    };
    for (const auto &route : routes) {
        if (httpd_register_uri_handler(server, &route) != ESP_OK) {
            httpd_stop(server); server = nullptr; return;
        }
    }
    if (!service_config_web_register(server)) { httpd_stop(server); server = nullptr; return; }
    running.store(true);
    puts("[MUSIC WEB] hotspot browser player ready");
}
