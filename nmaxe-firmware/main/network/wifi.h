/**
 * @file wifi.h
 * @brief WiFi 연결 관리
 *
 * - STA 모드: 설정된 AP에 연결
 * - AP 모드: 초기 설정용 소프트AP (SSID/PW 미설정 시)
 * - 자동 재연결
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

#define WIFI_SSID_MAX     32
#define WIFI_PASS_MAX     64
#define WIFI_RETRY_MAX    5

typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_MODE,
    WIFI_STATE_ERROR,
} wifi_state_t;

typedef void (*wifi_event_cb_t)(wifi_state_t state);

esp_err_t    wifi_init(const char *ssid, const char *pass, wifi_event_cb_t cb);
esp_err_t    wifi_connect(void);
void         wifi_disconnect(void);
wifi_state_t wifi_get_state(void);
bool         wifi_is_connected(void);
void         wifi_get_ip(char *buf, size_t len);
