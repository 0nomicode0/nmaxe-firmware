/**
 * @file hal.h
 * @brief HAL 공통 핀맵 + 타입 정의 (NMAxe BM1366)
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ── UART (BM1366) ── */
#define HAL_UART_NUM        UART_NUM_1
#define HAL_UART_TX_PIN     17
#define HAL_UART_RX_PIN     18
#define HAL_UART_BAUD       115200
#define HAL_UART_BUF_SIZE   1024

/* ── I2C (전압 컨트롤러) ── */
#define HAL_I2C_NUM         I2C_NUM_0
#define HAL_I2C_SDA_PIN     21
#define HAL_I2C_SCL_PIN     22
#define HAL_I2C_FREQ_HZ     400000

/* ── GPIO ── */
#define HAL_GPIO_ASIC_RST   10
#define HAL_GPIO_ASIC_PWR   11
#define HAL_GPIO_FAN_PWM    12
#define HAL_GPIO_LED        13
#define HAL_GPIO_BTN        0
