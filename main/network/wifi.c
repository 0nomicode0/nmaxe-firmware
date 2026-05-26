/**
 * @file wifi.c
 * @brief WiFi 연결 관리 구현
 */
#include "wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "WIFI";

/* 이벤트 그룹 비트 */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t  s_event_group  = NULL;
static wifi_state_t        s_state        = WIFI_STATE_DISCONNECTED;
static wifi_event_cb_t     s_cb           = NULL;
static int                 s_retry        = 0;
static char                s_ssid[WIFI_SSID_MAX];
static char                s_pass[WIFI_PASS_MAX];
static esp_netif_t        *s_netif_sta    = NULL;

/* ── 상태 변경 헬퍼 ── */
static void set_state(wifi_state_t st)
{
    s_state = st;
    if (s_cb) s_cb(st);
}

/* ── 이벤트 핸들러 ── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting...");
            set_state(WIFI_STATE_CONNECTING);
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_retry < WIFI_RETRY_MAX) {
                s_retry++;
                ESP_LOGW(TAG, "Disconnected, retry %d/%d",
                         s_retry, WIFI_RETRY_MAX);
                set_state(WIFI_STATE_CONNECTING);
                esp_wifi_connect();
            } else {
                ESP_LOGE(TAG, "Max retries reached");
                set_state(WIFI_STATE_ERROR);
                xEventGroupSetBits(s_event_group, WIFI_FAIL_BIT);
            }
            break;

        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        s_retry = 0;
        set_state(WIFI_STATE_CONNECTED);
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ── 초기화 ── */
esp_err_t wifi_init(const char *ssid, const char *pass, wifi_event_cb_t cb)
{
    strncpy(s_ssid, ssid ? ssid : "", WIFI_SSID_MAX - 1);
    strncpy(s_pass, pass ? pass : "", WIFI_PASS_MAX - 1);
    s_cb = cb;

    s_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif_sta = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 이벤트 핸들러 등록 */
    esp_event_handler_instance_t h_wifi, h_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &h_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, &h_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_LOGI(TAG, "WiFi init OK, SSID: %s", s_ssid);
    return ESP_OK;
}

/* ── 연결 ── */
esp_err_t wifi_connect(void)
{
    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid,     s_ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, s_pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* 연결 대기 (최대 15초) */
    EventBits_t bits = xEventGroupWaitBits(s_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(15000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP: %s", s_ssid);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to connect to AP: %s", s_ssid);
    return ESP_FAIL;
}

/* ── 연결 해제 ── */
void wifi_disconnect(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
    set_state(WIFI_STATE_DISCONNECTED);
}

wifi_state_t wifi_get_state(void)  { return s_state; }
bool         wifi_is_connected(void) { return s_state == WIFI_STATE_CONNECTED; }

void wifi_get_ip(char *buf, size_t len)
{
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_netif_sta, &info) == ESP_OK) {
        snprintf(buf, len, IPSTR, IP2STR(&info.ip));
    } else {
        strncpy(buf, "0.0.0.0", len);
    }
}
