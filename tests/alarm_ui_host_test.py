"""Host-compile production AlarmUI.c against real LVGL, UITheme and UiTransition."""
from pathlib import Path
import shutil
import subprocess
from PIL import Image
from music_playlist_ui_host_test import HARNESS

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools' / 'alarm-ai-20261116' / 'ui-host'
SOURCE = r'''
#include "lvgl.h"
#include "AlarmService.h"
#include "ScreenIdle.h"
#include "EdgeBack.h"
#include "UiTransition.h"
#include "UITheme.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static lv_color_t pixels[480 * 480];
lv_font_t *chinese_font_20;
static alarm_snapshot_t snap;
static alarm_config_t saved_config;
static unsigned save_requests, dismisses, wakes, back_fires;
static bool fail_save, have_save;
static lv_obj_t *bound_screen;
static lv_event_cb_t bound_back;
static bool edge_down;
static int edge_origin;

void alarm_get_snapshot(alarm_snapshot_t *out) { *out = snap; }
bool alarm_request_save(const alarm_config_t *config, uint32_t *request_id)
{
    assert(config);
    ++save_requests;
    if (fail_save) return false;
    saved_config = *config;
    have_save = true;
    snap.save_busy = true;
    snap.save_failed = false;
    snap.request_id = save_requests;
    if (request_id) *request_id = snap.request_id;
    return true;
}
void alarm_dismiss(void)
{
    ++dismisses;
    snap.ringing = false;
    snap.ring_seconds_left = 0;
}
void screen_idle_request_wake(void) { ++wakes; }
void edge_back_init(void) {}
void edge_back_bind(lv_obj_t *screen, lv_event_cb_t callback, void *user_data)
{
    (void)user_data;
    bound_screen = screen;
    bound_back = callback;
}
bool edge_back_is_active(void) { return edge_down; }
bool edge_back_pointer(bool pressed, int x, int y, bool enabled)
{
    (void)y;
    if (!enabled || !bound_back || !bound_screen) return false;
    if (pressed) {
        if (!edge_down) {
            if (x > 20 || lv_scr_act() != bound_screen) return false;
            edge_down = true;
            edge_origin = x;
        }
        return true;
    }
    bool fire = edge_down && x - edge_origin >= 72 && lv_scr_act() == bound_screen;
    edge_down = false;
    if (!fire) return false;
    ++back_fires;
    bound_back(NULL);
    return true;
}

#include "@ROOT@/src/AlarmUI.c"
@DRIVER@

static void settle(void)
{
    advance(500);
    assert(!ui_transition_busy());
}
static bool capture_frame(lv_color_t *destination, size_t count, void *context)
{
    (void)context;
    assert(count == 480u * 480u);
    memcpy(destination, pixels, sizeof(pixels));
    return true;
}
static void swipe_back(void)
{
    edge_back_pointer(true, 1, 220, true);
    edge_back_pointer(true, 96, 220, true);
    edge_back_pointer(false, 96, 220, true);
    settle();
}
static void choose_repeat(unsigned mode)
{
    lv_dropdown_set_selected(repeat, mode);
    lv_event_send(repeat, LV_EVENT_VALUE_CHANGED, NULL);
}
static void publish_saved(uint32_t seconds)
{
    assert(have_save);
    snap.config = saved_config;
    snap.save_busy = false;
    snap.save_failed = false;
    snap.saved_request = snap.request_id;
    snap.revision++;
    snap.time_valid = true;
    snap.next_epoch = seconds ? snap.now_epoch + seconds : 0;
    snap.seconds_to_alarm = seconds;
    snap.ringing = false;
}
static lv_obj_t *stop_button(void)
{
    lv_obj_t *card = lv_obj_get_child(alert, 0);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(card); ++i) {
        lv_obj_t *child = lv_obj_get_child(card, i);
        if (lv_obj_check_type(child, &lv_btn_class)) return child;
    }
    assert(0);
    return NULL;
}
static unsigned option_lines(const char *text)
{
    unsigned lines = 1;
    for (const char *p = text; *p; ++p) if (*p == '\n') ++lines;
    return lines;
}

int main(int argc, char **argv)
{
@INIT@
    ui_transition_set_capture(capture_frame, NULL);
    lv_obj_t *home = lv_obj_create(NULL);
    lv_obj_set_size(home, 480, 480);
    lv_scr_load(home);
    ui_transition_set_desktop(home);
    advance(50);

    memset(&snap, 0, sizeof(snap));
    snap.config.hour = 7;
    snap.config.minute = 30;
    snap.config.ring_minutes = 1;
    snap.config.enabled = true;
    snap.now_epoch = 1700000000;
    snap.time_valid = true;
    snap.next_epoch = snap.now_epoch + 3661;
    snap.seconds_to_alarm = 3661;
    snap.revision = 4;

    alarm_ui_open(home, chinese_font_20);
    settle();
    assert(lv_scr_act() == page && chinese && font == chinese_font_20);
    assert(bound_screen == page && bound_back);
    assert(lv_obj_get_child_cnt(page) >= 6);
    assert(lv_roller_get_option_cnt(hour) == 24 && lv_roller_get_option_cnt(minute) == 60);
    assert(option_lines(lv_roller_get_options(hour)) == 24);
    assert(option_lines(lv_roller_get_options(minute)) == 60);
    assert(strncmp(lv_roller_get_options(hour), "00\n", 3) == 0);
    assert(strstr(lv_roller_get_options(hour), "\n23") && !strstr(lv_roller_get_options(hour), "24"));
    assert(strncmp(lv_roller_get_options(minute), "00\n", 3) == 0);
    assert(strstr(lv_roller_get_options(minute), "\n59"));
    assert(lv_roller_get_selected(hour) == 7 && lv_roller_get_selected(minute) == 30);
    assert(lv_dropdown_get_selected(repeat) == 0 && mask == 0);
    assert(lv_dropdown_get_option_cnt(duration) == 60 && lv_dropdown_get_selected(duration) == 0);
    assert(lv_obj_has_state(enabled, LV_STATE_CHECKED));
    assert(strstr(lv_label_get_text(remaining), "还有1小时1分提醒"));
    assert(lv_obj_get_width(page) == 480 && lv_obj_get_height(page) == 480);
    screenshot(argv[2], "alarm-main");

    lv_obj_t *body = NULL;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(page); ++i) {
        lv_obj_t *child = lv_obj_get_child(page, i);
        if (lv_obj_get_height(child) == 270) body = child;
    }
    assert(body && lv_obj_get_parent(hour) && lv_obj_get_parent(lv_obj_get_parent(hour)) == body);
    lv_obj_scroll_to_y(body, 1000, LV_ANIM_OFF);
    advance(80);
    assert(lv_obj_get_scroll_y(body) > 40);
    screenshot(argv[2], "alarm-scrolled");
    lv_obj_scroll_to_y(body, 0, LV_ANIM_OFF);
    advance(40);

    lv_roller_set_selected(hour, 0, LV_ANIM_OFF);
    lv_roller_set_selected(minute, 0, LV_ANIM_OFF);
    lv_roller_set_selected(hour, 23, LV_ANIM_OFF);
    lv_event_send(hour, LV_EVENT_VALUE_CHANGED, NULL);
    lv_roller_set_selected(minute, 59, LV_ANIM_OFF);
    lv_event_send(minute, LV_EVENT_VALUE_CHANGED, NULL);
    assert(lv_roller_get_selected(hour) == 23 && lv_roller_get_selected(minute) == 59);
    assert(edited && time_edited);

    choose_repeat(1);
    assert(mask == 127 && lv_dropdown_get_selected(repeat) == 1);
    choose_repeat(2);
    assert(mask == 31 && lv_dropdown_get_selected(repeat) == 2);
    choose_repeat(0);
    assert(mask == 0);
    for (unsigned day = 0; day < 7; ++day) lv_event_send(days[day], LV_EVENT_CLICKED, NULL);
    assert(mask == 127 && lv_dropdown_get_selected(repeat) == 3);
    lv_event_send(days[0], LV_EVENT_CLICKED, NULL);
    lv_event_send(days[6], LV_EVENT_CLICKED, NULL);
    assert(mask == 0x3E && lv_dropdown_get_selected(repeat) == 3);
    for (unsigned day = 0; day < 7; ++day) {
        if (mask & (1u << day)) lv_event_send(days[day], LV_EVENT_CLICKED, NULL);
    }
    assert(mask == 0 && lv_dropdown_get_selected(repeat) == 3);
    unsigned saves_before = save_requests;
    lv_event_send(save_button, LV_EVENT_CLICKED, NULL);
    assert(save_requests == saves_before && strstr(lv_label_get_text(notice), "请选择重复星期"));

    for (unsigned day = 0; day < 7; ++day) lv_event_send(days[day], LV_EVENT_CLICKED, NULL);
    assert(mask == 127);
    char ring_text[32];
    lv_dropdown_set_selected(duration, 0);
    lv_dropdown_get_selected_str(duration, ring_text, sizeof(ring_text));
    assert(strcmp(ring_text, "1 分钟") == 0);
    lv_dropdown_set_selected(duration, 59);
    lv_event_send(duration, LV_EVENT_VALUE_CHANGED, NULL);
    lv_dropdown_get_selected_str(duration, ring_text, sizeof(ring_text));
    assert(strcmp(ring_text, "60 分钟") == 0);
    lv_dropdown_set_selected(duration, 4);
    lv_event_send(duration, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_clear_state(enabled, LV_STATE_CHECKED);
    lv_event_send(enabled, LV_EVENT_VALUE_CHANGED, NULL);

    fail_save = true;
    lv_event_send(save_button, LV_EVENT_CLICKED, NULL);
    assert(save_requests == saves_before + 1 && !snap.save_busy);
    assert(strstr(lv_label_get_text(notice), "暂未保存"));
    assert(edited);
    fail_save = false;

    tap(240, 428);
    assert(save_requests == saves_before + 2 && snap.save_busy && have_save);
    assert(saved_config.hour == 23 && saved_config.minute == 59);
    assert(saved_config.ring_minutes == 5 && saved_config.weekdays == 127 && !saved_config.enabled);
    assert(saved_config.once_epoch == 0);
    advance(300);
    assert(lv_obj_has_state(save_button, LV_STATE_DISABLED));
    assert(strstr(lv_label_get_text(notice), "正在保存"));
    publish_saved(125);
    advance(300);
    assert(!edited && lv_roller_get_selected(hour) == 23 && lv_roller_get_selected(minute) == 59);
    assert(lv_dropdown_get_selected(repeat) == 1 && lv_dropdown_get_selected(duration) == 4);
    assert(!lv_obj_has_state(enabled, LV_STATE_CHECKED));
    assert(strstr(lv_label_get_text(remaining), "闹钟已关闭"));
    snap.config.enabled = true;
    snap.revision++;
    advance(300);
    assert(lv_obj_has_state(enabled, LV_STATE_CHECKED));
    assert(strstr(lv_label_get_text(remaining), "还有2分5秒提醒"));
    snap.seconds_to_alarm = 90000;
    snap.next_epoch = snap.now_epoch + 90000;
    advance(300);
    assert(strstr(lv_label_get_text(remaining), "还有1天1小时0分提醒"));
    snap.time_valid = false;
    advance(300);
    assert(strstr(lv_label_get_text(remaining), "等待校时"));
    snap.time_valid = true;
    snap.next_epoch = 0;
    advance(300);
    assert(strstr(lv_label_get_text(remaining), "本次时间已过"));
    snap.next_epoch = snap.now_epoch + 125;
    snap.seconds_to_alarm = 125;

    snap.save_failed = true;
    advance(300);
    assert(strstr(lv_label_get_text(notice), "保存失败，请重试"));
    assert(!lv_obj_has_state(save_button, LV_STATE_DISABLED));
    snap.save_failed = false;

    lv_roller_set_selected(hour, 22, LV_ANIM_OFF);
    lv_event_send(hour, LV_EVENT_VALUE_CHANGED, NULL);
    assert(edited);
    uint32_t seen = shown_revision;
    snap.revision = seen + 9;
    snap.config.hour = 6;
    snap.config.minute = 15;
    advance(300);
    assert(shown_revision == seen && lv_roller_get_selected(hour) == 22);
    swipe_back();
    assert(lv_scr_act() == home && back_fires == 1);
    alarm_ui_open(home, chinese_font_20);
    settle();
    assert(lv_scr_act() == page && !edited && lv_roller_get_selected(hour) == 6);
    assert(lv_roller_get_selected(minute) == 15 && shown_revision == snap.revision);

    unsigned wakes_before = wakes;
    snap.ringing = true;
    snap.ring_seconds_left = 125;
    snap.config.hour = 8;
    snap.config.minute = 5;
    advance(300);
    assert(alarm_ui_alert_visible() && alert && wakes > wakes_before);
    assert(lv_obj_get_width(alert) == 480 && lv_obj_get_height(alert) == 480);
    assert(strcmp(lv_label_get_text(alert_time), "08:05") == 0);
    assert(strstr(lv_label_get_text(alert_left), "2分5秒后停止"));
    assert(strstr(lv_label_get_text(remaining), "正在响铃"));
    assert(lv_roller_get_selected(hour) == 6);
    screenshot(argv[2], "alarm-ringing");
    lv_event_send(stop_button(), LV_EVENT_CLICKED, NULL);
    assert(dismisses == 1 && !alarm_ui_alert_visible() && !snap.ringing);
    advance(300);
    assert(!alert && strstr(lv_label_get_text(remaining), "还有2分5秒提醒"));

    snap.ringing = true;
    snap.ring_seconds_left = 9;
    advance(300);
    assert(alert && wakes > wakes_before);
    snap.ringing = false;
    advance(300);
    assert(!alarm_ui_alert_visible());

    lv_mem_monitor_t before, after;
    lv_mem_monitor(&before);
    for (int i = 0; i < 20; ++i) {
        swipe_back();
        alarm_ui_open(home, chinese_font_20);
        settle();
        assert(lv_scr_act() == page && lv_roller_get_selected(hour) == 8 && lv_roller_get_selected(minute) == 5);
    }
    lv_mem_monitor(&after);
    assert(after.free_size + 256 >= before.free_size);
    assert(lv_mem_test() == LV_RES_OK);
    puts("alarm UI: real LVGL open/rollers/repeat/ring length/save/countdown/failure/external revision/ring overlay/back/reopen/memory PASS");
    return 0;
}
'''


