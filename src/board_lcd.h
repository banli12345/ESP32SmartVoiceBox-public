#ifndef SMARTVOICEBOX_BOARD_LCD_H
#define SMARTVOICEBOX_BOARD_LCD_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

bool board_lcd_init(void);
void board_lcd_release_spi_pins(void);
uint16_t *board_lcd_get_framebuffer(int index);
bool board_lcd_double_buffer_ready(void);
esp_err_t board_lcd_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *color);
uint32_t board_lcd_read_frame_complete_count(void);
uint32_t board_lcd_read_vsync_count(void);
/* GUI task only, on the same core that initialized the RGB peripheral. */
bool board_lcd_wait_frame_complete(uint32_t after, uint32_t timeout_ms);
bool board_lcd_recover_scanout(void);
void service_display_restart(void);

#ifdef __cplusplus
}
#endif

#endif
