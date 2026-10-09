/* Included only from Sound.cpp. Runs on MusicAudioTask except the WAV prefetch. */

static bool IRAM_ATTR video_i2s_on_sent(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user)
{
    (void)handle;
    (void)user;
    if (!event || event->size == 0) return false;
    portENTER_CRITICAL_ISR(&video_i2s_mux);
    if (video_i2s_count) {
        uint32_t size = (uint32_t)event->size;
        if (video_i2s_skip_bytes) {
            const uint32_t skip = size < video_i2s_skip_bytes ? size : video_i2s_skip_bytes;
            video_i2s_skip_bytes -= skip;
            size -= skip;
        }
        video_i2s_sent_bytes += size;
    }
    portEXIT_CRITICAL_ISR(&video_i2s_mux);
    return false;
}

static void mute_music_i2s(void)
{
    if (!music_i2s_tx || !music_i2s_enabled) return;
    memset(music_audio_buffer, 0, sizeof(music_audio_buffer));
    size_t silence_written = 0;
    const esp_err_t silence_error = i2s_channel_write(
        music_i2s_tx, music_audio_buffer, sizeof(music_audio_buffer),
        &silence_written, pdMS_TO_TICKS(100));
    if (silence_error != ESP_OK) {
        Serial.printf("Music I2S mute write failed: %s\n", esp_err_to_name(silence_error));
    }
    const esp_err_t disable_error = i2s_channel_disable(music_i2s_tx);
    if (disable_error != ESP_OK && disable_error != ESP_ERR_INVALID_STATE) {
        Serial.printf("Music I2S disable failed: %s\n", esp_err_to_name(disable_error));
    }
    music_i2s_enabled = false;
}

static size_t ring_fill_now(void)
{
    portENTER_CRITICAL(&video_ring_mux);
    const size_t fill = video_ring_fill;
    portEXIT_CRITICAL(&video_ring_mux);
    return fill;
}

static void ring_clear(void)
{
    portENTER_CRITICAL(&video_ring_mux);
    video_ring_read = 0;
    video_ring_fill = 0;
    portEXIT_CRITICAL(&video_ring_mux);
}

static size_t ring_push(const uint8_t *src, size_t len)
{
    if (!video_ring || !src || !len) return 0;
    portENTER_CRITICAL(&video_ring_mux);
    const size_t space = VIDEO_RING_BYTES - video_ring_fill;
    if (len > space) len = space;
    size_t at = (video_ring_read + video_ring_fill) % VIDEO_RING_BYTES;
    const size_t first = len < (VIDEO_RING_BYTES - at) ? len : (VIDEO_RING_BYTES - at);
    memcpy(video_ring + at, src, first);
    if (len > first) memcpy(video_ring, src + first, len - first);
    video_ring_fill += len;
    portEXIT_CRITICAL(&video_ring_mux);
    return len;
}

static size_t ring_pop(uint8_t *dst, size_t len)
{
    if (!video_ring || !dst || !len) return 0;
    portENTER_CRITICAL(&video_ring_mux);
    if (len > video_ring_fill) len = video_ring_fill;
    len &= ~3u;
    const size_t first = len < (VIDEO_RING_BYTES - video_ring_read) ? len
                                                                    : (VIDEO_RING_BYTES - video_ring_read);
    memcpy(dst, video_ring + video_ring_read, first);
    if (len > first) memcpy(dst + first, video_ring, len - first);
    video_ring_read = (video_ring_read + len) % VIDEO_RING_BYTES;
    video_ring_fill -= len;
    portEXIT_CRITICAL(&video_ring_mux);
    return len;
}

static void ring_unpop(const uint8_t *src, size_t len)
{
    if (!video_ring || !src || !len) return;
    portENTER_CRITICAL(&video_ring_mux);
    if (video_ring_fill + len > VIDEO_RING_BYTES) len = VIDEO_RING_BYTES - video_ring_fill;
    video_ring_read = (video_ring_read + VIDEO_RING_BYTES - len) % VIDEO_RING_BYTES;
    size_t left = len;
    size_t at = video_ring_read;
    const uint8_t *from = src;
    while (left) {
        size_t n = VIDEO_RING_BYTES - at;
        if (n > left) n = left;
        memcpy(video_ring + at, from, n);
        from += n;
        left -= n;
        at = 0;
    }
    video_ring_fill += len;
    portEXIT_CRITICAL(&video_ring_mux);
}

