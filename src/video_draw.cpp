#include "video_draw.h"

#include "SDcar.h"
#include <src/draw/sw/lv_draw_sw.h>
#include <esp_attr.h>
#include <cstring>
#ifdef VIDEO_DRAW_VERIFY
#include <Arduino.h>
#endif

extern "C" bool video_playback_active(void);

namespace {
constexpr int kMaxWidth = 480;
static lv_obj_t *video_image = nullptr;
static bool video_screen_paint = false;

static void video_screen_draw_event(lv_event_t *event)
{
    video_screen_paint = lv_event_get_code(event) == LV_EVENT_DRAW_MAIN_BEGIN;
}

static void video_image_delete_event(lv_event_t *event)
{
    if (lv_event_get_target(event) != video_image) return;
    while (lv_obj_remove_event_cb(lv_obj_get_parent(video_image), video_screen_draw_event)) {}
    video_image = nullptr;
    video_screen_paint = false;
}

struct SampleX {
    int16_t index;
    int16_t neighbor;
    uint8_t weight;
    uint8_t edge_alpha;
};

// LVGL draws on one task. These small work buffers stay in internal SRAM.
static DRAM_ATTR SampleX samples[kMaxWidth];
static DRAM_ATTR lv_color_t source_row[kMaxWidth];
static DRAM_ATTR lv_color_t output_row[kMaxWidth];
static DRAM_ATTR uint32_t packed_rows[2][kMaxWidth];
static DRAM_ATTR uint32_t horizontal_row[kMaxWidth];
constexpr uint32_t kPackedMask = 0x07e0f81f;

// LVGL's masked RGB565 representation survives each mix unchanged. Keeping
// rows in this form avoids packing/unpacking each of the three AA samples.
static inline __attribute__((always_inline)) uint32_t pack_rgb565(lv_color_t color)
{
    return (color.full | (uint32_t(color.full) << 16)) & kPackedMask;
}

static inline __attribute__((always_inline)) lv_color_t unpack_rgb565(uint32_t packed)
{
    lv_color_t result;
    result.full = packed | (packed >> 16);
    return result;
}

static inline __attribute__((always_inline)) uint32_t mix_packed(
    uint32_t foreground, uint32_t background, uint8_t weight5)
{
    return (background + (((foreground - background) * weight5) >> 5)) & kPackedMask;
}

static inline __attribute__((always_inline)) void load_packed_row(
    uint32_t *packed, const lv_color_t *pixels, int width)
{
    std::memcpy(source_row, pixels, width * sizeof(lv_color_t));
    for (int x = 0; x < width; ++x) packed[x] = pack_rgb565(source_row[x]);
}

static uint8_t aa_weight(int32_t coordinate, int &direction)
{
    const int fraction = coordinate & 255;
    direction = fraction < 128 ? -1 : 1;
    return fraction < 128 ? (127 - fraction) * 2 : (fraction - 128) * 2;
}

static int32_t floor_div(int32_t numerator, int32_t denominator)
{
    return numerator >= 0 ? numerator / denominator : -((-numerator + denominator - 1) / denominator);
}

static bool video_opaque_area(lv_area_t &area)
{
    const lv_img_dsc_t *frame = get_video_frame_image();
    lv_obj_t *image = video_image;
    lv_obj_t *screen = lv_scr_act();
    if (!image || !frame || lv_obj_get_parent(image) != screen ||
        lv_img_get_src(image) != frame || lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN) ||
        lv_img_get_offset_x(image) || lv_img_get_offset_y(image) ||
        lv_img_get_angle(image) || !lv_img_get_antialias(image) ||
        lv_img_get_zoom(image) <= LV_IMG_ZOOM_NONE ||
        lv_obj_get_width(image) != frame->header.w || lv_obj_get_height(image) != frame->header.h ||
        lv_obj_get_style_opa(image, LV_PART_MAIN) != LV_OPA_COVER ||
        lv_obj_get_style_img_opa(image, LV_PART_MAIN) != LV_OPA_COVER ||
        lv_obj_get_style_img_recolor_opa(image, LV_PART_MAIN) != LV_OPA_TRANSP ||
        lv_obj_get_style_blend_mode(image, LV_PART_MAIN) != LV_BLEND_MODE_NORMAL ||
        lv_obj_get_style_transform_angle(image, LV_PART_MAIN) ||
        lv_obj_get_style_transform_zoom(image, LV_PART_MAIN) != LV_IMG_ZOOM_NONE ||
        lv_obj_get_style_transform_width(image, LV_PART_MAIN) ||
        lv_obj_get_style_transform_height(image, LV_PART_MAIN) ||
        lv_obj_get_style_pad_left(image, LV_PART_MAIN) || lv_obj_get_style_pad_right(image, LV_PART_MAIN) ||
        lv_obj_get_style_pad_top(image, LV_PART_MAIN) || lv_obj_get_style_pad_bottom(image, LV_PART_MAIN) ||
        lv_obj_get_style_opa(screen, LV_PART_MAIN) != LV_OPA_COVER ||
        lv_obj_get_style_transform_angle(screen, LV_PART_MAIN) ||
        lv_obj_get_style_transform_zoom(screen, LV_PART_MAIN) != LV_IMG_ZOOM_NONE) return false;
    lv_area_t coordinates;
    lv_obj_get_coords(image, &coordinates);
    lv_point_t pivot;
    lv_img_get_pivot(image, &pivot);
    const int32_t step = 65536 / lv_img_get_zoom(image);
    // Both neighbors are inside the source in this rectangle. The scaler
    // always overwrites these pixels; faded borders still need their background.
    const int32_t left = coordinates.x1 + pivot.x - floor_div(pivot.x * 256 - 128, step);
    const int32_t top = coordinates.y1 + pivot.y - floor_div(pivot.y * 256 - 128, step);
    const int32_t right = coordinates.x1 + pivot.x + floor_div(frame->header.w * 256 - 129 - pivot.x * 256, step);
    const int32_t bottom = coordinates.y1 + pivot.y + floor_div(frame->header.h * 256 - 129 - pivot.y * 256, step);
    // Clip before narrowing to LVGL coordinates (very large zooms can extend far off-screen).
    area = {static_cast<lv_coord_t>(LV_MAX(0, left)), static_cast<lv_coord_t>(LV_MAX(0, top)),
            static_cast<lv_coord_t>(LV_MIN(479, right)), static_cast<lv_coord_t>(LV_MIN(479, bottom))};
    return left <= right && top <= bottom && left <= 479 && top <= 479 && right >= 0 && bottom >= 0;
}

