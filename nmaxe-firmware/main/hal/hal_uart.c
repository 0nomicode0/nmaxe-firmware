/**
 * @file hal_uart.c
 * @brief UART HAL 구현
 */
#include "hal_uart.h"
#include "hal.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "HAL_UART";

esp_err_t hal_uart_init(void)
{
    const uart_config_t cfg = {
        .baud_rate  = HAL_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret;
    ret = uart_driver_install(HAL_UART_NUM, HAL_UART_BUF_SIZE * 2,
                              HAL_UART_BUF_SIZE * 2, 0, NULL, 0);
    if (ret != ESP_OK) return ret;
    ret = uart_param_config(HAL_UART_NUM, &cfg);
    if (ret != ESP_OK) return ret;
    ret = uart_set_pin(HAL_UART_NUM, HAL_UART_TX_PIN, HAL_UART_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    ESP_LOGI(TAG, "UART%d init: TX=%d RX=%d BAUD=%d",
             HAL_UART_NUM, HAL_UART_TX_PIN, HAL_UART_RX_PIN, HAL_UART_BAUD);
    return ret;
}

esp_err_t hal_uart_write(const uint8_t *data, size_t len)
{
    int n = uart_write_bytes(HAL_UART_NUM, data, len);
    return (n < 0) ? ESP_FAIL : ESP_OK;
}

int hal_uart_read(uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    return uart_read_bytes(HAL_UART_NUM, buf, len, pdMS_TO_TICKS(timeout_ms));
}

void hal_uart_flush(void)
{
    uart_flush(HAL_UART_NUM);
}
