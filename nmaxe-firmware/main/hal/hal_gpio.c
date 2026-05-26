/**
 * @file hal_gpio.c
 * @brief GPIO HAL 구현
 */
#include "hal_gpio.h"
#include "hal.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HAL_GPIO";

#define FAN_PWM_TIMER       LEDC_TIMER_0
#define FAN_PWM_CHANNEL     LEDC_CHANNEL_0
#define FAN_PWM_FREQ_HZ     25000
#define FAN_PWM_RES         LEDC_TIMER_8_BIT

esp_err_t hal_gpio_init(void)
{
    /* 출력 핀 설정 */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << HAL_GPIO_ASIC_RST) |
                        (1ULL << HAL_GPIO_ASIC_PWR)  |
                        (1ULL << HAL_GPIO_LED),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    /* 입력 핀 (버튼) */
    io.pin_bit_mask = (1ULL << HAL_GPIO_BTN);
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    /* 팬 PWM */
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = FAN_PWM_TIMER,
        .duty_resolution = FAN_PWM_RES,
        .freq_hz         = FAN_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = FAN_PWM_CHANNEL,
        .timer_sel  = FAN_PWM_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = HAL_GPIO_FAN_PWM,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);

    /* 초기 상태 */
    gpio_set_level(HAL_GPIO_ASIC_RST, 1);
    gpio_set_level(HAL_GPIO_ASIC_PWR, 0);
    gpio_set_level(HAL_GPIO_LED,      0);
    hal_gpio_fan_set(50);

    ESP_LOGI(TAG, "GPIO initialized");
    return ESP_OK;
}

void hal_gpio_asic_reset(void)
{
    gpio_set_level(HAL_GPIO_ASIC_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(HAL_GPIO_ASIC_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "ASIC reset");
}

void hal_gpio_asic_power(bool on)
{
    gpio_set_level(HAL_GPIO_ASIC_PWR, on ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void hal_gpio_fan_set(uint8_t pct)
{
    if (pct > 100) pct = 100;
    uint32_t duty = (pct * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_PWM_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, FAN_PWM_CHANNEL);
}

void hal_gpio_led_set(bool on)
{
    gpio_set_level(HAL_GPIO_LED, on ? 1 : 0);
}

void hal_gpio_led_toggle(void)
{
    static bool s = false;
    s = !s;
    gpio_set_level(HAL_GPIO_LED, s);
}

bool hal_gpio_btn_pressed(void)
{
    return gpio_get_level(HAL_GPIO_BTN) == 0;
}
