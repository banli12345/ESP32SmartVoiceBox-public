#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <strings.h>

#ifndef STORAGE_HOST_TEST
#include "IdfRuntime.h"
#else
#include "host_time.h"
#endif
#include "SDcar.h"
#include "SerialUpload.h"
#include "Storage.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "serial_upload_protocol.h"

namespace {
constexpr uint32_t LOG_BAUD = 115200;
constexpr uint32_t UPLOAD_BAUD = 921600;
constexpr unsigned MAX_ENTRIES = SDUP_MAX_FILES * 2;
constexpr const char *JOURNAL = "/.sdup-journal";
constexpr const char *JOURNAL_NEW = "/.sdup-journal-new";
constexpr uart_port_t UPLOAD_UART = UART_NUM_0;
RTC_DATA_ATTR uint32_t upload_latch = 0;
constexpr uint32_t LATCH = 0x53445531;

struct Entry {
    char path[SDUP_PATH_MAX + 1];
    uint32_t size, crc, received, running_crc;
    bool verified, remove_only, had_original;
};
Entry *entries = nullptr;
unsigned entry_count = 0, file_count = 0;
int open_index = -1;
StorageStream current;
uint8_t *buffer = nullptr;
bool staged = false;

int discard_upload_log(const char *, va_list)
{
    return 0;
}

void uart_write_all(const void *data, size_t length)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    size_t sent = 0;
    while (sent < length) {
        const int wrote = uart_write_bytes(UPLOAD_UART, bytes + sent, length - sent);
        if (wrote <= 0) break;
        sent += static_cast<size_t>(wrote);
    }
}

void auxiliary_path(const char *path, const char *suffix, char out[96])
{
    snprintf(out, 96, "%s%s", path, suffix);
}

bool is_file(const char *path)
{
    return storage_is_file(path);
}

bool remove_if_exists(const char *path)
{
    return !storage_exists(path) || storage_remove(path);
}

void close_current()
{
    if (current.active()) {
        current.flush();
        current.close();
    }
    open_index = -1;
}

void reset_entries()
{
    close_current();
    memset(entries, 0, MAX_ENTRIES * sizeof(Entry));
    file_count = entry_count = 0;
    staged = false;
}

bool discard_staging()
{
    close_current();
    bool ok = true;
    for (unsigned i = 0; i < entry_count; ++i) {
        char path[96];
        auxiliary_path(entries[i].path, ".sdup-part", path);
        if (!remove_if_exists(path)) ok = false;
    }
    return ok;
}

bool recover_journal()
{
    StorageStream journal;
    if (!journal.open(JOURNAL, "rb")) return remove_if_exists(JOURNAL_NEW);
    const uint64_t size64 = journal.byte_count();
    if (size64 < 12 || size64 > MAX_ENTRIES * (SDUP_PATH_MAX + 2) + 16) {
        journal.close();
        return false;
    }
    const size_t size = static_cast<size_t>(size64);
    if (journal.read(buffer, size) != size) {
        journal.close();
        return false;
    }
    journal.close();
    if (memcmp(buffer, "SDJ1", 4) || sdup_u16(buffer + 6)) return false;
    unsigned count = sdup_u16(buffer + 4);
    if (!count || count > MAX_ENTRIES) return false;
    size_t at = 8;
    for (unsigned i = 0; i < count; ++i) {
        if (at + 2 > size) return false;
        unsigned flags = buffer[at++];
        unsigned len = buffer[at++];
        if (flags > 3 || !len || len > SDUP_PATH_MAX || at + len + 4 > size) return false;
        Entry &e = entries[i];
        memset(&e, 0, sizeof(e));
        memcpy(e.path, buffer + at, len);
        at += len;
        if (!sdup_valid_path(e.path)) return false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcasecmp(entries[j].path, e.path)) return false;
        e.had_original = flags & 1;
        e.remove_only = flags & 2;
    }
    if (size < at + 4 || size > at + 8) return false;
    if (sdup_u32(buffer + at) != sdup_crc32(0, buffer, at)) return false;
    bool committed = at + 8 == size && !memcmp(buffer + at + 4, "DONE", 4);
    if (at + 8 == size && !committed) return false;
    for (unsigned i = 0; i < count; ++i) {
        Entry &e = entries[i];
        char backup[96], part[96];
        auxiliary_path(e.path, ".sdup-bak", backup);
        auxiliary_path(e.path, ".sdup-part", part);
        if (committed) {
            if (!remove_if_exists(backup)) return false;
        } else if (e.had_original && storage_exists(backup)) {
            if (!remove_if_exists(e.path) || !storage_rename(backup, e.path)) return false;
        } else if (!e.had_original) {
            if (!remove_if_exists(e.path)) return false;
        } else if (!is_file(e.path)) {
            return false;
        }
        if (!remove_if_exists(part)) return false;
    }
    return remove_if_exists(JOURNAL) && remove_if_exists(JOURNAL_NEW);
}

