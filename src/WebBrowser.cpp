#include "WebBrowser.h"
#include "IdfRuntime.h"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <strings.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509.h"

extern "C" bool get_wifi_connected(void);

struct BrowserHeaders {
    char content_type[192];
    char content_encoding[64];
    char location[BROWSER_URL_CAP];
};

static void copy_header(char *dest, size_t dest_size, const char *value)
{
    if (!dest || dest_size == 0) return;
    dest[0] = '\0';
    if (value) strlcpy(dest, value, dest_size);
}

extern "C" {
static esp_err_t browser_http_event(esp_http_client_event_t *event)
{
    if (!event || event->event_id != HTTP_EVENT_ON_HEADER || !event->header_key || !event->user_data) return ESP_OK;
    auto *headers = static_cast<BrowserHeaders *>(event->user_data);
    const char *value = event->header_value ? event->header_value : "";
    if (strcasecmp(event->header_key, "Content-Type") == 0) copy_header(headers->content_type, sizeof(headers->content_type), value);
    else if (strcasecmp(event->header_key, "Content-Encoding") == 0)
        copy_header(headers->content_encoding, sizeof(headers->content_encoding), value);
    else if (strcasecmp(event->header_key, "Location") == 0) copy_header(headers->location, sizeof(headers->location), value);
    return ESP_OK;
}
}

