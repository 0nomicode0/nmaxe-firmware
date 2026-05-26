/**
 * @file thermal.h
 * @brief DVFS 열관리 시스템
 *
 * 4존 온도 구간:
 *   NOMINAL  < 55°C  : 최대 성능
 *   WARM     55~65°C : 주파수 -5%
 *   HOT      65~75°C : 주파수 -15%, 팬 100%
 *   CRITICAL > 75°C  : 주파수 -30%, 팬 100%
 *   SHUTDOWN > 85°C  : 즉시 ASIC 전원 차단
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    THERMAL_ZONE_NOMINAL  = 0,
    THERMAL_ZONE_WARM     = 1,
    THERMAL_ZONE_HOT      = 2,
    THERMAL_ZONE_CRITICAL = 3,
    THERMAL_ZONE_SHUTDOWN = 4,
} thermal_zone_t;

typedef struct {
    float          temp_c;
    thermal_zone_t zone;
    uint16_t       freq_mhz;
    uint8_t        fan_pct;
    bool           throttled;
} thermal_status_t;

esp_err_t      thermal_init(uint16_t base_freq_mhz);
void           thermal_tick(void);
thermal_zone_t thermal_get_zone(void);
void           thermal_get_status(thermal_status_t *st);
bool           thermal_is_safe(void);
