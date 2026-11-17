#define SDCAR_H
inline bool sdcar_is_ready() { return true; }
#include "../src/SerialUpload.cpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <stdexcept>

static unsigned checks = 0, failures = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::cerr << "FAIL line " << __LINE__ << '\n'; } } while (0)
using Bytes = std::vector<uint8_t>;
static std::string root_store;

static void check_fixture_root()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::absolute(root_store).lexically_normal();
    fs::path temporary = fs::absolute(fs::temp_directory_path()).lexically_normal();
    if (temporary.filename().empty()) temporary = temporary.parent_path();
    if (root.parent_path() != temporary ||
        root.filename().string().rfind("sdup-receiver-native-", 0) != 0)
        throw std::runtime_error("Unsafe receiver test fixture path");
}

static void reset_tree()
{
    namespace fs = std::filesystem;
    check_fixture_root();
    fs::remove_all(root_store);
    fs::create_directories(root_store);
    storage_host_set_root(root_store.c_str());
    storage_mkdir("/videos");
}

static void initialize()
{
    const char *root = storage_host_hooks.root;
    storage_host_hooks = {};
    storage_host_hooks.root = root;
    host_milliseconds = 0;
    host_uart = {};
    reset_tree();
    if (!buffer) buffer = static_cast<uint8_t *>(std::malloc(SDUP_MAX_PAYLOAD));
    if (!entries) entries = static_cast<Entry *>(std::calloc(MAX_ENTRIES, sizeof(Entry)));
    reset_entries();
}

static Bytes bytes(const char *text) { return Bytes(text, text + std::strlen(text)); }
static void u16(Bytes &payload, uint16_t value)
{
    payload.push_back(static_cast<uint8_t>(value & 255));
    payload.push_back(static_cast<uint8_t>(value >> 8));
}
static void u32(Bytes &payload, uint32_t value)
{
    for (int i = 0; i < 4; ++i) payload.push_back(static_cast<uint8_t>((value >> (i * 8)) & 255));
}
static Bytes manifest()
{
    Bytes payload;
    u16(payload, 2);
    for (const char *path : {"/videos/a.rvf", "/videos/a.wav"}) {
        u16(payload, static_cast<uint16_t>(std::strlen(path)));
        u32(payload, 3);
        u32(payload, sdup_crc32(0, "new", 3));
        payload.insert(payload.end(), path, path + std::strlen(path));
    }
    return payload;
}
static Bytes data(unsigned index)
{
    Bytes payload;
    u16(payload, static_cast<uint16_t>(index));
    u32(payload, 0);
    auto body = bytes("new");
    payload.insert(payload.end(), body.begin(), body.end());
    return payload;
}
static Bytes index_of(unsigned index)
{
    Bytes payload;
    u16(payload, static_cast<uint16_t>(index));
    return payload;
}
static void request(unsigned op, unsigned seq, const Bytes &payload = {})
{
    Bytes frame = {'S', 'D', 'U', '1', static_cast<uint8_t>(op), 0};
    u16(frame, static_cast<uint16_t>(payload.size()));
    u32(frame, static_cast<uint32_t>(seq));
    frame.insert(frame.end(), payload.begin(), payload.end());
    u32(frame, sdup_crc32(0, frame.data() + 4, frame.size() - 4));
    host_uart.rx.insert(host_uart.rx.end(), frame.begin(), frame.end());
}
static void write_text(const char *logical, const char *text)
{
    StorageStream file;
    CHECK(file.open(logical, "wb"));
    CHECK(file.write(text, std::strlen(text)) == std::strlen(text));
}
static void old_files()
{
    write_text("/videos/a.rvf", "old-video");
    write_text("/videos/a.wav", "old-audio");
}
static Bytes read_text(const char *logical)
{
    StorageStream file;
    if (!file.open(logical, "rb")) return {};
    Bytes out(static_cast<size_t>(file.byte_count()));
    if (!out.empty()) CHECK(file.read(out.data(), out.size()) == out.size());
    return out;
}
static int count_files()
{
    int count = 0;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root_store))
        if (entry.is_regular_file()) ++count;
    return count;
}
static void prepare_pair()
{
    auto listing = manifest();
    CHECK(!begin_batch(listing.data(), static_cast<uint16_t>(listing.size())));
    for (unsigned i = 0; i < 2; ++i) {
        auto chunk = data(i);
        CHECK(!write_chunk(chunk.data(), static_cast<uint16_t>(chunk.size())));
        auto name = index_of(i);
        CHECK(!finish_file(name.data(), static_cast<uint16_t>(name.size()), i + 1));
    }
}
static void originals()
{
    CHECK(read_text("/videos/a.rvf") == bytes("old-video"));
    CHECK(read_text("/videos/a.wav") == bytes("old-audio"));
}

