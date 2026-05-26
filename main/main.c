/**
 * @file main.c
 * @brief NMAxe Custom Firmware v1.0 - 완성본
 *
 * Phase 1: HAL + BM1366         ✅
 * Phase 2: WiFi + Stratum        ✅
 * Phase 3: Work Manager          ✅ (extranonce2 자체 순환)
 * Phase 4: DVFS 열관리           ✅ (4존 히스테리시스)
 * Phase 5: 웹 대시보드           ✅ (HTTP REST API)
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "hal/hal_uart.h"
#include "hal/hal_i2c.h"
#include "hal/hal_gpio.h"
#include "asic/bm1366.h"
#include "network/wifi.h"
#include "network/stratum.h"
#include "mining/work_manager.h"
#include "system/config.h"
#include "system/thermal.h"
#include "system/display.h"

static const char *TAG = "NMAXE";
#define FW_VERSION "1.0.0"

/* ── 태스크 설정 ── */
#define STACK_MINING    8192
#define STACK_NETWORK   6144
#define STACK_SYSTEM    4096

#define PRIO_MINING     (configMAX_PRIORITIES - 1)  /* Core 1 전용 */
#define PRIO_NETWORK    (configMAX_PRIORITIES - 2)
#define PRIO_SYSTEM     (configMAX_PRIORITIES - 3)

/* ── 이벤트 비트 ── */
#define EVT_WIFI_READY      BIT0
#define EVT_STRATUM_READY   BIT1
#define EVT_WORK_READY      BIT2

static EventGroupHandle_t s_evt;
static nmaxe_config_t     s_cfg;
static TaskHandle_t       h_mining  = NULL;
static TaskHandle_t       h_network = NULL;
static TaskHandle_t       h_system  = NULL;

/* ──────────────────────────────────────────
 * 콜백
 * ────────────────────────────────────────── */

static void on_notify(const stratum_notify_t *n)
{
    if (!thermal_is_safe()) {
        ESP_LOGW(TAG, "Thermal shutdown: ignoring new job");
        return;
    }
    work_manager_on_notify(n);
    xEventGroupSetBits(s_evt, EVT_WORK_READY);
}

static void on_diff(double d)
{
    ESP_LOGI(TAG, "Pool difficulty: %.1f", d);
}

static void on_stratum_conn(bool ok)
{
    if (ok) {
        work_manager_init();
        xEventGroupSetBits(s_evt, EVT_STRATUM_READY);
        ESP_LOGI(TAG, "Pool connected");
    } else {
        xEventGroupClearBits(s_evt, EVT_STRATUM_READY | EVT_WORK_READY);
        ESP_LOGW(TAG, "Pool disconnected");
    }
}

static void on_wifi(wifi_state_t st)
{
    if (st == WIFI_STATE_CONNECTED) {
        xEventGroupSetBits(s_evt, EVT_WIFI_READY);
        /* WiFi 연결 후 웹 대시보드 시작 */
        display_init();
    } else {
        xEventGroupClearBits(s_evt,
            EVT_WIFI_READY | EVT_STRATUM_READY | EVT_WORK_READY);
    }
}

/* ──────────────────────────────────────────
 * 태스크: Mining (Core 1 전용)
 * ────────────────────────────────────────── */

