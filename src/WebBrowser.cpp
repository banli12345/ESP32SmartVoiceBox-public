#include "WebBrowser.h"
#include "browser_body.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509.h>
#include <time.h>

namespace {
constexpr size_t DOWNLOAD_LIMIT = 256 * 1024;
constexpr uint32_t REQUEST_TIMEOUT_MS = 15000;
constexpr uint32_t WORKER_STACK_BYTES = 10240;
// Request the mobile HTML variant. Some sites return a script-only redirect
// shell to an unknown agent, which has no text for this browser to render.
constexpr char BROWSER_USER_AGENT[] =
    "Mozilla/5.0 (Linux; Android 10) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/120.0 Mobile Safari/537.36 ESP32TextBrowser/1.5";
struct BrowserWork {
    browser_document_t document;
    browser_parser_t parser;
    char url[BROWSER_URL_CAP];
    uint8_t transfer_buffer[512];
};
SemaphoreHandle_t browser_mutex;
BrowserWork *work;
browser_state_t state = BROWSER_IDLE;
bool worker_running;
TaskHandle_t worker_task;
// Reserve a contiguous INTERNAL stack before WiFi fragments the heap. Reuse
// one blocked task instead of allocating/deleting a task for every page.
alignas(16) StackType_t worker_stack[WORKER_STACK_BYTES / sizeof(StackType_t)];
StaticTask_t worker_task_storage;
bool release_pending;
uint32_t revision;
char error_message[160];
std::atomic<bool> cancel_requested{false};
std::atomic<uint32_t> received_bytes{0};

template <class Base> class BrowserClient final : public Base {
public:
    explicit BrowserClient(uint32_t started) : started_(started) {}
    int available() override { return expired() ? 0 : Base::available(); }
    uint8_t connected() override { return expired() ? 0 : Base::connected(); }
private:
    bool expired()
    {
        // HTTPClient can wait for EOF without invoking the output Stream when
        // Content-Length is absent. Bound that wait as well as parsed data.
        if (!cancel_requested.load() && millis() - started_ < REQUEST_TIMEOUT_MS) return false;
        Base::stop();
        return true;
    }
    uint32_t started_;
};

bool ensure_mutex()
{
    // The first call comes from the LVGL task, before any worker exists.
    if (!browser_mutex) browser_mutex = xSemaphoreCreateMutex();
    return browser_mutex != nullptr;
}

size_t page_end(const char *text, size_t length, size_t start)
{
    size_t end = start + BROWSER_PAGE_BYTES;
    if (end >= length) return length;
    while (end > start && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
    return end;
}

class ParserStream final : public Stream {
public:
    explicit ParserStream(BrowserWork *job, uint32_t start) : job_(job), start_(start) {}
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t *bytes, size_t size) override
    {
        if (limited || timed_out || cancel_requested.load() || millis() - start_ >= REQUEST_TIMEOUT_MS) {
            timed_out = timed_out || (!limited && !cancel_requested.load());
            setWriteError();
            return 0;
        }
        const size_t used = received_bytes.load();
        const size_t count = size < DOWNLOAD_LIMIT - used ? size : DOWNLOAD_LIMIT - used;
        browser_parser_feed(&job_->parser, reinterpret_cast<const char *>(bytes), count);
        received_bytes.store(used + count);
        if (count < size || job_->document.truncated) {
            limited = true;
            job_->document.truncated = true;
            setWriteError();
            return count < size ? count : 0;
        }
        vTaskDelay(1);
        return count;
    }
    bool limited = false;
    bool timed_out = false;
private:
    BrowserWork *job_;
    uint32_t start_;
};

size_t parser_write(void *context, const uint8_t *bytes, size_t length)
{
    return static_cast<ParserStream *>(context)->write(bytes, length);
}

int read_body(HTTPClient &http, NetworkClient &client, ParserStream &sink,
              BrowserWork *job, uint32_t started)
{
    String transfer_encoding = http.header("Transfer-Encoding");
    transfer_encoding.toLowerCase();
    transfer_encoding.trim();
    const int length = http.getSize();
    const browser_body_mode_t mode = transfer_encoding == "chunked" ? BROWSER_BODY_CHUNKED :
                                     (length >= 0 ? BROWSER_BODY_FIXED : BROWSER_BODY_CLOSE);
    browser_body_t body;
    browser_body_init(&body, mode, length > 0 ? static_cast<uint64_t>(length) : 0);
    uint32_t last_progress = millis();
    // HTTPClient::writeToStream allocates a 4 KiB internal buffer. That leaves
    // too little DMA memory for AES on larger PSRAM-resident TLS records.
    // Use a bounded buffer inside the existing PSRAM job and decode framing.
    while (body.result == BROWSER_BODY_READING && !sink.limited && !cancel_requested.load()) {
        if (millis() - started >= REQUEST_TIMEOUT_MS) { sink.timed_out = true; break; }
        const int available = client.available();
        if (available > 0) {
            const size_t count = static_cast<size_t>(available) < sizeof(job->transfer_buffer) ?
                                 static_cast<size_t>(available) : sizeof(job->transfer_buffer);
            const int read = client.read(job->transfer_buffer, count);
            if (read > 0) {
                browser_body_feed(&body, job->transfer_buffer, static_cast<size_t>(read), parser_write, &sink);
                last_progress = millis();
                continue;
            }
        }
        if (!client.connected()) { browser_body_eof(&body); break; }
        if (millis() - last_progress >= 5000) { sink.timed_out = true; break; }
        vTaskDelay(1);
    }
    Serial.printf("[BROWSER] body mode=%u result=%u length=%d elapsed_ms=%u internal_free=%u internal_largest=%u\n",
                  static_cast<unsigned>(mode), static_cast<unsigned>(body.result), length, millis() - started,
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    return body.result == BROWSER_BODY_DONE ? static_cast<int>(received_bytes.load()) : HTTPC_ERROR_STREAM_WRITE;
}

bool supported_charset(String content_type)
{
    content_type.toLowerCase();
    const int position = content_type.indexOf("charset=");
    if (position < 0) return true;
    String charset = content_type.substring(position + 8);
    const int semicolon = charset.indexOf(';');
    if (semicolon >= 0) charset.remove(semicolon);
    charset.trim();
    charset.replace("\"", "");
    charset.replace("'", "");
    return charset == "utf-8" || charset == "utf8" || charset == "us-ascii" || charset == "ascii";
}

void load_page(BrowserWork *job)
{
    char failure[160] = {};
    bool success = false;
    const uint32_t started = millis();
    for (unsigned hop = 0; hop < 5 && !cancel_requested.load(); ++hop) {
        if (millis() - started >= REQUEST_TIMEOUT_MS) {
            snprintf(failure, sizeof(failure), "连接超时，请稍后重试");
            break;
        }
        const bool https = strncmp(job->url, "https://", 8) == 0;
        if (https && time(nullptr) < 1577836800) {
            snprintf(failure, sizeof(failure), "设备时间未同步，暂时无法验证 HTTPS");
            break;
        }
        BrowserClient<NetworkClient> plain(started);
        BrowserClient<NetworkClientSecure> secure(started);
        secure.useBuiltinCACertBundle();
        secure.setHandshakeTimeout(8);
        HTTPClient http;
        NetworkClient &client = https ? static_cast<NetworkClient &>(secure) : plain;
        if (!http.begin(client, job->url)) {
            snprintf(failure, sizeof(failure), "网址无法打开");
            break;
        }
        http.setReuse(false);
        http.setConnectTimeout(5000);
        http.setTimeout(5000);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        const char *headers[] = {"Content-Type", "Content-Encoding", "Location", "Transfer-Encoding"};
        http.collectHeaders(headers, 4);
        http.setUserAgent(BROWSER_USER_AGENT);
        http.addHeader("Accept", "text/html, application/xhtml+xml, text/plain");
        http.addHeader("Accept-Encoding", "identity");
        Serial.printf("[BROWSER] connect tls=%u epoch=%llu internal_free=%u internal_largest=%u\n",
                      static_cast<unsigned>(https), static_cast<unsigned long long>(time(nullptr)),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        const int status = http.GET();
        char tls_error_text[120] = {};
        const int tls_error = https && status < 0 ? secure.lastError(tls_error_text, sizeof(tls_error_text)) : 0;
        Serial.printf("[BROWSER] http=%d tls_error=%d tls_detail=%s internal_free=%u internal_largest=%u\n",
                      status, tls_error, tls_error_text,
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        if (cancel_requested.load()) { http.end(); break; }
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            char next_url[BROWSER_URL_CAP];
            const bool resolved = browser_url_resolve(job->url, http.header("Location").c_str(), next_url, sizeof(next_url));
            http.end();
            if (!resolved || hop == 4) {
                snprintf(failure, sizeof(failure), "网页跳转无效或次数过多");
                break;
            }
            xSemaphoreTake(browser_mutex, portMAX_DELAY);
            strlcpy(job->url, next_url, sizeof(job->url));
            xSemaphoreGive(browser_mutex);
            continue;
        }
        if (status < 200 || status >= 300) {
            if (https && (tls_error == MBEDTLS_ERR_SSL_ALLOC_FAILED || tls_error == MBEDTLS_ERR_X509_ALLOC_FAILED))
                snprintf(failure, sizeof(failure), "HTTPS 内存不足，请先退出其他应用");
            else if (https && tls_error == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
                snprintf(failure, sizeof(failure), "HTTPS 证书验证失败，请检查设备时间");
            else if (status < 0) snprintf(failure, sizeof(failure), https ? "HTTPS 连接失败，请检查网络和设备时间" : "网络连接失败，请检查 WiFi");
            else snprintf(failure, sizeof(failure), "网页返回 HTTP %d", status);
            http.end();
            break;
        }
        String content_type = http.header("Content-Type");
        content_type.toLowerCase();
        const bool plain_text = content_type.startsWith("text/plain");
        String encoding = http.header("Content-Encoding");
        encoding.toLowerCase();
        encoding.trim();
        if (content_type.length() && !plain_text && !content_type.startsWith("text/html") &&
            !content_type.startsWith("application/xhtml+xml")) {
            snprintf(failure, sizeof(failure), "此链接不是文字网页");
        } else if (!supported_charset(content_type)) {
            snprintf(failure, sizeof(failure), "暂时只支持 UTF-8 编码的网页");
        } else if (encoding.length() && encoding != "identity") {
            snprintf(failure, sizeof(failure), "暂时不支持此网页的压缩格式");
        } else {
            browser_parser_init(&job->parser, &job->document, job->url, plain_text);
            ParserStream sink(job, started);
            const int transferred = read_body(http, client, sink, job, started);
            browser_parser_finish(&job->parser);
            if (!cancel_requested.load()) {
                if (job->document.unsupported_charset) snprintf(failure, sizeof(failure), "暂时只支持 UTF-8 编码的网页");
                else if (sink.timed_out || millis() - started >= REQUEST_TIMEOUT_MS) snprintf(failure, sizeof(failure), "网页读取超时，请重试");
                else if (transferred < 0 && !sink.limited) snprintf(failure, sizeof(failure), "网页读取失败，请检查网络");
                else success = true;
            }
        }
        http.end();
        break;
    }
    const unsigned stack_free = uxTaskGetStackHighWaterMark(nullptr);
    xSemaphoreTake(browser_mutex, portMAX_DELAY);
    const bool cancelled = cancel_requested.load();
    state = cancelled ? BROWSER_IDLE : (success ? BROWSER_READY : BROWSER_ERROR);
    strlcpy(error_message, failure[0] ? failure : "网页无法打开", sizeof(error_message));
    worker_running = false;
    if (release_pending) {
        heap_caps_free(work);
        work = nullptr;
        release_pending = false;
        state = BROWSER_IDLE;
    }
    ++revision;
    Serial.printf("[BROWSER] state=%u bytes=%u text_bytes=%u links=%u truncated=%u cancelled=%u stack_free_min=%u\n",
                  static_cast<unsigned>(state), static_cast<unsigned>(received_bytes.load()),
                  static_cast<unsigned>(work ? work->document.text_length : 0),
                  static_cast<unsigned>(work ? work->document.link_count : 0),
                  static_cast<unsigned>(work ? work->document.truncated : false),
                  static_cast<unsigned>(cancelled), stack_free);
    xSemaphoreGive(browser_mutex);
}

void browser_worker(void *)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(browser_mutex, portMAX_DELAY);
        BrowserWork *job = worker_running ? work : nullptr;
        xSemaphoreGive(browser_mutex);
        if (job) load_page(job);
    }
}
} // namespace

extern "C" void *__real_esp_mbedtls_mem_calloc(size_t count, size_t size);
extern "C" void *__wrap_esp_mbedtls_mem_calloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size) return nullptr;
    const size_t bytes = count * size;
    // Scope the override to this task. Certificate parsing and RSA verification
    // also allocate many small objects: using internal RAM for those exhausted
    // the heap even with external I/O buffers. SDK free accepts either heap.
    if (worker_task && xTaskGetCurrentTaskHandle() == worker_task) {
        void *buffer = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (bytes >= 4096 || !buffer) {
            Serial.printf("[BROWSER] tls_psram_alloc bytes=%u ok=%u internal_free=%u\n",
                          static_cast<unsigned>(bytes), static_cast<unsigned>(buffer != nullptr),
                          static_cast<unsigned>(ESP.getFreeHeap()));
        }
        return buffer;
    }
    return __real_esp_mbedtls_mem_calloc(count, size);
}

