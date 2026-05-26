/**
 * @file hal_i2c.h
 * @brief I2C HAL - 전압 컨트롤러 통신
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

esp_err_t hal_i2c_init(void);
esp_err_t hal_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val);
esp_err_t hal_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *val);
