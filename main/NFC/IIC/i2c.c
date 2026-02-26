#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include "i2c.h"

// I2C_config
#define I2C_MASTER_FREQ_HZ 400000
#define I2C_TICKS_TO_WAIT 100

i2c_master_dev_handle_t nfc_dev_handle;
iic_device_t iic_device;

// 使用外部已有的I2C总线句柄初始化NFC设备
esp_err_t i2c_Init_With_Bus(i2c_master_bus_handle_t bus_handle, uint16_t nfc_addr)
{
    ESP_LOGI(__FUNCTION__, "Using existing I2C bus for NFC");
    ESP_LOGI(__FUNCTION__, "NFC I2C地址: 0x%02X", nfc_addr);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_7,
        .device_address = nfc_addr,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &nfc_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(__FUNCTION__, "i2c_master_bus_add_device failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return ret;
    }

    ESP_LOGI(__FUNCTION__, "NFC device added to I2C bus successfully");
    return ESP_OK;
}

uint8_t i2c_getRegister(iic_device_t iic_dev, uint8_t reg)
{
    esp_err_t ret = ESP_OK;
    uint8_t scratch = 0;
    uint8_t out_buf[1];
    out_buf[0] = reg;
    uint8_t in_buf[1];

    ret = i2c_master_transmit_receive(nfc_dev_handle, out_buf, 1, in_buf, 1, -1);

    if(ret == ESP_OK)
    {
        scratch = in_buf[0];
        ESP_LOGD(__FUNCTION__, "getRegister reg=0x%02x successfully scratch=0x%02x", reg, scratch);
    }
    else
    {
        ESP_LOGE(__FUNCTION__, "getRegister reg=0x%02x failed. code: 0x%02x", reg, ret);
    }

    return scratch;
}


void i2c_setRegister(iic_device_t iic_dev, uint8_t reg, uint8_t value)
{
    esp_err_t ret = ESP_OK;
    uint8_t out_buf[2];
    out_buf[0] = reg;
    out_buf[1] = value;

    ret = i2c_master_transmit(nfc_dev_handle, out_buf, 2, I2C_TICKS_TO_WAIT);

    if(ret == ESP_OK)
    {
        ESP_LOGD(__FUNCTION__, "setRegister reg=0x%02x value=0x%02x successfully", reg, value);
    }
    else
    {
        ESP_LOGE(__FUNCTION__, "setRegister reg=0x%02x value=0x%02x failed. code: 0x%02x", reg, value, ret);
    }
}