static void IRAM_ATTR __attribute__((optimize("O3"))) fill_opaque_area(
    lv_draw_ctx_t *ctx, const lv_area_t &area, lv_color_t color)
{
    if (area.x1 > area.x2 || area.y1 > area.y2) return;
    const int width = lv_area_get_width(&area), height = lv_area_get_height(&area);
    const int stride = lv_area_get_width(ctx->buf_area);
    auto *row = static_cast<lv_color_t *>(ctx->buf) +
                (area.y1 - ctx->buf_area->y1) * stride + area.x1 - ctx->buf_area->x1;
    if ((color.full & 255) == (color.full >> 8) && width == stride) {
        std::memset(row, color.full & 255, width * height * sizeof(lv_color_t));
    } else {
        for (int x = 0; x < width; ++x) output_row[x] = color;
        for (int y = 0; y < height; ++y) {
            std::memcpy(row, output_row, width * sizeof(lv_color_t));
            row += stride;
        }
    }
}

// Exact LVGL 8.3.1 RGB565, angle-zero AA sampling, including faded edges.
// Only opaque, unmasked true-color images enter this path.
static bool IRAM_ATTR __attribute__((optimize("O3"))) draw_scaled_rgb565(lv_draw_ctx_t *ctx, const lv_draw_img_dsc_t *dsc,
                               const lv_area_t *coords, const uint8_t *pixels)
{
    const int source_width = lv_area_get_width(coords);
    const int source_height = lv_area_get_height(coords);
    lv_area_t area;
    if (!_lv_area_intersect(&area, ctx->clip_area, ctx->buf_area)) return true;
    const int width = lv_area_get_width(&area);
    if (width > kMaxWidth || source_width > kMaxWidth ||
        source_width < 1 || source_height < 1) return false;

    const int32_t inverse_zoom = 65536 / dsc->zoom;
    const int32_t x_start = (area.x1 - coords->x1 - dsc->pivot.x) * inverse_zoom +
                            dsc->pivot.x * 256;
    int first = -1, last = -1;
    for (int x = 0; x < width; ++x) {
        const int32_t position = x_start + x * inverse_zoom;
        const int index = position >> 8;
        int direction;
        const uint8_t weight = aa_weight(position, direction);
        samples[x] = {static_cast<int16_t>(index), static_cast<int16_t>(index + direction),
                      static_cast<uint8_t>((weight + 4) >> 3),
                      static_cast<uint8_t>((255 * (255 - weight)) >> 8)};
        if (index >= 0 && index < source_width) {
            if (first < 0) first = x;
            last = x;
        }
    }
    if (first < 0) return true;
    int interior_first = first, interior_last = last;
    while (interior_first <= last &&
           (samples[interior_first].neighbor < 0 || samples[interior_first].neighbor >= source_width))
        ++interior_first;
    while (interior_last >= interior_first &&
           (samples[interior_last].neighbor < 0 || samples[interior_last].neighbor >= source_width))
        --interior_last;

    if (ctx->wait_for_finish) ctx->wait_for_finish(ctx);
    const auto *source = reinterpret_cast<const lv_color_t *>(pixels);
    auto *destination = static_cast<lv_color_t *>(ctx->buf);
    const int destination_stride = lv_area_get_width(ctx->buf_area);
    int cached_source_y = -1, cached_neighbor_y = -1;
    uint32_t *source_packed = packed_rows[0], *neighbor_packed = packed_rows[1];

    for (int y = area.y1; y <= area.y2; ++y) {
        const int32_t position = (y - coords->y1 - dsc->pivot.y) * inverse_zoom +
                                  dsc->pivot.y * 256;
        const int source_y = position >> 8;
        if (source_y < 0 || source_y >= source_height) continue;
        int direction;
        const uint8_t weight_y = aa_weight(position, direction);
        const uint8_t weight5_y = (weight_y + 4) >> 3;
        const uint8_t alpha_y = (255 * (255 - weight_y)) >> 8;
        const int neighbor_y = source_y + direction;
        const bool neighbor_valid = neighbor_y >= 0 && neighbor_y < source_height;
        if (cached_source_y != source_y) {
            if (cached_neighbor_y == source_y) {
                uint32_t *old_source = source_packed;
                source_packed = neighbor_packed;
                neighbor_packed = old_source;
                cached_neighbor_y = cached_source_y;
            } else {
                load_packed_row(source_packed, source + source_y * source_width, source_width);
            }
            cached_source_y = source_y;
            for (int x = interior_first; x <= interior_last; ++x) {
                const SampleX &sample = samples[x];
                horizontal_row[x] = mix_packed(source_packed[sample.neighbor],
                                               source_packed[sample.index], sample.weight);
            }
        }
        if (neighbor_valid && cached_neighbor_y != neighbor_y) {
            load_packed_row(neighbor_packed, source + neighbor_y * source_width, source_width);
            cached_neighbor_y = neighbor_y;
        }
        lv_color_t *row = destination + (y - ctx->buf_area->y1) * destination_stride +
                                      (area.x1 - ctx->buf_area->x1);
        if (neighbor_valid) {
            for (int x = interior_first; x <= interior_last; ++x) {
                const int index = samples[x].index;
                const uint32_t vertical = mix_packed(neighbor_packed[index],
                                                      source_packed[index], weight5_y);
                // LV_OPA_50 quantizes to 16/32: independent channel floor means.
                output_row[x] = unpack_rgb565(((horizontal_row[x] + vertical) >> 1) & kPackedMask);
            }
        } else {
            for (int x = interior_first; x <= interior_last; ++x)
                output_row[x] = unpack_rgb565(mix_packed(source_packed[samples[x].index],
                                                         pack_rgb565(row[x]), (alpha_y + 4) >> 3));
        }
        // LVGL gives the horizontal edge priority at a corner.
        for (int x = first; x < interior_first; ++x) {
            const SampleX &sample = samples[x];
            output_row[x] = unpack_rgb565(mix_packed(source_packed[sample.index],
                                                     pack_rgb565(row[x]), (sample.edge_alpha + 4) >> 3));
        }
        for (int x = interior_last + 1; x <= last; ++x) {
            const SampleX &sample = samples[x];
            output_row[x] = unpack_rgb565(mix_packed(source_packed[sample.index],
                                                     pack_rgb565(row[x]), (sample.edge_alpha + 4) >> 3));
        }
        std::memcpy(row + first, output_row + first, (last - first + 1) * sizeof(lv_color_t));
    }
    return true;
}