// Keep queued bytes reserved until I2S actually accepts them. The producer can
// fill spare space during a partial write without overwriting the unwritten PCM.
static size_t ring_peek(uint8_t *dst, size_t len)
{
    portENTER_CRITICAL(&video_ring_mux);
    if (len > video_ring_fill) len = video_ring_fill;
    len &= ~3u;
    const size_t first = len < VIDEO_RING_BYTES - video_ring_read
                             ? len : VIDEO_RING_BYTES - video_ring_read;
    memcpy(dst, video_ring + video_ring_read, first);
    if (len > first) memcpy(dst + first, video_ring, len - first);
    portEXIT_CRITICAL(&video_ring_mux);
    return len;
}

static void ring_consume(size_t len)
{
    portENTER_CRITICAL(&video_ring_mux);
    video_ring_read = (video_ring_read + len) % VIDEO_RING_BYTES;
    video_ring_fill -= len;
    portEXIT_CRITICAL(&video_ring_mux);
}

static int wav_file_read(void *ctx, void *dst, uint32_t len)
{
    return video_wav_reader_read(ctx, dst, len);
}

static int wav_file_seek(void *ctx, uint32_t offset)
{
    return video_wav_reader_seek(ctx, offset);
}

static bool read_video_stage(size_t want, bool cancelable)
{
    // The WAV offset/remaining count identifies this owner's stream cursor.
    // Recovery must resume after any bytes already read into this stage.
    const uint32_t start = video_data_offset + video_data_bytes - video_data_remaining;
    size_t done = 0;
    unsigned retries = 0;
    while (done < want) {
        if (cancelable && video_wav_stop) return false;
        const int got = wav_file_read(&video_wav_file, video_stage + done,
                                      static_cast<uint32_t>(want - done));
        if (got > 0) done += static_cast<size_t>(got);
        if (done == want) return true;
        if (retries >= 3 || (cancelable && video_wav_stop)) break;
        ++retries;
        // Retain the partial PCM and retry at the next unread byte.
        if (video_wav_reader_seek(&video_wav_file, start + done) != 0) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (!(cancelable && video_wav_stop))
        Serial.printf("[VIDEO AUDIO] prefetch-read wav=%s offset=%u want=%u got=%u retries=%u\n",
                      video_wav_path, static_cast<unsigned>(start),
                      static_cast<unsigned>(want), static_cast<unsigned>(done), retries);
    return false;
}

static bool refill_video_ring(size_t target_fill, bool allow_wrap)
{
    while (ring_fill_now() < target_fill) {
        if (video_data_remaining == 0) {
            if (!allow_wrap) return ring_fill_now() > 0;
            if (video_wav_reader_seek(&video_wav_file, video_data_offset) != 0) return false;
            video_data_remaining = video_data_bytes;
        }
        const size_t space = VIDEO_RING_BYTES - ring_fill_now();
        if (space < 4) return true;
        size_t want = space < VIDEO_STAGE_BYTES ? space : VIDEO_STAGE_BYTES;
        want &= ~3u;
        if (want > video_data_remaining) want = video_data_remaining & ~3u;
        if (want == 0) return false;
        if (!read_video_stage(want, false)) return false;
        if (ring_push(video_stage, want) != want) return false;
        video_data_remaining -= (uint32_t)want;
    }
    return true;
}

static void video_wav_prefetch_task(void *)
{
    while (!video_wav_stop) {
        if (video_data_remaining == 0) {
            if (video_wav_reader_seek(&video_wav_file, video_data_offset) != 0) {
                Serial.printf("[VIDEO AUDIO] prefetch-seek wav=%s offset=%u\n",
                              video_wav_path, static_cast<unsigned>(video_data_offset));
                video_wav_error = true;
                break;
            }
            video_data_remaining = video_data_bytes;
            continue;
        }
        if (VIDEO_RING_BYTES - ring_fill_now() < VIDEO_STAGE_BYTES) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(8));
            continue;
        }
        size_t want = VIDEO_STAGE_BYTES;
        if (want > video_data_remaining) want = video_data_remaining & ~3u;
        if (want == 0 || video_wav_stop) break;
        if (!read_video_stage(want, true)) {
            if (!video_wav_stop) video_wav_error = true;
            break;
        }
        if (ring_push(video_stage, want) != want) {
            Serial.printf("[VIDEO AUDIO] prefetch-ring wav=%s want=%u fill=%u\n",
                          video_wav_path, static_cast<unsigned>(want),
                          static_cast<unsigned>(ring_fill_now()));
            video_wav_error = true;
            break;
        }
        video_data_remaining -= (uint32_t)want;
        if (ring_fill_now() > 32 * 1024) vTaskDelay(pdMS_TO_TICKS(1));
    }
    // Keep the task handle alive after a read error until the owner requests
    // stop. The owner must never notify a task that already deleted itself.
    while (!video_wav_stop)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    xSemaphoreGive(video_wav_done);
    vTaskDelete(nullptr);
}

