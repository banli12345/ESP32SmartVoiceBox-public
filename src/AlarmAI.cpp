#include "AlarmAI.h"
#include "AlarmIntent.h"
#include "AlarmService.h"
#include "IdfRuntime.h"
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
void to_local(time_t due, tm &out) {
#if defined(_WIN32)
    localtime_s(&out, &due);
#else
    localtime_r(&due, &out);
#endif
}
void describe(const alarm_snapshot_t &s, char *reply, size_t capacity, bool saved)
{
    if (!s.config.enabled) {
        snprintf(reply, capacity, "%s", s.ringing ? "闹钟正在响铃，可以说停止响铃。" : "闹钟已关闭。");
        return;
    }
    if (!s.time_valid || !s.next_epoch) {
        snprintf(reply, capacity, "%s", !s.time_valid ? "设置已保存，等待校时后提醒，请连接 WiFi。" : "本次提醒时间已过，请重新设置。");
        return;
    }
    time_t due = static_cast<time_t>(s.next_epoch);
    tm local = {}; to_local(due, local);
    char repeat[96] = "仅一次";
    if (s.config.weekdays == 127) strcpy(repeat, "每天");
    else if (s.config.weekdays == 31) strcpy(repeat, "周一到周五");
    else if (s.config.weekdays) {
        static const char *names[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
        repeat[0] = 0;
        for (unsigned i = 0; i < 7; ++i) if (s.config.weekdays & (1u << i)) {
            const size_t used = strlen(repeat);
            snprintf(repeat + used, sizeof(repeat) - used, "%s%s", used ? "、" : "", names[i]);
        }
    }
    const unsigned n = s.seconds_to_alarm;
    snprintf(reply, capacity, "%s%04d年%d月%d日 %02d:%02d，%s，响铃%u分钟。还有%u天%u小时%u分%u秒提醒。设备仅保留一个闹钟。",
        saved ? "闹钟已保存，下次提醒：" : "下次提醒：", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
        local.tm_hour, local.tm_min, repeat, unsigned(s.config.ring_minutes),
        n / 86400, n / 3600 % 24, n / 60 % 60, n % 60);
}
}

extern "C" bool alarm_ai_handle(const char *message, char *reply, size_t capacity)
{
    if (!message || !reply || !capacity) return false;
    const alarm_intent_t intent = alarm_intent_parse(message);
    if (intent.action == ALARM_INTENT_NONE) return false;
    // Unrelated requests such as setting the volume still belong to their handler.
    if (intent.action == ALARM_INTENT_INVALID && !strstr(message, "闹钟") &&
        !strstr(message, "提醒") && !strstr(message, "叫我") && !strstr(message, "响铃") &&
        !strstr(message, "点") && !strstr(message, "分钟后")) return false;
    if (intent.action == ALARM_INTENT_INVALID) {
        snprintf(reply, capacity, "这条闹钟指令没有执行。请明确一个时间，例如：帮我设置明天8点的闹钟；设置每天7点半的闹钟，响铃5分钟。仅保留一个闹钟。");
        return true;
    }
    alarm_snapshot_t s; alarm_get_snapshot(&s);
    if (intent.action == ALARM_INTENT_QUERY) { describe(s, reply, capacity, false); return true; }
    if (intent.action == ALARM_INTENT_STOP) {
        alarm_dismiss(); snprintf(reply, capacity, "已停止本次响铃，重复闹钟的后续提醒仍保留。"); return true;
    }
    alarm_config_t c = s.config;
    if (intent.action == ALARM_INTENT_DISABLE) c.enabled = false;
    else {
        c.enabled = true; c.hour = intent.hour; c.minute = intent.minute;
        c.weekdays = intent.weekdays; c.once_epoch = 0;
        if (intent.ring_minutes) c.ring_minutes = intent.ring_minutes;
        if (intent.delay_minutes || intent.day_offset >= 0) {
            if (!s.time_valid) {
                snprintf(reply, capacity, "设备尚未校准时间，未设置闹钟。请先连接 WiFi 校时。"); return true;
            }
            c.once_epoch = intent.delay_minutes ? s.now_epoch + intent.delay_minutes * 60 :
                alarm_day_time(s.now_epoch, intent.day_offset, c.hour, c.minute);
            if (c.once_epoch <= s.now_epoch) {
                snprintf(reply, capacity, "指定时间已经过去，未修改闹钟。请指定未来的时间。"); return true;
            }
            time_t due = static_cast<time_t>(c.once_epoch);
            tm local = {}; to_local(due, local);
            c.hour = local.tm_hour; c.minute = local.tm_min;
        }
    }
    uint32_t ticket = 0;
    if (!alarm_request_save(&c, &ticket)) {
        snprintf(reply, capacity, "闹钟未保存，可能有其他保存任务或时间无效，请稍后重试。"); return true;
    }
    const uint32_t began = app_millis();
    do {
        alarm_get_snapshot(&s);
        if (s.saved_request == ticket) {
            if (intent.action == ALARM_INTENT_DISABLE) snprintf(reply, capacity, "闹钟已关闭并保存。");
            else describe(s, reply, capacity, true);
            return true;
        }
        if (s.failed_request == ticket) {
            snprintf(reply, capacity, "闹钟保存失败，原设置仍保留，请重试。"); return true;
        }
        app_delay_ms(20);
    } while (app_millis() - began < 5000);
    snprintf(reply, capacity, "闹钟正在保存，请到时钟页面确认保存结果。");
    return true;
}