def review_images():
    issues = []
    paths = [OUT / name for name in ('alarm-main.png', 'alarm-scrolled.png', 'alarm-ringing.png')]
    images = []
    for path in paths:
        if not path.exists():
            issues.append(path.name + ' missing')
            continue
        image = Image.open(path)
        images.append(image)
        if image.size != (480, 480):
            issues.append('%s size %s' % (path.name, image.size))
        if image.mode != 'RGB':
            issues.append('%s mode %s' % (path.name, image.mode))
        colors = image.getcolors(maxcolors=500000)
        if not colors or len(colors) < 8:
            issues.append('%s has too little color variation' % path.name)
    if len(images) == 3:
        main_px = list(images[0].getdata())
        scroll_px = list(images[1].getdata())
        ring_px = list(images[2].getdata())
        changed = sum(a != b for a, b in zip(main_px, scroll_px))
        if changed < 1000:
            issues.append('scrolled screenshot is almost identical to the main screenshot')
        def luma(pixels):
            return sum(sum(pixel) for pixel in pixels) / (len(pixels) * 3)
        if luma(ring_px) >= luma(main_px):
            issues.append('ringing screenshot is not darker than the main screenshot')
        dark_bottom = sum(1 for pixel in main_px[458 * 480:] if pixel[1] < 90 and pixel[0] < 90)
        if dark_bottom < 20:
            issues.append('main notice line at y=458 has little visible text; it may be clipped by the 480px screen')
    return issues