static void stop_wav_prefetch(void)
{
    if (!video_wav_task) return;
    video_wav_stop = true;
    xTaskNotifyGive(video_wav_task);
    xSemaphoreTake(video_wav_done, portMAX_DELAY);
    video_wav_task = nullptr;
}

static bool start_wav_prefetch(void)
{
    if (video_wav_task) return true;
    video_wav_stop = false;
    video_wav_error = false;
    if (!video_wav_done) video_wav_done = xSemaphoreCreateBinary();
    if (!video_wav_done) return false;
    xSemaphoreTake(video_wav_done, 0);
    if (xTaskCreatePinnedToCore(video_wav_prefetch_task, "VideoWav", 4096, nullptr, 1,
                                &video_wav_task, 0) != pdPASS) {
        video_wav_task = nullptr;
        return false;
    }
    return true;
}

static bool ensure_video_ring(void)
{
    if (video_ring) return true;
    video_ring = static_cast<uint8_t *>(
        heap_caps_malloc(VIDEO_RING_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!video_ring) {
        Serial.println("[VIDEO AUDIO] error alloc");
        return false;
    }
    ring_clear();
    return true;
}

static void free_video_ring(void)
{
    if (!video_ring) return;
    heap_caps_free(video_ring);
    video_ring = nullptr;
    video_ring_read = 0;
    video_ring_fill = 0;
}

static uint64_t heard_pcm_bytes(void)
{
    portENTER_CRITICAL(&video_i2s_mux);
    const uint64_t sent = video_i2s_sent_bytes;
    const uint64_t accepted = video_pcm_accepted;
    portEXIT_CRITICAL(&video_i2s_mux);
    return sent < accepted ? sent : accepted;
}

static void accept_pcm(uint32_t bytes)
{
    portENTER_CRITICAL(&video_i2s_mux);
    video_pcm_accepted += bytes;
    portEXIT_CRITICAL(&video_i2s_mux);
}

static void reset_video_clock(void)
{
    portENTER_CRITICAL(&video_i2s_mux);
    video_i2s_sent_bytes = 0;
    video_i2s_skip_bytes = 0;
    video_pcm_accepted = 0;
    video_i2s_count = false;
    portEXIT_CRITICAL(&video_i2s_mux);
    video_underruns = 0;
    video_logged_cycle = 0;
}

static bool seek_wav_to_heard(void)
{
    const uint64_t heard = heard_pcm_bytes() & ~3ull;
    const uint32_t into = video_duration_samples
                              ? (uint32_t)((heard / 4u) % video_duration_samples) * 4u
                              : 0;
    if (video_wav_reader_seek(&video_wav_file, video_data_offset + into) != 0) return false;
    video_data_remaining = video_data_bytes - into;
    ring_clear();
    portENTER_CRITICAL(&video_i2s_mux);
    video_pcm_accepted = heard;
    video_i2s_skip_bytes = 0;
    portEXIT_CRITICAL(&video_i2s_mux);
    return true;
}

static bool preload_video_dma(void)
{
    size_t loaded_total = 0;
    const size_t limit = music_dma_bytes ? music_dma_bytes : 5760u;
    while (loaded_total < limit) {
        const size_t chunk = ring_pop(music_audio_buffer, 512);
        if (chunk == 0) break;
        memcpy(music_audio_buffer + 1024, music_audio_buffer, chunk);
        apply_audio_volume(music_audio_buffer + 1024, chunk);
        size_t loaded = 0;
        const esp_err_t err = i2s_channel_preload_data(
            music_i2s_tx, music_audio_buffer + 1024, chunk, &loaded);
        const size_t keep = loaded & ~3u;
        if (keep < chunk) ring_unpop(music_audio_buffer + keep, chunk - keep);
        if (keep) accept_pcm((uint32_t)keep);
        loaded_total += keep;
        if (err != ESP_OK || keep < chunk) break;
    }
    return loaded_total > 0;
}

static void stop_video_playback(bool log_end)
{
    const bool was_running = video_running;
    portENTER_CRITICAL(&video_i2s_mux);
    video_i2s_count = false;
    portEXIT_CRITICAL(&video_i2s_mux);
    stop_wav_prefetch();
    if (!ai_i2s_held) mute_music_i2s();
    video_wav_reader_close(&video_wav_file);
    video_running = false;
    video_prepared = false;
    video_need_rearm = false;
    video_wav_error = false;
    video_sync_cancel();
    if (log_end && was_running) Serial.println("[VIDEO AUDIO] end stop");
}

static void take_output_for_video(void)
{
    portENTER_CRITICAL(&music_control_mux);
    if (!video_session) {
        const bool playing = music_audio_playing && !music_audio_stop_requested &&
                             !music_audio_pause_requested;
        music_resume_after_video = playing;
        video_session = true;
    }
    music_audio_playing = false;
    music_play_requested = false;
    portEXIT_CRITICAL(&music_control_mux);
    if (music_audio_stop_requested) {
        music_resume_after_video = false;
        if (sdcar_music_file_is_open()) sdcar_close_music();
        music_audio_stop_requested = false;
    }
    if (!ai_i2s_held) mute_music_i2s();
}

static bool open_companion_wav(const char *rvf_path, uint32_t frames, uint32_t fps_milli)
{
    char path[96];
    uint32_t expect = 0;
    if (!wav_pcm_companion_path(rvf_path, path, sizeof(path))) {
        Serial.println("[VIDEO AUDIO] error path");
        return false;
    }
    if (!wav_pcm_video_sample_frames(frames, fps_milli, &expect)) {
        Serial.println("[VIDEO AUDIO] error duration");
        return false;
    }
    // SD_MMC's logical paths are mounted under /root in sdcar_init(). Read the
    // companion through VFS directly, without File's buffered stdio stream.
    char mounted_path[sizeof(path) + sizeof("/root")];
    snprintf(mounted_path, sizeof(mounted_path), "/root%s", path);
    if (video_wav_reader_open(&video_wav_file, mounted_path) != 0) {
        Serial.printf("[VIDEO AUDIO] error no-companion wav=%s errno=%d\n", path, errno);
        return false;
    }
    const uint64_t bytes = video_wav_file.size;
    if (bytes < 44 || bytes > 0xffffffffull) {
        Serial.println("[VIDEO AUDIO] error range");
        video_wav_reader_close(&video_wav_file);
        return false;
    }
    wav_pcm_reader_t reader = {};
    reader.read = wav_file_read;
    reader.seek = wav_file_seek;
    reader.ctx = &video_wav_file;
    reader.size = (uint32_t)bytes;
    wav_pcm_info_t info = {};
    const wav_pcm_status_t status = wav_pcm_parse(&reader, &info);
    if (status != WAV_PCM_OK) {
        Serial.printf("[VIDEO AUDIO] error %s rvf=%s wav=%s bytes=%u\n",
                      wav_pcm_status_name(status), rvf_path, path,
                      static_cast<unsigned>(bytes));
        video_wav_reader_close(&video_wav_file);
        return false;
    }
    if (info.sample_frames != expect) {
        Serial.printf("[VIDEO AUDIO] error duration wav=%u video=%u\n",
                      static_cast<unsigned>(info.sample_frames),
                      static_cast<unsigned>(expect));
        video_wav_reader_close(&video_wav_file);
        return false;
    }
    if (video_wav_reader_seek(&video_wav_file, info.data_offset) != 0) {
        video_wav_reader_close(&video_wav_file);
        Serial.println("[VIDEO AUDIO] error seek");
        return false;
    }
    video_data_offset = info.data_offset;
    video_data_bytes = info.data_bytes;
    video_data_remaining = info.data_bytes;
    video_duration_samples = expect;
    video_fps_milli = fps_milli;
    strlcpy(video_wav_path, path, sizeof(video_wav_path));
    return true;
}

static void handle_video_prepare(void)
{
    take_output_for_video();
    if (ai_i2s_held) {
        Serial.println("[VIDEO AUDIO] error ai-busy");
        video_cmd_ok = false;
        return;
    }
    stop_video_playback(false);
    video_session = true;
    if (!video_i2s_cb_ok) {
        Serial.println("[VIDEO AUDIO] error callback");
        video_cmd_ok = false;
        return;
    }
    if (!open_companion_wav(video_cmd_path, video_cmd_frames, video_cmd_fps) ||
        !ensure_video_ring()) {
        video_wav_reader_close(&video_wav_file);
        video_cmd_ok = false;
        return;
    }
    ring_clear();
    const size_t target = video_data_bytes < (32u * 1024u) ? video_data_bytes : (32u * 1024u);
    if (!refill_video_ring(target, false) || ring_fill_now() < 4096) {
        Serial.println("[VIDEO AUDIO] error prefill");
        video_wav_reader_close(&video_wav_file);
        video_cmd_ok = false;
        return;
    }
    video_prepared = true;
    video_cmd_ok = true;
}

static void handle_video_start(void)
{
    if (!video_prepared || video_wav_file.fd < 0 || ai_i2s_held || !video_session) {
        video_cmd_ok = false;
        return;
    }
    reset_video_clock();
    if (!preload_video_dma() || !start_wav_prefetch()) {
        stop_wav_prefetch();
        Serial.println("[VIDEO AUDIO] error preload");
        video_cmd_ok = false;
        return;
    }
    portENTER_CRITICAL(&video_i2s_mux);
    video_i2s_count = true;
    portEXIT_CRITICAL(&video_i2s_mux);
    if (!enable_music_i2s_channel()) {
        portENTER_CRITICAL(&video_i2s_mux);
        video_i2s_count = false;
        portEXIT_CRITICAL(&video_i2s_mux);
        stop_wav_prefetch();
        video_cmd_ok = false;
        return;
    }
    video_running = true;
    video_cmd_ok = true;
    const uint32_t duration_ms =
        (uint32_t)((uint64_t)video_duration_samples * 1000ull / WAV_PCM_SAMPLE_RATE);
    Serial.printf("[VIDEO AUDIO] start %s samples=%u duration_ms=%u\n",
                  video_wav_path, static_cast<unsigned>(video_duration_samples),
                  static_cast<unsigned>(duration_ms));
}

static void queue_video_silence(size_t bytes)
{
    bytes &= ~3u;
    if (bytes == 0 || bytes > 1024) bytes = 960;
    memset(music_audio_buffer, 0, bytes);
    portENTER_CRITICAL(&video_i2s_mux);
    video_i2s_skip_bytes += (uint32_t)bytes;
    portEXIT_CRITICAL(&video_i2s_mux);
    size_t written = 0;
    i2s_channel_write(music_i2s_tx, music_audio_buffer, bytes, &written, 20);
    if (written < bytes) {
        portENTER_CRITICAL(&video_i2s_mux);
        // The ISR may already have consumed this reservation while write was
        // waiting. A short/failed write must not wrap the clock skip counter.
        const uint32_t unwritten = static_cast<uint32_t>(bytes - written);
        video_i2s_skip_bytes = video_i2s_skip_bytes >= unwritten
                                  ? video_i2s_skip_bytes - unwritten : 0;
        portEXIT_CRITICAL(&video_i2s_mux);
    }
}

static void note_video_loop(void)
{
    const uint64_t heard = heard_pcm_bytes() / 4u;
    const uint32_t cycle = video_duration_samples ? heard / video_duration_samples : 0;
    if (cycle != video_logged_cycle) {
        video_logged_cycle = cycle;
        if (cycle > 0)
            Serial.printf("[VIDEO AUDIO] loop cycle=%u\n", static_cast<unsigned>(cycle));
    }
}

static void fail_video_runtime(const char *reason)
{
    Serial.printf("[VIDEO AUDIO] error %s wav=%s ring=%u remaining=%u\n", reason,
                  video_wav_path, static_cast<unsigned>(ring_fill_now()),
                  static_cast<unsigned>(video_data_remaining));
    stop_video_playback(false);
}

static void service_video_audio(void)
{
    if (!video_running || ai_i2s_held || !music_i2s_enabled || !music_i2s_tx) return;
    if (video_wav_error) {
        fail_video_runtime("read");
        return;
    }
    const size_t chunk = ring_peek(music_audio_buffer, 1024);
    if (video_wav_task) xTaskNotifyGive(video_wav_task);
    if (chunk == 0) {
        uint32_t skip = 0;
        portENTER_CRITICAL(&video_i2s_mux);
        skip = video_i2s_skip_bytes;
        const uint64_t accepted = video_pcm_accepted;
        portEXIT_CRITICAL(&video_i2s_mux);
        if (skip == 0 && heard_pcm_bytes() + 2048 >= accepted) {
            ++video_underruns;
            queue_video_silence(960);
        }
        note_video_loop();
        vTaskDelay(pdMS_TO_TICKS(2));
        return;
    }
    memcpy(music_audio_buffer + 2048, music_audio_buffer, chunk);
    apply_audio_volume(music_audio_buffer + 2048, chunk);
    size_t written = 0;
    const esp_err_t err = i2s_channel_write(
        music_i2s_tx, music_audio_buffer + 2048, chunk, &written, 0);
    const size_t keep = written & ~3u;
    ring_consume(keep);
    if (keep) accept_pcm((uint32_t)keep);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT && keep == 0) {
        Serial.printf("[VIDEO AUDIO] error write %s\n", esp_err_to_name(err));
        fail_video_runtime("write");
        return;
    }
    note_video_loop();
    vTaskDelay(pdMS_TO_TICKS(2));
}