namespace {
constexpr size_t DOWNLOAD_LIMIT = 256 * 1024;
constexpr uint32_t REQUEST_TIMEOUT_MS = 15000;
constexpr uint32_t WORKER_STACK_BYTES = 10240;
constexpr char BROWSER_USER_AGENT[] =
    "Mozilla/5.0 (Linux; Android 10) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/120.0 Mobile Safari/537.36 ESP32TextBrowser/1.5";
constexpr char TAG[] = "BROWSER";

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
alignas(16) StackType_t worker_stack[WORKER_STACK_BYTES / sizeof(StackType_t)];
StaticTask_t worker_task_storage;
bool release_pending;
uint32_t revision;
char error_message[160];
std::atomic<bool> cancel_requested{false};
std::atomic<uint32_t> received_bytes{0};

bool ensure_mutex()
{
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

bool starts_with_ci(const char *value, const char *prefix)
{
    if (!value || !prefix) return false;
    return strncasecmp(value, prefix, strlen(prefix)) == 0;
}

bool supported_charset(const char *content_type)
{
    if (!content_type || !content_type[0]) return true;
    const char *found = nullptr;
    for (const char *cursor = content_type; *cursor; ++cursor) {
        if (strncasecmp(cursor, "charset=", 8) == 0) {
            found = cursor + 8;
            break;
        }
    }
    if (!found) return true;
    char charset[64];
    size_t length = 0;
    while (*found && *found != ';' && length + 1 < sizeof(charset)) {
        if (*found != '"' && *found != '\'') charset[length++] = static_cast<char>(tolower(static_cast<unsigned char>(*found)));
        ++found;
    }
    charset[length] = '\0';
    size_t begin = 0;
    while (charset[begin] && isspace(static_cast<unsigned char>(charset[begin]))) ++begin;
    size_t end = strlen(charset + begin);
    while (end > 0 && isspace(static_cast<unsigned char>(charset[begin + end - 1]))) --end;
    charset[begin + end] = '\0';
    const char *token = charset + begin;
    return strcmp(token, "utf-8") == 0 || strcmp(token, "utf8") == 0 ||
           strcmp(token, "us-ascii") == 0 || strcmp(token, "ascii") == 0;
}

int connect_budget_ms(bool https, uint32_t started)
{
    const uint32_t elapsed = app_millis() - started;
    if (elapsed >= REQUEST_TIMEOUT_MS) return 0;
    const uint32_t remain = REQUEST_TIMEOUT_MS - elapsed;
    const uint32_t preferred = https ? 8000u : 5000u;
    return static_cast<int>(remain < preferred ? remain : preferred);
}

bool body_is_complete(esp_http_client_handle_t client)
{
    const int64_t length = esp_http_client_get_content_length(client);
    /* fetch_headers reports 0 and marks chunked when the length is absent.
     * A real zero-length body is complete. Unknown length uses the client's
     * decoded-body completion flag. */
    if (length == 0 && !esp_http_client_is_chunked_response(client)) return true;
    return esp_http_client_is_complete_data_received(client);
}

void load_page(BrowserWork *job)
{
    char failure[160] = {};
    bool success = false;
    const uint32_t started = app_millis();
    for (unsigned hop = 0; hop < 5 && !cancel_requested.load(); ++hop) {
        if (app_millis() - started >= REQUEST_TIMEOUT_MS) {
            snprintf(failure, sizeof(failure), "连接超时，请稍后重试");
            break;
        }
        const bool https = strncmp(job->url, "https://", 8) == 0;
        if (https && time(nullptr) < 1577836800) {
            snprintf(failure, sizeof(failure), "设备时间未同步，暂时无法验证 HTTPS");
            break;
        }
        const int timeout_ms = connect_budget_ms(https, started);
        if (timeout_ms <= 0) {
            snprintf(failure, sizeof(failure), "连接超时，请稍后重试");
            break;
        }
        BrowserHeaders headers = {};
        esp_http_client_config_t config = {};
        config.url = job->url;
        config.event_handler = browser_http_event;
        config.user_data = &headers;
        config.timeout_ms = timeout_ms;
        config.disable_auto_redirect = true;
        config.user_agent = BROWSER_USER_AGENT;
        if (https) config.crt_bundle_attach = esp_crt_bundle_attach;
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            snprintf(failure, sizeof(failure), "网址无法打开");
            break;
        }
        esp_http_client_set_header(client, "Accept", "text/html, application/xhtml+xml, text/plain");
        esp_http_client_set_header(client, "Accept-Encoding", "identity");
        ESP_LOGI(TAG, "connect tls=%u epoch=%llu internal_free=%u internal_largest=%u",
                 static_cast<unsigned>(https), static_cast<unsigned long long>(time(nullptr)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        int status = -1;
        const esp_err_t opened = esp_http_client_open(client, 0);
        if (opened == ESP_OK) {
            const int64_t header_result = esp_http_client_fetch_headers(client);
            status = esp_http_client_get_status_code(client);
            if (header_result < 0 && status <= 0) status = -1;
        }
        int tls_error = 0;
        int tls_flags = 0;
        if (https && status < 0) {
            esp_http_client_get_and_clear_last_tls_error(client, &tls_error, &tls_flags);
        }
        ESP_LOGI(TAG, "http=%d tls_error=%d tls_flags=%d internal_free=%u internal_largest=%u",
                 status, tls_error, tls_flags,
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        if (cancel_requested.load()) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            break;
        }
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            char next_url[BROWSER_URL_CAP];
            const bool resolved = browser_url_resolve(job->url, headers.location, next_url, sizeof(next_url));
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
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
            else if (https && (tls_error == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED || tls_flags != 0))
                snprintf(failure, sizeof(failure), "HTTPS 证书验证失败，请检查设备时间");
            else if (status < 0)
                snprintf(failure, sizeof(failure), https ? "HTTPS 连接失败，请检查网络和设备时间" : "网络连接失败，请检查 WiFi");
            else snprintf(failure, sizeof(failure), "网页返回 HTTP %d", status);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            break;
        }
        const bool plain_text = starts_with_ci(headers.content_type, "text/plain");
        char encoding[64];
        strlcpy(encoding, headers.content_encoding, sizeof(encoding));
        for (char *cursor = encoding; *cursor; ++cursor) *cursor = static_cast<char>(tolower(static_cast<unsigned char>(*cursor)));
        size_t enc_begin = 0;
        while (encoding[enc_begin] && isspace(static_cast<unsigned char>(encoding[enc_begin]))) ++enc_begin;
        size_t enc_end = strlen(encoding + enc_begin);
        while (enc_end > 0 && isspace(static_cast<unsigned char>(encoding[enc_begin + enc_end - 1]))) --enc_end;
        encoding[enc_begin + enc_end] = '\0';
        const char *encoding_token = encoding + enc_begin;
        if (headers.content_type[0] && !plain_text && !starts_with_ci(headers.content_type, "text/html") &&
            !starts_with_ci(headers.content_type, "application/xhtml+xml")) {
            snprintf(failure, sizeof(failure), "此链接不是文字网页");
        } else if (!supported_charset(headers.content_type)) {
            snprintf(failure, sizeof(failure), "暂时只支持 UTF-8 编码的网页");
        } else if (encoding_token[0] && strcmp(encoding_token, "identity") != 0) {
            snprintf(failure, sizeof(failure), "暂时不支持此网页的压缩格式");
        } else {
            browser_parser_init(&job->parser, &job->document, job->url, plain_text);
            esp_http_client_set_timeout_ms(client, 1000);
            bool limited = false;
            bool timed_out = false;
            bool read_error = false;
            bool complete = false;
            while (!limited && !cancel_requested.load()) {
                if (app_millis() - started >= REQUEST_TIMEOUT_MS) {
                    timed_out = true;
                    break;
                }
                const int n = esp_http_client_read(client, reinterpret_cast<char *>(job->transfer_buffer),
                                                   static_cast<int>(sizeof(job->transfer_buffer)));
                if (n == -ESP_ERR_HTTP_EAGAIN) {
                    if (cancel_requested.load()) break;
                    continue;
                }
                if (n < 0) {
                    read_error = true;
                    break;
                }
                if (n == 0) {
                    complete = body_is_complete(client);
                    break;
                }
                const size_t used = received_bytes.load();
                const size_t room = used >= DOWNLOAD_LIMIT ? 0 : DOWNLOAD_LIMIT - used;
                const size_t count = static_cast<size_t>(n) < room ? static_cast<size_t>(n) : room;
                if (count > 0) browser_parser_feed(&job->parser, reinterpret_cast<const char *>(job->transfer_buffer), count);
                received_bytes.store(static_cast<uint32_t>(used + count));
                if (count < static_cast<size_t>(n) || job->document.truncated) {
                    limited = true;
                    job->document.truncated = true;
                    break;
                }
                vTaskDelay(1);
            }
            browser_parser_finish(&job->parser);
            if (!cancel_requested.load()) {
                if (job->document.unsupported_charset) snprintf(failure, sizeof(failure), "暂时只支持 UTF-8 编码的网页");
                else if (timed_out || app_millis() - started >= REQUEST_TIMEOUT_MS)
                    snprintf(failure, sizeof(failure), "网页读取超时，请重试");
                else if ((read_error || !complete) && !limited) snprintf(failure, sizeof(failure), "网页读取失败，请检查网络");
                else success = true;
            }
        }
        const int64_t content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "body bytes=%u elapsed_ms=%u content_length=%lld internal_free=%u internal_largest=%u",
                 static_cast<unsigned>(received_bytes.load()),
                 static_cast<unsigned>(app_millis() - started),
                 static_cast<long long>(content_length),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
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
    ESP_LOGI(TAG, "state=%u bytes=%u text_bytes=%u links=%u truncated=%u cancelled=%u stack_free_min=%u",
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

bool web_browser_start(const char *url)
{
    if (!ensure_mutex()) return false;
    char normalized[BROWSER_URL_CAP];
    const bool valid = browser_url_normalize(url, normalized, sizeof(normalized));
    xSemaphoreTake(browser_mutex, portMAX_DELAY);
    if (worker_running) {
        xSemaphoreGive(browser_mutex);
        return false;
    }
    state = BROWSER_ERROR;
    ++revision;
    if (!valid || !get_wifi_connected()) {
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
    if (!worker_task) {
        worker_task = xTaskCreateStaticPinnedToCore(
            browser_worker, "TextBrowser", WORKER_STACK_BYTES, nullptr, 1,
            worker_stack, &worker_task_storage, 0);
    }
    if (!worker_task) {
        worker_running = false;
        state = BROWSER_ERROR;
        strlcpy(error_message, "可用内存不足，网页任务无法启动", sizeof(error_message));
    }
    const bool started = worker_running;
    ESP_LOGI(TAG, "started=%u work_bytes=%u stack_bytes=%u stack=INTERNAL_STATIC internal_free=%u internal_largest=%u psram_free=%u",
             static_cast<unsigned>(started),
             static_cast<unsigned>(sizeof(BrowserWork)), WORKER_STACK_BYTES,
             internal_free, internal_largest,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
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
    else {
        heap_caps_free(work);
        work = nullptr;
    }
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