#ifdef VIDEO_DRAW_VERIFY
static void video_blend(lv_draw_ctx_t *ctx, const lv_draw_sw_blend_dsc_t *dsc);
#include "../tests/video_draw_reference_check.inc"
#endif

static void video_img_decoded(lv_draw_ctx_t *ctx, const lv_draw_img_dsc_t *dsc,
                              const lv_area_t *coords, const uint8_t *pixels, lv_img_cf_t cf)
{
#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0 && !LV_COLOR_SCREEN_TRANSP && LV_COLOR_MIX_ROUND_OFS == 0
    const lv_img_dsc_t *frame = get_video_frame_image();
    lv_disp_t *display = _lv_refr_get_disp_refreshing();
    if (frame && pixels == frame->data && cf == LV_IMG_CF_TRUE_COLOR &&
        dsc->angle == 0 && dsc->zoom > LV_IMG_ZOOM_NONE && dsc->antialias &&
        dsc->opa == LV_OPA_COVER && dsc->recolor_opa == LV_OPA_TRANSP &&
        dsc->blend_mode == LV_BLEND_MODE_NORMAL &&
        display && !display->driver->set_px_cb && !display->driver->screen_transp &&
        lv_area_get_width(coords) == frame->header.w &&
        lv_area_get_height(coords) == frame->header.h &&
        !lv_draw_mask_is_any(ctx->clip_area)) {
#ifdef VIDEO_DRAW_VERIFY
        static bool verified = false;
        if (!verified) {
            verified = true;
            verify_video_draw(ctx, dsc, coords, pixels);
        }
#endif
        if (draw_scaled_rgb565(ctx, dsc, coords, pixels)) return;
    }
#endif
    lv_draw_sw_img_decoded(ctx, dsc, coords, pixels, cf);
}