def main():
    compiler = shutil.which('cl')
    if not compiler:
        raise SystemExit('Use the Visual Studio developer environment.')
    OUT.mkdir(parents=True, exist_ok=True)
    config = (ROOT / 'include' / 'lv_conf.h').read_text(encoding='utf-8')
    config = config.replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0')
    config = config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>')
    config = config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
    (OUT / 'lv_conf.h').write_text(config, encoding='utf-8')
    driver = HARNESS[HARNESS.index('static bool down;'):HARNESS.index('int main(')]
    init = HARNESS[HARNESS.index(' assert(argc==3);'):HARNESS.index(' for(int i=0;i<12;')]
    unit = OUT / 'test.c'
    unit.write_text(SOURCE.replace('@ROOT@', ROOT.as_posix()).replace('@DRIVER@', driver).replace('@INIT@', init), encoding='utf-8')
    arguments = ['/nologo', '/O2', '/utf-8', '/D_CRT_SECURE_NO_WARNINGS', '/DLV_CONF_INCLUDE_SIMPLE', '/DLV_KCONFIG_IGNORE',
                 '/I' + str(OUT), '/I' + str(ROOT / 'src'), '/I' + str(ROOT / 'components' / 'lvgl'),
                 str(unit), str(ROOT / 'src' / 'UiTransition.c'),
                 *[str(path) for path in (ROOT / 'components' / 'lvgl' / 'src').rglob('*.c')],
                 '/Fe:' + str(OUT / 'test.exe'), '/Fo' + str(OUT) + '\\']
    response = OUT / 'compile.rsp'
    response.write_text('\n'.join(subprocess.list2cmdline([item]) for item in arguments), encoding='utf-8')
    result = subprocess.run([compiler, '@' + str(response)], capture_output=True, timeout=300)
    (OUT / 'compile.log').write_bytes(result.stdout + result.stderr)
    if result.returncode:
        print((result.stdout + result.stderr).decode(errors='replace')[-4500:])
        raise SystemExit(result.returncode)
    subprocess.run([str(OUT / 'test.exe'), str(ROOT / 'sd_card_files' / 'fonts' / 'chinese_20.bin'), str(OUT)], check=True, timeout=60)
    for file in OUT.glob('*.rgb565'):
        raw = file.read_bytes()
        rgb = bytearray()
        for index in range(0, len(raw), 2):
            color = raw[index] | raw[index + 1] << 8
            rgb.extend((((color >> 11) & 31) * 255 // 31, ((color >> 5) & 63) * 255 // 63, (color & 31) * 255 // 31))
        Image.frombytes('RGB', (480, 480), bytes(rgb)).save(file.with_suffix('.png'))
    issues = review_images()
    print('IMAGE_REVIEW ' + ('; '.join(issues) if issues else '480x480 main/scrolled/ringing PNG ok'))


if __name__ == '__main__':
    main()
