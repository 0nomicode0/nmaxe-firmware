/**
 * @file work_manager.c
 * @brief Work Manager 구현
 *
 * 핵심 차별화: extranonce2 자체 순환
 *
 *   일반 방식:
 *     Nonce 소진 → Pool에 새 작업 요청 → 지연 발생
 *
 *   NMAxe 방식:
 *     Nonce 소진 → extranonce2 직접 증가
 *                → Coinbase 재조립
 *                → Merkle Root 재계산
 *                → 새 작업 즉시 BM1366에 투입
 *                → Pool 요청 없이 무한 탐색
 */
#include "work_manager.h"
#include "sha256d.h"
#include "../network/stratum.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>

static const char *TAG = "WORK_MGR";

/* ── 내부 상태 ── */
static work_job_t   s_jobs[WORK_MAX_JOBS];
static work_stats_t s_stats;
static int          s_cur_job   = -1;
static uint8_t      s_asic_id   = 0;

/* extranonce 정보 (Stratum에서 수신) */
static uint8_t  s_en1[8];
static uint32_t s_en1_len  = 4;
static uint32_t s_en2_size = 4;
static uint32_t s_version_mask = 0x1FFFE000;

/* ──────────────────────────────────────────
 * 유틸리티
 * ────────────────────────────────────────── */

static void swap_bytes(uint8_t *dst, const uint8_t *src, size_t len)
{
    /* Bitcoin 해시는 리틀엔디안으로 표시되므로 역전 필요 */
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[len - 1 - i];
    }
}

/**
 * @brief Coinbase 트랜잭션 조립
 *
 * coinbase = coinbase1 + extranonce1 + extranonce2 + coinbase2
 */
static uint32_t build_coinbase(const work_job_t *job,
                                uint8_t *coinbase_out)
{
    uint32_t idx = 0;

    /* coinbase1 */
    memcpy(coinbase_out + idx, job->coinbase1, job->coinbase1_len);
    idx += job->coinbase1_len;

    /* extranonce1 */
    memcpy(coinbase_out + idx, s_en1, s_en1_len);
    idx += s_en1_len;

    /* extranonce2 (빅엔디안, s_en2_size 바이트) */
    for (int i = (int)s_en2_size - 1; i >= 0; i--) {
        coinbase_out[idx++] = (job->extranonce2 >> (i * 8)) & 0xFF;
    }

    /* coinbase2 */
    memcpy(coinbase_out + idx, job->coinbase2, job->coinbase2_len);
    idx += job->coinbase2_len;

    return idx;
}

/**
 * @brief 작업에서 BM1366 job 구조체 생성
 *
 * 순서:
 *   1. Coinbase 조립
 *   2. Coinbase SHA256d → coinbase_hash
 *   3. Merkle Root 계산
 *   4. 블록 헤더 80바이트 구성
 *   5. Midstate 계산 (첫 64바이트)
 *   6. BM1366 job 구조체 채우기
 */
