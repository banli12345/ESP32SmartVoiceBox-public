#include "BootSplash.h"
#include "SystemBranding.h"

LV_FONT_DECLARE(lv_font_breeze_brand_24);

#define BOOT_MS 3000
#define BOOT_GATHER_MS 1200
#define BOOT_SLIDE_MS 2640
#define BOOT_PERIOD_MS 30
/* Text fades keep the previous 2s timeline proportions on the 3s splash. */
#define BOOT_TITLE_AT 1290
#define BOOT_TAG_AT 1620
#define BOOT_FADE_MS 630
#define BOOT_ARC_AT 630
#define BOOT_WAVE_SEGS 96

typedef enum {
    BOOT_OFF = 0,
    BOOT_IDLE,
    BOOT_RUN
} boot_phase_t;

static struct {
    lv_obj_t *overlay;
    lv_obj_t *mark;
    lv_obj_t *badge;
    lv_obj_t *letter;
    lv_obj_t *title;
    lv_obj_t *tagline;
    lv_obj_t *brand_label;
    lv_timer_t *timer;
    uint32_t t0;
    uint32_t elapsed;
    boot_phase_t phase;
    bool busy;
} splash;

static int clamp_i(int value, int lo, int hi)
{
    if(value < lo) return lo;
    if(value > hi) return hi;
    return value;
}

/* Smoothstep in Q8 (0..256). Zero slope at both ends, integer only. */
static int smooth_u8(int t, int span)
{
    if(span <= 0 || t >= span) return 256;
    if(t <= 0) return 0;
    int u = t * 256 / span;
    return (u * u) * (768 - 2 * u) / 65536;
}

static int ease_i(int from, int to, int t, int span)
{
    return from + (to - from) * smooth_u8(t, span) / 256;
}

static void drop_timer(void)
{
    if(splash.timer == NULL) return;
    lv_timer_t *timer = splash.timer;
    splash.timer = NULL;
    lv_timer_del(timer);
}

static void forget_widgets(void)
{
    splash.overlay = NULL;
    splash.mark = NULL;
    splash.badge = NULL;
    splash.letter = NULL;
    splash.title = NULL;
    splash.tagline = NULL;
    splash.brand_label = NULL;
}

void boot_splash_cancel(void)
{
    lv_obj_t *overlay = splash.overlay;
    forget_widgets();
    drop_timer();
    splash.busy = false;
    splash.phase = BOOT_OFF;
    splash.elapsed = 0;
    if(overlay != NULL) lv_obj_del(overlay);
}

static void set_text_opa(lv_obj_t *label, lv_opa_t opa)
{
    if(label == NULL) return;
    lv_obj_set_style_text_opa(label, opa, LV_PART_MAIN);
}

static void apply_text(uint32_t elapsed)
{
    int title = 96 + clamp_i(((int)elapsed - BOOT_TITLE_AT) * 159 / BOOT_FADE_MS, 0, 159);
    int tag = clamp_i(((int)elapsed - BOOT_TAG_AT) * 180 / BOOT_FADE_MS, 0, 170);
    int emblem = clamp_i((int)elapsed * 155 / BOOT_GATHER_MS, 0, 155);
    if(splash.badge != NULL) {
        lv_obj_set_style_bg_opa(splash.badge, (lv_opa_t)(100 + emblem), LV_PART_MAIN);
        lv_obj_set_style_border_opa(splash.badge, (lv_opa_t)(100 + emblem), LV_PART_MAIN);
    }
    set_text_opa(splash.letter, (lv_opa_t)(160 + emblem * 95 / 155));
    set_text_opa(splash.title, (lv_opa_t)title);
    set_text_opa(splash.tagline, (lv_opa_t)tag);
    set_text_opa(splash.brand_label, elapsed == 0 ? 170 : 210);
}

