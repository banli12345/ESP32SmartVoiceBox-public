"""Run actual Sound online-source queue and frame functions against fault fakes."""
from pathlib import Path
import subprocess
import tempfile
from audio_handoff_host_test import function

ROOT = Path(__file__).resolve().parents[1]
FAKES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "Mp3Source.h"
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_TIMEOUT 2
#define ESP_ERR_INVALID_STATE 3
#define pdMS_TO_TICKS(n) (n)
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
typedef int esp_err_t;
static int music_control_mux, music_i2s_tx=1, music_i2s_gpio_config, music_audio_task_handle=1;
static bool music_i2s_enabled=true, driver_enabled=true, ai_i2s_held, video_session;
static bool music_audio_ready=true, music_audio_playing, music_audio_stop_requested, music_audio_pause_requested, music_play_requested;
static bool music_resume_after_video, music_resume_after_ai_audio;
static bool online_mode,online_request_pending,online_cancel_requested,online_pending_play;
static uint32_t online_job,online_request_job,music_elapsed_ms,output_sample_rate=44100,driver_rate=44100;
static int16_t music_track_request=-1;
static uint32_t music_track_request_generation; static int music_delete_state;
static uint8_t music_audio_buffer[8192];
static char online_path[96],online_title[256],online_request_path[96],online_request_title[256];
static mp3_source_t *online_decoder;
static unsigned closes,local_closes,notifications,writes,error_job;
static bool fail_open,fail_write,fail_enable,noted_pause;
static int frame_result=1;
static uint32_t frame_rate=48000;
static uint8_t frame_channels=1;
static int16_t pcm[2304];
static size_t write_limit=100000;
static void online_music_note_pause(bool paused) {noted_pause=paused;}
static void online_music_cancel_pending(void) {}
static void online_music_playback_error(uint32_t job,const char *message) {(void)message;error_job=job;}
static size_t strlcpy(char *out,const char *in,size_t n) {size_t k=strlen(in);if(n){size_t c=k<n?k:n-1;memcpy(out,in,c);out[c]=0;}return k;}
static uint8_t sdcar_get_music_track_count(void) {return 2;}
static bool sdcar_copy_music_track(uint8_t i,char*p,size_t n,uint32_t*g){(void)p;(void)n;if(g)*g=1;return i<2;}
static void sdcar_close_music(void) {++local_closes;}
static void xTaskNotifyGive(int task) {(void)task;++notifications;}
static const char *esp_err_to_name(int error) {(void)error;return "fake";}
typedef struct {uint32_t sample_rate;} i2s_std_clk_config_t;
#define I2S_STD_CLK_DEFAULT_CONFIG(n) ((i2s_std_clk_config_t){n})
static int i2s_channel_disable(int handle) {(void)handle;driver_enabled=false;return 0;}
static int i2s_channel_enable(int handle) {(void)handle;if(fail_enable)return ESP_FAIL;driver_enabled=true;return 0;}
static int i2s_channel_reconfig_std_clock(int handle,const i2s_std_clk_config_t *clock) {(void)handle;assert(!driver_enabled);driver_rate=clock->sample_rate;return 0;}
static int i2s_channel_reconfig_std_gpio(int handle,const int *cfg) {(void)handle;(void)cfg;return 0;}
static void apply_audio_volume(uint8_t *data,size_t count) {(void)data;(void)count;}
static int i2s_channel_write(int handle,const void *data,size_t count,size_t *written,int timeout) {
 (void)handle;(void)data;(void)timeout;assert(driver_enabled);++writes;
 *written=fail_write?0:(count<write_limit?count:write_limit);return fail_write?ESP_FAIL:0;
}
static void mute_music_i2s(void) {driver_enabled=music_i2s_enabled=false;}
mp3_source_t *mp3_source_open(const char *path) {assert(path&&path[0]);return fail_open?NULL:(mp3_source_t*)1;}
void mp3_source_close(mp3_source_t *p) {if(p)++closes;}
int mp3_source_read_frame(mp3_source_t *p,mp3_source_frame_t *frame) {
 assert(p);frame->pcm=pcm;frame->frames=1152;frame->sample_rate=frame_rate;frame->channels=frame_channels;return frame_result;
}
'''
CASES = r'''
int main(void) {
 pcm[0]=1234;pcm[1]=-2345;
 assert(music_audio_queue_online("/.online/song0.mp3","first",7,false));
 assert(online_request_pending&&music_audio_online_uses_path("/.online/song0.mp3"));
 char active[96];assert(!music_audio_copy_active_online_path(active,sizeof(active)));
 service_online_requests();assert(online_mode&&!online_request_pending&&online_job==7&&local_closes==1);
 assert(music_audio_copy_active_online_path(active,sizeof(active))&&!strcmp(active,"/.online/song0.mp3"));
 assert(service_online_frame());assert(writes==1&&driver_rate==48000&&music_elapsed_ms==24);
 assert(((int16_t*)music_audio_buffer)[0]==1234&&((int16_t*)music_audio_buffer)[1]==1234);
 assert(((int16_t*)music_audio_buffer)[2]==-2345&&((int16_t*)music_audio_buffer)[3]==-2345);
 music_audio_pause();assert(noted_pause&&!music_audio_playing&&online_decoder);
 service_online_requests();assert(online_decoder&&closes==0); /* Resume keeps decoder cursor. */
 assert(music_audio_queue_online("/.online/song1.mp3","paused replacement",8,true));
 assert(music_audio_copy_active_online_path(active,sizeof(active))&&!strcmp(active,"/.online/song0.mp3"));
 service_online_requests();assert(!music_audio_playing&&online_mode&&!driver_enabled&&closes==1);
 assert(music_audio_copy_active_online_path(active,sizeof(active))&&!strcmp(active,"/.online/song1.mp3"));
 assert(!strcmp(online_path,"/.online/song1.mp3"));
 music_audio_stop();service_online_requests();assert(!online_mode&&!online_request_pending);
 /* Stop arriving after publish but before the owner applies it cancels the open. */
 assert(music_audio_queue_online("/.online/song0.mp3","cancel",9,false));
 music_audio_stop();service_online_requests();assert(!online_mode&&!online_request_pending&&!online_decoder);
 /* A selected new song waits through an AI/video owner, including idle old music. */
 ai_i2s_held=true;music_audio_playing=false;
 assert(music_audio_queue_online("/.online/song0.mp3","wait AI",10,false));
 service_online_requests();assert(online_request_pending&&!online_mode&&!music_audio_playing);
 ai_i2s_held=false;service_online_requests();assert(online_mode&&!online_request_pending&&music_audio_playing);
 frame_channels=2;write_limit=96;
 assert(service_online_frame());assert(writes>2);
 /* Reject invalid output without touching another owner's I2S. */
 ai_i2s_held=true;assert(!service_online_frame()&&error_job==10);ai_i2s_held=false;
 frame_rate=96000;assert(!service_online_frame());frame_rate=44100;
 fail_write=true;assert(!service_online_frame());fail_write=false;
 close_online_decoder();fail_open=true;assert(!service_online_frame());fail_open=false;
 music_audio_stop();service_online_requests();
 assert(music_audio_queue_online("/.online/song1.mp3","wait video",11,false));
 video_session=true;service_online_requests();assert(online_request_pending);
 video_session=false;service_online_requests();assert(online_mode&&online_job==11);
 music_select_track(1);service_online_requests();assert(!online_mode&&music_track_request==1);
 assert(music_audio_queue_online("/.online/song1.mp3","discard pending",12,false));
 music_audio_discard_online_queue();assert(!online_request_pending);
 puts("online audio: PCM/rate/partial writes, pause, stop, AI/video ownership and replacement passed");
 return 0;
}
'''


def main():
    sound = (ROOT / "src/Sound.cpp").read_text(encoding="utf-8")
    names = ["music_audio_is_online", "music_audio_online_uses_path", "music_audio_copy_active_online_path", "music_audio_discard_online_queue",
             "music_audio_queue_online", "disable_music_i2s_for_ai", "route_music_i2s_gpio",
             "set_output_sample_rate", "enable_online_i2s", "close_online_decoder", "service_online_requests",
             "service_online_frame", "music_audio_pause", "music_audio_stop", "music_select_library_track", "music_select_track"]
    bodies = [function(sound, name) for name in names]
    declarations = [body[:body.index("{")].rstrip() + ";" for body in bodies]
    with tempfile.TemporaryDirectory(prefix="online-audio-") as tmp:
        folder = Path(tmp)
        unit = folder / "test.c"
        unit.write_text(FAKES + "\n".join(declarations + bodies) + CASES, encoding="utf-8")
        exe = folder / "test.exe"
        subprocess.run([str(ROOT / ".tools/host-tcc/tcc/tcc.exe"), f"-I{ROOT / 'src'}", str(unit), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
