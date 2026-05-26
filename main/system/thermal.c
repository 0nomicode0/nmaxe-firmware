/**
 * @file thermal.c
 * @brief DVFS 열관리 시스템 구현
 *
 * 히스테리시스 적용으로 주파수 진동 방지:
 *   존 상승: 즉시 적용
 *   존 하강: 온도가 임계값 -3°C 이하로 유지될 때만 적용
 */
#include "thermal.h"
#include "../asic/bm1366.h"
#include "../hal/hal_gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "THERMAL";

/* ── 온도 임계값 ── */
#define TEMP_WARM       55.0f
#define TEMP_HOT        65.0f
#define TEMP_CRITICAL   75.0f
#define TEMP_SHUTDOWN   85.0f
#define TEMP_HYSTERESIS  3.0f   /* 히스테리시스 */

/* ── 주파수 스케일 (기본 주파수 대비 %) ── */
#define FREQ_SCALE_NOMINAL  100
#define FREQ_SCALE_WARM      95
#define FREQ_SCALE_HOT       85
#define FREQ_SCALE_CRITICAL  70

/* ── 팬 속도 ── */
#define FAN_NOMINAL  40
#define FAN_WARM     60
#define FAN_HOT     100
#define FAN_CRITICAL 100

/* ── 내부 상태 ── */
static thermal_status_t s_status;
static uint16_t         s_base_freq = 485;
static bool             s_initialized = false;

/* ── 존 → 주파수/팬 매핑 ── */
static const struct {
    uint8_t freq_scale;
    uint8_t fan_pct;
    const char *name;
} ZONE_MAP[5] = {
    { FREQ_SCALE_NOMINAL,  FAN_NOMINAL,  "NOMINAL"  },
    { FREQ_SCALE_WARM,     FAN_WARM,     "WARM"     },
    { FREQ_SCALE_HOT,      FAN_HOT,      "HOT"      },
    { FREQ_SCALE_CRITICAL, FAN_CRITICAL, "CRITICAL" },
    { 0,                   FAN_CRITICAL, "SHUTDOWN" },
};

/* ── 온도 → 존 결정 (히스테리시스 적용) ── */
static thermal_zone_t calc_zone(float temp, thermal_zone_t cur)
{
    /* 존 상승: 즉시 */
    if (temp >= TEMP_SHUTDOWN)  return THERMAL_ZONE_SHUTDOWN;
    if (temp >= TEMP_CRITICAL)  return THERMAL_ZONE_CRITICAL;
    if (temp >= TEMP_HOT)       return THERMAL_ZONE_HOT;
    if (temp >= TEMP_WARM)      return THERMAL_ZONE_WARM;

    /* 존 하강: 히스테리시스 적용 */
    switch (cur) {
    case THERMAL_ZONE_SHUTDOWN:
        if (temp < TEMP_SHUTDOWN - TEMP_HYSTERESIS)
            return THERMAL_ZONE_CRITICAL;
        return cur;
    case THERMAL_ZONE_CRITICAL:
        if (temp < TEMP_CRITICAL - TEMP_HYSTERESIS)
            return THERMAL_ZONE_HOT;
        return cur;
    case THERMAL_ZONE_HOT:
        if (temp < TEMP_HOT - TEMP_HYSTERESIS)
            return THERMAL_ZONE_WARM;
        return cur;
    case THERMAL_ZONE_WARM:
        if (temp < TEMP_WARM - TEMP_HYSTERESIS)
            return THERMAL_ZONE_NOMINAL;
        return cur;
    default:
        return THERMAL_ZONE_NOMINAL;
    }
}

/* ── 공개 API ── */

esp_err_t thermal_init(uint16_t base_freq_mhz)
{
    s_base_freq = base_freq_mhz;
    memset(&s_status, 0, sizeof(s_status));
    s_status.zone     = THERMAL_ZONE_NOMINAL;
    s_status.freq_mhz = base_freq_mhz;
    s_status.fan_pct  = FAN_NOMINAL;
    s_initialized = true;

    hal_gpio_fan_set(FAN_NOMINAL);
    ESP_LOGI(TAG, "Thermal init: base=%dMHz", base_freq_mhz);
    return ESP_OK;
}

void thermal_tick(void)
{
    if (!s_initialized) return;

    /* 온도 읽기 */
    float temp = 0.0f;
    if (bm1366_read_temp(&temp) != ESP_OK) return;

    thermal_zone_t old_zone = s_status.zone;
    thermal_zone_t new_zone = calc_zone(temp, old_zone);

    s_status.temp_c = temp;
    s_status.zone   = new_zone;

    /* 주파수/팬 설정 */
    uint16_t new_freq = (s_base_freq * ZONE_MAP[new_zone].freq_scale) / 100;
    uint8_t  new_fan  = ZONE_MAP[new_zone].fan_pct;
    bool     throttled = (new_zone > THERMAL_ZONE_NOMINAL);

    /* 존 변경 시에만 ASIC/팬 업데이트 */
    if (new_zone != old_zone) {
        ESP_LOGW(TAG, "Zone: %s→%s temp=%.1f°C freq=%dMHz fan=%d%%",
                 ZONE_MAP[old_zone].name, ZONE_MAP[new_zone].name,
                 temp, new_freq, new_fan);

        if (new_zone == THERMAL_ZONE_SHUTDOWN) {
            ESP_LOGE(TAG, "SHUTDOWN: temp=%.1f°C > %.1f°C", temp, TEMP_SHUTDOWN);
            hal_gpio_asic_power(false);
        } else {
            bm1366_set_frequency(new_freq);
        }
        hal_gpio_fan_set(new_fan);
    }

    s_status.freq_mhz = new_freq;
    s_status.fan_pct  = new_fan;
    s_status.throttled = throttled;
}

thermal_zone_t thermal_get_zone(void)   { return s_status.zone; }
bool           thermal_is_safe(void)    { return s_status.zone < THERMAL_ZONE_SHUTDOWN; }

void thermal_get_status(thermal_status_t *st)
{
    if (st) *st = s_status;
}
