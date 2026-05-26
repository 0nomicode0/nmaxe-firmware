/**
 * @file hal_gpio.h
 * @brief GPIO HAL - ASIC 리셋/전원, 팬, LED
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t hal_gpio_init(void);
void      hal_gpio_asic_reset(void);
void      hal_gpio_asic_power(bool on);
void      hal_gpio_fan_set(uint8_t pct);
void      hal_gpio_led_set(bool on);
void      hal_gpio_led_toggle(void);
bool      hal_gpio_btn_pressed(void);
