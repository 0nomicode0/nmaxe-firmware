/**
 * @file stratum.c
 * @brief Stratum v1 프로토콜 클라이언트 구현
 *
 * 연결 순서:
 *   1. TCP 연결
 *   2. mining.configure (version rolling 협상)
 *   3. mining.subscribe (extranonce 수신)
 *   4. mining.authorize (인증)
 *   5. mining.notify 수신 루프
 */
#include "stratum.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "STRATUM";

/* ── 내부 상태 ── */
static stratum_config_t   s_cfg;
static stratum_notify_cb_t s_on_notify   = NULL;
static stratum_diff_cb_t   s_on_diff     = NULL;
static stratum_connect_cb_t s_on_connect = NULL;

static int      s_sock          = -1;
static bool     s_connected     = false;
static int      s_msg_id        = 1;
static double   s_difficulty    = 1.0;
static uint32_t s_version_mask  = 0x1FFFE000; /* BIP320 기본 마스크 */

/* extranonce */
static uint8_t  s_en1[STRATUM_EXTRANONCE_MAX];
static uint32_t s_en1_len  = 4;
static uint32_t s_en2_size = 4;

/* 수신 버퍼 */
static char s_recv_buf[STRATUM_BUF_SIZE];
static int  s_recv_len = 0;

/* ──────────────────────────────────────────
 * 유틸리티
 * ────────────────────────────────────────── */

/** hex 문자열 → 바이트 배열 */
static void hex2bin(const char *hex, uint8_t *bin, size_t bin_len)
{
    for (size_t i = 0; i < bin_len && hex[i*2]; i++) {
        char tmp[3] = { hex[i*2], hex[i*2+1], 0 };
        bin[i] = (uint8_t)strtol(tmp, NULL, 16);
    }
}

/** 바이트 배열 → hex 문자열 */
static void bin2hex(const uint8_t *bin, size_t len, char *hex)
{
    for (size_t i = 0; i < len; i++) {
        sprintf(hex + i*2, "%02x", bin[i]);
    }
    hex[len*2] = '\0';
}

/* ──────────────────────────────────────────
 * TCP 송수신
 * ────────────────────────────────────────── */

static esp_err_t tcp_send(const char *data)
{
    if (s_sock < 0) return ESP_FAIL;
    int len = strlen(data);
    int sent = send(s_sock, data, len, 0);
    if (sent != len) {
        ESP_LOGE(TAG, "send failed: %d/%d", sent, len);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "TX: %s", data);
    return ESP_OK;
}

/** JSON 메서드 전송 헬퍼 */
static esp_err_t send_json(cJSON *root)
{
    char *str = cJSON_PrintUnformatted(root);
    if (!str) return ESP_ERR_NO_MEM;

    /* Stratum은 줄바꿈(\n)으로 메시지 구분 */
    char buf[STRATUM_BUF_SIZE];
    snprintf(buf, sizeof(buf), "%s\n", str);
    free(str);

    return tcp_send(buf);
}

/* ──────────────────────────────────────────
 * Stratum 메서드 전송
 * ────────────────────────────────────────── */

/** mining.configure (version rolling 협상) */
static esp_err_t send_configure(void)
{
    cJSON *root   = cJSON_CreateObject();
    cJSON *params = cJSON_CreateArray();
    cJSON *ext    = cJSON_CreateArray();
    cJSON *obj    = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "id",     s_msg_id++);
    cJSON_AddStringToObject(root, "method", "mining.configure");
    cJSON_AddItemToArray(ext, cJSON_CreateString("version-rolling"));
    cJSON_AddStringToObject(obj, "version-rolling.mask", "1fffe000");
    cJSON_AddNumberToObject(obj, "version-rolling.min-bit-count", 16);
    cJSON_AddItemToArray(params, ext);
    cJSON_AddItemToArray(params, obj);
    cJSON_AddItemToObject(root, "params", params);

    esp_err_t ret = send_json(root);
    cJSON_Delete(root);
    return ret;
}

