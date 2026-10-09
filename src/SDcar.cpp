#include "SDcar.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include "Sound.h"
#include "wav_pcm.h"
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SD_SCK 48
#define SD_MISO 41
#define SD_MOSI 47
#define SD_DAT2 42

static File sd_open_file(const char *path, const char *mode = FILE_READ);

extern "C" void update_sd_status(bool mounted, const char *type,
                                 uint64_t size_bytes, uint64_t used_bytes,
                                 uint32_t file_count);
extern "C" {
lv_font_t *chinese_font_16 = nullptr;
lv_font_t *chinese_font_20 = nullptr;
lv_font_t *chinese_font_32 = nullptr;
extern const lv_font_t lv_font_chinese_20_builtin;
lv_img_dsc_t sd_icon_music = {};
lv_img_dsc_t sd_icon_settings = {};
lv_img_dsc_t sd_icon_weather = {};
lv_img_dsc_t sd_icon_clock = {};
lv_img_dsc_t sd_icon_photos = {};
lv_img_dsc_t sd_icon_browser = {};
lv_img_dsc_t sd_icon_video = {};
lv_img_dsc_t sd_icon_calendar = {};
lv_img_dsc_t sd_icon_ai_assistant = {};
}

static int wallpaper_choice = 0;
static bool sd_available = false;
static const char *wallpaper_paths[] = {
    "S:/wallpapers/wallpaper1.bin",
    "S:/wallpapers/wallpaper2.bin",
    "S:/wallpapers/wallpaper3.bin"
};
static lv_img_dsc_t wallpaper_images[3] = {};
static uint8_t *wallpaper_image_data[3] = {};

// Keep only photo paths in RAM. The gallery shows filenames and opens an image
// only after the user selects it.
#define MAX_PHOTO_FILES 32
#define PHOTO_PATH_LENGTH 96
static char photo_paths[MAX_PHOTO_FILES][PHOTO_PATH_LENGTH] = {};
static int photo_count = 0;
static char photo_sd_dir[16] = "/images";
static lv_img_dsc_t loaded_photo_image = {};
static uint8_t *loaded_photo_data = nullptr;
static char photo_load_path[PHOTO_PATH_LENGTH] = {};
static TaskHandle_t photo_loader_task_handle = nullptr;
static volatile int photo_load_state = 0; // 0 idle, 1 loading, 2 ready, -1 failed

static bool photo_path_has_bin_extension(const char *name) {
    if (!name) return false;
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return strcasecmp(dot, ".bin") == 0;
}

extern "C" int scan_photo_files(void) {
    photo_count = 0;
    memset(photo_paths, 0, sizeof(photo_paths));
    if (!sd_available) return 0;

    File root = sd_open_file("/imges");
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        root = sd_open_file("/images");
        if (!root || !root.isDirectory()) {
            if (root) root.close();
            return 0;
        }
        strlcpy(photo_sd_dir, "/images", sizeof(photo_sd_dir));
    } else {
        strlcpy(photo_sd_dir, "/imges", sizeof(photo_sd_dir));
    }

    File entry = root.openNextFile();
    while (entry && photo_count < MAX_PHOTO_FILES) {
        const char *name = entry.name();
        if (!entry.isDirectory() && photo_path_has_bin_extension(name)) {
            const char *file_name = name;
            const char *slash = strrchr(name, '/');
            if (slash && slash[1] != '\0') file_name = slash + 1;
            snprintf(photo_paths[photo_count], PHOTO_PATH_LENGTH,
                     "S:%s/%s", photo_sd_dir, file_name);
            ++photo_count;
        }
        entry.close();
        entry = root.openNextFile();
    }
    if (entry) entry.close();
    root.close();

    Serial.printf("Photos: found %d .bin file(s) in %s\n", photo_count, photo_sd_dir);
    return photo_count;
}

extern "C" int get_photo_count(void) {
    return photo_count;
}

extern "C" const char *get_photo_path(int index) {
    if (index < 0 || index >= photo_count) return nullptr;
    return photo_paths[index];
}

