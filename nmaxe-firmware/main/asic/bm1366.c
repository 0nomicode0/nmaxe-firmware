/**
 * @file bm1366.c
 * @brief BM1366 ASIC 드라이버 구현
 *
 * 프레임 구조 (명령):
 *   [0x55][0xAA][TYPE][LEN][ADDR][REG][DATA×4][CRC5]
 *
 * 프레임 구조 (응답):
 *   [0xAA][0x55][TYPE][LEN][DATA×4][JOB_ID][CRC5] = 9바이트
 */
#include "bm1366.h"
#include "../hal/hal_uart.h"
#include "../hal/hal_gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "BM1366";
static bool s_ready = false;

/* ── CRC5 (레지스터 명령용) ── */
static uint8_t crc5(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x1F;
    for (size_t i = 0; i < len; i++) {
        for (int b = 7; b >= 0; b--) {
            uint8_t bit = (data[i] >> b) & 1;
            uint8_t top = (crc >> 4) & 1;
            crc = ((crc << 1) & 0x1F) | (top ^ bit);
            if (top ^ bit) crc ^= 0x05;
        }
    }
    return crc & 0x1F;
}

/* ── CRC16 (작업 전송용) ── */
static uint16_t crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

/* ── 레지스터 쓰기 ── */
esp_err_t bm1366_write_reg(uint8_t addr, uint8_t reg, uint32_t val)
{
    uint8_t f[11];
    f[0]  = 0x55;
    f[1]  = 0xAA;
    f[2]  = BM1366_TYPE_CMD;
    f[3]  = 0x09;
    f[4]  = addr;
    f[5]  = reg;
    f[6]  = (val >> 24) & 0xFF;
    f[7]  = (val >> 16) & 0xFF;
    f[8]  = (val >>  8) & 0xFF;
    f[9]  = (val >>  0) & 0xFF;
    f[10] = crc5(f + 2, 8) << 3;
    return hal_uart_write(f, sizeof(f));
}

/* ── 레지스터 읽기 ── */
esp_err_t bm1366_read_reg(uint8_t addr, uint8_t reg, uint32_t *val)
{
    uint8_t f[7];
    f[0] = 0x55;
    f[1] = 0xAA;
    f[2] = BM1366_TYPE_CMD | 0x04;
    f[3] = 0x05;
    f[4] = addr;
    f[5] = reg;
    f[6] = crc5(f + 2, 4) << 3;
    esp_err_t ret = hal_uart_write(f, sizeof(f));
    if (ret != ESP_OK) return ret;

    uint8_t rsp[BM1366_RSP_LEN];
    int n = hal_uart_read(rsp, BM1366_RSP_LEN, 50);
    if (n != BM1366_RSP_LEN) return ESP_ERR_TIMEOUT;
    if (rsp[0] != 0xAA || rsp[1] != 0x55) return ESP_FAIL;

    *val = ((uint32_t)rsp[4] << 24) | ((uint32_t)rsp[5] << 16) |
           ((uint32_t)rsp[6] <<  8) | ((uint32_t)rsp[7]);
    return ESP_OK;
}

/* ── 주파수 설정 (PLL) ── */
esp_err_t bm1366_set_frequency(uint16_t freq_mhz)
{
    if (freq_mhz < BM1366_FREQ_MIN || freq_mhz > BM1366_FREQ_MAX) {
        ESP_LOGE(TAG, "Invalid freq: %dMHz", freq_mhz);
        return ESP_ERR_INVALID_ARG;
    }
    /* fbdiv = freq / refclk(25MHz) */
    uint32_t fbdiv = freq_mhz / 25;
    uint32_t pll0  = 0x40000000 | ((fbdiv & 0xFF) << 16) | 0x0174;
    uint32_t pll1  = 0x00070001;

    esp_err_t ret = bm1366_write_reg(BM1366_ADDR_BROADCAST, BM1366_REG_PLL0, pll0);
    if (ret != ESP_OK) return ret;
    ret = bm1366_write_reg(BM1366_ADDR_BROADCAST, BM1366_REG_PLL1, pll1);
    ESP_LOGI(TAG, "Frequency: %dMHz (PLL0=0x%08X)", freq_mhz, (unsigned)pll0);
    return ret;
}

