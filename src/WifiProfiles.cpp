#include "WifiProfiles.h"

#include <cstring>

namespace {

constexpr uint32_t kMagic = 0x31504657u; /* 'WPF1' little-endian */
constexpr uint8_t kVersion = 1;
constexpr size_t kHeader = 8;
constexpr size_t kSsidField = 33;
constexpr size_t kPasswordField = 64;
constexpr size_t kRecord = 1 + 1 + kSsidField + kPasswordField;
static_assert(kHeader + WIFI_PROFILE_CAPACITY * kRecord == WIFI_PROFILE_BLOB_BYTES, "profile blob size");

bool terminated_field(const char *field, size_t field_size, uint8_t length)
{
    if (!field || length >= field_size) return false;
    if (field[length] != '\0') return false;
    if (std::strlen(field) != length) return false;
    for (size_t i = static_cast<size_t>(length) + 1; i < field_size; ++i) {
        if (field[i] != '\0') return false;
    }
    return true;
}

void write_field(char *field, size_t field_size, const char *value, size_t length)
{
    std::memset(field, 0, field_size);
    if (value && length) std::memcpy(field, value, length);
}

bool decode_record(const uint8_t *raw, wifi_profile_t *out)
{
    const uint8_t ssid_len = raw[0];
    const uint8_t password_len = raw[1];
    const char *ssid = reinterpret_cast<const char *>(raw + 2);
    const char *password = ssid + kSsidField;
    if (ssid_len == 0 || ssid_len > WIFI_PROFILE_SSID_MAX || password_len > WIFI_PROFILE_PASSWORD_MAX) return false;
    if (!terminated_field(ssid, kSsidField, ssid_len)) return false;
    if (!terminated_field(password, kPasswordField, password_len)) return false;
    std::memset(out, 0, sizeof(*out));
    out->ssid_len = ssid_len;
    out->password_len = password_len;
    std::memcpy(out->ssid, ssid, ssid_len);
    if (password_len) std::memcpy(out->password, password, password_len);
    return true;
}

void encode_record(uint8_t *raw, const wifi_profile_t *item)
{
    std::memset(raw, 0, kRecord);
    raw[0] = item->ssid_len;
    raw[1] = item->password_len;
    write_field(reinterpret_cast<char *>(raw + 2), kSsidField, item->ssid, item->ssid_len);
    write_field(reinterpret_cast<char *>(raw + 2 + kSsidField), kPasswordField, item->password, item->password_len);
}

bool same_ssid(const char *a, size_t a_len, const char *b, size_t b_len)
{
    return a_len == b_len && a_len > 0 && a_len <= WIFI_PROFILE_SSID_MAX &&
           std::memcmp(a, b, a_len) == 0;
}

bool same_secret(const wifi_profile_t *item, const char *password, size_t password_len)
{
    return item->password_len == password_len &&
           (password_len == 0 || std::memcmp(item->password, password, password_len) == 0);
}

void wipe_queue_secret(wifi_request_queue_t *queue)
{
    if (!queue) return;
    std::memset(queue->password, 0, sizeof(queue->password));
    queue->password_len = 0;
}

} // namespace

extern "C" bool wifi_profile_credentials_ok(const char *ssid, size_t ssid_len, const char *password, size_t password_len)
{
    if (!ssid || !password) return false;
    if (ssid_len == 0 || ssid_len > WIFI_PROFILE_SSID_MAX || password_len > WIFI_PROFILE_PASSWORD_MAX) return false;
    if (std::strlen(ssid) != ssid_len || std::strlen(password) != password_len) return false;
    return true;
}

extern "C" void wifi_profile_store_clear(wifi_profile_store_t *store)
{
    if (store) std::memset(store, 0, sizeof(*store));
}

extern "C" int wifi_profile_find(const wifi_profile_store_t *store, const char *ssid, size_t ssid_len)
{
    if (!store || !ssid) return -1;
    const int count = store->count;
    if (count < 0 || count > WIFI_PROFILE_CAPACITY) return -1;
    for (int i = 0; i < count; ++i) {
        if (same_ssid(store->items[i].ssid, store->items[i].ssid_len, ssid, ssid_len)) return i;
    }
    return -1;
}