void response(uint8_t op, uint32_t seq, const void *payload = nullptr, uint16_t length = 0)
{
    uint8_t header[SDUP_HEADER_SIZE] = {'S', 'D', 'U', '1', op, 0, 0, 0, 0, 0, 0, 0};
    uint8_t trailer[4];
    sdup_put_u16(header + 6, length);
    sdup_put_u32(header + 8, seq);
    uint32_t crc = sdup_crc32(0, header + 4, 8);
    crc = sdup_crc32(crc, payload, length);
    sdup_put_u32(trailer, crc);
    uart_write_all(header, sizeof(header));
    if (length) uart_write_all(payload, length);
    uart_write_all(trailer, 4);
}

void error(uint32_t seq, const char *message)
{
    response(SDUP_ERROR, seq, message, static_cast<uint16_t>(strlen(message)));
}

bool read_exact(uint8_t *dst, size_t length, uint32_t timeout_ms = 5000)
{
    uint32_t last = app_millis();
    size_t got = 0;
    while (got < length) {
        const int n = uart_read_bytes(UPLOAD_UART, dst + got,
                                      static_cast<uint32_t>(length - got), 0);
        if (n > 0) {
            got += static_cast<size_t>(n);
            last = app_millis();
        } else {
            app_delay_ms(1);
        }
        if (app_millis() - last > timeout_ms) return false;
    }
    return true;
}

bool install_journal()
{
    size_t at = 8;
    memcpy(buffer, "SDJ1", 4);
    sdup_put_u16(buffer + 4, static_cast<uint16_t>(entry_count));
    sdup_put_u16(buffer + 6, 0);
    for (unsigned i = 0; i < entry_count; ++i) {
        Entry &e = entries[i];
        e.had_original = is_file(e.path);
        if (storage_exists(e.path) && !e.had_original) return false;
        unsigned len = static_cast<unsigned>(strlen(e.path));
        buffer[at++] = static_cast<uint8_t>((e.had_original ? 1 : 0) | (e.remove_only ? 2 : 0));
        buffer[at++] = static_cast<uint8_t>(len);
        memcpy(buffer + at, e.path, len);
        at += len;
        char backup[96];
        auxiliary_path(e.path, ".sdup-bak", backup);
        if (storage_exists(backup)) return false;
    }
    sdup_put_u32(buffer + at, sdup_crc32(0, buffer, at));
    at += 4;
    if (!remove_if_exists(JOURNAL_NEW)) return false;
    StorageStream journal;
    if (!journal.open(JOURNAL_NEW, "wb")) return false;
    const bool ok = journal.write(buffer, at) == at && journal.flush();
    journal.close();
    return ok && storage_rename(JOURNAL_NEW, JOURNAL);
}