static bool rearm_video_output(void)
{
    if (video_wav_file.fd < 0 || !video_prepared) return false;
    if (!seek_wav_to_heard() || !refill_video_ring(16 * 1024, true) || !preload_video_dma())
        return false;
    portENTER_CRITICAL(&video_i2s_mux);
    video_i2s_count = true;
    portEXIT_CRITICAL(&video_i2s_mux);
    if (!enable_music_i2s_channel() || !start_wav_prefetch()) return false;
    video_need_rearm = false;
    return true;
}

static void suspend_video_output(void)
{
    portENTER_CRITICAL(&video_i2s_mux);
    video_i2s_count = false;
    portEXIT_CRITICAL(&video_i2s_mux);
    stop_wav_prefetch();
    mute_music_i2s();
    video_need_rearm = video_prepared && video_wav_file.fd >= 0;
}

static bool post_audio_cmd(uint8_t cmd, uint32_t timeout_ms)
{
    if (!music_audio_task_handle || !audio_cmd_lock || !audio_cmd_done) return false;
    (void)timeout_ms;
    if (xSemaphoreTake(audio_cmd_lock, portMAX_DELAY) != pdTRUE) return false;
    xSemaphoreTake(audio_cmd_done, 0);
    video_cmd_ok = false;
    video_cmd = cmd;
    xTaskNotifyGive(music_audio_task_handle);
    // Once published, wait for the owner to finish. A late acknowledgement
    // must never be mistaken for a later command or allow an early SD close.
    xSemaphoreTake(audio_cmd_done, portMAX_DELAY);
    const bool ok = video_cmd_ok;
    xSemaphoreGive(audio_cmd_lock);
    return ok;
}