static void splash_timer(lv_timer_t *timer)
{
    (void)timer;
    if(splash.phase != BOOT_RUN || splash.overlay == NULL) {
        boot_splash_cancel();
        return;
    }
    uint32_t elapsed = lv_tick_elaps(splash.t0);
    if(elapsed > BOOT_MS) elapsed = BOOT_MS;
    splash.elapsed = elapsed;
    apply_text(elapsed);
    if(elapsed >= BOOT_SLIDE_MS) {
        lv_coord_t height = lv_disp_get_ver_res(NULL);
        int travel = (int)elapsed - BOOT_SLIDE_MS;
        int span = BOOT_MS - BOOT_SLIDE_MS;
        lv_obj_set_y(splash.overlay, (lv_coord_t)(-smooth_u8(travel, span) * (int)height / 256));
    }
    if(splash.mark != NULL) lv_obj_invalidate(splash.mark);
    if(elapsed >= BOOT_MS) boot_splash_cancel();
}

static void stroke(lv_draw_ctx_t *draw, lv_color_t color, lv_opa_t opa, lv_coord_t width,
                   lv_coord_t x1, lv_coord_t y1, lv_coord_t x2, lv_coord_t y2,
                   bool cap_start, bool cap_end)
{
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.opa = opa;
    line.width = width;
    /* One round end per segment fills the outside of each join, as lv_line does. */
    line.round_start = cap_start ? 1 : 0;
    line.round_end = cap_end ? 1 : 0;
    lv_point_t a = {x1, y1};
    lv_point_t b = {x2, y2};
    lv_draw_line(draw, &line, &a, &b);
}

/* Degree sine with an 8-bit fraction, interpolated between lv_trigo_sin samples. */
static int sin_deg_q8(int deg_q8)
{
    const int turn = 360 << 8;
    int angle = deg_q8 % turn;
    if(angle < 0) angle += turn;
    int deg = angle >> 8;
    int frac = angle & 255;
    int next = deg >= 359 ? 0 : deg + 1;
    int s0 = lv_trigo_sin(deg);
    int s1 = lv_trigo_sin(next);
    return s0 + (s1 - s0) * frac / 256;
}

static lv_coord_t q8_round(int value)
{
    if(value >= 0) return (lv_coord_t)((value + 128) >> 8);
    return (lv_coord_t)(-(((-value) + 128) >> 8));
}

static void dot(lv_draw_ctx_t *draw, lv_color_t color, lv_opa_t opa, lv_coord_t x, lv_coord_t y, lv_coord_t r)
{
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = color;
    rect.bg_opa = opa;
    rect.radius = LV_RADIUS_CIRCLE;
    lv_area_t area = {(lv_coord_t)(x - r), (lv_coord_t)(y - r), (lv_coord_t)(x + r), (lv_coord_t)(y + r)};
    lv_draw_rect(draw, &rect, &area);
}

static void draw_wave(lv_draw_ctx_t *draw, int elapsed, int which, lv_coord_t ox, lv_coord_t oy)
{
    const int gather = elapsed > BOOT_GATHER_MS ? BOOT_GATHER_MS : elapsed;
    const int amp_q8 = ease_i((72 - which * 6) * 256, (12 + which * 2) * 256, gather, BOOT_GATHER_MS);
    const int y_base_q8 = ease_i((28 + which * 78) * 256, (78 + which * 32) * 256, gather, BOOT_GATHER_MS);
    const int x0_q8 = ease_i((-20 + which * 16) * 256, 108 * 256, gather, BOOT_GATHER_MS);
    const int x1_q8 = ease_i((460 - which * 12) * 256, 320 * 256, gather, BOOT_GATHER_MS);
    const lv_color_t color = lv_color_hex(which == 1 ? 0xE7FFF4 : 0x9EE8C4);
    /* Start at exactly the idle pose; retain fractional degrees between frames. */
    const int drift_q8 = which * 24 * 256 + (splash.phase == BOOT_RUN ? elapsed * 256 / 24 : 0);
    const int sweep_q8 = 224 << 8;
    const lv_coord_t width = (lv_coord_t)(which == 1 ? 6 : 4);
    lv_coord_t px = 0;
    lv_coord_t py = 0;
    for(int i = 0; i <= BOOT_WAVE_SEGS; ++i) {
        int x_q8 = x0_q8 + (x1_q8 - x0_q8) * i / BOOT_WAVE_SEGS;
        int ang_q8 = drift_q8 + which * (21 << 8) + sweep_q8 * i / BOOT_WAVE_SEGS;
        int y_q8 = y_base_q8 + amp_q8 * sin_deg_q8(ang_q8) / 32767;
        y_q8 += (amp_q8 / 4) * sin_deg_q8(ang_q8 * 2) / 32767;
        lv_coord_t ax = (lv_coord_t)(ox + q8_round(x_q8));
        lv_coord_t ay = (lv_coord_t)(oy + q8_round(y_q8));
        if(i > 0) {
            stroke(draw, color, LV_OPA_COVER, width, px, py, ax, ay, i == 1, true);
        }
        px = ax;
        py = ay;
    }
}