/** mining.subscribe */
static esp_err_t send_subscribe(void)
{
    cJSON *root   = cJSON_CreateObject();
    cJSON *params = cJSON_CreateArray();

    cJSON_AddNumberToObject(root, "id",     s_msg_id++);
    cJSON_AddStringToObject(root, "method", "mining.subscribe");
    cJSON_AddItemToArray(params, cJSON_CreateString("NMAxe/0.1.0"));
    cJSON_AddItemToObject(root, "params", params);

    esp_err_t ret = send_json(root);
    cJSON_Delete(root);
    return ret;
}

/** mining.authorize */
static esp_err_t send_authorize(void)
{
    cJSON *root   = cJSON_CreateObject();
    cJSON *params = cJSON_CreateArray();

    cJSON_AddNumberToObject(root, "id",     s_msg_id++);
    cJSON_AddStringToObject(root, "method", "mining.authorize");
    cJSON_AddItemToArray(params, cJSON_CreateString(s_cfg.user));
    cJSON_AddItemToArray(params, cJSON_CreateString(
        s_cfg.pass[0] ? s_cfg.pass : "x"));
    cJSON_AddItemToObject(root, "params", params);

    esp_err_t ret = send_json(root);
    cJSON_Delete(root);
    return ret;
}

/* ──────────────────────────────────────────
 * 수신 메시지 파싱
 * ────────────────────────────────────────── */

static void parse_subscribe_result(cJSON *result)
{
    /* result: [[["mining.notify","session_id"]],extranonce1,extranonce2_size] */
    if (!cJSON_IsArray(result)) return;

    cJSON *en1_item = cJSON_GetArrayItem(result, 1);
    cJSON *en2_item = cJSON_GetArrayItem(result, 2);

    if (cJSON_IsString(en1_item)) {
        const char *en1_hex = en1_item->valuestring;
        s_en1_len = strlen(en1_hex) / 2;
        hex2bin(en1_hex, s_en1, s_en1_len);
        ESP_LOGI(TAG, "extranonce1: %s (%d bytes)", en1_hex, s_en1_len);
    }
    if (cJSON_IsNumber(en2_item)) {
        s_en2_size = (uint32_t)en2_item->valuedouble;
        ESP_LOGI(TAG, "extranonce2_size: %d", s_en2_size);
    }
}

static void parse_notify(cJSON *params)
{
    /* params: [job_id, prevhash, coinb1, coinb2, merkle_branch[],
     *          version, nbits, ntime, clean_jobs] */
    if (!cJSON_IsArray(params)) return;

    stratum_notify_t notify = { 0 };
    int n = cJSON_GetArraySize(params);
    if (n < 9) {
        ESP_LOGW(TAG, "notify: too few params (%d)", n);
        return;
    }

    cJSON *p;

    /* job_id */
    p = cJSON_GetArrayItem(params, 0);
    if (cJSON_IsString(p))
        strncpy(notify.job_id, p->valuestring, sizeof(notify.job_id) - 1);

    /* prev_hash */
    p = cJSON_GetArrayItem(params, 1);
    if (cJSON_IsString(p)) hex2bin(p->valuestring, notify.prev_hash, 32);

    /* coinbase1 */
    p = cJSON_GetArrayItem(params, 2);
    if (cJSON_IsString(p)) {
        notify.coinbase1_len = strlen(p->valuestring) / 2;
        hex2bin(p->valuestring, notify.coinbase1, notify.coinbase1_len);
    }

    /* coinbase2 */
    p = cJSON_GetArrayItem(params, 3);
    if (cJSON_IsString(p)) {
        notify.coinbase2_len = strlen(p->valuestring) / 2;
        hex2bin(p->valuestring, notify.coinbase2, notify.coinbase2_len);
    }

    /* merkle_branches */
    cJSON *branches = cJSON_GetArrayItem(params, 4);
    if (cJSON_IsArray(branches)) {
        notify.merkle_count = cJSON_GetArraySize(branches);
        for (int i = 0; i < notify.merkle_count && i < 32; i++) {
            cJSON *b = cJSON_GetArrayItem(branches, i);
            if (cJSON_IsString(b))
                hex2bin(b->valuestring, notify.merkle_branches[i], 32);
        }
    }

    /* version */
    p = cJSON_GetArrayItem(params, 5);
    if (cJSON_IsString(p)) notify.version = strtoul(p->valuestring, NULL, 16);

    /* nbits */
    p = cJSON_GetArrayItem(params, 6);
    if (cJSON_IsString(p)) notify.nbits = strtoul(p->valuestring, NULL, 16);

    /* ntime */
    p = cJSON_GetArrayItem(params, 7);
    if (cJSON_IsString(p)) notify.ntime = strtoul(p->valuestring, NULL, 16);

    /* clean_jobs */
    p = cJSON_GetArrayItem(params, 8);
    notify.clean_jobs = cJSON_IsTrue(p);

    ESP_LOGI(TAG, "notify: job=%s clean=%d", notify.job_id, notify.clean_jobs);

    if (s_on_notify) s_on_notify(&notify);
}