static bool request_ai_release(void)
{
    if (!music_audio_task_handle || !audio_cmd_lock || !audio_cmd_done) return false;
    if (xSemaphoreTake(audio_cmd_lock, pdMS_TO_TICKS(1500)) != pdTRUE) return false;
    xSemaphoreTake(audio_cmd_done, 0);
    ai_release_request = true;
    xTaskNotifyGive(music_audio_task_handle);
    xSemaphoreTake(audio_cmd_done, portMAX_DELAY);
    const bool ok = ai_i2s_held;
    xSemaphoreGive(audio_cmd_lock);
    return ok;
}

static void handle_ai_release(void)
{
    ai_release_request = false;
    if (video_running) {
        portENTER_CRITICAL(&video_i2s_mux);
        video_i2s_count = false;
        portEXIT_CRITICAL(&video_i2s_mux);
        stop_wav_prefetch();
        mute_music_i2s();
        video_need_rearm = video_prepared && video_wav_file.fd >= 0;
    } else {
        portENTER_CRITICAL(&music_control_mux);
        if (music_audio_playing && !video_session) music_resume_after_ai_audio = true;
        music_audio_playing = false;
        portEXIT_CRITICAL(&music_control_mux);
        mute_music_i2s();
    }
    ai_i2s_held = true;
    video_cmd_ok = true;
}

