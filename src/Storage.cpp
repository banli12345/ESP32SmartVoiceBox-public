#include "Storage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <new>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <windows.h>
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif
#else
#include <dirent.h>
#include <unistd.h>
#endif

#ifndef STORAGE_HOST_TEST
#include "IdfRuntime.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "nvs.h"
#include "sd_protocol_defs.h"
#include "sdmmc_cmd.h"
#endif

namespace {

constexpr size_t kPathCap = 512;

#ifdef STORAGE_HOST_TEST
const char *storage_root(void)
{
    return storage_host_hooks.root && storage_host_hooks.root[0]
               ? storage_host_hooks.root : ".";
}
#else
const char *storage_root(void)
{
    return "/sdcard";
}
#endif

bool join_root(const char *suffix, char *out, size_t out_size)
{
    const char *root = storage_root();
    const int wrote = (suffix && suffix[0])
                          ? snprintf(out, out_size, "%s%s", root, suffix)
                          : snprintf(out, out_size, "%s", root);
    return wrote > 0 && static_cast<size_t>(wrote) < out_size;
}

bool stat_full(const char *full, struct stat *info)
{
    return full && info && stat(full, info) == 0;
}

#if defined(_WIN32)
bool make_one_dir(const char *full)
{
    return _mkdir(full) == 0 || errno == EEXIST;
}
#else
bool make_one_dir(const char *full)
{
    return mkdir(full, 0775) == 0 || errno == EEXIST;
}
#endif

void stream_seek(FILE *file, uint64_t offset, int whence)
{
#if defined(_WIN32)
    _fseeki64(file, static_cast<__int64>(offset), whence);
#else
    fseeko(file, static_cast<off_t>(offset), whence);
#endif
}

uint64_t stream_tell(FILE *file)
{
#if defined(_WIN32)
    const __int64 at = _ftelli64(file);
#else
    const off_t at = ftello(file);
#endif
    return at < 0 ? 0 : static_cast<uint64_t>(at);
}

struct DirState {
#if defined(_WIN32)
    HANDLE handle = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAA data = {};
    bool pending = false;
#else
    DIR *dir = nullptr;
#endif
    char path[kPathCap] = {};
};

#ifndef STORAGE_HOST_TEST
sdmmc_card_t *sd_card = nullptr;

void release_lcd_pins_for_sd(void)
{
    gpio_reset_pin(GPIO_NUM_47);
    gpio_reset_pin(GPIO_NUM_48);
    gpio_set_direction(GPIO_NUM_47, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_47, GPIO_PULLUP_ONLY);
    gpio_set_direction(GPIO_NUM_48, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_48, GPIO_PULLUP_ONLY);
    app_delay_ms(2);

    gpio_reset_pin(GPIO_NUM_42);
    gpio_set_direction(GPIO_NUM_42, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_42, 1);

    gpio_set_direction(GPIO_NUM_39, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_39, 1);
    app_delay_ms(100);
}

void unmount_sd(void)
{
    if (!sd_card) return;
    esp_vfs_fat_sdcard_unmount("/sdcard", sd_card);
    sd_card = nullptr;
}

bool fat_space(uint64_t *total, uint64_t *free_bytes)
{
    FATFS *fs = nullptr;
    DWORD free_clusters = 0;
    if (f_getfree("0:", &free_clusters, &fs) != FR_OK || !fs) return false;
#if FF_MAX_SS != FF_MIN_SS
    const uint32_t sector = fs->ssize;
#else
    const uint32_t sector = FF_MAX_SS;
#endif
    const uint64_t total_sectors =
        static_cast<uint64_t>(fs->n_fatent - 2) * fs->csize;
    const uint64_t free_sectors = static_cast<uint64_t>(free_clusters) * fs->csize;
    if (total) *total = total_sectors * sector;
    if (free_bytes) *free_bytes = free_sectors * sector;
    return true;
}

const char *card_type_name(const sdmmc_card_t *card)
{
    if (!card) return "UNKNOWN";
    if (card->is_mmc) return "MMC";
    if (card->ocr & SD_OCR_SDHC_CAP) return "SDHC";
    return "SDSC";
}

uint32_t count_root_entries(void)
{
    StorageDirectory root;
    if (!root.open("/")) return 0;
    uint32_t count = 0;
    char name[256];
    bool is_dir = false;
    while (root.read_next(name, sizeof(name), &is_dir)) ++count;
    return count;
}

bool mount_once(int frequency_khz)
{
    esp_vfs_fat_mount_config_t mount = {};
    mount.format_if_mount_failed = false;
    mount.max_files = 8;
    mount.allocation_unit_size = 0;
    mount.disk_status_check_enable = false;
    mount.use_one_fat = false;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_DEINIT_ARG;
    host.max_freq_khz = frequency_khz;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = GPIO_NUM_48;
    slot.cmd = GPIO_NUM_47;
    slot.d0 = GPIO_NUM_41;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    sdmmc_card_t *card = nullptr;
    const esp_err_t err = esp_vfs_fat_sdmmc_mount(
        "/sdcard", &host, &slot, &mount, &card);
    if (err != ESP_OK || !card) {
        if (card) esp_vfs_fat_sdcard_unmount("/sdcard", card);
        return false;
    }
    sd_card = card;
    DIR *root = opendir("/sdcard");
    if (!root) {
        unmount_sd();
        return false;
    }
    closedir(root);
    return true;
}
#endif

#ifdef STORAGE_HOST_TEST
bool ends_with(const char *text, const char *suffix)
{
    if (!text || !suffix) return false;
    const size_t text_len = strlen(text);
    const size_t suffix_len = strlen(suffix);
    return text_len >= suffix_len &&
           memcmp(text + text_len - suffix_len, suffix, suffix_len) == 0;
}
#endif

}  // namespace