static void parse_set_difficulty(cJSON *params)
{
    cJSON *d = cJSON_GetArrayItem(params, 0);
    if (cJSON_IsNumber(d)) {
        s_difficulty = d->valuedouble;
        ESP_LOGI(TAG, "Difficulty: %.1f", s_difficulty);
        if (s_on_diff) s_on_diff(s_difficulty);
    }
}

static void parse_message(const char *line)
{
    cJSON *root = cJSON_Parse(line);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse error: %s", line);
        return;
    }

    cJSON *method = cJSON_GetObjectItem(root, "method");
    cJSON *result = cJSON_GetObjectItem(root, "result");
    cJSON *params = cJSON_GetObjectItem(root, "params");
    cJSON *id     = cJSON_GetObjectItem(root, "id");

    if (cJSON_IsString(method)) {
        const char *m = method->valuestring;
        if (strcmp(m, "mining.notify") == 0) {
            parse_notify(params);
        } else if (strcmp(m, "mining.set_difficulty") == 0) {
            parse_set_difficulty(params);
        } else {
            ESP_LOGD(TAG, "Unhandled method: %s", m);
        }
    } else if (cJSON_IsNumber(id)) {
        /* 응답 메시지 */
        int rid = (int)id->valuedouble;
        if (rid == 1) {
            /* mining.configure 응답 - version mask 파싱 */
            if (cJSON_IsObject(result)) {
                cJSON *mask = cJSON_GetObjectItem(result, "version-rolling.mask");
                if (cJSON_IsString(mask)) {
                    s_version_mask = strtoul(mask->valuestring, NULL, 16);
                    ESP_LOGI(TAG, "Version mask: 0x%08X", (unsigned)s_version_mask);
                }
            }
        } else if (rid == 2) {
            /* mining.subscribe 응답 */
            parse_subscribe_result(result);
        } else if (rid == 3) {
            /* mining.authorize 응답 */
            if (cJSON_IsTrue(result)) {
                ESP_LOGI(TAG, "Authorized: %s", s_cfg.user);
            } else {
                ESP_LOGE(TAG, "Authorization failed!");
            }
        }
    }

    cJSON_Delete(root);
}

/* ──────────────────────────────────────────
 * 수신 루프 (태스크에서 호출)
 * ────────────────────────────────────────── */

static void recv_loop(void)
{
    char buf[512];
    while (s_connected) {
        int n = recv(s_sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            ESP_LOGW(TAG, "recv returned %d, disconnecting", n);
            s_connected = false;
            if (s_on_connect) s_on_connect(false);
            break;
        }
        buf[n] = '\0';

        /* 버퍼에 추가 */
        if (s_recv_len + n < STRATUM_BUF_SIZE) {
            memcpy(s_recv_buf + s_recv_len, buf, n);
            s_recv_len += n;
        }

        /* 줄 단위로 파싱 */
        char *start = s_recv_buf;
        char *nl;
        while ((nl = memchr(start, '\n', s_recv_len - (start - s_recv_buf)))) {
            *nl = '\0';
            if (nl > start) {
                ESP_LOGD(TAG, "RX: %s", start);
                parse_message(start);
            }
            start = nl + 1;
        }

        /* 남은 데이터 정리 */
        int remaining = s_recv_len - (start - s_recv_buf);
        if (remaining > 0 && start != s_recv_buf) {
            memmove(s_recv_buf, start, remaining);
        }
        s_recv_len = remaining;
    }
}

/* ──────────────────────────────────────────
 * 공개 API
 * ────────────────────────────────────────── */