static void build_bm1366_job(const work_job_t *job, bm1366_job_t *bj)
{
    /* 1. Coinbase 조립 */
    uint8_t coinbase[512];
    uint32_t cb_len = build_coinbase(job, coinbase);

    /* 2. Coinbase hash */
    uint8_t coinbase_hash[32];
    sha256d(coinbase, cb_len, coinbase_hash);

    /* 3. Merkle Root */
    uint8_t root[32];
    merkle_root(coinbase_hash,
                (const uint8_t (*)[32])job->merkle_branches,
                job->merkle_count, root);

    /* 4. 블록 헤더 80바이트 구성
     *   [version(4)][prev_hash(32)][merkle_root(32)][ntime(4)][nbits(4)][nonce(4)]
     *   nonce는 ASIC이 채움 (0으로 초기화)
     */
    uint8_t header[80];
    uint32_t ver = job->version | job->version_rolling;

    header[0] = (ver      ) & 0xFF;
    header[1] = (ver >>  8) & 0xFF;
    header[2] = (ver >> 16) & 0xFF;
    header[3] = (ver >> 24) & 0xFF;

    /* prev_hash: 바이트 역전 (리틀엔디안 → 빅엔디안) */
    for (int i = 0; i < 8; i++) {
        uint32_t word;
        memcpy(&word, job->prev_hash + i*4, 4);
        header[4 + i*4 + 0] = (word      ) & 0xFF;
        header[4 + i*4 + 1] = (word >>  8) & 0xFF;
        header[4 + i*4 + 2] = (word >> 16) & 0xFF;
        header[4 + i*4 + 3] = (word >> 24) & 0xFF;
    }

    memcpy(header + 36, root, 32);

    header[68] = (job->ntime      ) & 0xFF;
    header[69] = (job->ntime >>  8) & 0xFF;
    header[70] = (job->ntime >> 16) & 0xFF;
    header[71] = (job->ntime >> 24) & 0xFF;

    header[72] = (job->nbits      ) & 0xFF;
    header[73] = (job->nbits >>  8) & 0xFF;
    header[74] = (job->nbits >> 16) & 0xFF;
    header[75] = (job->nbits >> 24) & 0xFF;

    memset(header + 76, 0, 4); /* nonce = 0 */

    /* 5. Midstate 계산 */
    calc_midstate(header, bj->midstate);

    /* 6. BM1366 job 구조체 채우기 */
    bj->job_id       = job->asic_job_id;
    bj->version      = ver;
    bj->version_mask = s_version_mask;

    /* merkle_root 마지막 4바이트 */
    memcpy(bj->merkle_root, root + 28, 4);

    /* nbits, ntime (리틀엔디안) */
    bj->nbits[0] = (job->nbits      ) & 0xFF;
    bj->nbits[1] = (job->nbits >>  8) & 0xFF;
    bj->nbits[2] = (job->nbits >> 16) & 0xFF;
    bj->nbits[3] = (job->nbits >> 24) & 0xFF;

    bj->ntime[0] = (job->ntime      ) & 0xFF;
    bj->ntime[1] = (job->ntime >>  8) & 0xFF;
    bj->ntime[2] = (job->ntime >> 16) & 0xFF;
    bj->ntime[3] = (job->ntime >> 24) & 0xFF;
}

/* ──────────────────────────────────────────
 * 작업 제출 (BM1366에 전송)
 * ────────────────────────────────────────── */

static void submit_job_to_asic(work_job_t *job)
{
    bm1366_job_t bj;
    build_bm1366_job(job, &bj);

    esp_err_t ret = bm1366_set_work(&bj);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Job→ASIC: id=%d en2=0x%llx ver_roll=0x%08X",
                 job->asic_job_id,
                 (unsigned long long)job->extranonce2,
                 (unsigned)job->version_rolling);
    }
}

/* ──────────────────────────────────────────
 * extranonce2 순환 (핵심 차별화)
 *
 * BM1366이 Nonce 4GB를 모두 탐색하면 자동 호출
 * Pool에 새 작업 요청 없이 extranonce2를 증가시켜
 * 완전히 새로운 해시 공간에서 계속 탐색
 * ────────────────────────────────────────── */

static void rollover_extranonce2(work_job_t *job)
{
    job->extranonce2++;

    /* extranonce2 최대값 오버플로우 처리 */
    uint64_t max_en2 = (1ULL << (s_en2_size * 8));
    if (job->extranonce2 >= max_en2) {
        job->extranonce2 = 0;
        s_stats.en2_rollovers++;
        ESP_LOGW(TAG, "extranonce2 overflow! rollover #%d",
                 s_stats.en2_rollovers);
    }

    /* ntime도 현재 시간으로 갱신 */
    job->ntime = (uint32_t)time(NULL);

    ESP_LOGD(TAG, "extranonce2 rolled: 0x%llx",
             (unsigned long long)job->extranonce2);

    /* 새 작업 즉시 ASIC에 투입 */
    submit_job_to_asic(job);
}

/* ──────────────────────────────────────────
 * 공개 API
 * ────────────────────────────────────────── */

esp_err_t work_manager_init(void)
{
    memset(s_jobs,  0, sizeof(s_jobs));
    memset(&s_stats, 0, sizeof(s_stats));
    s_cur_job = -1;
    s_asic_id = 0;

    /* extranonce 정보 로드 */
    stratum_get_extranonce(s_en1, &s_en1_len, &s_en2_size);
    s_version_mask = stratum_get_version_mask();

    ESP_LOGI(TAG, "WorkMgr init: en1_len=%d en2_size=%d vmask=0x%08X",
             s_en1_len, s_en2_size, (unsigned)s_version_mask);
    return ESP_OK;
}

