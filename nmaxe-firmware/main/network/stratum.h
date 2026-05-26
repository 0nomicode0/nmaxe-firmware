/**
 * @file stratum.h
 * @brief Stratum v1 프로토콜 클라이언트
 *
 * 지원 메서드:
 *   mining.subscribe   → extranonce1, extranonce2_size 수신
 *   mining.authorize   → 인증
 *   mining.notify      → 작업 수신
 *   mining.set_difficulty → 난이도 설정
 *   mining.configure   → version rolling 협상
 *   mining.submit      → Share 제출
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define STRATUM_HOST_MAX        128
#define STRATUM_USER_MAX        128
#define STRATUM_PASS_MAX        64
#define STRATUM_BUF_SIZE        4096
#define STRATUM_EXTRANONCE_MAX  8

/* ── mining.notify 작업 구조체 ── */
typedef struct {
    char     job_id[32];
    uint8_t  prev_hash[32];
    uint8_t  coinbase1[128];
    uint32_t coinbase1_len;
    uint8_t  coinbase2[128];
    uint32_t coinbase2_len;
    uint8_t  merkle_branches[32][32];
    uint8_t  merkle_count;
    uint32_t version;
    uint32_t nbits;
    uint32_t ntime;
    bool     clean_jobs;
} stratum_notify_t;

/* ── Stratum 클라이언트 설정 ── */
typedef struct {
    char     host[STRATUM_HOST_MAX];
    uint16_t port;
    char     user[STRATUM_USER_MAX];    /* BTC주소.worker */
    char     pass[STRATUM_PASS_MAX];
} stratum_config_t;

/* ── 콜백 ── */
typedef void (*stratum_notify_cb_t)(const stratum_notify_t *notify);
typedef void (*stratum_diff_cb_t)(double difficulty);
typedef void (*stratum_connect_cb_t)(bool connected);

/* ── API ── */
esp_err_t stratum_init(const stratum_config_t *cfg,
                       stratum_notify_cb_t    on_notify,
                       stratum_diff_cb_t      on_diff,
                       stratum_connect_cb_t   on_connect);
esp_err_t stratum_connect(void);
void      stratum_disconnect(void);
esp_err_t stratum_submit(const char    *job_id,
                         uint64_t       extranonce2,
                         uint32_t       ntime,
                         uint32_t       nonce,
                         uint32_t       version);
bool      stratum_is_connected(void);
void      stratum_get_extranonce(uint8_t *en1, uint32_t *en1_len,
                                 uint32_t *en2_size);
double    stratum_get_difficulty(void);
uint32_t  stratum_get_version_mask(void);