static void consume_music_requests_during_video(void)
{
    if (!video_session) return;
    if (music_audio_stop_requested) {
        music_resume_after_video = false;
        if (sdcar_music_file_is_open()) sdcar_close_music();
        music_audio_stop_requested = false;
        music_audio_pause_requested = false;
        music_audio_playing = false;
        music_play_requested = false;
    } else if (music_audio_pause_requested) {
        music_resume_after_video = false;
        music_audio_pause_requested = false;
        music_audio_playing = false;
        music_play_requested = false;
    }
}

static bool audio_request_pending(void)
{
    return video_cmd != VIDEO_CMD_NONE || ai_release_request || alarm_audio_requested;
}

static void play_alarm_blocking(void)
{
    alarm_audio_requested = false;
    if (!enable_music_i2s_channel() || !open_alarm_wav()) {
        Serial.println("Alarm: playback finished");
        return;
    }
    alarm_audio_active = true;
    while (sdcar_alarm_file_is_open() && music_i2s_enabled && !audio_request_pending()) {
        const size_t count = sdcar_read_alarm(music_audio_buffer, sizeof(music_audio_buffer));
        if (count == 0) break;
        apply_audio_volume(music_audio_buffer, count);
        size_t written = 0;
        if (i2s_channel_write(music_i2s_tx, music_audio_buffer, count, &written,
                              pdMS_TO_TICKS(100)) != ESP_OK) {
            break;
        }
    }
    sdcar_close_alarm();
    alarm_audio_active = false;
    Serial.println("Alarm: playback finished");
}