esp_err_t stratum_init(const stratum_config_t *cfg,
                       stratum_notify_cb_t     on_notify,
                       stratum_diff_cb_t       on_diff,
                       stratum_connect_cb_t    on_connect)
{
    memcpy(&s_cfg, cfg, sizeof(s_cfg));
    s_on_notify  = on_notify;
    s_on_diff    = on_diff;
    s_on_connect = on_connect;
    ESP_LOGI(TAG, "Stratum init: %s:%d user=%s", cfg->host, cfg->port, cfg->user);
    return ESP_OK;
}

esp_err_t stratum_connect(void)
{
    /* TCP 연결 */
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res  = NULL;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", s_cfg.port);

    ESP_LOGI(TAG, "Resolving %s:%s ...", s_cfg.host, port_str);
    if (getaddrinfo(s_cfg.host, port_str, &hints, &res) != 0) {
        ESP_LOGE(TAG, "DNS failed");
        return ESP_FAIL;
    }

    s_sock = socket(res->ai_family, res->ai_socktype, 0);
    if (s_sock < 0) {
        freeaddrinfo(res);
        return ESP_FAIL;
    }

    /* 타임아웃 설정 */
    struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(s_sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "TCP connect failed");
        close(s_sock);
        s_sock = -1;
        freeaddrinfo(res);
        return ESP_FAIL;
    }
    freeaddrinfo(res);

    s_connected = true;
    s_msg_id    = 1;
    s_recv_len  = 0;
    ESP_LOGI(TAG, "TCP connected to %s:%d", s_cfg.host, s_cfg.port);

    /* 핸드셰이크 순서 */
    send_configure();   /* id=1 */
    send_subscribe();   /* id=2 */
    send_authorize();   /* id=3 */

    if (s_on_connect) s_on_connect(true);

    /* 수신 루프 (블로킹) */
    recv_loop();

    close(s_sock);
    s_sock = -1;
    ESP_LOGW(TAG, "Disconnected from pool");
    return ESP_OK;
}

void stratum_disconnect(void)
{
    s_connected = false;
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
}

esp_err_t stratum_submit(const char *job_id, uint64_t extranonce2,
                         uint32_t ntime, uint32_t nonce, uint32_t version)
{
    if (!s_connected) return ESP_FAIL;

    char en2_hex[17];
    snprintf(en2_hex, sizeof(en2_hex), "%0*llx",
             (int)(s_en2_size * 2), (unsigned long long)extranonce2);

    char ntime_hex[9], nonce_hex[9], ver_hex[9];
    snprintf(ntime_hex, sizeof(ntime_hex), "%08x", (unsigned)ntime);
    snprintf(nonce_hex, sizeof(nonce_hex), "%08x", (unsigned)nonce);
    snprintf(ver_hex,   sizeof(ver_hex),   "%08x", (unsigned)version);

    cJSON *root   = cJSON_CreateObject();
    cJSON *params = cJSON_CreateArray();
    cJSON_AddNumberToObject(root, "id",     s_msg_id++);
    cJSON_AddStringToObject(root, "method", "mining.submit");
    cJSON_AddItemToArray(params, cJSON_CreateString(s_cfg.user));
    cJSON_AddItemToArray(params, cJSON_CreateString(job_id));
    cJSON_AddItemToArray(params, cJSON_CreateString(en2_hex));
    cJSON_AddItemToArray(params, cJSON_CreateString(ntime_hex));
    cJSON_AddItemToArray(params, cJSON_CreateString(nonce_hex));
    cJSON_AddItemToArray(params, cJSON_CreateString(ver_hex));
    cJSON_AddItemToObject(root, "params", params);

    esp_err_t ret = send_json(root);
    cJSON_Delete(root);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Submit: job=%s nonce=%s", job_id, nonce_hex);
    }
    return ret;
}

bool     stratum_is_connected(void)   { return s_connected; }
double   stratum_get_difficulty(void) { return s_difficulty; }
uint32_t stratum_get_version_mask(void) { return s_version_mask; }

void stratum_get_extranonce(uint8_t *en1, uint32_t *en1_len, uint32_t *en2_size)
{
    if (en1)      memcpy(en1, s_en1, s_en1_len);
    if (en1_len)  *en1_len  = s_en1_len;
    if (en2_size) *en2_size = s_en2_size;
}
