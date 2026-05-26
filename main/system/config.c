/**
 * @file config.c
 * @brief NVS 설정 저장/로드 구현
 */
#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG    = "CONFIG";
static const char *NVS_NS = "nmaxe";

esp_err_t config_init(void)
{
    ESP_LOGI(TAG, "NVS config init");
    return ESP_OK;
}

void config_set_defaults(nmaxe_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->wifi_ssid,  "",                    sizeof(cfg->wifi_ssid)  - 1);
    strncpy(cfg->wifi_pass,  "",                    sizeof(cfg->wifi_pass)  - 1);
    strncpy(cfg->pool_host,  "solo.ckpool.org",     sizeof(cfg->pool_host)  - 1);
    cfg->pool_port = 3333;
    strncpy(cfg->pool_user,  "YOUR_BTC_ADDRESS.w1", sizeof(cfg->pool_user)  - 1);
    strncpy(cfg->pool_pass,  "x",                   sizeof(cfg->pool_pass)  - 1);
    cfg->asic_freq = 485;
    cfg->asic_volt = 1200;
}

esp_err_t config_load(nmaxe_config_t *cfg)
{
    config_set_defaults(cfg);

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No config found, using defaults");
        return ESP_OK;
    }
    if (ret != ESP_OK) return ret;

    size_t len;

    #define LOAD_STR(key, field) \
        len = sizeof(cfg->field); \
        nvs_get_str(h, key, cfg->field, &len)

    LOAD_STR(CFG_WIFI_SSID_KEY, wifi_ssid);
    LOAD_STR(CFG_WIFI_PASS_KEY, wifi_pass);
    LOAD_STR(CFG_POOL_HOST_KEY, pool_host);
    LOAD_STR(CFG_POOL_USER_KEY, pool_user);
    LOAD_STR(CFG_POOL_PASS_KEY, pool_pass);

    nvs_get_u16(h, CFG_POOL_PORT_KEY, &cfg->pool_port);
    nvs_get_u16(h, CFG_ASIC_FREQ_KEY, &cfg->asic_freq);
    nvs_get_u16(h, CFG_ASIC_VOLT_KEY, &cfg->asic_volt);

    nvs_close(h);
    ESP_LOGI(TAG, "Config loaded: pool=%s:%d user=%s freq=%dMHz",
             cfg->pool_host, cfg->pool_port, cfg->pool_user, cfg->asic_freq);
    return ESP_OK;
}

esp_err_t config_save(const nmaxe_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;

    nvs_set_str(h, CFG_WIFI_SSID_KEY, cfg->wifi_ssid);
    nvs_set_str(h, CFG_WIFI_PASS_KEY, cfg->wifi_pass);
    nvs_set_str(h, CFG_POOL_HOST_KEY, cfg->pool_host);
    nvs_set_str(h, CFG_POOL_USER_KEY, cfg->pool_user);
    nvs_set_str(h, CFG_POOL_PASS_KEY, cfg->pool_pass);
    nvs_set_u16(h, CFG_POOL_PORT_KEY, cfg->pool_port);
    nvs_set_u16(h, CFG_ASIC_FREQ_KEY, cfg->asic_freq);
    nvs_set_u16(h, CFG_ASIC_VOLT_KEY, cfg->asic_volt);

    ret = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Config saved");
    return ret;
}