const char *begin_batch(const uint8_t *payload, uint16_t len)
{
    if (staged) return "batch already active";
    if (len < 2) return "invalid manifest";
    unsigned count = sdup_u16(payload);
    size_t at = 2;
    if (!count || count > SDUP_MAX_FILES) return "too many files";
    reset_entries();
    uint64_t total = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (at + 10 > len) {
            reset_entries();
            return "invalid manifest";
        }
        unsigned path_len = sdup_u16(payload + at);
        at += 2;
        Entry &e = entries[i];
        e.size = sdup_u32(payload + at);
        e.crc = sdup_u32(payload + at + 4);
        at += 8;
        if (!path_len || path_len > SDUP_PATH_MAX || at + path_len > len || e.size > 0x7fffffffu) {
            reset_entries();
            return "invalid manifest";
        }
        memcpy(e.path, payload + at, path_len);
        at += path_len;
        if (memchr(e.path, 0, path_len) || !sdup_valid_path(e.path)) {
            reset_entries();
            return "invalid SD path";
        }
        for (unsigned j = 0; j < i; ++j) {
            if (!strcasecmp(e.path, entries[j].path)) {
                reset_entries();
                return "duplicate target path";
            }
        }
        total += e.size;
    }
    if (at != len) {
        reset_entries();
        return "invalid manifest length";
    }
    file_count = entry_count = count;
    for (unsigned i = 0; i < file_count; ++i) {
        const char *ext = strrchr(entries[i].path, '.');
        if (!ext || strcmp(ext, ".rvf")) continue;
        char wav[SDUP_PATH_MAX + 1];
        strcpy(wav, entries[i].path);
        strcpy(strrchr(wav, '.'), ".wav");
        bool paired = false;
        for (unsigned j = 0; j < file_count; ++j)
            if (!strcasecmp(entries[j].path, wav)) paired = true;
        if (!paired && storage_exists(wav)) {
            Entry &e = entries[entry_count++];
            strcpy(e.path, wav);
            e.remove_only = e.verified = true;
        }
    }
    if (total > storage_free_bytes()) {
        reset_entries();
        return "not enough free SD space for staging";
    }
    for (unsigned i = 0; i < entry_count; ++i) {
        Entry &e = entries[i];
        char dir[SDUP_PATH_MAX + 1];
        strcpy(dir, e.path);
        *strrchr(dir, '/') = 0;
        if (dir[0] && !storage_exists(dir) && !storage_mkdir(dir)) {
            reset_entries();
            return "cannot create SD directory";
        }
        char part[96], backup[96];
        auxiliary_path(e.path, ".sdup-part", part);
        auxiliary_path(e.path, ".sdup-bak", backup);
        if (storage_exists(backup)) {
            reset_entries();
            return "unresolved backup; recovery required";
        }
        if (!remove_if_exists(part)) {
            reset_entries();
            return "cannot clear old staging file";
        }
    }
    staged = true;
    return nullptr;
}

const char *write_chunk(const uint8_t *payload, uint16_t len)
{
    if (!staged || len < 6) return "no active file batch";
    unsigned index = sdup_u16(payload);
    uint32_t offset = sdup_u32(payload + 2);
    if (index >= file_count) return "invalid file index";
    Entry &e = entries[index];
    size_t bytes = len - 6;
    if (e.verified || offset != e.received || bytes > e.size - e.received || !bytes)
        return "invalid file offset";
    if (open_index != static_cast<int>(index)) {
        close_current();
        char part[96];
        auxiliary_path(e.path, ".sdup-part", part);
        if (!current.open(part, e.received ? "ab" : "wb")) return "cannot open staging file";
        current.use_buffer(4096);
        open_index = static_cast<int>(index);
    }
    if (current.write(payload + 6, bytes) != bytes) return "SD write failed";
    e.running_crc = sdup_crc32(e.running_crc, payload + 6, bytes);
    e.received += static_cast<uint32_t>(bytes);
    return nullptr;
}