static void handle_video_command(uint8_t cmd)
{
    if (cmd == VIDEO_CMD_PREPARE) {
        video_prepare_pending = false;
        video_start_pending = false;
        handle_video_prepare();
    } else if (cmd == VIDEO_CMD_START) {
        if (video_prepare_pending) {
            video_start_pending = true;
            video_cmd_ok = true;
        } else handle_video_start();
    } else if (cmd == VIDEO_CMD_HALT) {
        video_prepare_pending = false;
        video_start_pending = false;
        stop_video_playback(true);
        video_cmd_ok = true;
    } else if (cmd == VIDEO_CMD_RELEASE) {
        video_prepare_pending = false;
        video_start_pending = false;
        const bool resume_video_music = music_resume_after_video;
        stop_video_playback(true);
        portENTER_CRITICAL(&music_control_mux);
        video_session = false;
        music_resume_after_video = false;
        if (resume_video_music && !music_audio_stop_requested && !music_audio_pause_requested) {
            if (ai_i2s_held) music_resume_after_ai_audio = true;
            else {
                music_audio_playing = true;
                music_audio_stop_requested = false;
                music_audio_pause_requested = false;
            }
        }
        portEXIT_CRITICAL(&music_control_mux);
        free_video_ring();
        video_cmd_ok = true;
    }
}

static void defer_video_command_for_ai(uint8_t cmd)
{
    if (cmd == VIDEO_CMD_PREPARE) {
        stop_video_playback(false);
        portENTER_CRITICAL(&music_control_mux);
        if (music_resume_after_ai_audio) {
            music_resume_after_video = true;
            music_resume_after_ai_audio = false;
        }
        video_session = true;
        music_audio_playing = false;
        portEXIT_CRITICAL(&music_control_mux);
        video_prepare_pending = true;
        video_start_pending = false;
        video_cmd_ok = true;
        Serial.println("[VIDEO AUDIO] queued until AI releases output");
    } else {
        video_start_pending = video_prepare_pending;
        video_cmd_ok = video_start_pending;
    }
}

static void service_deferred_video_audio(void)
{
    if (ai_i2s_held || !video_prepare_pending) return;
    const bool start = video_start_pending;
    video_prepare_pending = false;
    video_start_pending = false;
    handle_video_prepare();
    if (video_cmd_ok && start) handle_video_start();
    if (!video_cmd_ok) stop_video_playback(false);
}

extern "C" bool video_audio_prepare(const char *rvf_path, uint32_t frame_count, uint32_t fps_milli)
{
    if (!rvf_path || !frame_count || !fps_milli) return false;
    strlcpy(video_cmd_path, rvf_path, sizeof(video_cmd_path));
    video_cmd_frames = frame_count;
    video_cmd_fps = fps_milli;
    return post_audio_cmd(VIDEO_CMD_PREPARE, 2000);
}

extern "C" bool video_audio_start(void)
{
    return post_audio_cmd(VIDEO_CMD_START, 1000);
}

extern "C" void video_audio_halt(void)
{
    if (!video_session && !video_running && !video_prepared) return;
    post_audio_cmd(VIDEO_CMD_HALT, 1500);
}

extern "C" void video_audio_release(void)
{
    if (!video_session && !video_running && !video_prepared) return;
    post_audio_cmd(VIDEO_CMD_RELEASE, 1500);
}

extern "C" bool video_audio_is_active(void)
{
    return video_running;
}

extern "C" void video_audio_get_clock(uint32_t *position_samples, uint32_t *duration_samples,
                                      uint32_t *cycle, uint32_t *underruns)
{
    const uint32_t duration = video_running ? video_duration_samples : 0;
    const uint64_t heard = duration ? heard_pcm_bytes() / 4u : 0;
    if (position_samples) *position_samples = duration ? heard % duration : 0;
    if (duration_samples) *duration_samples = duration;
    if (cycle) *cycle = duration ? heard / duration : 0;
    if (underruns) *underruns = video_running ? video_underruns : 0;
}

