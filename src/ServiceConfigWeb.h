#pragma once
#include <stdbool.h>
#include "esp_http_server.h"
bool service_config_web_register(httpd_handle_t server);
/* AppServices only. Performs a requested reboot after the reply has been sent. */
void service_config_web_poll(void);