const char *finish_file(const uint8_t *payload, uint16_t len, uint32_t seq)
{
    if (!staged || len != 2) return "invalid finish request";
    unsigned index = sdup_u16(payload);
    if (index >= file_count) return "invalid file index";
    Entry &e = entries[index];
    if (e.verified) return nullptr;
    if (e.received != e.size || e.running_crc != e.crc) return "file size or checksum mismatch";
    close_current();
    char part[96];
    auxiliary_path(e.path, ".sdup-part", part);
    if (!e.size) {
        StorageStream empty;
        if (!empty.open(part, "wb")) return "cannot create empty file";
        empty.close();
    }
    StorageStream staged_file;
    if (!staged_file.open(part, "rb") || staged_file.byte_count() != e.size) {
        staged_file.close();
        return "SD file size mismatch";
    }
    uint32_t crc = 0;
    uint32_t done = 0;
    uint32_t last_progress = app_millis();
    while (done < e.size) {
        size_t n = e.size - done;
        if (n > SDUP_CHUNK_SIZE) n = SDUP_CHUNK_SIZE;
        const size_t read = staged_file.read(buffer, n);
        if (read != n) {
            staged_file.close();
            return "SD verification read failed";
        }
        crc = sdup_crc32(crc, buffer, n);
        done += static_cast<uint32_t>(n);
        if (app_millis() - last_progress > 500) {
            uint8_t progress[4];
            sdup_put_u32(progress, done);
            response(SDUP_PROGRESS, seq, progress, 4);
            last_progress = app_millis();
        }
        app_delay_ms(1);
    }
    staged_file.close();
    if (crc != e.crc) return "SD verification checksum mismatch";
    e.verified = true;
    return nullptr;
}

const char *commit_batch()
{
    if (!staged) return "no active batch";
    for (unsigned i = 0; i < entry_count; ++i)
        if (!entries[i].verified) return "batch has unverified files";
    close_current();
    if (!install_journal()) return "cannot write commit journal";
    bool ok = true;
    for (unsigned i = 0; i < entry_count && ok; ++i) {
        Entry &e = entries[i];
        char part[96], backup[96];
        auxiliary_path(e.path, ".sdup-part", part);
        auxiliary_path(e.path, ".sdup-bak", backup);
        if (e.had_original) ok = storage_rename(e.path, backup);
        if (ok && !e.remove_only) ok = storage_rename(part, e.path);
    }
    if (ok) {
        StorageStream journal;
        ok = journal.open(JOURNAL, "ab") && journal.write("DONE", 4) == 4;
        if (journal.active()) {
            if (!journal.flush()) {
                journal.close();
                return "commit sync failed; backups retained for recovery";
            }
            journal.close();
        }
    }
    if (!recover_journal()) return "commit recovery failed; original backups retained";
    staged = false;
    if (!ok) return "commit failed; original files restored";
    reset_entries();
    return nullptr;
}

bool entry_command()
{
    static char line[48];
    static unsigned used = 0;
    uint8_t ch = 0;
    while (uart_read_bytes(UPLOAD_UART, &ch, 1, 0) == 1) {
        if (ch == '\n') {
            line[used] = 0;
            used = 0;
            if (!strcmp(line, "@SDUP1 HELLO")) return true;
        } else if (ch != '\r') {
            if (used + 1 < sizeof(line)) line[used++] = static_cast<char>(ch);
            else used = 0;
        }
    }
    return false;
}

void reboot()
{
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(200));
    app_delay_ms(200);
    uart_set_baudrate(UPLOAD_UART, LOG_BAUD);
    upload_latch = 0;
    esp_restart();
}

