/**
 * @file display.c
 * @brief 웹 대시보드 구현 (ESP HTTP Server)
 *
 * - 브라우저에서 http://[장치IP]/ 접속
 * - 실시간 해시레이트, 온도, Share 통계 표시
 * - 설정 변경 API 제공
 */
#include "display.h"
#include "thermal.h"
#include "config.h"
#include "../asic/bm1366.h"
#include "../mining/work_manager.h"
#include "../network/stratum.h"
#include "../network/wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG   = "DISPLAY";
static httpd_handle_t s_server = NULL;

/* ── HTML 대시보드 (인라인) ── */
static const char DASHBOARD_HTML[] =
"<!DOCTYPE html><html><head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>NMAxe Miner</title>"
"<style>"
"body{background:#0f172a;color:#f8fafc;font-family:-apple-system,sans-serif;margin:0;padding:16px}"
"h1{color:#38bdf8;font-size:20px;margin:0 0 16px}"
".grid{display:grid;grid-template-columns:repeat(2,1fr);gap:12px;margin-bottom:16px}"
".card{background:#1e293b;border:1px solid #334155;border-radius:10px;padding:14px;text-align:center}"
".lbl{font-size:10px;color:#94a3b8;text-transform:uppercase;margin-bottom:4px}"
".val{font-size:20px;font-weight:700}"
".green{color:#10b981}.blue{color:#38bdf8}.yellow{color:#f59e0b}.red{color:#ef4444}"
".status{font-size:12px;color:#64748b;text-align:center;margin-bottom:16px}"
"button{background:#0284c7;color:#fff;border:none;border-radius:6px;padding:10px 20px;cursor:pointer;margin:4px}"
"button:hover{background:#0369a1}"
".warn{background:#7c3aed}"
"</style></head><body>"
"<h1>⛏️ NMAxe Miner</h1>"
"<div id='status' class='status'>Loading...</div>"
"<div class='grid'>"
"<div class='card'><div class='lbl'>Hashrate</div><div id='hr' class='val green'>—</div></div>"
"<div class='card'><div class='lbl'>Temperature</div><div id='temp' class='val blue'>—</div></div>"
"<div class='card'><div class='lbl'>Shares</div><div id='shares' class='val yellow'>—</div></div>"
"<div class='card'><div class='lbl'>Frequency</div><div id='freq' class='val'>—</div></div>"
"<div class='card'><div class='lbl'>Fan</div><div id='fan' class='val'>—</div></div>"
"<div class='card'><div class='lbl'>EN2 Rollovers</div><div id='en2' class='val'>—</div></div>"
"</div>"
"<button onclick='fetchStatus()'>🔄 Refresh</button>"
"<button class='warn' onclick='restart()'>⚡ Restart</button>"
"<script>"
"async function fetchStatus(){"
"try{const r=await fetch('/api/status');const d=await r.json();"
"document.getElementById('status').textContent='Last update: '+new Date().toLocaleTimeString();"
"document.getElementById('hr').textContent=d.hashrate_5m||'—';"
"document.getElementById('temp').textContent=(d.temp_c||0).toFixed(1)+'°C';"
"document.getElementById('shares').textContent=(d.shares_submitted||0)+'/'+( d.shares_found||0);"
"document.getElementById('freq').textContent=(d.freq_mhz||0)+'MHz';"
"document.getElementById('fan').textContent=(d.fan_pct||0)+'%';"
"document.getElementById('en2').textContent=d.en2_rollovers||0;"
"}catch(e){document.getElementById('status').textContent='Error: '+e.message;}}"
"async function restart(){"
"if(confirm('Restart miner?')){await fetch('/api/restart',{method:'POST'});}"
"}"
"fetchStatus();"
"setInterval(fetchStatus,5000);"
"</script></body></html>";

/* ──────────────────────────────────────────
 * HTTP 핸들러
 * ────────────────────────────────────────── */

/* GET / → 대시보드 HTML */
static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, DASHBOARD_HTML, strlen(DASHBOARD_HTML));
    return ESP_OK;
}