extern "C" const char *get_photo_name(int index) {
    const char *path = get_photo_path(index);
    if (!path) return nullptr;
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void photo_loader_task(void *parameter) {
    (void)parameter;
    File file = sd_open_file(photo_load_path + 2, FILE_READ);
    uint8_t *data = nullptr;
    bool ok = false;
    lv_img_header_t header = {};

    if (file) {
        file.setBufferSize(32 * 1024);
        if (file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) ==
                sizeof(header) &&
            header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA &&
            header.w > 0 && header.w <= 480 &&
            header.h > 0 && header.h <= 480) {
            const size_t data_size = static_cast<size_t>(header.w) * header.h *
                                     LV_IMG_PX_SIZE_ALPHA_BYTE;
            if (file.size() >= sizeof(lv_img_header_t) + data_size) {
                data = static_cast<uint8_t *>(heap_caps_malloc(
                    data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                if (data) {
                    size_t total = 0;
                    while (total < data_size) {
                        const size_t remaining = data_size - total;
                        const size_t chunk = remaining > 32 * 1024 ?
                                             32 * 1024 : remaining;
                        const size_t count = file.read(
                            data + total, chunk);
                        if (count == 0) break;
                        total += count;
                        vTaskDelay(1);
                    }
                    ok = total == data_size;
                }
            }
        }
        file.close();
    }

    if (ok) {
        if (loaded_photo_data) heap_caps_free(loaded_photo_data);
        loaded_photo_data = data;
        loaded_photo_image.header = header;
        loaded_photo_image.data_size = static_cast<uint32_t>(
            static_cast<size_t>(header.w) * header.h * LV_IMG_PX_SIZE_ALPHA_BYTE);
        loaded_photo_image.data = loaded_photo_data;
        photo_load_state = 2;
    } else {
        if (data) heap_caps_free(data);
        photo_load_state = -1;
    }

    photo_loader_task_handle = nullptr;
    vTaskDelete(nullptr);
}

extern "C" bool start_photo_load(const char *path) {
    if (!path || photo_loader_task_handle) return false;
    strlcpy(photo_load_path, path, sizeof(photo_load_path));
    photo_load_state = 1;
    if (xTaskCreatePinnedToCore(photo_loader_task, "PhotoLoadTask", 4096,
                                nullptr, 1, &photo_loader_task_handle, 0) != pdPASS) {
        photo_loader_task_handle = nullptr;
        photo_load_state = -1;
        return false;
    }
    return true;
}

extern "C" int get_photo_load_status(void) {
    return photo_load_state;
}

extern "C" const lv_img_dsc_t *get_loaded_photo_image(void) {
    return photo_load_state == 2 ? &loaded_photo_image : nullptr;
}

// RVF streams RGB565 frames through two PSRAM buffers: one displayed frame
// and one frame prefetched by a worker, without copying completed frames.
#define MAX_VIDEO_FILES 32
#define VIDEO_PATH_LENGTH 96
#define VIDEO_MAGIC "RVF1"
#define VIDEO_PIXEL_FORMAT_RGB565_LE 1
#define VIDEO_MAX_WIDTH 480
#define VIDEO_MAX_HEIGHT 480
// Keep the VFS buffer in internal RAM small enough for the rest of the UI,
// while still avoiding the tiny default stdio buffer used for SD reads.
static constexpr size_t SD_FILE_BUFFER_SIZE = 32 * 1024;
static constexpr size_t SD_VIDEO_READ_CHUNK = 64 * 1024;

struct __attribute__((packed)) VideoFileHeader {
    char magic[4];
    uint16_t version;
    uint16_t header_size;
    uint16_t width;
    uint16_t height;
    uint32_t fps_milli;
    uint32_t frame_count;
    uint32_t frame_size;
    uint32_t pixel_format;
    uint32_t reserved;
};

static_assert(sizeof(VideoFileHeader) == 32, "RVF header must be 32 bytes");

static char video_paths[MAX_VIDEO_FILES][VIDEO_PATH_LENGTH] = {};
static int video_count = 0;
static File video_file;
static VideoFileHeader video_header = {};
static lv_img_dsc_t video_frame_image = {};
// Keep SD writes away from the source frame currently consumed by LVGL.
// Only LVGLTask publishes a completed back buffer and releases the old front.
static uint8_t *video_frame_data = nullptr;
static uint8_t *video_frame_back_data = nullptr;
static size_t video_frame_capacity = 0;
static uint32_t video_frame_index = 0;
static TaskHandle_t video_prefetch_task_handle = nullptr;
static SemaphoreHandle_t video_prefetch_done = nullptr;
static portMUX_TYPE video_prefetch_lock = portMUX_INITIALIZER_UNLOCKED;
static bool video_prefetch_stop = false;
static int video_prefetch_state = 0; // 0 pending, 1 ready, -1 read error
static uint32_t video_frame_sequence = 0;
static uint32_t video_read_frames = 0;
static uint32_t video_read_total_ms = 0;
static uint32_t video_read_max_ms = 0;
static uint32_t video_file_next_index = 0xffffffffu;
static uint32_t video_displayed_index = 0;
static uint32_t video_displayed_cycle = 0;
static uint32_t video_back_index = 0;
static uint32_t video_back_cycle = 0;
static uint32_t video_target_frame = 0;
static uint32_t video_target_cycle = 0;
static uint32_t video_target_generation = 0;
static bool video_target_valid = false;
static bool video_loading = false;
static uint32_t video_loading_frame = 0;
static uint32_t video_loading_cycle = 0;

static bool video_prefetch_should_stop(void) {
    portENTER_CRITICAL(&video_prefetch_lock);
    const bool stop = video_prefetch_stop;
    portEXIT_CRITICAL(&video_prefetch_lock);
    return stop;
}

static bool video_path_has_rvf_extension(const char *name) {
    if (!name) return false;
    const char *dot = strrchr(name, '.');
    return dot && strcasecmp(dot, ".rvf") == 0;
}

static bool read_video_bytes(uint8_t *destination, size_t length) {
    size_t total = 0;
    while (total < length) {
        if (video_prefetch_should_stop()) return false;
        const size_t chunk = min(SD_VIDEO_READ_CHUNK, length - total);
        const size_t count = video_file.read(destination + total, chunk);
        if (count == 0) return false;
        total += count;
        // One yield per large transfer keeps the UI/task watchdog serviced
        // without adding ten scheduler gaps to every 320x240 frame.
        yield();
    }
    return true;
}

static bool seek_video_frame(uint32_t index) {
    if (!video_file || index >= video_header.frame_count) return false;
    const uint64_t offset64 = (uint64_t)video_header.header_size +
                              (uint64_t)index * video_header.frame_size;
    if (offset64 > 0xffffffffull) return false;
    if (video_file_next_index == index) return true;
    if (!video_file.seek((uint32_t)offset64)) {
        video_file_next_index = 0xffffffffu;
        return false;
    }
    video_file_next_index = index;
    return true;
}

static bool read_video_frame_at(uint32_t index) {
    if (!video_file || !video_frame_data || !video_frame_back_data ||
        video_header.frame_count == 0) return false;
    const uint32_t started_ms = millis();
    if (!seek_video_frame(index)) return false;
    if (!read_video_bytes(video_frame_back_data, video_header.frame_size)) {
        video_file_next_index = 0xffffffffu;
        return false;
    }
    video_file_next_index = index + 1u;
    video_back_index = index;
    const uint32_t elapsed_ms = millis() - started_ms;
    portENTER_CRITICAL(&video_prefetch_lock);
    ++video_read_frames;
    video_read_total_ms += elapsed_ms;
    if (elapsed_ms > video_read_max_ms) video_read_max_ms = elapsed_ms;
    portEXIT_CRITICAL(&video_prefetch_lock);
    return true;
}

static bool read_video_frame_to_back(void) {
    uint32_t index = video_frame_index;
    if (video_header.frame_count == 0) return false;
    if (index >= video_header.frame_count) index = 0;
    if (!read_video_frame_at(index)) return false;
    video_frame_index = index + 1u;
    return true;
}

// Only LVGLTask publishes frames. Call while holding video_prefetch_lock.
static void publish_video_back_frame(void) {
    uint8_t *completed_frame = video_frame_back_data;
    video_frame_back_data = video_frame_data;
    video_frame_data = completed_frame;
    video_frame_image.data = video_frame_data;
    ++video_frame_sequence;
}

static bool read_next_video_frame_impl(void) {
    if (!read_video_frame_to_back()) return false;
    portENTER_CRITICAL(&video_prefetch_lock);
    publish_video_back_frame();
    video_displayed_index = video_back_index;
    video_displayed_cycle = video_back_cycle;
    portEXIT_CRITICAL(&video_prefetch_lock);
    return true;
}

static void video_request_frame(uint32_t frame, uint32_t cycle) {
    if (video_header.frame_count == 0) return;
    if (frame >= video_header.frame_count) frame = video_header.frame_count - 1u;
    bool notify = false;
    portENTER_CRITICAL(&video_prefetch_lock);
    const bool showing = frame == video_displayed_index && cycle == video_displayed_cycle;
    const bool other_ready = video_prefetch_state == 1 &&
                             (video_back_index != frame || video_back_cycle != cycle);
    if (showing && !other_ready) {
        video_target_valid = false;
        portEXIT_CRITICAL(&video_prefetch_lock);
        return;
    }
    const bool same = video_target_valid && video_target_frame == frame &&
                      video_target_cycle == cycle &&
                      (video_loading || (video_prefetch_state == 1 &&
                                         video_back_index == frame &&
                                         video_back_cycle == cycle));
    if (!same) {
        video_target_frame = frame;
        video_target_cycle = cycle;
        video_target_valid = true;
        ++video_target_generation;
        if (other_ready) video_prefetch_state = 0;
        notify = true;
    }
    portEXIT_CRITICAL(&video_prefetch_lock);
    if (notify && video_prefetch_task_handle) xTaskNotifyGive(video_prefetch_task_handle);
}

static void video_prefetch_task(void *) {
    while (!video_prefetch_should_stop()) {
        bool need_read = false;
        portENTER_CRITICAL(&video_prefetch_lock);
        // A notify can arrive before this wait. Read only after the consumer
        // drops the back buffer, so a seek notify cannot skip a ready frame.
        need_read = video_prefetch_state != 1 && !video_prefetch_stop;
        const bool stop_now = video_prefetch_stop;
        portEXIT_CRITICAL(&video_prefetch_lock);
        if (stop_now) break;
        if (!need_read) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        uint32_t target = 0;
        uint32_t generation = 0;
        uint32_t cycle = 0;
        bool use_target = false;
        portENTER_CRITICAL(&video_prefetch_lock);
        use_target = video_target_valid;
        target = use_target ? video_target_frame : video_frame_index;
        generation = video_target_generation;
        cycle = video_target_cycle;
        if (!use_target && target >= video_header.frame_count) target = 0;
        video_loading = true;
        video_loading_frame = target;
        video_loading_cycle = use_target ? cycle : video_displayed_cycle;
        portEXIT_CRITICAL(&video_prefetch_lock);

        const bool loaded = use_target ? read_video_frame_at(target)
                                       : read_video_frame_to_back();

        portENTER_CRITICAL(&video_prefetch_lock);
        const bool stop = video_prefetch_stop;
        const bool stale = use_target &&
                           (generation != video_target_generation || !video_target_valid ||
                            video_target_frame != target);
        video_loading = false;
        if (stop || stale) {
            video_prefetch_state = 0;
        } else if (!loaded) {
            video_prefetch_state = -1;
        } else {
            if (use_target) video_frame_index = target + 1u;
            video_back_cycle = use_target ? cycle : video_displayed_cycle;
            video_prefetch_state = 1;
        }
        portEXIT_CRITICAL(&video_prefetch_lock);
        if (!loaded && !stop && !stale)
            Serial.println("Videos: prefetch read failed; keeping current frame");
        if (stop) break;
        if (!loaded && !stale) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    portENTER_CRITICAL(&video_prefetch_lock);
    video_prefetch_state = 0;
    portEXIT_CRITICAL(&video_prefetch_lock);
    // No File/buffer accesses after this acknowledgement.
    xSemaphoreGive(video_prefetch_done);
    vTaskDelete(nullptr);
}

extern "C" int scan_video_files(void) {
    video_count = 0;
    memset(video_paths, 0, sizeof(video_paths));
    if (!sd_available) return 0;

    File root = sd_open_file("/videos");
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        Serial.println("Videos: /videos directory not found");
        return 0;
    }

    File entry = root.openNextFile();
    while (entry && video_count < MAX_VIDEO_FILES) {
        const char *name = entry.name();
        if (!entry.isDirectory() && video_path_has_rvf_extension(name)) {
            const char *file_name = name;
            const char *slash = strrchr(name, '/');
            if (slash && slash[1] != '\0') file_name = slash + 1;
            snprintf(video_paths[video_count], VIDEO_PATH_LENGTH,
                     "/videos/%s", file_name);
            ++video_count;
        }
        entry.close();
        entry = root.openNextFile();
    }
    if (entry) entry.close();
    root.close();
    Serial.printf("Videos: found %d .rvf file(s)\n", video_count);
    return video_count;
}

extern "C" int get_video_count(void) {
    return video_count;
}

extern "C" const char *get_video_name(int index) {
    if (index < 0 || index >= video_count) return nullptr;
    const char *slash = strrchr(video_paths[index], '/');
    return slash ? slash + 1 : video_paths[index];
}

extern "C" void close_video_file(void) {
    // Stop companion audio before joining the frame worker so both SD users
    // finish their current read instead of being deleted mid-transfer.
    video_audio_halt();
    if (video_prefetch_task_handle) {
        portENTER_CRITICAL(&video_prefetch_lock);
        video_prefetch_stop = true;
        video_target_valid = false;
        portEXIT_CRITICAL(&video_prefetch_lock);
        xTaskNotifyGive(video_prefetch_task_handle);
        xSemaphoreTake(video_prefetch_done, portMAX_DELAY);
        video_prefetch_task_handle = nullptr;
    }
    if (video_file) video_file.close();
    video_frame_index = 0;
    video_file_next_index = 0xffffffffu;
    video_target_valid = false;
    video_loading = false;
}

extern "C" bool open_video_file(int index) {
    close_video_file();
    if (index < 0 || index >= video_count) {
        video_audio_release();
        return false;
    }
    portENTER_CRITICAL(&video_prefetch_lock);
    video_prefetch_stop = false;
    video_prefetch_state = 0;
    portEXIT_CRITICAL(&video_prefetch_lock);

    video_file = sd_open_file(video_paths[index], FILE_READ);
    if (!video_file) {
        close_video_file();
        video_audio_release();
        return false;
    }
    video_file.setBufferSize(SD_FILE_BUFFER_SIZE);
    if (video_file.read(reinterpret_cast<uint8_t *>(&video_header), sizeof(video_header)) !=
        sizeof(video_header)) {
        close_video_file();
        video_audio_release();
        return false;
    }

    const uint32_t expected_frame_size =
        static_cast<uint32_t>(video_header.width) * video_header.height * sizeof(lv_color_t);
    const uint64_t expected_file_size =
        static_cast<uint64_t>(video_header.header_size) +
        static_cast<uint64_t>(video_header.frame_count) * video_header.frame_size;
    if (memcmp(video_header.magic, VIDEO_MAGIC, 4) != 0 ||
        video_header.version != 1 ||
        video_header.header_size < sizeof(VideoFileHeader) ||
        video_header.width == 0 || video_header.width > VIDEO_MAX_WIDTH ||
        video_header.height == 0 || video_header.height > VIDEO_MAX_HEIGHT ||
        video_header.fps_milli < 1000 || video_header.fps_milli > 30000 ||
        video_header.frame_count == 0 ||
        video_header.frame_size != expected_frame_size ||
        video_header.pixel_format != VIDEO_PIXEL_FORMAT_RGB565_LE ||
        expected_file_size > video_file.size()) {
        Serial.printf("Videos: invalid RVF file %s\n", video_paths[index]);
        close_video_file();
        video_audio_release();
        return false;
    }

    if (video_frame_capacity < video_header.frame_size ||
        !video_frame_data || !video_frame_back_data) {
        if (video_frame_data) heap_caps_free(video_frame_data);
        if (video_frame_back_data) heap_caps_free(video_frame_back_data);
        video_frame_data = static_cast<uint8_t *>(
            heap_caps_malloc(video_header.frame_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        video_frame_back_data = static_cast<uint8_t *>(
            heap_caps_malloc(video_header.frame_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!video_frame_data || !video_frame_back_data) {
            if (video_frame_data) heap_caps_free(video_frame_data);
            if (video_frame_back_data) heap_caps_free(video_frame_back_data);
            video_frame_data = nullptr;
            video_frame_back_data = nullptr;
            video_frame_capacity = 0;
            Serial.printf("Videos: double PSRAM allocation failed (%u bytes)\n",
                          static_cast<unsigned>(video_header.frame_size * 2U));
            close_video_file();
            video_audio_release();
            return false;
        }
        video_frame_capacity = video_header.frame_size;
    }

    if (!video_file.seek(video_header.header_size)) {
        close_video_file();
        video_audio_release();
        return false;
    }
    video_file_next_index = 0;

    memset(&video_frame_image, 0, sizeof(video_frame_image));
    video_frame_image.header.cf = LV_IMG_CF_TRUE_COLOR;
    video_frame_image.header.w = video_header.width;
    video_frame_image.header.h = video_header.height;
    video_frame_image.data_size = video_header.frame_size;
    video_frame_image.data = video_frame_data;
    video_frame_index = 0;
    video_displayed_index = 0;
    video_displayed_cycle = 0;
    video_back_cycle = 0;
    video_target_valid = false;

    if (!read_next_video_frame_impl()) {
        close_video_file();
        video_audio_release();
        return false;
    }

    if (!video_prefetch_done) video_prefetch_done = xSemaphoreCreateBinary();
    if (video_prefetch_done) {
        xSemaphoreTake(video_prefetch_done, 0);
        if (xTaskCreatePinnedToCore(video_prefetch_task, "VideoPrefetch", 4096,
                                   nullptr, 1, &video_prefetch_task_handle, 0) != pdPASS)
            video_prefetch_task_handle = nullptr;
    }
    if (!video_prefetch_task_handle)
        Serial.println("Videos: prefetch unavailable; using synchronous reads");

    Serial.printf("Videos: playing %s %ux%u %.1f FPS, %u frames\n",
                  video_paths[index], video_header.width, video_header.height,
                  video_header.fps_milli / 1000.0f,
                  static_cast<unsigned>(video_header.frame_count));
    return true;
}

extern "C" const lv_img_dsc_t *get_video_frame_image(void) {
    return video_frame_data ? &video_frame_image : nullptr;
}

extern "C" uint32_t get_video_frame_delay_ms(void) {
    if (video_header.fps_milli == 0) return 100;
    return constrain(1000000UL / video_header.fps_milli, 33UL, 1000UL);
}

extern "C" bool read_next_video_frame(void) {
    // Clear a pending audio seek when a failed soundtrack falls back to video.
    portENTER_CRITICAL(&video_prefetch_lock);
    if (video_target_valid) {
        video_target_valid = false;
        ++video_target_generation;
    }
    portEXIT_CRITICAL(&video_prefetch_lock);
    if (!video_prefetch_task_handle) {
        const bool loaded = read_next_video_frame_impl();
        if (loaded) lv_img_cache_invalidate_src(&video_frame_image);
        return loaded;
    }
    portENTER_CRITICAL(&video_prefetch_lock);
    const bool ready = video_prefetch_state == 1;
    if (ready) {
        publish_video_back_frame();
        video_displayed_index = video_back_index;
        video_displayed_cycle = video_back_cycle;
        video_prefetch_state = 0;
    }
    portEXIT_CRITICAL(&video_prefetch_lock);
    if (!ready) return false;
    lv_img_cache_invalidate_src(&video_frame_image);
    xTaskNotifyGive(video_prefetch_task_handle);
    return true;
}

extern "C" uint32_t get_video_frame_sequence(void) {
    portENTER_CRITICAL(&video_prefetch_lock);
    const uint32_t sequence = video_frame_sequence;
    portEXIT_CRITICAL(&video_prefetch_lock);
    return sequence;
}

extern "C" uint32_t get_video_source_fps_milli(void) {
    return video_file ? video_header.fps_milli : 0;
}

extern "C" void get_video_read_metrics(uint32_t *frames, uint32_t *total_ms,
                                       uint32_t *max_ms) {
    portENTER_CRITICAL(&video_prefetch_lock);
    if (frames) *frames = video_read_frames;
    if (total_ms) *total_ms = video_read_total_ms;
    if (max_ms) *max_ms = video_read_max_ms;
    portEXIT_CRITICAL(&video_prefetch_lock);
}

extern "C" const char *get_video_path(int index) {
    if (index < 0 || index >= video_count) return nullptr;
    return video_paths[index];
}

extern "C" uint32_t get_video_frame_count(void) {
    return video_file ? video_header.frame_count : 0;
}

extern "C" void video_sync_cancel(void) {
    portENTER_CRITICAL(&video_prefetch_lock);
    video_target_valid = false;
    ++video_target_generation;
    portEXIT_CRITICAL(&video_prefetch_lock);
    if (video_prefetch_task_handle) xTaskNotifyGive(video_prefetch_task_handle);
}

extern "C" void get_video_sync_diag(uint32_t *displayed_frame, int32_t *drift_ms) {
    if (displayed_frame) *displayed_frame = video_displayed_index;
    if (drift_ms) {
        uint32_t position = 0, duration = 0, cycle = 0, frame_sample = 0;
        video_audio_get_clock(&position, &duration, &cycle, nullptr);
        int64_t drift = 0;
        if (duration && wav_pcm_frame_sample(video_displayed_index,
                                             video_header.fps_milli, &frame_sample)) {
            // Measure after rendering too, including a soundtrack loop boundary.
            drift = ((int64_t)cycle - video_displayed_cycle) * duration + position -
                    (int64_t)frame_sample;
            drift = drift * 1000 / WAV_PCM_SAMPLE_RATE;
        }
        *drift_ms = (int32_t)drift;
    }
}

extern "C" bool video_present_for_audio(uint32_t position_samples,
                                       uint32_t duration_samples, uint32_t cycle,
                                       uint32_t fps_milli) {
    if (!video_file || video_header.frame_count == 0 || duration_samples == 0 ||
        fps_milli == 0) {
        return false;
    }

    if (!video_prefetch_task_handle) {
        uint32_t want = 0;
        const uint32_t position = position_samples % duration_samples;
        if (!wav_pcm_frame_index(position, fps_milli, video_header.frame_count, &want))
            return false;
        if (want == video_displayed_index && cycle == video_displayed_cycle) return false;
        if (!read_video_frame_at(want)) return false;
        portENTER_CRITICAL(&video_prefetch_lock);
        publish_video_back_frame();
        video_displayed_index = want;
        video_displayed_cycle = cycle;
        video_back_cycle = cycle;
        portEXIT_CRITICAL(&video_prefetch_lock);
        lv_img_cache_invalidate_src(&video_frame_image);
        return true;
    }

    bool ready = false;
    bool loading = false;
    uint32_t ready_frame = 0;
    uint32_t ready_cycle = 0;
    uint32_t loading_frame = 0;
    uint32_t loading_cycle = 0;
    portENTER_CRITICAL(&video_prefetch_lock);
    ready = video_prefetch_state == 1;
    ready_frame = video_back_index;
    ready_cycle = video_back_cycle;
    loading = video_loading;
    loading_frame = video_loading_frame;
    loading_cycle = video_loading_cycle;
    portEXIT_CRITICAL(&video_prefetch_lock);

    video_sync_input_t input = {};
    input.position_samples = position_samples;
    input.duration_samples = duration_samples;
    input.audio_cycle = cycle;
    input.fps_milli = fps_milli;
    input.video_frames = video_header.frame_count;
    input.ready_valid = ready;
    input.ready_frame = ready_frame;
    input.ready_cycle = ready_cycle;
    const video_sync_decision_t decision = video_sync_decide(&input);

    if (decision.action == VIDEO_SYNC_HOLD) return false;

    if (decision.action == VIDEO_SYNC_PUBLISH) {
        bool published = false;
        portENTER_CRITICAL(&video_prefetch_lock);
        if (video_prefetch_state == 1 && video_back_index == ready_frame &&
            video_back_cycle == ready_cycle) {
            publish_video_back_frame();
            video_displayed_index = ready_frame;
            video_displayed_cycle = ready_cycle;
            video_prefetch_state = 0;
            published = true;
        }
        portEXIT_CRITICAL(&video_prefetch_lock);
        if (!published) return false;
        lv_img_cache_invalidate_src(&video_frame_image);
        video_request_frame(decision.next_frame, cycle);
        return true;
    }

    if (loading && loading_cycle == cycle &&
        video_sync_frame_useful(position_samples % duration_samples, loading_frame,
                                fps_milli, video_header.frame_count)) {
        return false;
    }
    video_request_frame(decision.next_frame, cycle);
    return false;
}

void sdcar_load_wallpaper_cache(void) {
    for (int i = 0; i < 3; ++i) {
        File file = sd_open_file(wallpaper_paths[i] + 2, FILE_READ);
        if (!file) {
            Serial.printf("Wallpaper cache: %s open failed\n", wallpaper_paths[i]);
            continue;
        }
        file.setBufferSize(SD_FILE_BUFFER_SIZE);
        if (file.size() <= sizeof(lv_img_header_t)) {
            file.close();
            Serial.printf("Wallpaper cache: %s open failed\n", wallpaper_paths[i]);
            continue;
        }

        lv_img_header_t header = {};
        if (file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) != sizeof(header)) {
            file.close();
            Serial.printf("Wallpaper cache: %s header failed\n", wallpaper_paths[i]);
            continue;
        }

        const size_t data_size = static_cast<size_t>(file.size()) - sizeof(lv_img_header_t);
        uint8_t *data = static_cast<uint8_t *>(
            heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!data) {
            file.close();
            Serial.printf("Wallpaper cache: %s PSRAM allocation failed (%u bytes)\n",
                          wallpaper_paths[i], static_cast<unsigned>(data_size));
            continue;
        }

        size_t total = 0;
        while (total < data_size) {
            size_t count = file.read(data + total, data_size - total);
            if (count == 0) break;
            total += count;
        }
        file.close();

        if (total != data_size) {
            heap_caps_free(data);
            Serial.printf("Wallpaper cache: %s payload failed (%u/%u)\n",
                          wallpaper_paths[i], static_cast<unsigned>(total),
                          static_cast<unsigned>(data_size));
            continue;
        }

        wallpaper_images[i].header = header;
        wallpaper_images[i].data_size = data_size;
        wallpaper_images[i].data = data;
        wallpaper_image_data[i] = data;
        Serial.printf("Wallpaper cache: %s loaded into PSRAM (%u bytes)\n",
                      wallpaper_paths[i], static_cast<unsigned>(data_size));
    }
}

extern "C" const lv_img_dsc_t *get_wallpaper_image(void) {
    return wallpaper_images[wallpaper_choice].data ? &wallpaper_images[wallpaper_choice] : nullptr;
}

extern "C" const lv_img_dsc_t *get_wallpaper_choice_image(int choice) {
    if (choice < 0 || choice >= 3 || !wallpaper_images[choice].data) return nullptr;
    return &wallpaper_images[choice];
}

extern "C" const char *get_wallpaper_choice_path(int choice) {
    if (choice < 0 || choice >= 3) return nullptr;
    return wallpaper_paths[choice];
}

extern "C" const char *get_wallpaper_path(void) {
    return wallpaper_paths[wallpaper_choice];
}

extern "C" void set_wallpaper_choice(int choice) {
    if (choice < 0 || choice > 2) return;
    wallpaper_choice = choice;
    Preferences preferences;
    preferences.begin("wallpaper", false);
    preferences.putUChar("choice", static_cast<uint8_t>(choice));
    preferences.end();
}

void sdcar_load_wallpaper_choice(void) {
    Preferences preferences;
    preferences.begin("wallpaper", true);
    wallpaper_choice = preferences.getUChar("choice", 0);
    preferences.end();
    if (wallpaper_choice > 2) wallpaper_choice = 0;
}

// ========== 引脚定义 ==========
// The reference board uses SDMMC 1-bit mode and shares CLK/CMD with the LCD.
static lv_fs_drv_t sd_lvgl_fs_drv;
static File sd_open_file(const char *path, const char *mode) {
    if (!path) return File();
    // SD_MMC is mounted at /root, matching the original working firmware.
    // SD_MMC.open() receives paths relative to that mount.
    char logical_path[192];
    if (path[0] == '/') strlcpy(logical_path, path, sizeof(logical_path));
    else snprintf(logical_path, sizeof(logical_path), "/%s", path);
    return SD_MMC.open(logical_path, mode);
}
static bool sd_status_mounted = false;
static const char *sd_status_type = nullptr;
static uint64_t sd_status_size = 0;
static uint64_t sd_status_used = 0;
static uint32_t sd_status_file_count = 0;
static lv_pinyin_dict_t *sd_pinyin_dict = nullptr;
static char *sd_pinyin_blob = nullptr;
static uint16_t sd_pinyin_dict_count = 0;

static bool load_sd_pinyin_dict(void) {
    if (!sd_available) return false;

    File file = sd_open_file("/ime/pinyin_full.txt", FILE_READ);
    if (!file) {
        Serial.println("Pinyin dictionary: /ime/pinyin_full.txt not found");
        return false;
    }

    const size_t file_size = static_cast<size_t>(file.size());
    if (file_size == 0 || file_size > 512 * 1024) {
        Serial.printf("Pinyin dictionary: invalid size=%u\n",
                      static_cast<unsigned>(file_size));
        file.close();
        return false;
    }

    char *blob = static_cast<char *>(heap_caps_malloc(
        file_size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!blob) {
        blob = static_cast<char *>(malloc(file_size + 1));
    }
    if (!blob) {
        Serial.println("Pinyin dictionary: PSRAM allocation failed");
        file.close();
        return false;
    }

    size_t total = 0;
    while (total < file_size) {
        const size_t count = file.read(reinterpret_cast<uint8_t *>(blob + total),
                                       file_size - total);
        if (count == 0) break;
        total += count;
        yield();
    }
    file.close();
    if (total != file_size) {
        Serial.printf("Pinyin dictionary: read failed (%u/%u bytes)\n",
                      static_cast<unsigned>(total), static_cast<unsigned>(file_size));
        heap_caps_free(blob);
        return false;
    }
    blob[file_size] = '\0';

    uint16_t line_count = 0;
    for (size_t i = 0; i < file_size; i++) {
        if (blob[i] == '\n') line_count++;
    }
    if (file_size > 0 && blob[file_size - 1] != '\n') line_count++;
    if (line_count == 0 || line_count > 512) {
        Serial.printf("Pinyin dictionary: invalid group count=%u\n",
                      static_cast<unsigned>(line_count));
        heap_caps_free(blob);
        return false;
    }

    lv_pinyin_dict_t *dict = static_cast<lv_pinyin_dict_t *>(heap_caps_malloc(
        sizeof(lv_pinyin_dict_t) * (line_count + 1),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!dict) {
        dict = static_cast<lv_pinyin_dict_t *>(
            malloc(sizeof(lv_pinyin_dict_t) * (line_count + 1)));
    }
    if (!dict) {
        Serial.println("Pinyin dictionary: index allocation failed");
        heap_caps_free(blob);
        return false;
    }

    uint16_t count = 0;
    char *line = blob;
    while (*line && count < line_count) {
        char *next = strchr(line, '\n');
        if (next) *next = '\0';
        if (next && next > line && next[-1] == '\r') next[-1] = '\0';

        char *separator = strchr(line, '\t');
        /* Keep empty candidate rows (i/u/v).  They are not valid standalone
         * keyboard initials, but are required to keep LVGL's legacy index
         * layout aligned for every a-z initial. */
        if (separator && separator != line) {
            *separator = '\0';
            /* lv_pinyin_dict_t intentionally stores const pointer members;
             * construct each element in the malloc'ed array instead of
             * assigning those members after construction. */
            new (&dict[count]) lv_pinyin_dict_t{line, separator + 1};
            count++;
        }
        if (!next) break;
        line = next + 1;
    }
    new (&dict[count]) lv_pinyin_dict_t{nullptr, nullptr};
    if (count == 0) {
        heap_caps_free(dict);
        heap_caps_free(blob);
        Serial.println("Pinyin dictionary: no valid records");
        return false;
    }

    sd_pinyin_blob = blob;
    sd_pinyin_dict = dict;
    sd_pinyin_dict_count = count;
    Serial.printf("Pinyin dictionary: loaded %u groups, %u bytes from SD\n",
                  static_cast<unsigned>(count), static_cast<unsigned>(file_size));
    return true;
}

extern "C" lv_pinyin_dict_t *get_sd_pinyin_dict(void) {
    return sd_pinyin_dict;
}

static void set_sd_status(bool mounted, const char *type, uint64_t size_bytes,
                          uint64_t used_bytes, uint32_t file_count) {
    sd_status_mounted = mounted;
    sd_status_type = type;
    sd_status_size = size_bytes;
    sd_status_used = used_bytes;
    sd_status_file_count = file_count;
    // The label may not exist during early boot; values are republished after setup_ui().
    update_sd_status(mounted, type, size_bytes, used_bytes, file_count);
}

static void release_lcd_spi_pins_for_sd() {
    // The LCD uses a software SPI bus only during gfx->begin(). Its SCK/MOSI
    // pins overlap SDMMC CLK/CMD, so leave them released and pulled high
    // before SDMMC takes ownership of the pins.
    pinMode(SD_SCK, INPUT_PULLUP);
    pinMode(SD_MOSI, INPUT_PULLUP);
    delay(2);
}

static bool lvgl_sd_ready(lv_fs_drv_t *drv) {
    (void)drv;
    return sd_available;
}

static void *lvgl_sd_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode) {
    (void)drv;
    if (!sd_available || !(mode & LV_FS_MODE_RD)) return nullptr;

    // LVGL passes paths without the drive letter here (for example
    // "/fonts/chinese_20.bin"). SD_MMC is mounted at /sd, so translate the
    // logical LVGL path to the actual VFS mount point.
    const char *logical_path = path && path[0] == '/' ? path : "/";
    File *file = new File(sd_open_file(logical_path, FILE_READ));
    if (!file || !(*file)) {
        delete file;
        return nullptr;
    }
    file->setBufferSize(SD_FILE_BUFFER_SIZE);
    return file;
}

static lv_fs_res_t lvgl_sd_close(lv_fs_drv_t *drv, void *file_p) {
    (void)drv;
    File *file = static_cast<File *>(file_p);
    file->close();
    delete file;
    return LV_FS_RES_OK;
}

static lv_fs_res_t lvgl_sd_read(lv_fs_drv_t *drv, void *file_p, void *buf,
                                uint32_t bytes_to_read, uint32_t *bytes_read) {
    (void)drv;
    File *file = static_cast<File *>(file_p);
    uint8_t *dst = static_cast<uint8_t *>(buf);
    size_t total = 0;
    while (total < bytes_to_read) {
        const size_t count = file->read(dst + total, bytes_to_read - total);
        if (count == 0) break;
        total += count;
        // Large Photos images are decoded line-by-line from SD. Let the
        // sensor/WiFi tasks run between chunks so a full-screen image cannot
        // starve the scheduler while LVGL is drawing it.
        yield();
    }
    if (bytes_read) *bytes_read = static_cast<uint32_t>(total);
    return total == bytes_to_read ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t lvgl_sd_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos,
                                lv_fs_whence_t whence) {
    (void)drv;
    File *file = static_cast<File *>(file_p);
    uint32_t target = pos;
    if (whence == LV_FS_SEEK_CUR) target = static_cast<uint32_t>(file->position()) + pos;
    else if (whence == LV_FS_SEEK_END) target = static_cast<uint32_t>(file->size()) + pos;
    return file->seek(target) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t lvgl_sd_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *pos) {
    (void)drv;
    File *file = static_cast<File *>(file_p);
    *pos = static_cast<uint32_t>(file->position());
    return LV_FS_RES_OK;
}

static lv_font_t *load_sd_font(const char *sd_path, const char *lvgl_path,
                               uint32_t test_codepoint) {
    File font_file = sd_open_file(sd_path, FILE_READ);
    if (!font_file) {
        Serial.printf("Chinese font: %s not found\n", sd_path);
        return nullptr;
    }
    Serial.printf("Chinese font: %s size=%llu bytes\n", sd_path,
                  static_cast<unsigned long long>(font_file.size()));
    font_file.close();

    lv_font_t *font = lv_font_load(lvgl_path);
    if (font) {
        lv_font_glyph_dsc_t glyph;
        const uint8_t *bitmap = lv_font_get_glyph_bitmap(font, test_codepoint);
        if (!lv_font_get_glyph_dsc(font, &glyph, test_codepoint, 0) || !bitmap) {
            lv_font_free(font);
            font = nullptr;
        }
    }
    Serial.printf("Chinese font: %s %s\n", sd_path,
                  font ? "loaded" : "load/glyph validation failed");
    return font;
}

static bool validate_chinese20_key_glyphs(lv_font_t *font) {
    static const uint32_t required_glyphs[] = {
        0x65F6, 0x949F, 0x7167, 0x7247, 0x5230, 0x95F4, 0x4E86,
        0x63D0, 0x9192, 0x4FDD, 0x5B58, 0x5173, 0x95ED, 0x542F,
        0x7528, 0x5929, 0x6C14, 0x6674, 0x591A, 0x4E91, 0x9634,
        0x96FE, 0x973E, 0x96E8, 0x96EA, 0x96F7, 0x98CE, 0x9884,
        0x62A5, 0x767D, 0x591C, 0x9AD8, 0x4F4E, 0x6E29, 0x5EA6,
        0x2103,
        // APP page labels and title.
        0x5E94, 0x9640, 0x87BA, 0x4EEA, 0x6570, 0x5B57, 0x952E,
        0x97F3, 0x4E50, 0x6D4F, 0x89C8, 0x5668, 0x89C6, 0x9891,
        0x65E5, 0x5386,
        0xFF0C, 0x3002, 0xFF1A, 0xFF1B, 0x2018, 0x2019,
        0xFF01, 0x0040, 0x0023, 0xFFE5, 0x0025, 0x2026,
        0x0026, 0x002A, 0xFF08, 0xFF09
    };
    if (!font) return false;

    bool valid = true;
    for (uint32_t codepoint : required_glyphs) {
        lv_font_glyph_dsc_t glyph = {};
        if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) ||
            !lv_font_get_glyph_bitmap(font, codepoint)) {
            Serial.printf("Chinese font 20 missing glyph: U+%04lX ",
                          static_cast<unsigned long>(codepoint));
            valid = false;
        }
    }
    Serial.println(valid ? "Chinese font 20 key glyphs: ok" :
                           "Chinese font 20 key glyphs: incomplete");
    return valid;
}

static void log_lvgl_heap(const char *stage) {
    lv_mem_monitor_t monitor;
    lv_mem_monitor(&monitor);
    Serial.printf("LVGL heap %s: free=%u largest=%u used=%u%%\n", stage,
                  static_cast<unsigned>(monitor.free_size),
                  static_cast<unsigned>(monitor.free_biggest_size),
                  static_cast<unsigned>(monitor.used_pct));
    Serial.printf("PSRAM: total=%u free=%u largest=%u\n",
                  static_cast<unsigned>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM)),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}

static bool load_sd_icon(const char *path, lv_img_dsc_t *image) {
    File file = sd_open_file(path, FILE_READ);
    if (!file) {
        Serial.printf("SD icon: %s cannot open\n", path);
        return false;
    }
    file.setBufferSize(SD_FILE_BUFFER_SIZE);

    lv_img_header_t header = {};
    if (file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) != sizeof(header) ||
        header.cf != LV_IMG_CF_TRUE_COLOR_ALPHA || header.w == 0 || header.h == 0) {
        Serial.printf("SD icon: %s invalid header\n", path);
        file.close();
        return false;
    }

    const size_t data_size = static_cast<size_t>(header.w) * header.h * LV_IMG_PX_SIZE_ALPHA_BYTE;
    uint8_t *data = static_cast<uint8_t *>(malloc(data_size));
    if (!data || file.read(data, data_size) != data_size) {
        Serial.printf("SD icon: %s payload read failed\n", path);
        free(data);
        file.close();
        return false;
    }
    file.close();

    image->header = header;
    image->data_size = data_size;
    image->data = data;
    Serial.printf("SD icon: %s loaded into RAM (%u bytes)\n", path,
                  static_cast<unsigned>(data_size));
    return true;
}

static void load_sd_app_icons(void) {
    if (!sd_available) return;
    load_sd_icon("/icons/music.bin", &sd_icon_music);
    load_sd_icon("/icons/settings.bin", &sd_icon_settings);
    load_sd_icon("/icons/weather.bin", &sd_icon_weather);
    load_sd_icon("/icons/clock.bin", &sd_icon_clock);
    load_sd_icon("/icons/photos.bin", &sd_icon_photos);
    load_sd_icon("/icons/browser.bin", &sd_icon_browser);
    load_sd_icon("/icons/video.bin", &sd_icon_video);
    load_sd_icon("/icons/calendar.bin", &sd_icon_calendar);
    load_sd_icon("/icons/ai_assistant.bin", &sd_icon_ai_assistant);
}

static void validate_sd_app_icons(void) {
    static const struct {
        const char *lvgl_path;
        const char *sd_path;
    } icons[] = {
        {"S:/icons/music.bin", "/icons/music.bin"},
        {"S:/icons/settings.bin", "/icons/settings.bin"},
        {"S:/icons/weather.bin", "/icons/weather.bin"},
        {"S:/icons/clock.bin", "/icons/clock.bin"},
        {"S:/icons/photos.bin", "/icons/photos.bin"},
        {"S:/icons/browser.bin", "/icons/browser.bin"},
        {"S:/icons/video.bin", "/icons/video.bin"},
        {"S:/icons/calendar.bin", "/icons/calendar.bin"},
        {"S:/icons/ai_assistant.bin", "/icons/ai_assistant.bin"}
    };

    for (const auto &icon : icons) {
        File file = sd_open_file(icon.sd_path, FILE_READ);
        const uint32_t file_size = file ? static_cast<uint32_t>(file.size()) : 0;
        if (file) file.close();

        lv_img_header_t header = {};
        const lv_res_t result = lv_img_decoder_get_info(icon.lvgl_path, &header);
        if (result != LV_RES_OK) {
            Serial.printf("SD icon: %s missing or invalid (size=%u)\n",
                          icon.sd_path, static_cast<unsigned>(file_size));
            continue;
        }

        uint8_t line[64 * 4] = {};
        lv_img_decoder_dsc_t decoder = {};
        const lv_res_t open_result = lv_img_decoder_open(
            &decoder, icon.lvgl_path, lv_color_black(), 0);
        const lv_res_t line_result = open_result == LV_RES_OK
                                         ? lv_img_decoder_read_line(&decoder, 0, 0,
                                                                    header.w, line)
                                         : LV_RES_INV;
        if (open_result == LV_RES_OK) lv_img_decoder_close(&decoder);

        Serial.printf("SD icon: %s size=%u %ux%u format=%u decode=%s\n",
                      icon.sd_path, static_cast<unsigned>(file_size),
                      static_cast<unsigned>(header.w),
                      static_cast<unsigned>(header.h),
                      static_cast<unsigned>(header.cf),
                      line_result == LV_RES_OK ? "ok" : "failed");
    }
}

static void validate_sd_wallpapers(void) {
    static const char *paths[] = {
        "S:/wallpapers/wallpaper1.bin",
        "S:/wallpapers/wallpaper2.bin",
        "S:/wallpapers/wallpaper3.bin"
    };
    static uint8_t line[480 * 4];

    for (const char *path : paths) {
        File file = sd_open_file(path + 2, FILE_READ);
        const uint32_t file_size = file ? static_cast<uint32_t>(file.size()) : 0;
        if (file) file.close();

        lv_img_header_t header = {};
        const lv_res_t info_result = lv_img_decoder_get_info(path, &header);
        if (info_result != LV_RES_OK) {
            Serial.printf("Wallpaper: %s missing/invalid (size=%u)\n", path, file_size);
            continue;
        }

        lv_img_decoder_dsc_t decoder = {};
        const lv_res_t open_result = lv_img_decoder_open(&decoder, path, lv_color_black(), 0);
        const lv_res_t first_line = open_result == LV_RES_OK
                                        ? lv_img_decoder_read_line(&decoder, 0, 0, header.w, line)
                                        : LV_RES_INV;
        const lv_res_t last_line = open_result == LV_RES_OK
                                       ? lv_img_decoder_read_line(&decoder, 0, header.h - 1, header.w, line)
                                       : LV_RES_INV;
        if (open_result == LV_RES_OK) lv_img_decoder_close(&decoder);
        Serial.printf("Wallpaper: %s size=%u %ux%u format=%u decode=%s\n",
                      path, file_size, header.w, header.h, header.cf,
                      first_line == LV_RES_OK && last_line == LV_RES_OK ? "ok" : "failed");
    }
}

void sdcar_init_fonts(void) {
    if (!sd_available) {
        Serial.println("Chinese fonts: SD card unavailable");
        return;
    }

    lv_fs_drv_init(&sd_lvgl_fs_drv);
    sd_lvgl_fs_drv.letter = 'S';
    sd_lvgl_fs_drv.ready_cb = lvgl_sd_ready;
    sd_lvgl_fs_drv.open_cb = lvgl_sd_open;
    sd_lvgl_fs_drv.close_cb = lvgl_sd_close;
    sd_lvgl_fs_drv.read_cb = lvgl_sd_read;
    sd_lvgl_fs_drv.seek_cb = lvgl_sd_seek;
    sd_lvgl_fs_drv.tell_cb = lvgl_sd_tell;
    lv_fs_drv_register(&sd_lvgl_fs_drv);

    validate_sd_app_icons();
    validate_sd_wallpapers();

    chinese_font_16 = load_sd_font("/fonts/chinese_16.bin",
                                   "S:/fonts/chinese_16.bin", 0x6674);
    chinese_font_20 = load_sd_font("/fonts/chinese_20.bin",
                                   "S:/fonts/chinese_20.bin", 0x8BBE);
    if (chinese_font_20 && !validate_chinese20_key_glyphs(chinese_font_20)) {
        lv_font_free(chinese_font_20);
        chinese_font_20 = nullptr;
    }
    if (!chinese_font_20) {
        chinese_font_20 = const_cast<lv_font_t *>(&lv_font_chinese_20_builtin);
        Serial.println("Chinese font 20: using firmware fallback");
    }
    chinese_font_32 = load_sd_font("/fonts/chinese_32.bin",
                                   "S:/fonts/chinese_32.bin", 0x5357);
    load_sd_app_icons();
    load_sd_pinyin_dict();
    log_lvgl_heap("after fonts");
}

bool sdcar_init() {
    sd_available = false;
    Serial.println("SD_MMC: starting 1-bit initialization");
    Serial.flush();
    release_lcd_spi_pins_for_sd();
    // GPIO42 is the board's unused DAT2/TF control net in 1-bit mode.
    // Keep it high as done by the reference example.
    pinMode(SD_DAT2, OUTPUT);
    digitalWrite(SD_DAT2, HIGH);
    // The board should have external 10k pull-ups to 3.3 V. Do not force
    // these pins with Arduino GPIO modes before SDMMC takes ownership.
    delay(100);

    if (!SD_MMC.setPins(SD_SCK, SD_MOSI, SD_MISO)) {
        Serial.println("SD_MMC: setPins failed (CLK 48, CMD 47, D0 41)");
        set_sd_status(false, nullptr, 0, 0, 0);
        return false;
    }

    // The board is wired for 1-bit SDMMC. Try the 40 MHz high-speed mode
    // first, then fall back to the conservative clock if the card/wiring
    // cannot initialize reliably at that speed.
    const int sd_frequencies[] = {SDMMC_FREQ_HIGHSPEED, SDMMC_FREQ_DEFAULT};
    bool mounted = false;
    int selected_frequency = SDMMC_FREQ_DEFAULT;
    for (const int frequency : sd_frequencies) {
        Serial.printf("SD_MMC: trying 1-bit %s mode\n",
                      frequency == SDMMC_FREQ_HIGHSPEED ? "high-speed" : "default");
        if (SD_MMC.begin("/root", true, false, frequency)) {
            const sdcard_type_t candidate_type = SD_MMC.cardType();
            File candidate_root = sd_open_file("/");
            const bool root_ok = candidate_root && candidate_root.isDirectory();
            if (candidate_root) candidate_root.close();
            if (candidate_type != CARD_NONE && root_ok) {
                mounted = true;
                selected_frequency = frequency;
                break;
            }
            SD_MMC.end();
        }
        if (frequency == SDMMC_FREQ_HIGHSPEED) {
            Serial.println("SD_MMC: high-speed initialization failed, retrying default");
        }
    }

    if (!mounted) {
        Serial.println("SD_MMC: mount failed (CLK 48, CMD 47, D0 41)");
        set_sd_status(false, nullptr, 0, 0, 0);
        return false;
    }

    const sdcard_type_t type = SD_MMC.cardType();
    if (type == CARD_NONE) {
        Serial.println("SD_MMC: no card detected");
        SD_MMC.end();
        set_sd_status(false, nullptr, 0, 0, 0);
        return false;
    }

    const char *type_name = "UNKNOWN";
    if (type == CARD_MMC) type_name = "MMC";
    else if (type == CARD_SD) type_name = "SDSC";
    else if (type == CARD_SDHC) type_name = "SDHC";

    Serial.printf("SD_MMC: mounted, type=%s, bus=%s, size=%llu MB, used=%llu MB\n",
                  type_name,
                  selected_frequency == SDMMC_FREQ_HIGHSPEED ? "40MHz" : "default",
                  SD_MMC.cardSize() / (1024ULL * 1024ULL),
                  SD_MMC.usedBytes() / (1024ULL * 1024ULL));
    File root = sd_open_file("/");
    if (!root || !root.isDirectory()) {
        Serial.println("SD_MMC: mount reported success, but root directory is unavailable");
        SD_MMC.end();
        set_sd_status(false, nullptr, 0, 0, 0);
        return false;
    }
    uint32_t file_count = 0;
    while (File entry = root.openNextFile()) {
        file_count++;
        entry.close();
    }
    set_sd_status(true, type_name, SD_MMC.cardSize(), SD_MMC.usedBytes(), file_count);
    Serial.println("SD_MMC: root directory test passed");
    root.close();
    sd_available = true;
    Serial.println("SD_MMC: initialization complete");
    return true;
}


static constexpr uint8_t MUSIC_MAX_TRACKS = 32;
static constexpr uint8_t MUSIC_NAME_LENGTH = 64;
static char music_paths[MUSIC_MAX_TRACKS][MUSIC_NAME_LENGTH] = {};
static char music_names[MUSIC_MAX_TRACKS][MUSIC_NAME_LENGTH] = {};
static uint8_t music_track_count = 0;
static uint8_t current_lyric_track_index = 0;
static File music_audio_file;
static File alarm_audio_file;
struct MusicLyricEntry { uint32_t time_ms; char text[96]; };
static MusicLyricEntry music_lyrics[160];
static size_t music_lyric_count = 0;
static int32_t music_lyric_offset_ms = 0;


static bool music_is_wav_name(const char *name) {
    if (!name) return false;
    const char *dot = strrchr(name, '.');
    return dot && strcasecmp(dot, ".wav") == 0;
}

uint8_t sdcar_get_music_track_count(void) { return music_track_count; }

const char *sdcar_get_music_track_name(uint8_t index) {
    if (index >= music_track_count) return "";
    return music_names[index];
}

const char *sdcar_get_music_track_path(uint8_t index) {
    if (index >= music_track_count) return nullptr;
    return music_paths[index];
}

void sdcar_scan_music_tracks(void) {
    music_track_count = 0;
    if (!sd_available) return;
    File directory = sd_open_file("/music");
    if (!directory || !directory.isDirectory()) {
        Serial.println("Music: /music directory not found");
        return;
    }
    File entry = directory.openNextFile();
    while (entry && music_track_count < MUSIC_MAX_TRACKS) {
        if (!entry.isDirectory() && music_is_wav_name(entry.name())) {
            const char *entry_name = entry.name();
            if (entry_name[0] == '/') {
                strlcpy(music_paths[music_track_count], entry_name, MUSIC_NAME_LENGTH);
            } else {
                snprintf(music_paths[music_track_count], MUSIC_NAME_LENGTH,
                         "/music/%s", entry_name);
            }
            const char *slash = strrchr(entry.name(), '/');
            strlcpy(music_names[music_track_count], slash ? slash + 1 : entry.name(),
                    MUSIC_NAME_LENGTH);
            Serial.printf("Music: track[%u] %s\n", (unsigned)music_track_count,
                          music_paths[music_track_count]);
            ++music_track_count;
        }
        entry.close();
        entry = directory.openNextFile();
    }
    directory.close();
    if (music_track_count == 0) {
        Serial.println("Music: no WAV files in /music");
    } else {
        Serial.printf("Music: found %u WAV track(s)\n", (unsigned)music_track_count);
    }
}

static void load_music_lyrics(uint8_t track_index)
{
    current_lyric_track_index = track_index;
    music_lyric_count = 0;
    music_lyric_offset_ms = 0;
    char lyric_path[MUSIC_NAME_LENGTH];
    strlcpy(lyric_path, music_paths[track_index], sizeof(lyric_path));
    char *dot = strrchr(lyric_path, '.');
    if (dot) strlcpy(dot, ".lrc", sizeof(lyric_path) - (dot - lyric_path));
    File file = sd_open_file(lyric_path, FILE_READ);
    if (!file) {
        Serial.printf("Music: lyric file %s not found\n", lyric_path);
        return;
    }

    char line[160];
    while (file.available() && music_lyric_count <
           sizeof(music_lyrics) / sizeof(music_lyrics[0])) {
        const size_t length = file.readBytesUntil('\n', line, sizeof(line) - 1);
        line[length] = '\0';
        if (strncmp(line, "[offset:", 8) == 0) {
            music_lyric_offset_ms = (int32_t)strtol(line + 8, nullptr, 10);
            continue;
        }
        char *close = strchr(line, ']');
        if (!close || line[0] != '[' || line[3] != ':') continue;
        const int minutes = atoi(line + 1);
        const int seconds = atoi(line + 4);
        char *fraction = strchr(line + 6, '.');
        if (!fraction || fraction >= close || minutes < 0 || seconds < 0 || seconds > 59) continue;
        const char *fraction_start = fraction + 1;
        const size_t fraction_length = (size_t)(close - fraction_start);
        if (fraction_length == 0 || fraction_length > 3) continue;
        int fraction_value = atoi(fraction_start);
        int milliseconds = fraction_value;
        if (fraction_length == 1) milliseconds *= 100;
        else if (fraction_length == 2) milliseconds *= 10;
        MusicLyricEntry &entry = music_lyrics[music_lyric_count++];
        entry.time_ms = (uint32_t)(minutes * 60000 + seconds * 1000 + milliseconds);
        strlcpy(entry.text, close + 1, sizeof(entry.text));
        while (entry.text[0] == ' ' || entry.text[0] == '\t') {
            memmove(entry.text, entry.text + 1, strlen(entry.text));
        }
        const size_t text_length = strlen(entry.text);
        if (text_length && entry.text[text_length - 1] == '\r') entry.text[text_length - 1] = '\0';
    }
    file.close();
    Serial.printf("Music: loaded %u lyric line(s)\n", (unsigned)music_lyric_count);
}

const char *get_music_lyric(uint32_t position_ms)
{
    static char no_lyrics[96];
    static const char waiting[] = "♪";
    if (music_lyric_count == 0) {
        if (music_track_count && current_lyric_track_index < music_track_count) {
            snprintf(no_lyrics, sizeof(no_lyrics), "无歌词：%s",
                     music_names[current_lyric_track_index]);
        } else {
            strlcpy(no_lyrics, "音乐目录为空", sizeof(no_lyrics));
        }
        return no_lyrics;
    }
    const int64_t adjusted_position = (int64_t)position_ms + music_lyric_offset_ms;
    const uint32_t lookup_position = adjusted_position > 0 ?
        (uint32_t)adjusted_position : 0;
    const char *current = waiting;
    for (size_t i = 0; i < music_lyric_count; ++i) {
        if (lookup_position < music_lyrics[i].time_ms) break;
        current = music_lyrics[i].text;
    }
    return current;
}


bool sdcar_open_music_wav(uint8_t track_index) {
    sdcar_close_music();
    if (!sd_available) return false;
    if (music_track_count == 0) sdcar_scan_music_tracks();
    if (track_index >= music_track_count) return false;

    music_audio_file = sd_open_file(music_paths[track_index], FILE_READ);
    if (!music_audio_file) {
        Serial.printf("Music: %s not found\n", music_paths[track_index]);
        return false;
    }
    music_audio_file.setBufferSize(32 * 1024);
    uint8_t wav_header[44];
    if (music_audio_file.read(wav_header, sizeof(wav_header)) != sizeof(wav_header) ||
        memcmp(wav_header, "RIFF", 4) != 0 ||
        memcmp(wav_header + 8, "WAVE", 4) != 0 ||
        memcmp(wav_header + 12, "fmt ", 4) != 0 ||
        wav_header[20] != 1 || wav_header[21] != 0 ||
        wav_header[22] != 2 || wav_header[23] != 0) {
        Serial.println("Music: invalid PCM WAV header");
        sdcar_close_music();
        return false;
    }
    music_audio_file.seek(44);
    load_music_lyrics(track_index);
    Serial.printf("Music: opened %s (%u bytes)\n", music_paths[track_index],
                  static_cast<unsigned>(music_audio_file.size()));
    return true;
}

bool sdcar_is_ready(void) { return sd_available; }

bool sdcar_music_file_is_open(void) { return static_cast<bool>(music_audio_file); }

size_t sdcar_read_music(uint8_t *buffer, size_t capacity) {
    return music_audio_file ? music_audio_file.read(buffer, capacity) : 0;
}

void sdcar_close_music(void) {
    if (music_audio_file) music_audio_file.close();
}

bool sdcar_open_alarm_wav(void) {
    sdcar_close_alarm();
    if (!sd_available) return false;
    alarm_audio_file = sd_open_file("/music/alarm.wav", FILE_READ);
    if (!alarm_audio_file) {
        Serial.println("Alarm: /music/alarm.wav not found");
        return false;
    }
    alarm_audio_file.setBufferSize(16 * 1024);
    uint8_t header[44];
    if (alarm_audio_file.read(header, sizeof(header)) != sizeof(header) ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0 ||
        memcmp(header + 12, "fmt ", 4) != 0 || header[20] != 1 || header[21] != 0 ||
        header[22] != 2 || header[23] != 0) {
        Serial.println("Alarm: invalid PCM WAV header");
        sdcar_close_alarm();
        return false;
    }
    alarm_audio_file.seek(44);
    Serial.printf("Alarm: opened /music/alarm.wav (%u bytes)\n",
                  static_cast<unsigned>(alarm_audio_file.size()));
    return true;
}

bool sdcar_alarm_file_is_open(void) { return static_cast<bool>(alarm_audio_file); }

size_t sdcar_read_alarm(uint8_t *buffer, size_t capacity) {
    return alarm_audio_file ? alarm_audio_file.read(buffer, capacity) : 0;
}

void sdcar_close_alarm(void) {
    if (alarm_audio_file) alarm_audio_file.close();
}

void sdcar_publish_status(void) {
    update_sd_status(sd_status_mounted, sd_status_type, sd_status_size,
                     sd_status_used, sd_status_file_count);
}