/**
 * @brief Stratum notify 수신 시 호출
 *
 * notify → 내부 job 구조체 변환 → BM1366에 즉시 전송
 */
void work_manager_on_notify(const stratum_notify_t *notify)
{
    if (!notify) return;

    /* clean_jobs: 기존 작업 전부 무효화 */
    if (notify->clean_jobs) {
        for (int i = 0; i < WORK_MAX_JOBS; i++) {
            s_jobs[i].active = false;
        }
        ESP_LOGI(TAG, "Clean jobs: all cleared");
    }

    /* 새 job 슬롯 할당 */
    int slot = s_asic_id % WORK_MAX_JOBS;
    work_job_t *job = &s_jobs[slot];
    memset(job, 0, sizeof(*job));

    /* notify → job 복사 */
    strncpy(job->job_id, notify->job_id, sizeof(job->job_id) - 1);
    memcpy(job->prev_hash,        notify->prev_hash,     32);
    memcpy(job->coinbase1,        notify->coinbase1,     notify->coinbase1_len);
    job->coinbase1_len = notify->coinbase1_len;
    memcpy(job->coinbase2,        notify->coinbase2,     notify->coinbase2_len);
    job->coinbase2_len = notify->coinbase2_len;
    memcpy(job->merkle_branches,  notify->merkle_branches,
           notify->merkle_count * 32);
    job->merkle_count     = notify->merkle_count;
    job->version          = notify->version;
    job->nbits            = notify->nbits;
    job->ntime            = notify->ntime;
    job->extranonce2      = 0;       /* 새 작업은 0부터 시작 */
    job->version_rolling  = 0;
    job->asic_job_id      = s_asic_id & 0x7F;
    job->active           = true;

    s_cur_job = slot;
    s_asic_id = (s_asic_id + 1) & 0x7F;
    s_stats.jobs_received++;

    /* extranonce 갱신 */
    stratum_get_extranonce(s_en1, &s_en1_len, &s_en2_size);
    s_version_mask = stratum_get_version_mask();

    ESP_LOGI(TAG, "New job: %s slot=%d", job->job_id, slot);

    /* BM1366에 즉시 전송 */
    submit_job_to_asic(job);
}

/**
 * @brief 채굴 루프에서 주기적으로 호출
 *
 * - BM1366 결과 수신
 * - Stratum submit
 * - extranonce2 순환 처리
 */
void work_manager_tick(void)
{
    if (s_cur_job < 0 || !s_jobs[s_cur_job].active) return;
    if (!bm1366_is_ready()) return;

    work_job_t *job = &s_jobs[s_cur_job];

    /* BM1366 결과 수신 (논블로킹: timeout=0) */
    bm1366_result_t result;
    int r = bm1366_read_result(&result, 0);

    if (r == 1) {
        s_stats.shares_found++;

        /* job_id로 원본 작업 찾기 */
        work_job_t *found_job = NULL;
        for (int i = 0; i < WORK_MAX_JOBS; i++) {
            if (s_jobs[i].active &&
                s_jobs[i].asic_job_id == result.job_id) {
                found_job = &s_jobs[i];
                break;
            }
        }

        if (found_job) {
            /* version rolling 적용 */
            uint32_t final_ver = found_job->version | result.rolled_version;

            ESP_LOGI(TAG, "Share! job=%s nonce=0x%08X",
                     found_job->job_id, (unsigned)result.nonce);

            /* Stratum submit */
            esp_err_t ret = stratum_submit(
                found_job->job_id,
                found_job->extranonce2,
                found_job->ntime,
                result.nonce,
                final_ver
            );
            if (ret == ESP_OK) s_stats.shares_submitted++;
        } else {
            ESP_LOGW(TAG, "Share for unknown job_id=%d", result.job_id);
        }
    } else if (r < 0) {
        /* UART 에러 → extranonce2 순환으로 새 작업 투입 */
        rollover_extranonce2(job);
    }
}

void work_manager_get_stats(work_stats_t *stats)
{
    if (stats) memcpy(stats, &s_stats, sizeof(*stats));
}