/* GET /api/status → JSON */
static esp_err_t handler_status(httpd_req_t *req)
{
    thermal_status_t th;
    thermal_get_status(&th);

    work_stats_t ws;
    work_manager_get_stats(&ws);

    double diff = stratum_get_difficulty();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root,   "asic_ready",       bm1366_is_ready());
    cJSON_AddBoolToObject(root,   "pool_connected",   stratum_is_connected());
    cJSON_AddNumberToObject(root, "temp_c",           (double)th.temp_c);
    cJSON_AddNumberToObject(root, "freq_mhz",         th.freq_mhz);
    cJSON_AddNumberToObject(root, "fan_pct",          th.fan_pct);
    cJSON_AddBoolToObject(root,   "throttled",        th.throttled);
    cJSON_AddStringToObject(root, "thermal_zone",
        th.zone == THERMAL_ZONE_NOMINAL  ? "NOMINAL"  :
        th.zone == THERMAL_ZONE_WARM     ? "WARM"     :
        th.zone == THERMAL_ZONE_HOT      ? "HOT"      :
        th.zone == THERMAL_ZONE_CRITICAL ? "CRITICAL" : "SHUTDOWN");
    cJSON_AddNumberToObject(root, "difficulty",       diff);
    cJSON_AddNumberToObject(root, "jobs_received",    ws.jobs_received);
    cJSON_AddNumberToObject(root, "shares_found",     ws.shares_found);
    cJSON_AddNumberToObject(root, "shares_submitted", ws.shares_submitted);
    cJSON_AddNumberToObject(root, "shares_accepted",  ws.shares_accepted);
    cJSON_AddNumberToObject(root, "en2_rollovers",    ws.en2_rollovers);

    /* 업타임 (초) */
    cJSON_AddNumberToObject(root, "uptime_sec",
        (double)(esp_timer_get_time() / 1000000));

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

/* GET /api/config → JSON */
static esp_err_t handler_get_config(httpd_req_t *req)
{
    nmaxe_config_t cfg;
    config_load(&cfg);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifi_ssid",  cfg.wifi_ssid);
    cJSON_AddStringToObject(root, "pool_host",  cfg.pool_host);
    cJSON_AddNumberToObject(root, "pool_port",  cfg.pool_port);
    cJSON_AddStringToObject(root, "pool_user",  cfg.pool_user);
    cJSON_AddNumberToObject(root, "asic_freq",  cfg.asic_freq);
    cJSON_AddNumberToObject(root, "asic_volt",  cfg.asic_volt);
    /* 비밀번호는 반환 안 함 */

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

/* POST /api/config → 설정 저장 */
static esp_err_t handler_set_config(httpd_req_t *req)
{
    char buf[512];
    int  len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    nmaxe_config_t cfg;
    config_load(&cfg);

    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "wifi_ssid")) && cJSON_IsString(item))
        strncpy(cfg.wifi_ssid, item->valuestring, sizeof(cfg.wifi_ssid) - 1);
    if ((item = cJSON_GetObjectItem(root, "wifi_pass")) && cJSON_IsString(item))
        strncpy(cfg.wifi_pass, item->valuestring, sizeof(cfg.wifi_pass) - 1);
    if ((item = cJSON_GetObjectItem(root, "pool_host")) && cJSON_IsString(item))
        strncpy(cfg.pool_host, item->valuestring, sizeof(cfg.pool_host) - 1);
    if ((item = cJSON_GetObjectItem(root, "pool_port")) && cJSON_IsNumber(item))
        cfg.pool_port = (uint16_t)item->valuedouble;
    if ((item = cJSON_GetObjectItem(root, "pool_user")) && cJSON_IsString(item))
        strncpy(cfg.pool_user, item->valuestring, sizeof(cfg.pool_user) - 1);
    if ((item = cJSON_GetObjectItem(root, "asic_freq")) && cJSON_IsNumber(item))
        cfg.asic_freq = (uint16_t)item->valuedouble;

    cJSON_Delete(root);
    config_save(&cfg);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* POST /api/restart */
static esp_err_t handler_restart(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* ──────────────────────────────────────────
 * 서버 시작
 * ────────────────────────────────────────── */

esp_err_t display_init(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port     = 80;
    cfg.max_uri_handlers = 8;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed");
        return ESP_FAIL;
    }

    /* URI 등록 */
    static const httpd_uri_t uris[] = {
        { .uri="/",           .method=HTTP_GET,  .handler=handler_root       },
        { .uri="/api/status", .method=HTTP_GET,  .handler=handler_status     },
        { .uri="/api/config", .method=HTTP_GET,  .handler=handler_get_config },
        { .uri="/api/config", .method=HTTP_POST, .handler=handler_set_config },
        { .uri="/api/restart",.method=HTTP_POST, .handler=handler_restart    },
    };
    for (int i = 0; i < 5; i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }

    char ip[16];
    wifi_get_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "Dashboard: http://%s/", ip);
    return ESP_OK;
}

void display_update(void) { /* HTTP 서버는 비동기 처리 */ }
