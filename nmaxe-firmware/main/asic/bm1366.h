/**
 * @file bm1366.h
 * @brief BM1366 ASIC 드라이버
 *
 * UART 프로토콜:
 *   명령 프리앰블: 0x55 0xAA
 *   응답 프리앰블: 0xAA 0x55
 *   UART: 115200 8N1
 *   응답: 고정 9바이트
 *   CRC: CRC5(레지스터) / CRC16(작업)
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ── 프로토콜 상수 ── */
#define BM1366_RSP_LEN          9
#define BM1366_ADDR_BROADCAST   0x00
#define BM1366_TYPE_CMD         0x40
#define BM1366_TYPE_JOB         0x20

/* ── 레지스터 ── */
#define BM1366_REG_CHIP_ADDR    0x00
#define BM1366_REG_PLL0         0x08
#define BM1366_REG_PLL1         0x0C
#define BM1366_REG_TICKET_MASK  0x14
#define BM1366_REG_MISC         0x18
#define BM1366_REG_TEMP         0x50

/* ── 기본값 ── */
#define BM1366_FREQ_DEFAULT     485
#define BM1366_VOLT_DEFAULT     1200
#define BM1366_FREQ_MIN         200
#define BM1366_FREQ_MAX         600

/* ── 작업 구조체 ── */
typedef struct {
    uint8_t  job_id;
    uint8_t  midstate[32];
    uint8_t  merkle_root[4];
    uint8_t  nbits[4];
    uint8_t  ntime[4];
    uint32_t version;
    uint32_t version_mask;
} bm1366_job_t;

/* ── 결과 구조체 ── */
typedef struct {
    uint8_t  job_id;
    uint32_t nonce;
    uint32_t rolled_version;
} bm1366_result_t;

/* ── API ── */
esp_err_t bm1366_init(uint16_t freq_mhz, uint16_t volt_mv);
esp_err_t bm1366_reset(void);
esp_err_t bm1366_set_work(const bm1366_job_t *job);
int       bm1366_read_result(bm1366_result_t *result, uint32_t timeout_ms);
esp_err_t bm1366_set_frequency(uint16_t freq_mhz);
esp_err_t bm1366_set_voltage(uint16_t mv);
esp_err_t bm1366_read_temp(float *temp_c);
esp_err_t bm1366_write_reg(uint8_t addr, uint8_t reg, uint32_t val);
esp_err_t bm1366_read_reg(uint8_t addr, uint8_t reg, uint32_t *val);
bool      bm1366_is_ready(void);
