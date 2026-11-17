#ifndef SMARTVOICEBOX_BOARD_I2C_H
#define SMARTVOICEBOX_BOARD_I2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_I2C_TIMEOUT_MS 50

bool board_i2c_init(void);
i2c_master_bus_handle_t board_i2c_bus(void);
esp_err_t board_i2c_add_device(uint16_t addr, i2c_master_dev_handle_t *dev);
esp_err_t board_i2c_write(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len);
esp_err_t board_i2c_write_read(i2c_master_dev_handle_t dev, const uint8_t *wr, size_t wr_len,
                               uint8_t *rd, size_t rd_len);

#ifdef __cplusplus
}
#endif

#endif
