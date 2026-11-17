#include "UiTransition.h"

#include "src/core/lv_refr.h"

#include <string.h>
#include <stdlib.h>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#endif

#define UI_DEPTH_MAX_W 480
#define UI_DEPTH_MS 180
#define UI_DEPTH_SHADOW_W 8
#define UI_PREP_BUDGET_US 80000
#define UI_DEPTH_BG 0x0A03u

typedef struct {
    ui_transition_capture_cb capture;
    void *capture_ctx;
    lv_obj_t *desktop;
    lv_obj_t *overlay;
    lv_obj_t *target;
    lv_obj_t *page;
    lv_color_t *old_buf;
    lv_color_t *new_buf;
    uint16_t w;
    uint16_t h;
    uint16_t progress;
    bool back;
    bool active;
    bool preparing;
    bool cancel_req;
    uint32_t frames;
#if defined(ESP_PLATFORM)
    int64_t prep_us;
    int64_t t0;
    int64_t draw_sum_us;
    int64_t draw_max_us;
#endif
} ui_depth_state_t;

static ui_depth_state_t g;

static void anim_exec(void *var, int32_t value);
static void anim_ready(lv_anim_t *anim);
static void finish_transition(void);
static void on_desktop_deleted(lv_event_t *e);
static void on_root_deleted(lv_event_t *e);
static void overlay_evt(lv_event_t *e);

static void *pix_alloc(size_t bytes)
{
#if defined(ESP_PLATFORM)
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(bytes);
#endif
}

static void pix_free(void *ptr)
{
    if(ptr == NULL) return;
#if defined(ESP_PLATFORM)
    heap_caps_free(ptr);
#else
    free(ptr);
#endif
}

static lv_color_t depth_bg(void)
{
    lv_color_t color;
    color.full = (uint16_t)UI_DEPTH_BG;
    return color;
}

static inline uint16_t shade_rgb565(uint16_t color, uint8_t mul)
{
    uint32_t rb = ((((uint32_t)color & 0xF81Fu) * mul) >> 5) & 0xF81Fu;
    uint32_t green = ((((uint32_t)color & 0x07E0u) * mul) >> 5) & 0x07E0u;
    return (uint16_t)(rb | green);
}

static bool is_descendant(const lv_obj_t *obj, const lv_obj_t *ancestor)
{
    while(obj != NULL) {
        if(obj == ancestor) return true;
        obj = lv_obj_get_parent(obj);
    }
    return false;
}

static bool display_geometry(lv_obj_t *obj, lv_coord_t *w, lv_coord_t *h)
{
    lv_disp_t *disp = obj ? lv_obj_get_disp(obj) : lv_disp_get_default();
    if(disp == NULL) return false;
    *w = lv_disp_get_hor_res(disp);
    *h = lv_disp_get_ver_res(disp);
    if(*w < 2 || *h < 2 || *w > UI_DEPTH_MAX_W || *h > UI_DEPTH_MAX_W) return false;
    return true;
}

static void depth_log(void)
{
#if defined(ESP_PLATFORM)
    uint32_t frames = g.frames;
    uint32_t avg = frames ? (uint32_t)(g.draw_sum_us / (int64_t)frames) : 0u;
    uint32_t dur = (uint32_t)((esp_timer_get_time() - g.t0) / 1000);
    uint32_t prep = (uint32_t)(g.prep_us / 1000);
    uint32_t mx = (uint32_t)g.draw_max_us;
    ESP_LOGI("DEPTH", "[DEPTH] effect=layered_slide duration=%ums frames=%u prep=%ums frame_avg=%uus frame_max=%uus", dur, frames, prep, avg, mx);
#else
    (void)0;
#endif
}

static void unwatch(void)
{
    lv_obj_t *target = g.target;
    lv_obj_t *page = g.page;
    g.target = NULL;
    g.page = NULL;
    if(target != NULL && lv_obj_is_valid(target)) lv_obj_remove_event_cb(target, on_root_deleted);
    if(page != NULL && page != target && lv_obj_is_valid(page)) lv_obj_remove_event_cb(page, on_root_deleted);
}

