// Included only by the hardware build with VIDEO_DRAW_VERIFY defined.
// The oracle is the actual LVGL renderer, not a second copy of the fast algorithm.
static lv_color_t check_pattern[23 * 17];

static uint32_t check_draw_case(lv_draw_ctx_t *ctx, const lv_draw_img_dsc_t *dsc,
                               const lv_area_t *coords, const uint8_t *pixels,
                               const lv_area_t &bounds, const lv_area_t &clip,
                               uint32_t &checked)
{
    uint32_t mismatches = 0;
    const int width = lv_area_get_width(&bounds);
    const int height = lv_area_get_height(&bounds);
    const size_t size = width * height * sizeof(lv_color_t);
    // The existing LVGL pool has space for the oracle; keep scarce external
    // allocations available for the three wallpaper images and RVF buffers.
    auto *expected = static_cast<lv_color_t *>(lv_mem_alloc(size));
    auto *actual = static_cast<lv_color_t *>(lv_mem_alloc(size));
    if (!expected || !actual) {
        lv_mem_free(expected);
        lv_mem_free(actual);
        Serial.println("[VIDEO DRAW CHECK] allocation failed");
        return 1;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x)
            expected[y * width + x].full = actual[y * width + x].full =
                static_cast<uint16_t>((x * 131u + y * 733u) ^ 0xa55a);
    }
    lv_draw_sw_ctx_t reference = *reinterpret_cast<lv_draw_sw_ctx_t *>(ctx);
    lv_draw_sw_ctx_t candidate = reference;
    lv_area_t buffer_area = bounds;
    reference.base_draw.buf = expected;
    candidate.base_draw.buf = actual;
    reference.base_draw.buf_area = candidate.base_draw.buf_area = &buffer_area;
    reference.base_draw.clip_area = candidate.base_draw.clip_area = &clip;
    lv_draw_sw_img_decoded(&reference.base_draw, dsc, coords, pixels, LV_IMG_CF_TRUE_COLOR);
    if (!draw_scaled_rgb565(&candidate.base_draw, dsc, coords, pixels))
        ++mismatches;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int index = y * width + x;
            ++checked;
            if (expected[index].full != actual[index].full) {
                if (mismatches < 3)
                    Serial.printf("[VIDEO DRAW CHECK] mismatch x=%d y=%d old=%04x new=%04x\n",
                                  bounds.x1 + x, bounds.y1 + y, expected[index].full, actual[index].full);
                ++mismatches;
            }
        }
    }
    lv_mem_free(expected);
    lv_mem_free(actual);
    return mismatches;
}

static void verify_video_blend(lv_draw_ctx_t *ctx)
{
    lv_area_t bounds = {-7, -3, 63, 43};
    const lv_area_t clips[] = {bounds, {-4, 0, 55, 41}, {12, 0, 12, 41}};
    const uint16_t colors[] = {0, 0xffff, 0x39ef};
    const uint8_t opacities[] = {255, LV_OPA_MAX, 160, 0};
    const int count = lv_area_get_size(&bounds);
    auto *expected = static_cast<lv_color_t *>(lv_mem_alloc(count * sizeof(lv_color_t)));
    auto *actual = static_cast<lv_color_t *>(lv_mem_alloc(count * sizeof(lv_color_t)));
    if (!expected || !actual) {
        lv_mem_free(expected);
        lv_mem_free(actual);
        Serial.println("[VIDEO DRAW CHECK] blend allocation failed");
        return;
    }
    uint32_t checked = 0, mismatches = 0;
    for (const auto &clip : clips) {
        for (uint16_t color : colors) {
            for (uint8_t opacity : opacities) {
                for (int i = 0; i < count; ++i)
                    expected[i].full = actual[i].full = static_cast<uint16_t>(i * 0x1793u + 0x37au);
                lv_draw_sw_ctx_t reference = *reinterpret_cast<lv_draw_sw_ctx_t *>(ctx);
                lv_draw_sw_ctx_t candidate = reference;
                reference.base_draw.buf = expected;
                candidate.base_draw.buf = actual;
                reference.base_draw.buf_area = candidate.base_draw.buf_area = &bounds;
                reference.base_draw.clip_area = candidate.base_draw.clip_area = &clip;
                lv_draw_sw_blend_dsc_t blend = {};
                blend.blend_area = &bounds;
                blend.color.full = color;
                blend.opa = opacity;
                blend.blend_mode = LV_BLEND_MODE_NORMAL;
                lv_draw_sw_blend_basic(&reference.base_draw, &blend);
                video_blend(&candidate.base_draw, &blend);
                for (int i = 0; i < count; ++i) {
                    ++checked;
                    if (expected[i].full != actual[i].full) ++mismatches;
                }
            }
        }
    }
    lv_mem_free(expected);
    lv_mem_free(actual);
    Serial.printf("[VIDEO DRAW CHECK] blend checked=%lu mismatches=%lu cases=36\n",
                  static_cast<unsigned long>(checked), static_cast<unsigned long>(mismatches));
}