bool web_browser_start(const char *url)
{
    if (!ensure_mutex()) return false;
    char normalized[BROWSER_URL_CAP];
    const bool valid = browser_url_normalize(url, normalized, sizeof(normalized));
    xSemaphoreTake(browser_mutex, portMAX_DELAY);
    if (worker_running) { xSemaphoreGive(browser_mutex); return false; }
    state = BROWSER_ERROR;
    ++revision;
    if (!valid || !WiFi.isConnected()) {
        strlcpy(error_message, valid ? "请先连接 WiFi" : "请输入有效的 HTTP 或 HTTPS 网址", sizeof(error_message));
        xSemaphoreGive(browser_mutex);
        return false;
    }
    if (!work) work = static_cast<BrowserWork *>(heap_caps_calloc(1, sizeof(BrowserWork), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!work) {
        strlcpy(error_message, "可用内存不足，请先退出其他应用", sizeof(error_message));
        xSemaphoreGive(browser_mutex);
        return false;
    }
    strlcpy(work->url, normalized, sizeof(work->url));
    cancel_requested.store(false);
    received_bytes.store(0);
    release_pending = false;
    error_message[0] = '\0';
    state = BROWSER_LOADING;
    worker_running = true;
    const unsigned internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const unsigned internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!worker_task) worker_task = xTaskCreateStaticPinnedToCore(
        browser_worker, "TextBrowser", WORKER_STACK_BYTES, nullptr, 1,
        worker_stack, &worker_task_storage, 0);
    if (!worker_task) {
        worker_running = false;
        state = BROWSER_ERROR;
        strlcpy(error_message, "可用内存不足，网页任务无法启动", sizeof(error_message));
    }
    const bool started = worker_running;
    Serial.printf("[BROWSER] started=%u work_bytes=%u stack_bytes=%u stack=INTERNAL_STATIC internal_free=%u internal_largest=%u psram_free=%u\n",
                  static_cast<unsigned>(started),
                  static_cast<unsigned>(sizeof(BrowserWork)), WORKER_STACK_BYTES,
                  internal_free, internal_largest, static_cast<unsigned>(ESP.getFreePsram()));
    if (started) xTaskNotifyGive(worker_task);
    xSemaphoreGive(browser_mutex);
    return started;
}