static void draw_breeze(lv_event_t *event)
{
    lv_obj_t *overlay = lv_event_get_target(event);
    lv_draw_ctx_t *draw = lv_event_get_draw_ctx(event);
    lv_area_t coords;
    lv_obj_get_coords(overlay, &coords);
    const lv_coord_t ox = coords.x1;
    const lv_coord_t oy = coords.y1;
    const int elapsed = (int)splash.elapsed;
    for(int wave = 0; wave < 3; ++wave) draw_wave(draw, elapsed, wave, ox, oy);

    if(splash.phase == BOOT_RUN && elapsed > BOOT_ARC_AT && elapsed < BOOT_SLIDE_MS) {
        int sweep = (elapsed / 5) % 360;
        lv_point_t center = {(lv_coord_t)(ox + 220), (lv_coord_t)(oy + 120)};
        lv_draw_arc_dsc_t arc;
        lv_draw_arc_dsc_init(&arc);
        arc.color = lv_color_hex(0xF7FFFB);
        arc.opa = 150;
        arc.width = 3;
        arc.rounded = 1;
        lv_draw_arc(draw, &arc, &center, 78, (uint16_t)sweep, (uint16_t)((sweep + 42) % 360));
    }

    static const int16_t seeds_x[8] = {36, 78, 150, 250, 310, 390, 54, 360};
    static const int16_t seeds_y[8] = {24, 180, 48, 30, 200, 70, 220, 150};
    if(splash.phase == BOOT_RUN) {
        for(int i = 0; i < 8; ++i) {
            int ang = (elapsed / 12 + seeds_x[i]) % 360;
            lv_coord_t x = (lv_coord_t)(ox + seeds_x[i] + (18 * lv_trigo_sin(ang)) / 32767);
            lv_coord_t y = (lv_coord_t)(oy + seeds_y[i] + (12 * lv_trigo_sin((ang + 90) % 360)) / 32767);
            lv_opa_t opa = (lv_opa_t)(90 + (i * 17) % 120);
            dot(draw, lv_color_hex(0xD9FFE9), opa, x, y, (lv_coord_t)(i & 1 ? 2 : 3));
        }
    }
}