void receive_session()
{
    esp_log_level_set("*", ESP_LOG_NONE);
    esp_log_set_vprintf(discard_upload_log);
    printf("@SDUP1 READY 1 921600\n");
    fflush(stdout);
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(200));
    app_delay_ms(80);
    uart_set_baudrate(UPLOAD_UART, UPLOAD_BAUD);
    uint32_t last_activity = app_millis();
    uint32_t last_sequence = 0;
    uint32_t last_crc = 0;
    uint8_t last_opcode = 0;
    bool have_last = false;
    char last_error[160] = {0};
    unsigned matched = 0;
    while (app_millis() - last_activity < 30000) {
        uint8_t ch = 0;
        if (uart_read_bytes(UPLOAD_UART, &ch, 1, 0) != 1) {
            app_delay_ms(1);
            continue;
        }
        const char *magic = "SDU1";
        if (ch != static_cast<uint8_t>(magic[matched])) {
            matched = ch == 'S' ? 1 : 0;
            continue;
        }
        if (++matched < 4) continue;
        matched = 0;
        uint8_t header[SDUP_HEADER_SIZE] = {'S', 'D', 'U', '1'};
        uint8_t trailer[4];
        uint8_t op = 0;
        uint16_t len = 0;
        uint32_t seq = 0;
        if (!read_exact(header + 4, 8) || !sdup_decode_header(header, &op, &len, &seq)) continue;
        if (!read_exact(buffer, len) || !read_exact(trailer, 4)) continue;
        uint32_t crc = sdup_crc32(sdup_crc32(0, header + 4, 8), buffer, len);
        if (crc != sdup_u32(trailer)) {
            error(seq, "retry: frame checksum mismatch");
            continue;
        }
        last_activity = app_millis();
        if (op == SDUP_ABORT || op == SDUP_EXIT) {
            bool ok = !storage_exists(JOURNAL) || recover_journal();
            if (ok && staged) ok = discard_staging();
            if (ok) response(SDUP_ACK, seq);
            else error(seq, "staging cleanup failed; backups retained");
            reboot();
        }
        if (have_last && seq == last_sequence) {
            if (op != last_opcode || crc != last_crc) error(seq, "sequence reused with different request");
            else if (*last_error) error(seq, last_error);
            else response(SDUP_ACK, seq);
            continue;
        }
        if (have_last && seq != last_sequence + 1) {
            error(seq, "unexpected request sequence");
            continue;
        }
        const char *failure = nullptr;
        switch (op) {
        case SDUP_PING:
            if (len) failure = "invalid ping";
            break;
        case SDUP_BEGIN:
            failure = begin_batch(buffer, len);
            break;
        case SDUP_DATA:
            failure = write_chunk(buffer, len);
            break;
        case SDUP_FINISH:
            failure = finish_file(buffer, len, seq);
            break;
        case SDUP_COMMIT:
            failure = len ? "invalid commit" : commit_batch();
            break;
        default:
            failure = "unknown command";
            break;
        }
        have_last = true;
        last_sequence = seq;
        last_crc = crc;
        last_opcode = op;
        snprintf(last_error, sizeof(last_error), "%s", failure ? failure : "");
        if (failure) error(seq, failure);
        else response(SDUP_ACK, seq);
        last_activity = app_millis();
    }
    if (storage_exists(JOURNAL)) recover_journal();
    if (staged) discard_staging();
    reboot();
}
}  // namespace

void serial_upload_boot_window()
{
    bool requested = upload_latch == LATCH;
    upload_latch = 0;
    if (!sdcar_is_ready()) {
        if (requested) puts("@SDUP1 ERROR SD card unavailable");
        return;
    }
    buffer = static_cast<uint8_t *>(
        heap_caps_malloc(SDUP_MAX_PAYLOAD, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    entries = static_cast<Entry *>(
        heap_caps_calloc(MAX_ENTRIES, sizeof(Entry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer || !entries) {
        if (buffer) heap_caps_free(buffer);
        if (entries) heap_caps_free(entries);
        buffer = nullptr;
        entries = nullptr;
        puts("@SDUP1 ERROR upload allocation failed");
        return;
    }
    if (!recover_journal()) {
        puts("@SDUP1 ERROR upload journal recovery failed; backups retained");
        heap_caps_free(buffer);
        heap_caps_free(entries);
        buffer = nullptr;
        entries = nullptr;
        return;
    }
    reset_entries();
    const uint32_t started = app_millis();
    while (!requested && app_millis() - started < 400) {
        requested = entry_command();
        if (!requested) app_delay_ms(2);
    }
    if (requested) receive_session();
    heap_caps_free(buffer);
    heap_caps_free(entries);
    buffer = nullptr;
    entries = nullptr;
}

void serial_upload_poll()
{
    if (!entry_command()) return;
    puts("@SDUP1 REBOOTING");
    fflush(stdout);
    uart_wait_tx_done(UPLOAD_UART, pdMS_TO_TICKS(200));
    upload_latch = LATCH;
    app_delay_ms(100);
    esp_restart();
}
