/**
 * @file work_manager.h
 * @brief Work Manager - 핵심 차별화 모듈
 *
 * 주요 기능:
 *   1. Stratum notify → BM1366 작업 변환
 *   2. extranonce2 자체 순환 (Pool 재요청 없이 무한 탐색)
 *   3. Coinbase 조립 → Merkle Root 계산 → Midstate 계산
 *   4. BM1366 결과 수신 → Stratum submit
 *   5. Job ID 관리 (최대 128개 동시 추적)
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "../network/stratum.h"
#include "../asic/bm1366.h"

#define WORK_MAX_JOBS       128
#define WORK_EN2_MAX_SIZE   8

/* ── 내부 작업 구조체 ── */
typedef struct {
    bool     active;
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
    uint64_t extranonce2;       /* 현재 extranonce2 값 */
    uint32_t version_rolling;   /* 현재 version rolling 값 */
    uint8_t  asic_job_id;       /* BM1366에 전달한 7비트 job ID */
} work_job_t;

/* ── 통계 ── */
typedef struct {
    uint32_t jobs_received;
    uint32_t shares_found;
    uint32_t shares_submitted;
    uint32_t shares_accepted;
    uint32_t en2_rollovers;     /* extranonce2 순환 횟수 */
} work_stats_t;

/* ── API ── */
esp_err_t work_manager_init(void);
void      work_manager_on_notify(const stratum_notify_t *notify);
void      work_manager_tick(void);          /* 채굴 루프에서 주기적 호출 */
void      work_manager_get_stats(work_stats_t *stats);