static void finish_transition(void)
{
    if(!g.active && g.old_buf == NULL && g.new_buf == NULL) {
        lv_anim_del(&g, anim_exec);
        return;
    }
    g.active = false;
    lv_anim_del(&g, anim_exec);
    if(g.overlay != NULL && lv_obj_is_valid(g.overlay)) {
        lv_obj_add_flag(g.overlay, LV_OBJ_FLAG_HIDDEN);
    }
    depth_log();
    pix_free(g.old_buf);
    pix_free(g.new_buf);
    g.old_buf = NULL;
    g.new_buf = NULL;
    unwatch();
}

static void on_desktop_deleted(lv_event_t *e)
{
    if(lv_event_get_target(e) == g.desktop) {
        g.desktop = NULL;
        ui_transition_cancel();
    }
}

static void on_root_deleted(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    bool cancel = false;
    if(obj == g.target) {
        g.target = NULL;
        cancel = true;
    }
    if(obj == g.page) {
        g.page = NULL;
        cancel = true;
    }
    if(g.overlay != NULL && lv_obj_get_parent(g.overlay) == obj) {
        g.overlay = NULL;
        cancel = true;
    }
    if(cancel && (g.active || g.old_buf != NULL)) {
        finish_transition();
    }
}

static void watch_roots(lv_obj_t *target, lv_obj_t *page)
{
    g.target = target;
    g.page = page;
    if(target != NULL) lv_obj_add_event_cb(target, on_root_deleted, LV_EVENT_DELETE, NULL);
    if(page != NULL && page != target) lv_obj_add_event_cb(page, on_root_deleted, LV_EVENT_DELETE, NULL);
}

static bool render_offscreen(lv_obj_t *root, lv_color_t *pixels)
{
    if(root == NULL || pixels == NULL) return false;
    lv_obj_update_layout(root);
    lv_disp_t *real = lv_obj_get_disp(root);
    if(real == NULL || real->driver == NULL) return false;
    if(real->driver->draw_ctx_init == NULL || real->driver->draw_ctx_deinit == NULL) return false;
    if(real->driver->draw_ctx_size == 0) return false;

    lv_coord_t w = lv_disp_get_hor_res(real);
    lv_coord_t h = lv_disp_get_ver_res(real);
    if(w != (lv_coord_t)g.w || h != (lv_coord_t)g.h || w < 2 || h < 2) return false;

    lv_area_t coords;
    lv_obj_get_coords(root, &coords);
    if(coords.x1 != 0 || coords.y1 != 0) return false;
    if(lv_area_get_width(&coords) != w || lv_area_get_height(&coords) != h) return false;

    lv_draw_ctx_t *ctx = (lv_draw_ctx_t *)lv_mem_alloc(real->driver->draw_ctx_size);
    if(ctx == NULL) return false;

    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = w;
    driver.ver_res = h;

    lv_disp_t fake;
    lv_memset_00(&fake, sizeof(fake));
    fake.driver = &driver;

    real->driver->draw_ctx_init(&driver, ctx);
    driver.draw_ctx = ctx;

    lv_area_t bounds;
    bounds.x1 = 0;
    bounds.y1 = 0;
    bounds.x2 = (lv_coord_t)(w - 1);
    bounds.y2 = (lv_coord_t)(h - 1);
    ctx->buf = pixels;
    ctx->buf_area = &bounds;
    ctx->clip_area = &bounds;

    lv_disp_t *previous = _lv_refr_get_disp_refreshing();
    _lv_refr_set_disp_refreshing(&fake);
    memset(pixels, 0, (size_t)w * (size_t)h * sizeof(lv_color_t));
    lv_obj_redraw(ctx, root);
    if(ctx->wait_for_finish != NULL) ctx->wait_for_finish(ctx);
    _lv_refr_set_disp_refreshing(previous);
    real->driver->draw_ctx_deinit(&driver, ctx);
    lv_mem_free(ctx);
    return true;
}

