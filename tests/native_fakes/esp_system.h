#pragma once
struct Reboot {};
[[noreturn]] inline void esp_restart(void) { throw Reboot(); }