#ifdef STORAGE_HOST_TEST
StorageHostHooks storage_host_hooks = {};

void storage_host_set_root(const char *root)
{
    storage_host_hooks.root = root;
}
#endif

bool storage_vfs_path(const char *logical, char *out, size_t out_size)
{
    if (!logical || !out || out_size < 2) return false;
    const char *path = logical;
    if (path[0] == 'S' && path[1] == ':') path += 2;
    if (!path[0]) return join_root("", out, out_size);

    const char *root = storage_root();
    const size_t root_len = strlen(root);
    if (strncmp(path, root, root_len) == 0 &&
        (path[root_len] == '\0' || path[root_len] == '/' || path[root_len] == '\\')) {
        return snprintf(out, out_size, "%s", path) > 0 &&
               strlen(path) < out_size;
    }
    if (path[0] == '/' || path[0] == '\\') return join_root(path, out, out_size);
    const int wrote = snprintf(out, out_size, "%s/%s", root, path);
    return wrote > 0 && static_cast<size_t>(wrote) < out_size;
}

bool storage_exists(const char *logical)
{
    char full[kPathCap];
    struct stat info;
    return storage_vfs_path(logical, full, sizeof(full)) && stat_full(full, &info);
}

bool storage_is_file(const char *logical)
{
    char full[kPathCap];
    struct stat info;
    return storage_vfs_path(logical, full, sizeof(full)) && stat_full(full, &info) &&
           S_ISREG(info.st_mode);
}

bool storage_is_dir(const char *logical)
{
    char full[kPathCap];
    struct stat info;
    return storage_vfs_path(logical, full, sizeof(full)) && stat_full(full, &info) &&
           S_ISDIR(info.st_mode);
}

bool storage_remove(const char *logical)
{
#ifdef STORAGE_HOST_TEST
    if (storage_host_hooks.crash_backup_remove && ends_with(logical, ".sdup-bak")) {
        storage_host_hooks.crash_backup_remove = false;
        throw StorageHostCrash();
    }
#endif
    char full[kPathCap];
    if (!storage_vfs_path(logical, full, sizeof(full))) return false;
    if (!storage_exists(logical)) return true;
    return ::remove(full) == 0;
}

bool storage_rename(const char *from_logical, const char *to_logical)
{
#ifdef STORAGE_HOST_TEST
    if (storage_host_hooks.fail_rename_dest && to_logical &&
        strcmp(to_logical, storage_host_hooks.fail_rename_dest) == 0) {
        storage_host_hooks.fail_rename_dest = nullptr;
        return false;
    }
#endif
    char from_path[kPathCap];
    char to_path[kPathCap];
    if (!storage_vfs_path(from_logical, from_path, sizeof(from_path)) ||
        !storage_vfs_path(to_logical, to_path, sizeof(to_path))) {
        return false;
    }
    if (::rename(from_path, to_path) != 0) return false;
#ifdef STORAGE_HOST_TEST
    ++storage_host_hooks.rename_count;
    if (storage_host_hooks.crash_after_rename &&
        storage_host_hooks.rename_count == storage_host_hooks.crash_after_rename) {
        storage_host_hooks.crash_after_rename = 0;
        throw StorageHostCrash();
    }
#endif
    return true;
}

bool storage_mkdir(const char *logical)
{
    if (!logical || !logical[0]) return false;
    if (storage_is_dir(logical)) return true;
    char full[kPathCap];
    if (!storage_vfs_path(logical, full, sizeof(full))) return false;
    return make_one_dir(full);
}

