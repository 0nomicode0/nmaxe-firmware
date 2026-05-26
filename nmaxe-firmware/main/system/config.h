/**
 * @file config.h
 * @brief NVS 설정 저장/로드
 *
 * 저장 항목:
 *   WiFi SSID/Password
 *   Pool Host/Port/User/Pass
 *   ASIC 주파수/전압
 */
#pragma once
#include <stdint.h>
#include "esp_err.h"

#define CFG_WIFI_SSID_KEY    "wifi_ssid"
#define CFG_WIFI_PASS_KEY    "wifi_pass"
#define CFG_POOL_HOST_KEY    "pool_host"
#define CFG_POOL_PORT_KEY    "pool_port"
#define CFG_POOL_USER_KEY    "pool_user"
#define CFG_POOL_PASS_KEY    "pool_pass"
#define CFG_ASIC_FREQ_KEY    "asic_freq"
#define CFG_ASIC_VOLT_KEY    "asic_volt"

typedef struct {
    char     wifi_ssid[32];
    char     wifi_pass[64];
    char     pool_host[128];
    uint16_t pool_port;
    char     pool_user[128];
    char     pool_pass[64];
    uint16_t asic_freq;
    uint16_t asic_volt;
} nmaxe_config_t;

esp_err_t config_init(void);
esp_err_t config_load(nmaxe_config_t *cfg);
esp_err_t config_save(const nmaxe_config_t *cfg);
void      config_set_defaults(nmaxe_config_t *cfg);
