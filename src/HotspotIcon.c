#include "HotspotIcon.h"

/* A central dot with two radio waves on either side, distinct from WiFi. */
static void draw_hotspot(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_target(event);
    lv_draw_ctx_t *draw = lv_event_get_draw_ctx(event);
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    const lv_coord_t size = LV_MIN(lv_area_get_width(&area), lv_area_get_height(&area));
    const lv_point_t center = {(lv_coord_t)((area.x1 + area.x2) / 2),
                               (lv_coord_t)((area.y1 + area.y2) / 2)};
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = lv_obj_get_style_text_color(object, LV_PART_MAIN);
    arc.width = LV_MAX(3, size / 14);
    arc.rounded = 1;
    const lv_coord_t half = (lv_coord_t)((arc.width + 1) / 2);
    for (int wave = 0; wave < 2; ++wave) {
        const uint16_t radius = (uint16_t)(size * (6 + wave * 4) / 24 + half);
        lv_draw_arc(draw, &arc, &center, radius, 305, 55);
        lv_draw_arc(draw, &arc, &center, radius, 125, 235);
    }
    const lv_coord_t radius = LV_MAX(2, size / 11);
    lv_area_t dot = {(lv_coord_t)(center.x - radius), (lv_coord_t)(center.y - radius),
                     (lv_coord_t)(center.x + radius), (lv_coord_t)(center.y + radius)};
    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color = arc.color;
    fill.radius = LV_RADIUS_CIRCLE;
    lv_draw_rect(draw, &fill, &dot);
}

lv_obj_t *hotspot_icon_create(lv_obj_t *parent, lv_coord_t size)
{
    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, size, size);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(icon, draw_hotspot, LV_EVENT_DRAW_MAIN, NULL);
    return icon;
}