extern "C" bool wifi_profile_note_success(wifi_profile_store_t *store, const char *ssid, size_t ssid_len,
                                         const char *password, size_t password_len, bool *changed)
{
    if (changed) *changed = false;
    if (!store || !wifi_profile_credentials_ok(ssid, ssid_len, password, password_len)) return false;
    if (store->count > WIFI_PROFILE_CAPACITY) return false;
    const int found = wifi_profile_find(store, ssid, ssid_len);
    if (found == 0 && same_secret(&store->items[0], password, password_len)) return true;

    wifi_profile_t kept[WIFI_PROFILE_CAPACITY];
    std::memset(kept, 0, sizeof(kept));
    kept[0].ssid_len = static_cast<uint8_t>(ssid_len);
    kept[0].password_len = static_cast<uint8_t>(password_len);
    std::memcpy(kept[0].ssid, ssid, ssid_len);
    if (password_len) std::memcpy(kept[0].password, password, password_len);
    int count = 1;
    for (uint8_t i = 0; i < store->count; ++i) {
        if (found >= 0 && i == found) continue;
        if (count >= WIFI_PROFILE_CAPACITY) break;
        kept[count++] = store->items[i];
    }
    std::memset(store, 0, sizeof(*store));
    store->count = static_cast<uint8_t>(count);
    for (int i = 0; i < count; ++i) store->items[i] = kept[i];
    if (changed) *changed = true;
    return true;
}

extern "C" wifi_profiles_status_t wifi_profiles_load(wifi_profile_store_t *store, const wifi_nvs_io_t *io)
{
    if (!store || !io || !io->open_ns || !io->close_ns || !io->get_blob) return WIFI_PROFILES_IO_ERROR;
    const wifi_profile_store_t previous = *store;
    const int opened = io->open_ns(io->ctx, 0);
    if (opened == WIFI_NVS_NOT_FOUND) {
        wifi_profile_store_clear(store);
        return WIFI_PROFILES_NOT_FOUND;
    }
    if (opened != WIFI_NVS_OK) return WIFI_PROFILES_IO_ERROR;

    uint8_t blob[WIFI_PROFILE_BLOB_BYTES];
    size_t length = sizeof(blob);
    const int blob_status = io->get_blob(io->ctx, "profiles", blob, &length);
    if (blob_status == WIFI_NVS_ERR) {
        io->close_ns(io->ctx);
        return WIFI_PROFILES_IO_ERROR;
    }
    if (blob_status == WIFI_NVS_OK) {
        wifi_profile_store_t decoded;
        std::memset(&decoded, 0, sizeof(decoded));
        bool ok = length == WIFI_PROFILE_BLOB_BYTES;
        if (ok) {
            const uint32_t magic = static_cast<uint32_t>(blob[0]) |
                                   (static_cast<uint32_t>(blob[1]) << 8) |
                                   (static_cast<uint32_t>(blob[2]) << 16) |
                                   (static_cast<uint32_t>(blob[3]) << 24);
            const uint8_t version = blob[4];
            const uint8_t count = blob[5];
            const uint16_t flags = static_cast<uint16_t>(blob[6] | (blob[7] << 8));
            ok = magic == kMagic && version == kVersion && flags == 0 && count <= WIFI_PROFILE_CAPACITY;
            if (ok) {
                decoded.count = count;
                for (uint8_t i = 0; i < WIFI_PROFILE_CAPACITY; ++i) {
                    const uint8_t *raw = blob + kHeader + static_cast<size_t>(i) * kRecord;
                    if (i < count) {
                        if (!decode_record(raw, &decoded.items[i])) ok = false;
                    } else {
                        for (size_t b = 0; b < kRecord; ++b) if (raw[b] != 0) ok = false;
                    }
                }
            }
            if (ok) {
                for (uint8_t i = 0; i < decoded.count; ++i) {
                    for (uint8_t j = static_cast<uint8_t>(i + 1); j < decoded.count; ++j) {
                        if (same_ssid(decoded.items[i].ssid, decoded.items[i].ssid_len,
                                      decoded.items[j].ssid, decoded.items[j].ssid_len)) ok = false;
                    }
                }
            }
        }
        io->close_ns(io->ctx);
        if (!ok) {
            *store = previous;
            return WIFI_PROFILES_REJECTED;
        }
        *store = decoded;
        return WIFI_PROFILES_LOADED;
    }

    char ssid[33];
    char password[64];
    std::memset(ssid, 0, sizeof(ssid));
    std::memset(password, 0, sizeof(password));
    size_t ssid_len = sizeof(ssid);
    size_t password_len = sizeof(password);
    const int ssid_status = io->get_str ? io->get_str(io->ctx, "ssid", ssid, &ssid_len) : WIFI_NVS_NOT_FOUND;
    const int password_status = io->get_str ? io->get_str(io->ctx, "password", password, &password_len) : WIFI_NVS_NOT_FOUND;
    io->close_ns(io->ctx);
    if (ssid_status == WIFI_NVS_ERR || password_status == WIFI_NVS_ERR) return WIFI_PROFILES_IO_ERROR;
    if (ssid_status == WIFI_NVS_NOT_FOUND) return WIFI_PROFILES_NOT_FOUND;
    if (password_status == WIFI_NVS_NOT_FOUND || ssid_len == 0 || ssid[ssid_len - 1] != '\0' ||
        password_len == 0 || password[password_len - 1] != '\0') {
        *store = previous;
        return WIFI_PROFILES_REJECTED;
    }
    const size_t name = std::strlen(ssid);
    const size_t secret = std::strlen(password);
    if (name + 1 != ssid_len || secret + 1 != password_len ||
        !wifi_profile_credentials_ok(ssid, name, password, secret)) {
        *store = previous;
        return WIFI_PROFILES_REJECTED;
    }
    wifi_profile_store_clear(store);
    bool changed = false;
    if (!wifi_profile_note_success(store, ssid, name, password, secret, &changed)) {
        *store = previous;
        return WIFI_PROFILES_REJECTED;
    }
    std::memset(password, 0, sizeof(password));
    return WIFI_PROFILES_MIGRATED;
}

