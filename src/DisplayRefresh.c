#include "DisplayRefresh.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "board_lcd.h"
#include "board_pins.h"
#include "src/core/lv_refr.h"

static lv_area_t dirty[LV_INV_BUF_SIZE];
static unsigned dirty_count;
static bool copy_full;
static const lv_color_t *last_frame;
static bool redraw_pending;

static void record_dirty(const lv_area_t *area)
{
    if (copy_full) return;
    const lv_area_t screen = {0, 0, BOARD_SCREEN_WIDTH - 1, BOARD_SCREEN_HEIGHT - 1};
    lv_area_t clipped;
    if (!_lv_area_intersect(&clipped, area, &screen)) return;
    if (dirty_count == LV_INV_BUF_SIZE || lv_area_get_size(&clipped) == BOARD_SCREEN_WIDTH * BOARD_SCREEN_HEIGHT) {
        copy_full = true;
        dirty_count = 0;
    } else {
        dirty[dirty_count++] = clipped;
    }
}

static bool will_redraw(const lv_disp_t *display, const lv_area_t *area)
{
    for (unsigned i = 0; i < display->inv_p; ++i) {
        if (!display->inv_area_joined[i] && _lv_area_is_in(area, &display->inv_areas[i], 0)) return true;
    }
    return false;
}

static void synchronize_buffers(lv_color_t *destination, const lv_color_t *source, const lv_disp_t *display)
{
    if (copy_full) {
        const lv_area_t screen = {0, 0, BOARD_SCREEN_WIDTH - 1, BOARD_SCREEN_HEIGHT - 1};
        if (will_redraw(display, &screen)) return;
        memcpy(destination, source, BOARD_SCREEN_WIDTH * BOARD_SCREEN_HEIGHT * sizeof(*source));
        return;
    }
    for (unsigned i = 0; i < dirty_count; ++i) {
        const lv_area_t *area = &dirty[i];
        if (will_redraw(display, area)) continue;
        const size_t width = (size_t)(area->x2 - area->x1 + 1);
        if (width == BOARD_SCREEN_WIDTH) {
            const size_t offset = (size_t)area->y1 * BOARD_SCREEN_WIDTH;
            memcpy(destination + offset, source + offset,
                   width * (area->y2 - area->y1 + 1) * sizeof(*source));
        } else {
            for (int y = area->y1; y <= area->y2; ++y) {
                const size_t offset = (size_t)y * BOARD_SCREEN_WIDTH + area->x1;
                memcpy(destination + offset, source + offset, width * sizeof(*source));
            }
        }
    }
}

void display_refresh_begin(lv_disp_drv_t *driver)
{
    lv_disp_t *display = _lv_refr_get_disp_refreshing();
    lv_color_t *canvas = (lv_color_t *)driver->draw_buf->buf_act;
    if (last_frame && display && canvas != last_frame) {
        // The previous submission completed before LVGL released its buffer.
        // Bring its old dirty regions into the free canvas BEFORE drawing. A
        // whole-screen animation/video frame needs no synchronization copy at
        // all, because LVGL will overwrite every pixel of the canvas itself.
        synchronize_buffers(canvas, last_frame, display);
    }
    dirty_count = 0;
    copy_full = false;
}

void display_refresh_flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
    (void)area;
    // Direct mode flushes each invalid rectangle into the same full-size canvas.
    // Present only after the last rectangle; synchronize before the next draw.
    if (!lv_disp_flush_is_last(driver)) {
        lv_disp_flush_ready(driver);
        return;
    }

    // In LVGL 8 direct mode buf_area (and the flush area) covers the entire
    // canvas even when clip_area was small. Read its actual invalid regions.
    lv_disp_t *refreshing = _lv_refr_get_disp_refreshing();
    if (!refreshing || !driver->direct_mode) {
        copy_full = true;
    } else {
        for (unsigned i = 0; i < refreshing->inv_p; ++i) {
            if (!refreshing->inv_area_joined[i]) record_dirty(&refreshing->inv_areas[i]);
        }
    }

    lv_color_t *first = (lv_color_t *)board_lcd_get_framebuffer(0);
    lv_color_t *second = (lv_color_t *)board_lcd_get_framebuffer(1);
    bool ok = first && second && (pixels == first || pixels == second);
    if (ok) {
        ok = board_lcd_draw_bitmap(0, 0, BOARD_SCREEN_WIDTH, BOARD_SCREEN_HEIGHT, pixels) == ESP_OK;
        if (ok) {
            // Peripheral ISR and this GUI run on core 1: observing the counter
            // after submission fences an old completion before the buffer switch.
            const uint32_t after = board_lcd_read_frame_complete_count();
            ok = board_lcd_wait_frame_complete(after, 100);
        }
        if (!ok && board_lcd_recover_scanout()) {
            redraw_pending = true;
            copy_full = true;
            ok = board_lcd_draw_bitmap(0, 0, BOARD_SCREEN_WIDTH, BOARD_SCREEN_HEIGHT, pixels) == ESP_OK;
            if (ok) {
                const uint32_t after = board_lcd_read_frame_complete_count();
                ok = board_lcd_wait_frame_complete(after, 100);
            }
        }
    }
    if (ok) {
        last_frame = pixels;
    } else {
        // Never copy into an unconfirmed scanout buffer. Restore a whole canvas
        // on the next refresh rather than treating out-of-sync pixels as valid.
        last_frame = NULL;
        redraw_pending = true;
        puts("[LCD] present failed; full redraw queued");
    }
    lv_disp_flush_ready(driver);
}

void display_refresh_poll(void)
{
    if (!redraw_pending) return;
    redraw_pending = false;
    // LVGL discards invalidation requests made while it is still drawing.
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    lv_obj_invalidate(lv_layer_sys());
}

bool display_refresh_capture(lv_color_t *destination, size_t pixels, void *context)
{
    (void)context;
    if (!destination || !last_frame || pixels != BOARD_SCREEN_WIDTH * BOARD_SCREEN_HEIGHT) return false;
    memcpy(destination, last_frame, pixels * sizeof(*destination));
    return true;
}