int main() try
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    root_store = (std::filesystem::temp_directory_path() /
                  ("sdup-receiver-native-" + std::to_string(unique))).string();
    storage_host_set_root(root_store.c_str());
    initialize();
    old_files();
    request(SDUP_PING, 1);
    request(SDUP_BEGIN, 2, manifest());
    request(SDUP_DATA, 3, data(0));
    request(SDUP_DATA, 3, data(0));
    request(SDUP_FINISH, 4, index_of(0));
    request(SDUP_DATA, 5, data(1));
    request(SDUP_FINISH, 6, index_of(1));
    request(SDUP_COMMIT, 7);
    request(SDUP_COMMIT, 7);
    request(SDUP_EXIT, 8);
    try { receive_session(); CHECK(false); } catch (Reboot &) {}
    CHECK(count_files() == 2);
    CHECK(read_text("/videos/a.rvf") == bytes("new"));
    CHECK(read_text("/videos/a.wav") == bytes("new"));

    initialize();
    old_files();
    request(SDUP_BEGIN, 1, manifest());
    request(SDUP_DATA, 2, data(0));
    request(SDUP_ABORT, 0);
    try { receive_session(); CHECK(false); } catch (Reboot &) {}
    originals();
    CHECK(count_files() == 2);

    initialize();
    old_files();
    auto listing = manifest();
    CHECK(!begin_batch(listing.data(), static_cast<uint16_t>(listing.size())));
    auto chunk = data(0);
    CHECK(!write_chunk(chunk.data(), static_cast<uint16_t>(chunk.size())));
    close_current();
    {
        StorageStream part;
        CHECK(part.open("/videos/a.rvf.sdup-part", "rb+"));
        uint8_t first = 0;
        CHECK(part.read(&first, 1) == 1);
        CHECK(part.seek(0));
        first ^= 1;
        CHECK(part.write(&first, 1) == 1);
    }
    auto name = index_of(0);
    CHECK(finish_file(name.data(), static_cast<uint16_t>(name.size()), 1) != nullptr);
    originals();

    initialize();
    old_files();
    prepare_pair();
    storage_host_hooks.fail_rename_dest = "/videos/a.wav";
    CHECK(commit_batch() != nullptr);
    originals();
    CHECK(!storage_exists(JOURNAL));

    for (int step = 1; step <= 4; ++step) {
        initialize();
        old_files();
        prepare_pair();
        storage_host_hooks.crash_after_rename = storage_host_hooks.rename_count + step + 1;
        bool crashed = false;
        try { commit_batch(); } catch (StorageHostCrash &) { crashed = true; }
        CHECK(crashed);
        CHECK(recover_journal());
        originals();
        CHECK(count_files() == 2);
    }

    initialize();
    old_files();
    prepare_pair();
    storage_host_hooks.partial_done = true;
    CHECK(commit_batch() != nullptr);
    originals();
    CHECK(count_files() == 2);

    initialize();
    old_files();
    prepare_pair();
    storage_host_hooks.crash_backup_remove = true;
    bool crashed = false;
    try { commit_batch(); } catch (StorageHostCrash &) { crashed = true; }
    CHECK(crashed);
    CHECK(recover_journal());
    CHECK(count_files() == 2);
    CHECK(read_text("/videos/a.rvf") == bytes("new"));
    CHECK(read_text("/videos/a.wav") == bytes("new"));

    initialize();
    old_files();
    Bytes single;
    u16(single, 1);
    const char *path = "/videos/a.rvf";
    u16(single, static_cast<uint16_t>(std::strlen(path)));
    u32(single, 3);
    u32(single, sdup_crc32(0, "new", 3));
    single.insert(single.end(), path, path + std::strlen(path));
    CHECK(!begin_batch(single.data(), static_cast<uint16_t>(single.size())));
    chunk = data(0);
    CHECK(!write_chunk(chunk.data(), static_cast<uint16_t>(chunk.size())));
    name = index_of(0);
    CHECK(!finish_file(name.data(), static_cast<uint16_t>(name.size()), 1));
    CHECK(!commit_batch());
    CHECK(read_text("/videos/a.rvf") == bytes("new"));
    CHECK(!storage_exists("/videos/a.wav"));

    initialize(); old_files(); prepare_pair();
    storage_host_hooks.fail_flush_at = storage_host_hooks.flush_count + 1;
    CHECK(commit_batch() != nullptr);
    originals(); // Journal could not sync: no original was moved.

    initialize(); old_files(); prepare_pair();
    storage_host_hooks.fail_flush_at = storage_host_hooks.flush_count + 2;
    CHECK(commit_batch() != nullptr);
    CHECK(storage_exists("/videos/a.rvf.sdup-bak"));
    CHECK(storage_exists("/videos/a.wav.sdup-bak"));
    CHECK(!read_text("/videos/a.rvf").empty());
    storage_host_hooks.fail_flush_at = 0;
    CHECK(recover_journal());
    CHECK(read_text("/videos/a.rvf") == bytes("new"));
    CHECK(read_text("/videos/a.wav") == bytes("new"));

    std::cout << "receiver: " << checks << " checks, " << failures << " failures\n";
    std::free(buffer);
    std::free(entries);
    buffer = nullptr;
    entries = nullptr;
    check_fixture_root();
    std::filesystem::remove_all(root_store);
    return failures ? 1 : 0;
}
catch (const std::exception &error) {
    std::cerr << "receiver fixture error: " << error.what() << '\n';
    return 1;
}
catch (...) {
    std::cerr << "unexpected receiver test exception\n";
    return 1;
}