/* ── 초기화 ── */
esp_err_t bm1366_reset(void)
{
    hal_gpio_asic_reset();
    hal_uart_flush();
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

esp_err_t bm1366_init(uint16_t freq_mhz, uint16_t volt_mv)
{
    esp_err_t ret;
    ESP_LOGI(TAG, "Init BM1366: %dMHz %dmV", freq_mhz, volt_mv);

    ret = bm1366_reset();
    if (ret != ESP_OK) return ret;

    /* 체인 리셋 */
    bm1366_write_reg(BM1366_ADDR_BROADCAST, BM1366_REG_CHIP_ADDR, 0x00);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* MISC: nonce 롤링 활성화 */
    ret = bm1366_write_reg(BM1366_ADDR_BROADCAST, BM1366_REG_MISC, 0x00003A01);
    if (ret != ESP_OK) return ret;

    /* 주파수 설정 */
    ret = bm1366_set_frequency(freq_mhz);
    if (ret != ESP_OK) return ret;

    /* Share 난이도 마스크 (기본 256 = 0xFF) */
    ret = bm1366_write_reg(BM1366_ADDR_BROADCAST, BM1366_REG_TICKET_MASK, 0xFF);
    if (ret != ESP_OK) return ret;

    s_ready = true;
    ESP_LOGI(TAG, "BM1366 ready");
    return ESP_OK;
}

/* ── 작업 전송 ── */
esp_err_t bm1366_set_work(const bm1366_job_t *job)
{
    if (!job) return ESP_ERR_INVALID_ARG;

    /* 프레임: 프리앰블(2) + TYPE(1) + LEN(1) + job_id(1)
     *         + midstate(32) + merkle(4) + nbits(4) + ntime(4)
     *         + version(4) + vmask(4) + CRC16(2) = 59바이트 */
    uint8_t f[59];
    int idx = 0;

    f[idx++] = 0x55;
    f[idx++] = 0xAA;
    f[idx++] = BM1366_TYPE_JOB;
    f[idx++] = 0x35;                   /* LEN=53 */
    f[idx++] = job->job_id & 0x7F;

    memcpy(f + idx, job->midstate, 32);     idx += 32;
    memcpy(f + idx, job->merkle_root, 4);   idx += 4;
    memcpy(f + idx, job->nbits, 4);         idx += 4;
    memcpy(f + idx, job->ntime, 4);         idx += 4;

    f[idx++] = (job->version      ) & 0xFF;
    f[idx++] = (job->version >>  8) & 0xFF;
    f[idx++] = (job->version >> 16) & 0xFF;
    f[idx++] = (job->version >> 24) & 0xFF;

    f[idx++] = (job->version_mask      ) & 0xFF;
    f[idx++] = (job->version_mask >>  8) & 0xFF;
    f[idx++] = (job->version_mask >> 16) & 0xFF;
    f[idx++] = (job->version_mask >> 24) & 0xFF;

    uint16_t crc = crc16(f + 2, idx - 2);
    f[idx++] = (crc >> 8) & 0xFF;
    f[idx++] = (crc     ) & 0xFF;

    return hal_uart_write(f, idx);
}

/* ── 결과 수신 ── */
int bm1366_read_result(bm1366_result_t *result, uint32_t timeout_ms)
{
    if (!result) return -1;

    uint8_t buf[BM1366_RSP_LEN];
    int n = hal_uart_read(buf, BM1366_RSP_LEN, timeout_ms);
    if (n == 0) return 0;
    if (n != BM1366_RSP_LEN) { hal_uart_flush(); return -1; }
    if (buf[0] != 0xAA || buf[1] != 0x55) { hal_uart_flush(); return -1; }
    if ((buf[2] & 0xF0) != 0x60) return 0;  /* 작업 결과가 아님 */

    result->nonce = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                    ((uint32_t)buf[6] <<  8) | ((uint32_t)buf[7]);
    result->job_id        = buf[8] >> 3;
    result->rolled_version = ((uint32_t)(buf[2] & 0x0F) << 16) | buf[3];

    ESP_LOGD(TAG, "Result: job=%d nonce=0x%08X",
             result->job_id, (unsigned)result->nonce);
    return 1;
}

/* ── 온도 읽기 ── */
esp_err_t bm1366_read_temp(float *temp_c)
{
    uint32_t raw;
    esp_err_t ret = bm1366_read_reg(BM1366_ADDR_BROADCAST, BM1366_REG_TEMP, &raw);
    if (ret != ESP_OK) return ret;
    uint16_t adc = (raw >> 16) & 0x7FF;
    *temp_c = (float)adc * 0.1f - 273.15f;
    return ESP_OK;
}

/* ── 전압 설정 (Phase 5에서 완성) ── */
esp_err_t bm1366_set_voltage(uint16_t mv)
{
    ESP_LOGI(TAG, "Voltage: %dmV (Phase 5 DVFS에서 구현)", mv);
    return ESP_OK;
}

bool bm1366_is_ready(void) { return s_ready; }