static void task_mining(void *arg)
{
    ESP_LOGI(TAG, "[Mining] Core%d", xPortGetCoreID());

    /* BM1366 초기화 */
    if (bm1366_init(s_cfg.asic_freq, s_cfg.asic_volt) != ESP_OK) {
        ESP_LOGE(TAG, "[Mining] BM1366 init FAILED");
        vTaskDelete(NULL);
        return;
    }
    /* DVFS 초기화 */
    thermal_init(s_cfg.asic_freq);
    hal_gpio_led_set(true);
    ESP_LOGI(TAG, "[Mining] BM1366 ready @ %dMHz", s_cfg.asic_freq);

    /* Pool 연결 대기 */
    xEventGroupWaitBits(s_evt, EVT_STRATUM_READY,
                        pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "[Mining] Work loop start");

    /* ── 채굴 루프 ── */
    while (1) {
        /* 열 안전 확인 */
        if (!thermal_is_safe()) {
            ESP_LOGE(TAG, "[Mining] Thermal shutdown! Pausing...");
            hal_gpio_led_set(false);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        /* Work Manager tick (결과 수신 + share 제출 + extranonce2 순환) */
        work_manager_tick();

        /* 1ms 간격 */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ──────────────────────────────────────────
 * 태스크: Network (Core 0)
 * ────────────────────────────────────────── */

static void task_network(void *arg)
{
    ESP_LOGI(TAG, "[Network] Core%d", xPortGetCoreID());

    /* WiFi 초기화 */
    wifi_init(s_cfg.wifi_ssid, s_cfg.wifi_pass, on_wifi);

    /* Stratum 초기화 */
    stratum_config_t sc = { .port = s_cfg.pool_port };
    strncpy(sc.host, s_cfg.pool_host, sizeof(sc.host) - 1);
    strncpy(sc.user, s_cfg.pool_user, sizeof(sc.user) - 1);
    strncpy(sc.pass, s_cfg.pool_pass, sizeof(sc.pass) - 1);
    stratum_init(&sc, on_notify, on_diff, on_stratum_conn);

    while (1) {
        /* WiFi 연결 */
        if (!wifi_is_connected()) {
            ESP_LOGI(TAG, "[Network] WiFi 연결 중: %s", s_cfg.wifi_ssid);
            if (wifi_connect() != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(10000));
                continue;
            }
        }

        /* Pool 연결 (블로킹 - 끊기면 반환) */
        ESP_LOGI(TAG, "[Network] Pool 연결: %s:%d",
                 s_cfg.pool_host, s_cfg.pool_port);
        stratum_connect();

        ESP_LOGW(TAG, "[Network] Pool 끊김, 5초 후 재연결");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* ──────────────────────────────────────────
 * 태스크: System (Core 0)
 * DVFS + 통계 출력
 * ────────────────────────────────────────── */

static void task_system(void *arg)
{
    ESP_LOGI(TAG, "[System] Core%d", xPortGetCoreID());

    while (1) {
        /* DVFS tick (온도 읽기 + 주파수/팬 조절) */
        thermal_tick();

        /* 10초마다 통계 출력 */
        thermal_status_t th;
        thermal_get_status(&th);

        work_stats_t ws;
        work_manager_get_stats(&ws);

        ESP_LOGI(TAG,
            "[Stats] Temp:%.1f°C Zone:%d Freq:%dMHz Fan:%d%% "
            "Jobs:%d Shares:%d/%d EN2roll:%d",
            th.temp_c, th.zone, th.freq_mhz, th.fan_pct,
            ws.jobs_received,
            ws.shares_submitted, ws.shares_found,
            ws.en2_rollovers);

        /* LED: Pool 연결 상태 표시 */
        if (!stratum_is_connected()) {
            hal_gpio_led_toggle();
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* ──────────────────────────────────────────
 * 시스템 초기화
 * ────────────────────────────────────────── */

static void system_init(void)
{
    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }

    /* HAL */
    ESP_ERROR_CHECK(hal_uart_init());
    ESP_ERROR_CHECK(hal_i2c_init());
    ESP_ERROR_CHECK(hal_gpio_init());

    /* 설정 로드 */
    config_init();
    config_load(&s_cfg);

    ESP_LOGI(TAG, "Pool : %s:%d", s_cfg.pool_host, s_cfg.pool_port);
    ESP_LOGI(TAG, "User : %s",    s_cfg.pool_user);
    ESP_LOGI(TAG, "ASIC : %dMHz / %dmV",
             s_cfg.asic_freq, s_cfg.asic_volt);
}

/* ──────────────────────────────────────────
 * 진입점
 * ────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "  NMAxe Custom Firmware v%s", FW_VERSION);
    ESP_LOGI(TAG, "  BM1366 / ESP32-S3");
    ESP_LOGI(TAG, "================================");

    s_evt = xEventGroupCreate();
    system_init();

    xTaskCreatePinnedToCore(task_mining,  "mining",
        STACK_MINING,  NULL, PRIO_MINING,  &h_mining,  1);
    xTaskCreatePinnedToCore(task_network, "network",
        STACK_NETWORK, NULL, PRIO_NETWORK, &h_network, 0);
    xTaskCreatePinnedToCore(task_system,  "system",
        STACK_SYSTEM,  NULL, PRIO_SYSTEM,  &h_system,  0);

    ESP_LOGI(TAG, "All tasks started — Happy Mining! ⛏️");
}
