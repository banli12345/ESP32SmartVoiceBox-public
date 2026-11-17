#pragma once
#include "LifeData.h"
#ifdef __cplusplus
extern "C" {
#endif
bool local_lunar_get(int year, int month, int day, life_calendar_day_t *out);
#ifdef __cplusplus
}
#endif