static void music_audio_task(void *parameter)
{
    (void)parameter;
    bool reprocess = false;
    while (true) {
        if (!reprocess) {
            const uint32_t wait = (video_running || video_cmd || ai_release_request ||
                                   alarm_audio_requested)
                                      ? pdMS_TO_TICKS(2)
                                      : portMAX_DELAY;
            ulTaskNotifyTake(pdTRUE, wait);
        }
        reprocess = false;
        consume_music_requests_during_video();

        if (ai_release_request) {
            handle_ai_release();
            xSemaphoreGive(audio_cmd_done);
        }

        if (ai_i2s_held && (video_cmd == VIDEO_CMD_HALT || video_cmd == VIDEO_CMD_RELEASE ||
                            video_cmd == VIDEO_CMD_PREPARE || video_cmd == VIDEO_CMD_START)) {
            const uint8_t cmd = video_cmd;
            video_cmd = VIDEO_CMD_NONE;
            if (cmd == VIDEO_CMD_HALT || cmd == VIDEO_CMD_RELEASE) handle_video_command(cmd);
            else defer_video_command_for_ai(cmd);
            xSemaphoreGive(audio_cmd_done);
            continue;
        }
        if (ai_i2s_held) continue;

        if (video_cmd != VIDEO_CMD_NONE) {
            const uint8_t cmd = video_cmd;
            video_cmd = VIDEO_CMD_NONE;
            handle_video_command(cmd);
            xSemaphoreGive(audio_cmd_done);
        }

        service_deferred_video_audio();

        if (video_need_rearm && video_running) {
            if (!rearm_video_output()) fail_video_runtime("rearm");
        }

        if (alarm_audio_requested) {
            if (video_session && video_running) {
                suspend_video_output();
                play_alarm_blocking();
                if (video_running && video_need_rearm && !rearm_video_output())
                    fail_video_runtime("rearm");
            } else if (video_session) {
                play_alarm_blocking();
                mute_music_i2s();
            } else {
                const bool resume_music = music_audio_playing;
                music_audio_playing = false;
                sdcar_close_music();
                play_alarm_blocking();
                if (resume_music) {
                    music_audio_playing = true;
                    music_audio_stop_requested = false;
                }
            }
        }

        if (video_running) {
            service_video_audio();
            reprocess = true;
            continue;
        }

        while (music_audio_playing && !audio_request_pending()) {
            if (music_audio_stop_requested || music_audio_pause_requested) {
                music_audio_playing = false;
                break;
            }
            if (music_track_request >= 0) {
                const uint8_t requested = (uint8_t)music_track_request;
                music_track_request = -1;
                if (requested < sdcar_get_music_track_count()) {
                    sdcar_close_music();
                    music_track_index = requested;
                    music_elapsed_ms = 0;
                    Serial.printf("Music: switching to %s\n",
                                  sdcar_get_music_track_path(music_track_index));
                }
            }
            if (!enable_music_i2s_channel()) {
                music_audio_playing = false;
                music_audio_stop_requested = true;
                break;
            }
            if (!sdcar_music_file_is_open() && !open_music_wav()) {
                music_audio_playing = false;
                music_audio_stop_requested = true;
                break;
            }
            const size_t count = sdcar_read_music(music_audio_buffer, sizeof(music_audio_buffer));
            if (count == 0) {
                sdcar_close_music();
                const uint8_t mode = music_play_mode;
                bool advance = false;
                uint8_t next_track = music_track_index;
                if (mode == MUSIC_MODE_SINGLE) {
                    advance = true;
                } else if (mode == MUSIC_MODE_LOOP_ALL && sdcar_get_music_track_count() > 0) {
                    next_track = (music_track_index + 1) % sdcar_get_music_track_count();
                    advance = true;
                } else if (mode == MUSIC_MODE_SEQUENTIAL &&
                           music_track_index + 1 < sdcar_get_music_track_count()) {
                    next_track = music_track_index + 1;
                    advance = true;
                }
                if (advance && !music_audio_stop_requested && !music_audio_pause_requested) {
                    music_track_index = next_track;
                    music_elapsed_ms = 0;
                    Serial.printf("Music: track ended, advancing to %s\n",
                                  sdcar_get_music_track_path(music_track_index));
                    continue;
                }
                music_audio_playing = false;
                music_audio_stop_requested = true;
                break;
            }
            apply_audio_volume(music_audio_buffer, count);
            if (!music_i2s_tx) {
                music_audio_playing = false;
                music_audio_stop_requested = true;
                break;
            }
            size_t written = 0;
            const esp_err_t err = i2s_channel_write(
                music_i2s_tx, music_audio_buffer, count, &written, pdMS_TO_TICKS(100));
            if (err != ESP_OK) {
                Serial.printf("Music I2S write failed: %s\n", esp_err_to_name(err));
                music_audio_playing = false;
                music_audio_stop_requested = true;
                break;
            }
            music_elapsed_ms += (uint32_t)((written * 1000ULL) / (MUSIC_SAMPLE_RATE * 4ULL));
        }
        if (audio_request_pending() && music_audio_playing) {
            reprocess = true;
            continue;
        }
        if (!music_audio_playing && (music_audio_stop_requested || music_audio_pause_requested)) {
            if (music_audio_stop_requested && sdcar_music_file_is_open()) sdcar_close_music();
            if (music_i2s_enabled) {
                mute_music_i2s();
                Serial.printf("Music: I2S muted and channel disabled (%s)\n",
                              music_audio_stop_requested ? "stop" : "pause");
            }
            portENTER_CRITICAL(&music_control_mux);
            const bool restart = music_play_requested;
            music_play_requested = false;
            if (restart) {
                music_audio_stop_requested = false;
                music_audio_pause_requested = false;
                music_audio_playing = true;
            }
            portEXIT_CRITICAL(&music_control_mux);
            if (restart && music_audio_task_handle) xTaskNotifyGive(music_audio_task_handle);
        }
    }
}