uint64_t storage_free_bytes(void)
{
#ifdef STORAGE_HOST_TEST
    return 1024ull * 1024ull * 1024ull;
#else
    uint64_t free_bytes = 0;
    if (!sd_card || !fat_space(nullptr, &free_bytes)) return 0;
    return free_bytes;
#endif
}

bool storage_nvs_get_u8(const char *ns, const char *key, uint8_t *value)
{
#ifdef STORAGE_HOST_TEST
    (void)ns;
    (void)key;
    (void)value;
    return false;
#else
    if (!ns || !key || !value) return false;
    nvs_handle_t handle;
    if (nvs_open(ns, NVS_READONLY, &handle) != ESP_OK) return false;
    const esp_err_t err = nvs_get_u8(handle, key, value);
    nvs_close(handle);
    return err == ESP_OK;
#endif
}

bool storage_nvs_set_u8(const char *ns, const char *key, uint8_t value)
{
#ifdef STORAGE_HOST_TEST
    (void)ns;
    (void)key;
    (void)value;
    return false;
#else
    if (!ns || !key) return false;
    nvs_handle_t handle;
    if (nvs_open(ns, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(handle, key, value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
#endif
}

bool storage_mount_sdmmc(const char **type_name, uint64_t *total_bytes,
                         uint64_t *used_bytes, uint32_t *root_entries,
                         uint32_t *frequency_khz)
{
#ifdef STORAGE_HOST_TEST
    (void)type_name;
    (void)total_bytes;
    (void)used_bytes;
    (void)root_entries;
    (void)frequency_khz;
    return false;
#else
    if (type_name) *type_name = nullptr;
    if (total_bytes) *total_bytes = 0;
    if (used_bytes) *used_bytes = 0;
    if (root_entries) *root_entries = 0;
    if (frequency_khz) *frequency_khz = 0;

    release_lcd_pins_for_sd();
    const int frequencies[] = {SDMMC_FREQ_HIGHSPEED, SDMMC_FREQ_DEFAULT};
    bool mounted = false;
    int selected = SDMMC_FREQ_DEFAULT;
    for (int frequency : frequencies) {
        printf("SD_MMC: trying 1-bit %s mode\n",
               frequency == SDMMC_FREQ_HIGHSPEED ? "high-speed" : "default");
        fflush(stdout);
        if (mount_once(frequency)) {
            mounted = true;
            selected = frequency;
            break;
        }
        if (frequency == SDMMC_FREQ_HIGHSPEED) {
            puts("SD_MMC: high-speed initialization failed, retrying default");
        }
    }
    if (!mounted || !sd_card) {
        puts("SD_MMC: mount failed (CLK 48, CMD 47, D0 41)");
        unmount_sd();
        return false;
    }

    const char *name = card_type_name(sd_card);
    uint64_t card_bytes = static_cast<uint64_t>(sd_card->csd.capacity) *
                          static_cast<uint64_t>(sd_card->csd.sector_size);
    uint64_t fat_total = 0;
    uint64_t fat_free = 0;
    if (!fat_space(&fat_total, &fat_free)) fat_free = 0;
    if (card_bytes == 0) card_bytes = fat_total;
    const uint64_t used = card_bytes > fat_free ? card_bytes - fat_free : 0;
    const uint32_t entries = count_root_entries();
    if (type_name) *type_name = name;
    if (total_bytes) *total_bytes = card_bytes;
    if (used_bytes) *used_bytes = used;
    if (root_entries) *root_entries = entries;
    if (frequency_khz) *frequency_khz = static_cast<uint32_t>(selected);
    return true;
#endif
}

void StorageStream::release() noexcept
{
    if (file_) fclose(file_);
    delete[] buffer_;
    file_ = nullptr;
    buffer_ = nullptr;
}

StorageStream::~StorageStream()
{
    release();
}

StorageStream::StorageStream(StorageStream &&other) noexcept
    : file_(other.file_), buffer_(other.buffer_)
{
    other.file_ = nullptr;
    other.buffer_ = nullptr;
}

StorageStream &StorageStream::operator=(StorageStream &&other) noexcept
{
    if (this != &other) {
        release();
        file_ = other.file_;
        buffer_ = other.buffer_;
        other.file_ = nullptr;
        other.buffer_ = nullptr;
    }
    return *this;
}

bool StorageStream::open(const char *logical, const char *mode)
{
    close();
    if (!logical || !mode) return false;
    char full[kPathCap];
    if (!storage_vfs_path(logical, full, sizeof(full))) return false;
    struct stat info;
    if (stat_full(full, &info) && S_ISDIR(info.st_mode)) return false;
    const bool creating = strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+');
    if (!creating && (!stat_full(full, &info) || !S_ISREG(info.st_mode))) return false;
    file_ = fopen(full, mode);
    return file_ != nullptr;
}

void StorageStream::close()
{
    release();
}

size_t StorageStream::read(void *dst, size_t length)
{
    if (!file_ || !dst || !length) return 0;
    return fread(dst, 1, length, file_);
}

size_t StorageStream::write(const void *src, size_t length)
{
    if (!file_ || !src || !length) return 0;
#ifdef STORAGE_HOST_TEST
    if (storage_host_hooks.partial_done && length == 4 &&
        memcmp(src, "DONE", 4) == 0) {
        storage_host_hooks.partial_done = false;
        length = 2;
    }
#endif
    return fwrite(src, 1, length, file_);
}

bool StorageStream::seek(uint64_t offset)
{
    if (!file_) return false;
#if defined(_WIN32)
    return _fseeki64(file_, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    return fseeko(file_, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

uint64_t StorageStream::position() const
{
    if (!file_) return 0;
    return stream_tell(file_);
}

uint64_t StorageStream::byte_count() const
{
    if (!file_) return 0;
    const uint64_t at = stream_tell(file_);
    stream_seek(file_, 0, SEEK_END);
    const uint64_t size = stream_tell(file_);
    stream_seek(file_, at, SEEK_SET);
    return size;
}

bool StorageStream::flush()
{
    if (!file_ || fflush(file_) != 0) return false;
#ifdef STORAGE_HOST_TEST
    ++storage_host_hooks.flush_count;
    if (storage_host_hooks.fail_flush_at > 0 &&
        storage_host_hooks.flush_count == storage_host_hooks.fail_flush_at) return false;
#endif
#if defined(_WIN32)
    return _commit(_fileno(file_)) == 0;
#else
    return fsync(fileno(file_)) == 0;
#endif
}

void StorageStream::use_buffer(size_t bytes)
{
    if (!file_ || bytes < 128 || buffer_) return;
    char *buffer = new (std::nothrow) char[bytes];
    if (!buffer) return;
    if (setvbuf(file_, buffer, _IOFBF, bytes) != 0) delete[] buffer;
    else buffer_ = buffer;
}

int StorageStream::read_byte()
{
    if (!file_) return -1;
    return fgetc(file_);
}

StorageDirectory::~StorageDirectory()
{
    close();
}

bool StorageDirectory::open(const char *logical)
{
    close();
    if (!storage_is_dir(logical)) return false;
    char full[kPathCap];
    if (!storage_vfs_path(logical, full, sizeof(full))) return false;
    DirState *state = new (std::nothrow) DirState();
    if (!state) return false;
    snprintf(state->path, sizeof(state->path), "%s", full);
#if defined(_WIN32)
    char pattern[kPathCap];
    snprintf(pattern, sizeof(pattern), "%s\\*", full);
    state->handle = FindFirstFileA(pattern, &state->data);
    if (state->handle == INVALID_HANDLE_VALUE) {
        delete state;
        return false;
    }
    state->pending = true;
#else
    state->dir = opendir(full);
    if (!state->dir) {
        delete state;
        return false;
    }
#endif
    state_ = state;
    return true;
}

void StorageDirectory::close()
{
    DirState *state = static_cast<DirState *>(state_);
    if (!state) return;
#if defined(_WIN32)
    if (state->handle != INVALID_HANDLE_VALUE) FindClose(state->handle);
#else
    if (state->dir) closedir(state->dir);
#endif
    delete state;
    state_ = nullptr;
}

bool StorageDirectory::read_next(char *name, size_t name_size, bool *is_directory)
{
    DirState *state = static_cast<DirState *>(state_);
    if (!state || !name || name_size < 2) return false;
    for (;;) {
        const char *entry_name = nullptr;
#if defined(_WIN32)
        if (!state->pending) {
            if (!FindNextFileA(state->handle, &state->data)) return false;
        }
        state->pending = false;
        entry_name = state->data.cFileName;
#else
        errno = 0;
        dirent *entry = readdir(state->dir);
        if (!entry) return false;
        entry_name = entry->d_name;
#endif
        if (!entry_name || !entry_name[0]) continue;
        if (strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0) continue;
        if (snprintf(name, name_size, "%s", entry_name) < 0 ||
            strlen(entry_name) >= name_size) {
            continue;
        }
        char child[kPathCap];
        const int child_len = snprintf(child, sizeof(child), "%s/%s", state->path, name);
        if (child_len < 0 || static_cast<size_t>(child_len) >= sizeof(child)) continue;
        struct stat info;
        const bool know = stat_full(child, &info);
        if (is_directory) *is_directory = know && S_ISDIR(info.st_mode);
        return true;
    }
}