static void overlay_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_DELETE) {
        if(lv_event_get_target(event) == splash.overlay) {
            forget_widgets();
            drop_timer();
            splash.busy = false;
            splash.phase = BOOT_OFF;
            splash.elapsed = 0;
        }
        return;
    }
    if(code == LV_EVENT_COVER_CHECK) {
        lv_obj_t *overlay = lv_event_get_target(event);
        if(overlay == splash.overlay && splash.busy && lv_obj_get_y(overlay) == 0) {
            /* Opaque full-screen cover. Setting the result directly still wins after
             * the base class marks a non-covering object. */
            lv_cover_check_info_t *info = lv_event_get_param(event);
            if(info != NULL && info->res != LV_COVER_RES_MASKED) info->res = LV_COVER_RES_COVER;
        }
        return;
    }
    if(code == LV_EVENT_DRAW_MAIN && lv_event_get_target(event) == splash.mark) draw_breeze(event);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, const char *text, lv_opa_t opa)
{
    lv_obj_t *label = lv_label_create(parent);
    if(label == NULL) return NULL;
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF4FFF8), LV_PART_MAIN);
    lv_obj_set_style_text_opa(label, opa, LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

void boot_splash_start(const char *version)
{
    (void)version;
    if(lv_disp_get_default() == NULL) return;
    lv_coord_t width = lv_disp_get_hor_res(NULL);
    lv_coord_t height = lv_disp_get_ver_res(NULL);
    if(width < 32 || height < 32) return;

    boot_splash_cancel();

    lv_obj_t *top = lv_layer_top();
    if(top == NULL) return;
    lv_obj_t *overlay = lv_obj_create(top);
    if(overlay == NULL) return;

    splash.overlay = overlay;
    splash.phase = BOOT_IDLE;
    splash.busy = true;
    splash.elapsed = 0;
    splash.t0 = 0;

    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, width, height);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x083428), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(overlay, lv_color_hex(0x04140F), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(overlay, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_main_stop(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_stop(overlay, 255, LV_PART_MAIN);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(overlay, overlay_event, LV_EVENT_ALL, NULL);

    splash.mark = lv_obj_create(overlay);
    if(splash.mark != NULL) {
        lv_obj_remove_style_all(splash.mark);
        lv_obj_set_size(splash.mark, 440, 250);
        lv_obj_align(splash.mark, LV_ALIGN_TOP_MID, 0, 28);
        lv_obj_clear_flag(splash.mark, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(splash.mark, overlay_event, LV_EVENT_DRAW_MAIN, NULL);
    }

    if(splash.mark != NULL) {
        splash.badge = lv_obj_create(splash.mark);
        if(splash.badge != NULL) {
            lv_obj_remove_style_all(splash.badge);
            lv_obj_set_pos(splash.badge, 172, 72);
            lv_obj_set_size(splash.badge, 96, 96);
            lv_obj_set_style_radius(splash.badge, 26, LV_PART_MAIN);
            lv_obj_set_style_bg_color(splash.badge, lv_color_hex(0x0C4734), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(splash.badge, 100, LV_PART_MAIN);
            lv_obj_set_style_border_width(splash.badge, 2, LV_PART_MAIN);
            lv_obj_set_style_border_color(splash.badge, lv_color_hex(0x8EE5BC), LV_PART_MAIN);
            lv_obj_set_style_border_opa(splash.badge, 100, LV_PART_MAIN);
            lv_obj_clear_flag(splash.badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            splash.letter = make_label(splash.badge, &lv_font_montserrat_48, "B", 160);
            if(splash.letter != NULL) lv_obj_center(splash.letter);
        }
    }
    splash.title = make_label(overlay, &lv_font_montserrat_48, BREEZE_OS_NAME, 96);
    if(splash.title != NULL) lv_obj_align(splash.title, LV_ALIGN_TOP_MID, 0, 292);
    splash.tagline = make_label(overlay, &lv_font_montserrat_16, BREEZE_OS_TAGLINE, LV_OPA_TRANSP);
    if(splash.tagline != NULL) lv_obj_align(splash.tagline, LV_ALIGN_TOP_MID, 0, 354);
    splash.brand_label = make_label(overlay, &lv_font_breeze_brand_24, "青禾", 170);
    if(splash.brand_label != NULL) {
        lv_obj_set_style_text_letter_space(splash.brand_label, 6, LV_PART_MAIN);
        lv_obj_set_style_text_align(splash.brand_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(splash.brand_label, lv_color_hex(0xC9E6D4), LV_PART_MAIN);
        lv_obj_align(splash.brand_label, LV_ALIGN_BOTTOM_MID, 0, -28);
    }
}

void boot_splash_ready(void)
{
    if(!splash.busy || splash.phase != BOOT_IDLE || splash.overlay == NULL) return;
    lv_obj_move_foreground(splash.overlay);
    splash.phase = BOOT_RUN;
    splash.t0 = lv_tick_get();
    splash.elapsed = 0;
    splash.timer = lv_timer_create(splash_timer, BOOT_PERIOD_MS, NULL);
    if(splash.timer == NULL) boot_splash_cancel();
}

bool boot_splash_busy(void)
{
    return splash.busy;
}