static void verify_video_draw(lv_draw_ctx_t *ctx, const lv_draw_img_dsc_t *dsc,
                              const lv_area_t *coords, const uint8_t *pixels)
{
    uint32_t checked = 0;
    const lv_area_t full_screen = {0, 0, 479, 479};
    uint32_t mismatches = check_draw_case(ctx, dsc, coords, pixels,
                                         full_screen, full_screen, checked);
    for (int i = 0; i < 23 * 17; ++i)
        check_pattern[i].full = static_cast<uint16_t>((i * 0x23f7u + 0x1357u) ^ (i >> 2));
    const uint16_t zooms[] = {257, 384, 511, 1024};
    const int sizes[][2] = {{23, 17}, {2, 2}, {1, 1}};
    for (const auto &size : sizes) {
        for (uint16_t zoom : zooms) {
            lv_draw_img_dsc_t test = *dsc;
            test.zoom = zoom;
            test.pivot = {static_cast<lv_coord_t>(size[0] / 2),
                          static_cast<lv_coord_t>(size[1] / 2)};
            const lv_area_t coordinates = {12, 8, static_cast<lv_coord_t>(11 + size[0]),
                                           static_cast<lv_coord_t>(7 + size[1])};
            const lv_area_t bounds = {-7, -3, 63, 43};
            const lv_area_t clip = {-4, 0, 55, 41};
            mismatches += check_draw_case(ctx, &test, &coordinates,
                                           reinterpret_cast<const uint8_t *>(check_pattern),
                                           bounds, clip, checked);
            const lv_area_t single_column = {12, 0, 12, 41};
            mismatches += check_draw_case(ctx, &test, &coordinates,
                                           reinterpret_cast<const uint8_t *>(check_pattern),
                                           bounds, single_column, checked);
        }
    }
    Serial.printf("[VIDEO DRAW CHECK] checked=%lu mismatches=%lu cases=25\n",
                  static_cast<unsigned long>(checked), static_cast<unsigned long>(mismatches));
    verify_video_blend(ctx);
    // Compare a complete composition, including the skipped opaque background.
    auto *expected = static_cast<lv_color_t *>(lv_mem_alloc(480 * 480 * sizeof(lv_color_t)));
    auto *actual = static_cast<lv_color_t *>(lv_mem_alloc(480 * 480 * sizeof(lv_color_t)));
    if (!expected || !actual) {
        Serial.println("[VIDEO DRAW CHECK] composition allocation failed");
    } else {
        for (int i = 0; i < 480 * 480; ++i)
            expected[i].full = actual[i].full = static_cast<uint16_t>(i * 0x239du + 0x174au);
        lv_draw_sw_ctx_t reference = *reinterpret_cast<lv_draw_sw_ctx_t *>(ctx);
        lv_draw_sw_ctx_t candidate = reference;
        lv_area_t bounds = full_screen;
        reference.base_draw.buf = expected;
        candidate.base_draw.buf = actual;
        reference.base_draw.buf_area = candidate.base_draw.buf_area = &bounds;
        reference.base_draw.clip_area = candidate.base_draw.clip_area = &bounds;
        reference.blend = lv_draw_sw_blend_basic;
        lv_draw_sw_blend_dsc_t background = {};
        background.blend_area = &bounds;
        background.color = lv_color_black();
        background.opa = LV_OPA_COVER;
        background.blend_mode = LV_BLEND_MODE_NORMAL;
        lv_draw_sw_blend_basic(&reference.base_draw, &background);
        const bool old_paint = video_screen_paint;
        video_screen_paint = true;
        lv_area_t opaque;
        const bool skipped = video_opaque_area(opaque);
        video_blend(&candidate.base_draw, &background);
        video_screen_paint = old_paint;
        lv_draw_sw_img_decoded(&reference.base_draw, dsc, coords, pixels, LV_IMG_CF_TRUE_COLOR);
        draw_scaled_rgb565(&candidate.base_draw, dsc, coords, pixels);
        uint32_t differences = 0;
        for (int i = 0; i < 480 * 480; ++i)
            if (expected[i].full != actual[i].full) ++differences;
        Serial.printf("[VIDEO DRAW CHECK] composition checked=230400 mismatches=%lu skipped=%u\n",
                      static_cast<unsigned long>(differences), skipped);
    }
    lv_mem_free(expected);
    lv_mem_free(actual);
}
