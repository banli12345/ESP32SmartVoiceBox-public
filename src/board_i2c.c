#include "board_i2c.h"

#include <stdio.h>
#include "board_pins.h"

static i2c_master_bus_handle_t s_bus;

bool board_i2c_init(void)
{
    if (s_bus) return true;

    i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
        },
    };
    esp_err_t err = i2c_new_master_bus(&config, &s_bus);
    if (err != ESP_OK) {
        printf("[I2C] bus init failed: %s\n", esp_err_to_name(err));
        s_bus = NULL;
        return false;
    }
    return true;
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_bus;
}

esp_err_t board_i2c_add_device(uint16_t addr, i2c_master_dev_handle_t *dev)
{
    if (!s_bus || !dev) return ESP_ERR_INVALID_STATE;
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    return i2c_master_bus_add_device(s_bus, &config, dev);
}

esp_err_t board_i2c_write(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len)
{
    if (!dev || !data || !len) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit(dev, data, len, BOARD_I2C_TIMEOUT_MS);
}

esp_err_t board_i2c_write_read(i2c_master_dev_handle_t dev, const uint8_t *wr, size_t wr_len,
                               uint8_t *rd, size_t rd_len)
{
    if (!dev || !wr || !wr_len || !rd || !rd_len) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(dev, wr, wr_len, rd, rd_len, BOARD_I2C_TIMEOUT_MS);
}
