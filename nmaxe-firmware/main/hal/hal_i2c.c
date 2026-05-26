/**
 * @file hal_i2c.c
 * @brief I2C HAL 구현
 */
#include "hal_i2c.h"
#include "hal.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "HAL_I2C";
#define I2C_TIMEOUT_MS 50

esp_err_t hal_i2c_init(void)
{
    const i2c_config_t cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = HAL_I2C_SDA_PIN,
        .scl_io_num       = HAL_I2C_SCL_PIN,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = HAL_I2C_FREQ_HZ,
    };
    esp_err_t ret = i2c_param_config(HAL_I2C_NUM, &cfg);
    if (ret != ESP_OK) return ret;
    ret = i2c_driver_install(HAL_I2C_NUM, I2C_MODE_MASTER, 0, 0, 0);
    ESP_LOGI(TAG, "I2C%d init: SDA=%d SCL=%d", HAL_I2C_NUM,
             HAL_I2C_SDA_PIN, HAL_I2C_SCL_PIN);
    return ret;
}

esp_err_t hal_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, buf, 2, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(HAL_I2C_NUM, cmd,
                                         pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t hal_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, val, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(HAL_I2C_NUM, cmd,
                                         pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}
