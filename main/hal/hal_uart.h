/**
 * @file hal_uart.h
 * @brief UART HAL - BM1366 직렬 통신
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

esp_err_t hal_uart_init(void);
esp_err_t hal_uart_write(const uint8_t *data, size_t len);
int       hal_uart_read(uint8_t *buf, size_t len, uint32_t timeout_ms);
void      hal_uart_flush(void);