static void IRAM_ATTR __attribute__((optimize("O3"))) video_blend(
    lv_draw_ctx_t *ctx, const lv_draw_sw_blend_dsc_t *dsc)
{
#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0 && !LV_COLOR_SCREEN_TRANSP
    // During playback, opaque backgrounds can use the ROM's bulk stores.
    // Alpha, masks, gradients and image blends keep the standard LVGL path.
    lv_disp_t *display = _lv_refr_get_disp_refreshing();
    if (video_playback_active() && !dsc->src_buf && dsc->opa >= LV_OPA_MAX &&
        dsc->blend_mode == LV_BLEND_MODE_NORMAL &&
        (!dsc->mask_buf || dsc->mask_res == LV_DRAW_MASK_RES_FULL_COVER) &&
        display && !display->driver->set_px_cb && !display->driver->screen_transp) {
        lv_area_t area;
        if (!_lv_area_intersect(&area, dsc->blend_area, ctx->clip_area) ||
            !_lv_area_intersect(&area, &area, ctx->buf_area)) return;
        const int width = lv_area_get_width(&area);
        if (width <= kMaxWidth) {
            lv_area_t opaque, covered;
            if (video_screen_paint && display->driver->full_refresh && area.x1 == 0 && area.y1 == 0 &&
                area.x2 == 479 && area.y2 == 479 && !lv_draw_mask_is_any(&area) &&
                video_opaque_area(opaque) && _lv_area_intersect(&covered, &area, &opaque)) {
                fill_opaque_area(ctx, {area.x1, area.y1, area.x2, static_cast<lv_coord_t>(covered.y1 - 1)}, dsc->color);
                fill_opaque_area(ctx, {area.x1, static_cast<lv_coord_t>(covered.y2 + 1), area.x2, area.y2}, dsc->color);
                fill_opaque_area(ctx, {area.x1, covered.y1, static_cast<lv_coord_t>(covered.x1 - 1), covered.y2}, dsc->color);
                fill_opaque_area(ctx, {static_cast<lv_coord_t>(covered.x2 + 1), covered.y1, area.x2, covered.y2}, dsc->color);
            } else {
                fill_opaque_area(ctx, area, dsc->color);
            }
            return;
        }
    }
#endif
    lv_draw_sw_blend_basic(ctx, dsc);
}
} // namespace

void video_draw_init_ctx(lv_disp_drv_t *driver, lv_draw_ctx_t *ctx)
{
    lv_draw_sw_init_ctx(driver, ctx);
    ctx->draw_img_decoded = video_img_decoded;
    reinterpret_cast<lv_draw_sw_ctx_t *>(ctx)->blend = video_blend;
}

void video_draw_set_image(lv_obj_t *image)
{
    if (video_image) {
        while (lv_obj_remove_event_cb(lv_obj_get_parent(video_image), video_screen_draw_event)) {}
        lv_obj_remove_event_cb(video_image, video_image_delete_event);
    }
    video_image = image;
    video_screen_paint = false;
    if (image) {
        lv_obj_add_event_cb(lv_obj_get_parent(image), video_screen_draw_event, LV_EVENT_DRAW_MAIN_BEGIN, nullptr);
        lv_obj_add_event_cb(lv_obj_get_parent(image), video_screen_draw_event, LV_EVENT_DRAW_MAIN_END, nullptr);
        lv_obj_add_event_cb(image, video_image_delete_event, LV_EVENT_DELETE, nullptr);
    }
}