void web_browser_cancel(void) { cancel_requested.store(true); }

void web_browser_release(void)
{
    if (!browser_mutex) return;
    xSemaphoreTake(browser_mutex, portMAX_DELAY);
    cancel_requested.store(true);
    if (worker_running) release_pending = true;
    else { heap_caps_free(work); work = nullptr; }
    state = BROWSER_IDLE;
    ++revision;
    xSemaphoreGive(browser_mutex);
}

void web_browser_get_view(web_browser_view_t *view)
{
    if (!view) return;
    *view = {};
    if (!browser_mutex) return;
    xSemaphoreTake(browser_mutex, portMAX_DELAY);
    view->state = state;
    view->busy = worker_running;
    view->revision = revision;
    view->received_bytes = received_bytes.load();
    strlcpy(view->error, error_message, sizeof(view->error));
    if (work && !release_pending) {
        strlcpy(view->url, work->url, sizeof(view->url));
        if (state == BROWSER_READY) {
            strlcpy(view->title, work->document.title, sizeof(view->title));
            view->truncated = work->document.truncated;
            view->link_count = work->document.link_count;
            size_t position = 0;
            while (position < work->document.text_length) {
                position = page_end(work->document.text, work->document.text_length, position);
                ++view->page_count;
            }
            if (!view->page_count) view->page_count = 1;
        }
    }
    xSemaphoreGive(browser_mutex);
}

bool web_browser_copy_page(unsigned page, char *text, size_t capacity)
{
    if (!text || capacity < BROWSER_PAGE_BYTES + 1 || !browser_mutex) return false;
    text[0] = '\0';
    xSemaphoreTake(browser_mutex, portMAX_DELAY);
    bool copied = false;
    if (work && state == BROWSER_READY) {
        const browser_document_t &document = work->document;
        size_t start = 0;
        unsigned current = 0;
        while (current < page && start < document.text_length) {
            start = page_end(document.text, document.text_length, start);
            ++current;
        }
        if (current == page && (start < document.text_length || page == 0)) {
            const size_t end = page_end(document.text, document.text_length, start);
            memcpy(text, document.text + start, end - start);
            text[end - start] = '\0';
            copied = true;
        }
    }
    xSemaphoreGive(browser_mutex);
    return copied;
}

bool web_browser_copy_link(unsigned index, browser_link_t *link)
{
    if (!link || !browser_mutex) return false;
    xSemaphoreTake(browser_mutex, portMAX_DELAY);
    const bool found = work && state == BROWSER_READY && index < work->document.link_count;
    if (found) *link = work->document.links[index];
    xSemaphoreGive(browser_mutex);
    return found;
}