extern "C" bool wifi_profiles_store(const wifi_profile_store_t *store, const wifi_nvs_io_t *io)
{
    if (!store || !io || !io->open_ns || !io->close_ns || !io->set_blob || !io->commit) return false;
    if (store->count > WIFI_PROFILE_CAPACITY) return false;
    for (uint8_t i = 0; i < store->count; ++i) {
        if (!wifi_profile_credentials_ok(store->items[i].ssid, store->items[i].ssid_len,
                                         store->items[i].password, store->items[i].password_len)) return false;
        if (wifi_profile_find(store, store->items[i].ssid, store->items[i].ssid_len) != i) return false;
    }
    uint8_t blob[WIFI_PROFILE_BLOB_BYTES];
    std::memset(blob, 0, sizeof(blob));
    blob[0] = static_cast<uint8_t>(kMagic);
    blob[1] = static_cast<uint8_t>(kMagic >> 8);
    blob[2] = static_cast<uint8_t>(kMagic >> 16);
    blob[3] = static_cast<uint8_t>(kMagic >> 24);
    blob[4] = kVersion;
    blob[5] = store->count;
    for (uint8_t i = 0; i < store->count; ++i) {
        encode_record(blob + kHeader + static_cast<size_t>(i) * kRecord, &store->items[i]);
    }
    if (io->open_ns(io->ctx, 1) != WIFI_NVS_OK) return false;
    const int wrote = io->set_blob(io->ctx, "profiles", blob, sizeof(blob));
    const int committed = wrote == WIFI_NVS_OK ? io->commit(io->ctx) : WIFI_NVS_ERR;
    io->close_ns(io->ctx);
    std::memset(blob, 0, sizeof(blob));
    return wrote == WIFI_NVS_OK && committed == WIFI_NVS_OK;
}

extern "C" int wifi_autojoin_pick(const wifi_profile_store_t *store, const wifi_visible_ap_t *visible, int visible_count,
                                 uint8_t failed_mask)
{
    if (!store || !visible || visible_count <= 0) return -1;
    const int count = store->count;
    if (count <= 0 || count > WIFI_PROFILE_CAPACITY) return -1;
    for (int i = 0; i < count; ++i) {
        if (failed_mask & (1u << i)) continue;
        for (int v = 0; v < visible_count; ++v) {
            if (same_ssid(store->items[i].ssid, store->items[i].ssid_len, visible[v].ssid, visible[v].ssid_len)) {
                return i;
            }
        }
    }
    return -1;
}

