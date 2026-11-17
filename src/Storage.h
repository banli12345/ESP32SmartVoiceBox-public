#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Logical paths keep the LVGL "S:" form or a card-root form such as
 * "/music/a.wav". VFS paths always start with the mount root ("/sdcard"
 * on device). An existing mount prefix is not applied twice. */

#ifdef __cplusplus
#ifndef STORAGE_HOST_TEST
extern "C" {
#endif
#endif

bool storage_vfs_path(const char *logical, char *out, size_t out_size);
bool storage_exists(const char *logical);
bool storage_is_file(const char *logical);
bool storage_is_dir(const char *logical);
bool storage_remove(const char *logical);
bool storage_rename(const char *from_logical, const char *to_logical);
bool storage_mkdir(const char *logical);
uint64_t storage_free_bytes(void);
bool storage_nvs_get_u8(const char *ns, const char *key, uint8_t *value);
bool storage_nvs_set_u8(const char *ns, const char *key, uint8_t value);

/* 1-bit SDMMC at /sdcard. Tries 40 MHz, then 20 MHz. Does not format. */
bool storage_mount_sdmmc(const char **type_name, uint64_t *total_bytes,
                         uint64_t *used_bytes, uint32_t *root_entries,
                         uint32_t *frequency_khz);

#ifdef STORAGE_HOST_TEST
struct StorageHostCrash {};
struct StorageHostHooks {
    int crash_after_rename;
    int rename_count;
    const char *fail_rename_dest;
    bool crash_backup_remove;
    bool partial_done;
    int flush_count;
    int fail_flush_at;
    const char *root;
};
extern StorageHostHooks storage_host_hooks;
void storage_host_set_root(const char *root);
#endif

#ifdef __cplusplus
#ifndef STORAGE_HOST_TEST
}
#endif

/* Owns one FILE*. Directory handles are StorageDirectory, not this type. */
class StorageStream {
public:
    StorageStream() = default;
    ~StorageStream();
    StorageStream(StorageStream &&other) noexcept;
    StorageStream &operator=(StorageStream &&other) noexcept;
    StorageStream(const StorageStream &) = delete;
    StorageStream &operator=(const StorageStream &) = delete;

    bool open(const char *logical, const char *mode);
    void close();
    bool active() const { return file_ != nullptr; }
    size_t read(void *dst, size_t length);
    size_t write(const void *src, size_t length);
    bool seek(uint64_t offset);
    uint64_t position() const;
    uint64_t byte_count() const;
    bool flush();
    void use_buffer(size_t bytes);
    int read_byte();

private:
    void release() noexcept;
    FILE *file_ = nullptr;
    char *buffer_ = nullptr;
};

class StorageDirectory {
public:
    StorageDirectory() = default;
    ~StorageDirectory();
    StorageDirectory(const StorageDirectory &) = delete;
    StorageDirectory &operator=(const StorageDirectory &) = delete;

    bool open(const char *logical);
    void close();
    bool read_next(char *name, size_t name_size, bool *is_directory);

private:
    void *state_ = nullptr;
};
#endif