static bool ensure_overlay(lv_obj_t *screen)
{
    if(screen == NULL) return false;
    if(g.overlay != NULL && !lv_obj_is_valid(g.overlay)) g.overlay = NULL;
    if(g.overlay == NULL) {
        g.overlay = lv_obj_create(screen);
        if(g.overlay == NULL) return false;
        lv_obj_remove_style_all(g.overlay);
        lv_obj_add_event_cb(g.overlay, overlay_evt, LV_EVENT_ALL, NULL);
    } else if(lv_obj_get_parent(g.overlay) != screen) {
        lv_obj_set_parent(g.overlay, screen);
    }
    lv_obj_set_size(g.overlay, (lv_coord_t)g.w, (lv_coord_t)g.h);
    lv_obj_clear_flag(g.overlay, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_flag(g.overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    /* Root padding/scroll must not inset the full-screen opaque cover. */
    lv_coord_t border = lv_obj_get_style_border_width(screen, LV_PART_MAIN);
    lv_obj_set_pos(g.overlay, -lv_obj_get_style_pad_left(screen, LV_PART_MAIN) - border,
                             -lv_obj_get_style_pad_top(screen, LV_PART_MAIN) - border);
    /* The custom blit writes every pixel and explicitly reports cover. A
     * transparent base avoids a redundant full-screen background fill. */
    lv_obj_set_style_bg_opa(g.overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(g.overlay, depth_bg(), 0);
    lv_obj_set_style_radius(g.overlay, 0, 0);
    lv_obj_set_style_pad_all(g.overlay, 0, 0);
    lv_obj_set_style_border_width(g.overlay, 0, 0);
    lv_obj_set_style_shadow_width(g.overlay, 0, 0);
    lv_obj_set_style_outline_width(g.overlay, 0, 0);
    lv_obj_move_foreground(g.overlay);
    return true;
}

static void depth_blit(lv_draw_ctx_t *draw_ctx)
{
    if(!g.active || g.old_buf == NULL || g.new_buf == NULL || g.w < 2 || g.h < 2) return;
    if(draw_ctx == NULL) return;
    if(draw_ctx->buf == NULL || draw_ctx->buf_area == NULL || draw_ctx->clip_area == NULL) return;

    lv_area_t screen_area;
    screen_area.x1 = 0;
    screen_area.y1 = 0;
    screen_area.x2 = (lv_coord_t)(g.w - 1u);
    screen_area.y2 = (lv_coord_t)(g.h - 1u);
    lv_area_t area;
    if(!_lv_area_intersect(&area, draw_ctx->clip_area, draw_ctx->buf_area)) return;
    if(!_lv_area_intersect(&area, &area, &screen_area)) return;

    lv_coord_t stride = lv_area_get_width(draw_ctx->buf_area);
    if(stride <= 0) return;
    if(draw_ctx->wait_for_finish) draw_ctx->wait_for_finish(draw_ctx);
    lv_color_t *base = (lv_color_t *)draw_ctx->buf;
    const lv_area_t *buf_area = draw_ctx->buf_area;

#if defined(ESP_PLATFORM)
    int64_t t0 = esp_timer_get_time();
#endif

    if(g.progress == 0u || g.progress >= 1024u) {
        const lv_color_t *image = g.progress >= 1024u ? g.new_buf : g.old_buf;
        bool full = area.x1 == 0 && area.y1 == 0 && area.x2 == screen_area.x2 && area.y2 == screen_area.y2 &&
                    buf_area->x1 == 0 && buf_area->y1 == 0 && stride == (lv_coord_t)g.w;
        if(full) {
            memcpy(base, image, (size_t)g.w * (size_t)g.h * sizeof(lv_color_t));
        } else {
            for(lv_coord_t y = area.y1; y <= area.y2; y++) {
                lv_color_t *dst = base + (lv_coord_t)(y - buf_area->y1) * stride + (area.x1 - buf_area->x1);
                memcpy(dst, image + (size_t)y * g.w + (size_t)area.x1, (size_t)(area.x2 - area.x1 + 1) * sizeof(lv_color_t));
            }
        }
    } else {
        /* The foreground sheet moves at full speed over a gently moving back
         * layer. Both textures stay at 1:1 size. Two contiguous row copies
         * replace 230,400 scattered source samples and per-pixel transforms.
         * Only the narrow contact shadow needs RGB565 arithmetic. */
        const lv_coord_t split = (lv_coord_t)((uint32_t)g.w *
            (g.back ? g.progress : 1024u - g.progress) / 1024u);
        const lv_coord_t parallax = ((lv_coord_t)g.w - split) / 5;
        const lv_color_t *left_image = g.back ? g.new_buf : g.old_buf;
        const lv_color_t *right_image = g.back ? g.old_buf : g.new_buf;
        const lv_coord_t left_end = LV_MIN(area.x2 + 1, split);
        const lv_coord_t right_start = LV_MAX(area.x1, split);
        const lv_coord_t shadow_start = LV_MAX(area.x1, split - UI_DEPTH_SHADOW_W);
        for(lv_coord_t y = area.y1; y <= area.y2; ++y) {
            lv_color_t *row = base + (size_t)(y - buf_area->y1) * stride;
            const size_t source_row = (size_t)y * g.w;
            if(left_end > area.x1) {
                memcpy(row + area.x1 - buf_area->x1,
                       left_image + source_row + area.x1 + parallax,
                       (size_t)(left_end - area.x1) * sizeof(lv_color_t));
            }
            if(right_start <= area.x2) {
                memcpy(row + right_start - buf_area->x1,
                       right_image + source_row + right_start - split,
                       (size_t)(area.x2 - right_start + 1) * sizeof(lv_color_t));
            }
            for(lv_coord_t x = shadow_start; x < left_end; ++x) {
                uint8_t shade = (uint8_t)(8 + (split - x) * 3);
                lv_color_t *pixel = row + x - buf_area->x1;
                pixel->full = shade_rgb565(pixel->full, shade);
            }
        }
    }

    g.frames++;
#if defined(ESP_PLATFORM)
    int64_t dt = esp_timer_get_time() - t0;
    g.draw_sum_us += dt;
    if(dt > g.draw_max_us) g.draw_max_us = dt;
#endif
}

static void overlay_evt(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_DELETE) {
        if(lv_event_get_target(e) == g.overlay) {
            g.overlay = NULL;
            ui_transition_cancel();
        }
        return;
    }
    if(code == LV_EVENT_COVER_CHECK) {
        if(g.active && g.old_buf != NULL && g.new_buf != NULL && lv_event_get_target(e) == g.overlay) {
            /* lv_event_set_cover_res cannot promote NOT_COVER back to COVER
             * after the transparent base class check. Our blit is fully opaque. */
            lv_cover_check_info_t *info = lv_event_get_param(e);
            if(info->res != LV_COVER_RES_MASKED) info->res = LV_COVER_RES_COVER;
        } else {
            lv_event_set_cover_res(e, LV_COVER_RES_NOT_COVER);
        }
        return;
    }
    if(code != LV_EVENT_DRAW_MAIN) return;
    depth_blit(lv_event_get_draw_ctx(e));
}

static void anim_exec(void *var, int32_t value)
{
    (void)var;
    if(!g.active) return;
    if(value < 0) value = 0;
    if(value > 1024) value = 1024;
    g.progress = (uint16_t)value;
    if(g.overlay != NULL && lv_obj_is_valid(g.overlay)) lv_obj_invalidate(g.overlay);
}

static void anim_ready(lv_anim_t *anim)
{
    (void)anim;
    finish_transition();
}

static bool start_anim(void)
{
    g.progress = 0;
    g.frames = 0;
#if defined(ESP_PLATFORM)
    g.draw_sum_us = 0;
    g.draw_max_us = 0;
    g.t0 = esp_timer_get_time();
#endif
    g.active = true;
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &g);
    lv_anim_set_exec_cb(&anim, anim_exec);
    lv_anim_set_values(&anim, 0, 1024);
    lv_anim_set_time(&anim, UI_DEPTH_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&anim, anim_ready);
    if(lv_anim_start(&anim) == NULL) {
        finish_transition();
        return false;
    }
    return true;
}

static void release_pair(lv_color_t *a, lv_color_t *b)
{
    pix_free(a);
    pix_free(b);
}

static void transition_to(lv_obj_t *screen, lv_obj_t *page, bool back, bool do_load)
{
    if(screen == NULL || !lv_obj_is_valid(screen) || lv_obj_get_parent(screen) != NULL) return;
    if(do_load && screen == lv_scr_act()) return;
    if(g.preparing) {
        g.cancel_req = true;
        if(do_load) lv_scr_load(screen);
        return;
    }

    lv_obj_t *old = lv_scr_act();
    bool depth = false;
    if(do_load) {
        depth = g.desktop != NULL && old != NULL && old != screen && (old == g.desktop || screen == g.desktop);
    } else {
        depth = g.desktop != NULL && old == g.desktop && page != NULL && page != g.desktop && is_descendant(page, g.desktop);
    }
    if(!depth || g.capture == NULL) {
        if(g.active) ui_transition_cancel();
        if(do_load) lv_scr_load(screen);
        return;
    }

    lv_coord_t w = 0;
    lv_coord_t h = 0;
    if(!display_geometry(screen, &w, &h)) {
        if(g.active) ui_transition_cancel();
        if(do_load) lv_scr_load(screen);
        return;
    }

    if(g.active) ui_transition_cancel();

    size_t pixels = (size_t)w * (size_t)h;
    size_t bytes = pixels * sizeof(lv_color_t);
    lv_color_t *old_buf = (lv_color_t *)pix_alloc(bytes);
    lv_color_t *new_buf = (lv_color_t *)pix_alloc(bytes);
    if(old_buf == NULL || new_buf == NULL) {
        release_pair(old_buf, new_buf);
        if(do_load) lv_scr_load(screen);
        return;
    }

    g.preparing = true;
    g.cancel_req = false;
#if defined(ESP_PLATFORM)
    int64_t prep_t0 = esp_timer_get_time();
#endif
    bool captured = g.capture(old_buf, pixels, g.capture_ctx);
    if(!captured || g.cancel_req) {
        g.preparing = false;
        g.cancel_req = false;
        release_pair(old_buf, new_buf);
        if(do_load) lv_scr_load(screen);
        return;
    }
    if(do_load) lv_scr_load(screen);
    if(!lv_obj_is_valid(screen) || g.cancel_req) {
        g.preparing = false;
        g.cancel_req = false;
        release_pair(old_buf, new_buf);
        return;
    }
    if(g.overlay != NULL && lv_obj_is_valid(g.overlay)) lv_obj_add_flag(g.overlay, LV_OBJ_FLAG_HIDDEN);

    g.w = (uint16_t)w;
    g.h = (uint16_t)h;
    g.back = back;
    bool shot = render_offscreen(screen, new_buf);
    bool overlay_ok = shot && !g.cancel_req && lv_scr_act() == screen && ensure_overlay(screen);
    if(!overlay_ok || g.cancel_req) {
        g.preparing = false;
        g.cancel_req = false;
        if(g.overlay != NULL && lv_obj_is_valid(g.overlay)) lv_obj_add_flag(g.overlay, LV_OBJ_FLAG_HIDDEN);
        release_pair(old_buf, new_buf);
        return;
    }

#if defined(ESP_PLATFORM)
    g.prep_us = esp_timer_get_time() - prep_t0;
    if(g.prep_us > UI_PREP_BUDGET_US) {
        /* A slow first render should become usable as soon as it is ready. */
        g.preparing = false;
        g.cancel_req = false;
        lv_obj_add_flag(g.overlay, LV_OBJ_FLAG_HIDDEN);
        release_pair(old_buf, new_buf);
        ESP_LOGI("DEPTH", "[DEPTH] direct: preparation=%ums exceeded budget",
                 (unsigned)(g.prep_us / 1000));
        return;
    }
#endif
    g.old_buf = old_buf;
    g.new_buf = new_buf;
    watch_roots(screen, page);
    g.preparing = false;
    if(!start_anim()) return;
}

void ui_transition_set_capture(ui_transition_capture_cb callback, void *context)
{
    g.capture = callback;
    g.capture_ctx = context;
}

void ui_transition_set_desktop(lv_obj_t *root)
{
    if(root == g.desktop) return;
    if(g.active || g.preparing || g.old_buf != NULL) ui_transition_cancel();
    if(g.desktop != NULL && lv_obj_is_valid(g.desktop)) lv_obj_remove_event_cb(g.desktop, on_desktop_deleted);
    g.desktop = NULL;
    if(root != NULL && lv_obj_is_valid(root) && lv_obj_get_parent(root) == NULL) {
        g.desktop = root;
        lv_obj_add_event_cb(root, on_desktop_deleted, LV_EVENT_DELETE, NULL);
    }
}

void ui_screen_open(lv_obj_t *screen)
{
    transition_to(screen, NULL, false, true);
}

void ui_screen_back(lv_obj_t *screen)
{
    transition_to(screen, NULL, true, true);
}

void ui_transition_reveal(lv_obj_t *page, bool back)
{
    if(page == NULL || !lv_obj_is_valid(page)) return;
    lv_obj_t *active = lv_scr_act();
    if(g.desktop == NULL || active != g.desktop || page == g.desktop || !is_descendant(page, g.desktop)) return;
    lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
    transition_to(active, page, back, false);
}

bool ui_transition_busy(void)
{
    return g.active || g.preparing;
}

void ui_transition_cancel(void)
{
    if(g.preparing) {
        g.cancel_req = true;
        return;
    }
    if(!g.active && g.old_buf == NULL) return;
    finish_transition();
}