extern "C" bool wifi_deadline_elapsed(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

extern "C" bool wifi_auto_scan_due(bool radio_on, bool has_ip, bool connect_busy, bool scan_busy, bool have_profiles,
                                  uint32_t now, uint32_t due_at)
{
    if (!radio_on || has_ip || connect_busy || scan_busy || !have_profiles) return false;
    return wifi_deadline_elapsed(now, due_at);
}

extern "C" bool wifi_request_scan(wifi_request_queue_t *queue, bool radio_on)
{
    if (!queue || !radio_on) return false;
    queue->scan_pending = true;
    return true;
}

extern "C" bool wifi_request_connect(wifi_request_queue_t *queue, const char *ssid, const char *password, bool radio_on)
{
    if (!queue || !radio_on || !ssid || !password) return false;
    const size_t ssid_len = std::strlen(ssid);
    const size_t password_len = std::strlen(password);
    if (!wifi_profile_credentials_ok(ssid, ssid_len, password, password_len)) return false;
    wipe_queue_secret(queue);
    queue->connect_pending = true;
    queue->saved_connect = false;
    queue->ssid_len = static_cast<uint8_t>(ssid_len);
    std::memset(queue->ssid, 0, sizeof(queue->ssid));
    std::memcpy(queue->ssid, ssid, ssid_len);
    queue->password_len = static_cast<uint8_t>(password_len);
    if (password_len) std::memcpy(queue->password, password, password_len);
    queue->connect_seq += 1;
    if (queue->connect_seq == 0) queue->connect_seq = 1;
    return true;
}

extern "C" bool wifi_request_saved(wifi_request_queue_t *queue, const char *ssid, bool known, bool radio_on)
{
    if (!queue || !radio_on || !known || !ssid) return false;
    const size_t ssid_len = std::strlen(ssid);
    if (ssid_len == 0 || ssid_len > WIFI_PROFILE_SSID_MAX) return false;
    wipe_queue_secret(queue);
    queue->connect_pending = true;
    queue->saved_connect = true;
    queue->ssid_len = static_cast<uint8_t>(ssid_len);
    std::memset(queue->ssid, 0, sizeof(queue->ssid));
    std::memcpy(queue->ssid, ssid, ssid_len);
    queue->connect_seq += 1;
    if (queue->connect_seq == 0) queue->connect_seq = 1;
    return true;
}

extern "C" void wifi_request_cancel(wifi_request_queue_t *queue)
{
    if (!queue) return;
    wipe_queue_secret(queue);
    std::memset(queue, 0, sizeof(*queue));
}

extern "C" int wifi_radio_mode(bool sta_on, bool ap_on)
{
    if (sta_on && ap_on) return 3;
    if (ap_on) return 2;
    if (sta_on) return 1;
    return 0;
}

extern "C" bool wifi_scan_epoch_current(uint32_t event_epoch, uint32_t active_epoch)
{
    return event_epoch != 0 && event_epoch == active_epoch;
}

extern "C" wifi_attempt_result_t wifi_attempt_on_ip(const wifi_attempt_t *attempt, uint32_t event_epoch,
                                                   const char *connected_ssid, size_t connected_len,
                                                   const wifi_profile_store_t *store)
{
    if (!attempt || !attempt->active || event_epoch != attempt->epoch) return WIFI_ATTEMPT_IGNORED;
    if (!connected_ssid || !same_ssid(attempt->ssid, attempt->ssid_len, connected_ssid, connected_len)) {
        return WIFI_ATTEMPT_IGNORED;
    }
    const int found = store ? wifi_profile_find(store, attempt->ssid, attempt->ssid_len) : -1;
    if (found == 0 && store && same_secret(&store->items[0], attempt->password, attempt->password_len)) {
        return WIFI_ATTEMPT_NO_WRITE;
    }
    return WIFI_ATTEMPT_SAVE;
}

extern "C" wifi_attempt_result_t wifi_attempt_on_disconnect(const wifi_attempt_t *attempt, uint32_t event_epoch)
{
    if (!attempt || !attempt->active || event_epoch != attempt->epoch) return WIFI_ATTEMPT_IGNORED;
    return WIFI_ATTEMPT_FAIL;
}

extern "C" wifi_attempt_result_t wifi_attempt_on_timeout(const wifi_attempt_t *attempt, uint32_t now, uint32_t deadline)
{
    if (!attempt || !attempt->active) return WIFI_ATTEMPT_IGNORED;
    if (!wifi_deadline_elapsed(now, deadline)) return WIFI_ATTEMPT_IGNORED;
    return WIFI_ATTEMPT_FAIL;
}
