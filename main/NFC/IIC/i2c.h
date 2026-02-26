#ifndef I2C_H
#define I2C_H
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

typedef enum{
    NFC1 = 0
}iic_device_t;

extern iic_device_t iic_device;
extern i2c_master_dev_handle_t nfc_dev_handle;

// 使用外部已有的I2C总线句柄初始化NFC设备
esp_err_t i2c_Init_With_Bus(i2c_master_bus_handle_t bus_handle, uint16_t nfc_addr);

uint8_t i2c_getRegister(iic_device_t iic_dev, uint8_t reg);
void i2c_setRegister(iic_device_t iic_dev, uint8_t reg, uint8_t value);
#endif // I2C_H
